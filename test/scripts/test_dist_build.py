# Copyright (C) 2026 iceman50
"""Exercise the interactive distribution builder using isolated build-tool fixtures."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[2]


class DistBuildTest(unittest.TestCase):
    def setUp(self):
        scratch = ROOT / "build" / "test-dist-build"
        scratch.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix="workspace with spaces ", dir=scratch)
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        (self.root / "scripts").mkdir()
        shutil.copy2(ROOT / "scripts/dist-build.ps1", self.root / "scripts/dist-build.ps1")
        self.write("dcpp/version.h", '#define VERSIONSTRING "0.8842"\n')
        self.write("dcpp/version-rev-id.inc", '#define DCPP_REVISION "[123abcd]"\n')
        self.write("changelog-bfe.txt", "Fixture changelog")
        self.write("Themes/sample.xml", "theme")
        self.write("Emoticons/sample.dcemo", "emoticon")
        for name in ("Dark", "Neon-Circuit", "Aurora", "Copper", "Paper"):
            self.write("IconPacks/" + name + ".dcico", name)
        for mode in ("debug", "release"):
            self.outputs("SCons", "MinGW", mode)
            self.outputs("CMake", "MinGW", mode)
            self.outputs("CMake", "MSVC", mode)
        self.write("fake cmake.ps1", r'''
$repo = $PSScriptRoot
Add-Content -LiteralPath (Join-Path $repo 'commands.txt') -Value ($args -join '|')
if($args[0] -eq '-S') {
    if($env:DIST_FIXTURE_FAIL -eq 'configure') { $global:LASTEXITCODE = 17; return }
} elseif($args[0] -eq '--build') {
    if($env:DIST_FIXTURE_FAIL -eq 'build') { $global:LASTEXITCODE = 18; return }
} else { throw 'Unexpected CMake invocation' }
$global:LASTEXITCODE = 0
''')
        self.write("fake scons.ps1", r'''
Add-Content -LiteralPath (Join-Path $PSScriptRoot 'commands.txt') -Value ($args -join '|')
if($env:DIST_FIXTURE_FAIL -eq 'build') { $global:LASTEXITCODE = 19; return }
$global:LASTEXITCODE = 0
''')

    def write(self, relative, contents):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(contents, encoding="utf-8")
        return path

    def outputs(self, backend, compiler, mode, cpu="default"):
        suffix = "" if cpu in ("default", "nehalem") else "-" + cpu
        if backend == "SCons":
            directory = f"build/{mode}-mingw-x64{suffix}/bin"
        else:
            build = "build/cmake/" + ("msvc" if compiler == "MSVC" else "mingw-" + mode) + suffix
            directory = build + "/bin/" + mode.title()
            identity = "MSVC" if compiler == "MSVC" else "GNU"
            version = "19.51.12345.0" if compiler == "MSVC" else "13.2.0"
            self.write(build + "/CMakeFiles/4.4.0/CMakeCXXCompiler.cmake",
                       f'set(CMAKE_CXX_COMPILER_ID "{identity}")\n'
                       f'set(CMAKE_CXX_COMPILER_VERSION "{version}")\n')
            self.write(build + "/CMakeCache.txt", "# fixture cache\n")
        marker = f"{backend}/{compiler}/{mode}"
        for suffix in ("exe", "pdb"):
            self.write(directory + "/DCPlusPlus." + suffix, marker + "/" + suffix)
        return self.root / directory

    def run_builder(self, *args, answers=None, failure=None, success=True):
        env = dict(os.environ)
        env.pop("DIST_FIXTURE_FAIL", None)
        if failure:
            env["DIST_FIXTURE_FAIL"] = failure
        command = ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                   str(self.root / "scripts/dist-build.ps1"),
                   "-CMakeCommand", str(self.root / "fake cmake.ps1"),
                   "-SConsCommand", str(self.root / "fake scons.ps1"), *args]
        result = subprocess.run(command, cwd=ROOT, input=answers, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                timeout=60, env=env)
        if success:
            self.assertEqual(result.returncode, 0, result.stdout)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout)
        return result.stdout

    def check_packages(self, backend, compiler, modes):
        archives = list((self.root / "dist").glob("*.zip"))
        self.assertEqual(len(archives), len(modes))
        for mode in modes:
            archive, = [p for p in archives if "-" + mode.title() + "-" in p.name]
            self.assertIn("-" + backend + "-", archive.name)
            if compiler == "MSVC":
                self.assertIn("-DebugCRT-" if mode == "debug" else "-UCRT-", archive.name)
                self.assertNotIn("MSVCRT", archive.name)
            with zipfile.ZipFile(archive) as package:
                for suffix in ("exe", "pdb"):
                    self.assertEqual(package.read("DCPlusPlus." + suffix).decode(),
                                     f"{backend}/{compiler}/{mode}/{suffix}")
                self.assertIn("changelog-bfe.txt", package.namelist())
                self.assertIn("Themes/sample.xml", package.namelist())
                self.assertIn("Emoticons/sample.dcemo", package.namelist())
                self.assertIn("IconPacks/Dark.dcico", package.namelist())
        self.assertFalse(list((self.root / "dist").glob(".stage-*")))

    def test_legacy_configuration_only_uses_scons(self):
        self.run_builder("-Configuration", "Both", "-CompilerLabel", "MinGW-fixture", "-Jobs", "3")
        self.check_packages("SCons", "MinGW", ("debug", "release"))
        commands = (self.root / "commands.txt").read_text()
        self.assertIn("tools=mingw|arch=x64|mode=debug|cpu=default|-j3|build/debug-mingw-x64/bin/DCPlusPlus.exe", commands)
        self.assertIn("mode=release", commands)

    def test_cmake_mingw_both_configurations(self):
        self.run_builder("-Configuration", "Both", "-Compiler", "MinGW", "-BuildSystem", "CMake", "-Jobs", "2")
        self.check_packages("CMake", "MinGW", ("debug", "release"))
        commands = (self.root / "commands.txt").read_text()
        self.assertIn("--preset|mingw-debug", commands)
        self.assertIn("--preset|mingw-release", commands)
        self.assertIn("--target|application|--parallel|2", commands)
        self.assertTrue(all("MinGW-w64-13.2.0" in p.name for p in (self.root / "dist").glob("*.zip")))

    def test_msvc_defaults_to_native_cmake(self):
        self.run_builder("-Configuration", "Both", "-Compiler", "MSVC")
        self.check_packages("CMake", "MSVC", ("debug", "release"))
        commands = (self.root / "commands.txt").read_text()
        self.assertIn("--preset|msvc", commands)
        self.assertIn("--config|Debug", commands)
        self.assertIn("--config|Release", commands)
        self.assertTrue(all("MSVC-19.51.12345.0" in p.name for p in (self.root / "dist").glob("*.zip")))

    def test_custom_cpu_builds_use_separate_outputs_and_package_labels(self):
        for backend, cpu in (("SCons", "x86-64-v3"), ("CMake", "x86-64-v2")):
            with self.subTest(backend=backend):
                binary = self.outputs(backend, "MinGW", "release", cpu)
                (binary / "DCPlusPlus.exe").write_text(cpu)
                self.run_builder("-Configuration", "Release", "-BuildSystem", backend,
                                 "-CpuTarget", cpu.upper(), "-CompilerLabel", "fixture")
                commands = (self.root / "commands.txt").read_text()
                if backend == "SCons":
                    self.assertIn("cpu=" + cpu, commands)
                    self.assertIn("build/release-mingw-x64-" + cpu + "/bin/DCPlusPlus.exe", commands)
                else:
                    self.assertIn("-DDCPP_CPU_TARGET=" + cpu, commands)
                    self.assertIn("-B|" + str(self.root / ("build/cmake/mingw-release-" + cpu)), commands)
                archive, = (self.root / "dist").glob("*" + cpu + "*.zip")
                with zipfile.ZipFile(archive) as package:
                    self.assertEqual(package.read("DCPlusPlus.exe").decode(), cpu)

    def test_custom_cpu_no_build_never_falls_back_to_default_outputs(self):
        output = self.run_builder("-Configuration", "Release", "-CpuTarget", "x86-64-v3",
                                  "-NoBuild", "-CompilerLabel", "fixture", success=False)
        self.assertIn("x86-64-v3", output)
        self.assertFalse((self.root / "dist").exists())

    def test_rejects_msvc_cpu_override(self):
        output = self.run_builder("-Configuration", "Release", "-Compiler", "MSVC",
                                  "-CpuTarget", "x86-64-v3", success=False)
        self.assertIn("-CpuTarget requires MinGW", output)
        self.assertFalse((self.root / "commands.txt").exists())

    def test_interactive_msvc_reprompts_invalid_choices(self):
        self.run_builder("-NoBuild", answers="invalid\n2\ninvalid\nrelease\n")
        self.check_packages("CMake", "MSVC", ("release",))
        self.assertFalse((self.root / "commands.txt").exists())

    def test_interactive_mingw_cmake(self):
        self.run_builder("-NoBuild", answers="mingw\ndebug\ninvalid\ncmake\n")
        self.check_packages("CMake", "MinGW", ("debug",))

    def test_interactive_defaults(self):
        self.run_builder("-NoBuild", "-CompilerLabel", "MinGW-fixture", answers="\n\n\n")
        self.check_packages("SCons", "MinGW", ("debug", "release"))

    def test_rejects_unsupported_msvc_scons_pair(self):
        output = self.run_builder("-Configuration", "Release", "-Compiler", "MSVC", "-BuildSystem", "SCons", success=False)
        self.assertIn("MSVC distribution builds use CMake", output)
        self.assertFalse((self.root / "commands.txt").exists())
        self.assertFalse((self.root / "dist").exists())

    def test_failed_build_never_packages_stale_outputs(self):
        for failure in ("configure", "build"):
            with self.subTest(failure=failure):
                self.run_builder("-Configuration", "Release", "-Compiler", "MSVC", failure=failure, success=False)
                self.assertFalse((self.root / "dist").exists())
        self.run_builder("-Configuration", "Release", "-CompilerLabel", "MinGW-fixture", failure="build", success=False)
        self.assertFalse((self.root / "dist").exists())

    def test_missing_companion_is_an_error(self):
        (self.root / "build/cmake/msvc/bin/Debug/DCPlusPlus.pdb").unlink()
        output = self.run_builder("-Configuration", "Debug", "-Compiler", "MSVC", "-NoBuild", success=False)
        self.assertIn("Debug companion PDB was not found", output)
        self.assertFalse((self.root / "dist").exists())

    def test_overwrite_and_compression_failure_preserve_existing_package(self):
        # Load the production functions without the interactive entry point so
        # the package timestamp can stay fixed across overwrite attempts.
        wrapper = self.write("package fixture.ps1", r'''
param([string]$Action)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$tokens = $null
$errors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $PSScriptRoot 'scripts/dist-build.ps1'), [ref]$tokens, [ref]$errors)
foreach($definition in $ast.EndBlock.Statements) {
    if($definition -is [System.Management.Automation.Language.FunctionDefinitionAst]) {
        Invoke-Expression $definition.Extent.Text
    }
}
$Compiler = 'MSVC'
$BuildSystem = 'CMake'
$CpuTarget = 'default'
if($Action -eq 'fail') {
    function Compress-Archive { throw 'Simulated compression failure' }
}
New-DistPackage -RepoRoot $PSScriptRoot -Mode release -OutputRoot (Join-Path $PSScriptRoot 'dist') `
    -VersionNumber fixture -CompilerName MSVC-fixture -GitRevision fixture -BuildTimestamp fixed `
    -OverwriteExisting ($Action -ne 'refuse')
''')

        def package(action, succeeds):
            result = subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
                                     "-File", str(wrapper), action], capture_output=True, timeout=60)
            self.assertEqual(result.returncode == 0, succeeds, result.stdout + result.stderr)
            self.assertFalse(list((self.root / "dist").glob(".stage-*")))

        package("create", True)
        archive, = (self.root / "dist").glob("*.zip")
        original = archive.read_bytes()
        package("refuse", False)
        self.assertEqual(archive.read_bytes(), original)
        package("fail", False)
        self.assertEqual(archive.read_bytes(), original)
        self.write("build/cmake/msvc/bin/Release/DCPlusPlus.exe", "replacement executable")
        package("overwrite", True)
        with zipfile.ZipFile(archive) as contents:
            self.assertEqual(contents.read("DCPlusPlus.exe"), b"replacement executable")


if __name__ == "__main__":
    unittest.main(testRunner=unittest.TextTestRunner(stream=sys.stdout))

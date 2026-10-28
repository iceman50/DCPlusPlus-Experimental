# Copyright (C) 2026 iceman50
"""Compile C/C++ instruction-baseline probes: python test/cmake/test_cpu_target.py."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class CpuTargetTest(unittest.TestCase):
    def test_compiler_policy_and_reconfiguration(self):
        ninja = shutil.which("ninja")
        if not ninja:
            vswhere = Path(os.environ.get("ProgramFiles(x86)", "")) / "Microsoft Visual Studio/Installer/vswhere.exe"
            if vswhere.is_file():
                result = subprocess.run([str(vswhere), "-latest", "-products", "*", "-find",
                                         r"Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"],
                                        capture_output=True, text=True, check=True)
                ninja = next(iter(result.stdout.splitlines()), None)
        if not ninja or not shutil.which("cmake") or not shutil.which("gcc"):
            self.skipTest("Requires CMake, Ninja and MinGW on PATH")

        scratch = ROOT / "build/cmake/test-cpu"
        scratch.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as temp:
            source = Path(temp)
            (source / "CMakeLists.txt").write_text(f'''
cmake_minimum_required(VERSION 3.25)
project(CpuProbe LANGUAGES C CXX)
set(DCPP_ROOT "{ROOT.as_posix()}")
set(DCPP_ARCH x64)
set(DCPP_LIB_SUFFIX /x64)
if(PROBE_X86)
    set(DCPP_ARCH x86)
endif()
if(PROBE_MSVC)
    set(MINGW FALSE)
endif()
include("${{DCPP_ROOT}}/cmake/Compiler.cmake")
add_library(probe STATIC probe.c probe.cpp)
target_link_libraries(probe PRIVATE dcpp_options)
''', encoding="utf-8")
            build = source / "build"

            def configure(cpu, *extra, success=True):
                command = ["cmake", "-S", str(source), "-B", str(build), "-G", "Ninja",
                           "-DCMAKE_MAKE_PROGRAM=" + ninja,
                           "-DCMAKE_TOOLCHAIN_FILE=" + str(ROOT / "cmake/toolchains/mingw.cmake"),
                           "-DCMAKE_BUILD_TYPE=Release", "-DDCPP_CPU_TARGET=" + cpu, *extra]
                result = subprocess.run(command, capture_output=True, text=True, timeout=120)
                self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr)
                return result.stdout + result.stderr

            # Reuse the directory to verify changing the cache option updates both languages.
            for cpu in ("default", "x86-64-v2", "x86-64-v3", "nehalem", "default"):
                with self.subTest(cpu=cpu):
                    v3 = cpu == "x86-64-v3"
                    probe = '''
#if !defined(__SSE4_2__) || !defined(__POPCNT__)
#error Missing baseline instructions
#endif
'''
                    probe += ("#if !defined(__AVX2__) || !defined(__BMI2__) || !defined(__LZCNT__)\n"
                              if v3 else "#if defined(__AVX2__) || defined(__BMI2__)\n")
                    probe += "#error Wrong CPU target\n#endif\n"
                    for ext in ("c", "cpp"):
                        (source / ("probe." + ext)).write_text(probe + "int probe_" + ext + "(void) { return 0; }\n")
                    configure(cpu)
                    result = subprocess.run(["cmake", "--build", str(build)], capture_output=True,
                                            text=True, timeout=120)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("DCPP_CPU_TARGET must be", configure("typo", success=False))
            self.assertIn("requires MinGW x64", configure("x86-64-v3", "-DPROBE_X86=ON", success=False))
            self.assertIn("requires MinGW x64", configure("x86-64-v3", "-DPROBE_X86=OFF",
                                                          "-DPROBE_MSVC=ON", success=False))


if __name__ == "__main__":
    unittest.main()

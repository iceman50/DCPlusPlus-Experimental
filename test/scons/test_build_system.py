"""Build-system regressions: python test/scons/test_build_system.py."""
from contextlib import contextmanager
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
PREAMBLE = f"""
import os, sys
sys.path.insert(0, {str(ROOT)!r})
from build_util import Dev
DefaultEnvironment(tools=[])
"""
MINGW = PREAMBLE + """
env = Environment(tools=['mingw'], ENV=os.environ, TARGET_ARCH='amd64')
env.Replace(tools='mingw', mode='release', arch='x64', verbose=False,
            msvcproj=False, pch=False, i18n=False)
dev = Dev(env)
dev.prepare()
env.Append(CPPPATH=['#/'])
"""


@unittest.skipUnless(sys.platform == "win32", "Requires Windows build tools")
class BuildSystemTest(unittest.TestCase):
    @contextmanager
    def workspace(self):
        work = ROOT / "build" / "test-scons"
        work.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="workspace with spaces ", dir=work) as directory:
            yield Path(directory)

    def write(self, directory, name, text):
        path = directory / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")

    def build(self, directory, *targets):
        result = subprocess.run([sys.executable, "-m", "SCons", "-Q", "-j4", *targets],
                                cwd=directory, capture_output=True, timeout=120)
        self.assertEqual(result.returncode, 0, (result.stdout + result.stderr).decode(errors="replace"))
        return (result.stdout + result.stderr).decode(errors="replace")

    def test_custom_cpu_outputs_do_not_replace_normal_builds(self):
        with self.workspace() as directory:
            script = MINGW.replace("dev = Dev(env)", "env['cpu'] = ARGUMENTS.get('cpu', 'default')\ndev = Dev(env)")
            self.write(directory, "SConstruct", script + "\nobj = env.Object(dev.get_build_path('probe.o'), 'probe.cpp')\nDefault(env.Program(dev.get_target('sample/', 'probe'), obj))\n")
            self.write(directory, "probe.cpp", "int main() { return 1; }\n")
            self.build(directory)
            normal = directory / "build/release-mingw-x64/bin/probe.exe"
            before = normal.read_bytes()
            self.write(directory, "probe.cpp", "int main() { return 2; } // custom build\n")
            self.build(directory, "cpu=x86-64-v3")
            custom = directory / "build/release-mingw-x64-x86-64-v3/bin/probe.exe"
            self.assertEqual(subprocess.run([str(custom)], timeout=10).returncode, 2)
            self.assertEqual(normal.read_bytes(), before)
            self.assertEqual(subprocess.run([str(normal)], timeout=10).returncode, 1)

    def test_rejects_invalid_cpu_and_x86_override(self):
        for args, message in ((["arch=x64", "cpu=typo"], "Invalid value"),
                              (["arch=x86", "cpu=x86-64-v3"], "cpu requires a GCC x64 build")):
            result = subprocess.run([sys.executable, "-m", "SCons", "-Q", "-n",
                                     "tools=mingw", *args], cwd=ROOT, capture_output=True, timeout=60)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(message, (result.stdout + result.stderr).decode(errors="replace"))

    def test_forced_pch_generated_source_and_nested_header_rebuild(self):
        with self.workspace() as directory:
            self.write(directory, "SConstruct", MINGW + f"""
env['pch'] = True
env.Tool('gch', toolpath=[{str(ROOT)!r}])
env.Append(CXXFLAGS=['-H', '-Werror=invalid-pch'])
Default(dev.build('sample/'))
""")
            self.write(directory, "sample/SConscript", """
Import('dev source_path')
env, target, sources = dev.prepare_build(source_path, 'sample', precompiled_header='pch')
def generate(target, source, env):
    with open(target[0].abspath, 'w') as output:
        output.write('int main() { return PCH_VALUE; }\\n')
generated = env.Command(dev.get_build_path('sample/generated.cpp'), [], generate)
ret = env.Program(target, sources + generated)
Return('ret')
""")
            self.write(directory, "sample/pch.h", '#include "nested/value.h"\n')
            self.write(directory, "sample/pch.cpp", '#include "pch.h"\n')
            self.write(directory, "sample/nested/value.h", '#define PCH_VALUE 7\n')
            output = self.build(directory)
            self.assertRegex(output, r"! .*pch\.h\.gch", "GCC fell back to parsing the header")
            exe = directory / "build/release-mingw-x64/bin/sample.exe"
            self.assertEqual(subprocess.run([str(exe)], timeout=10).returncode, 7)
            obj = directory / "build/release-mingw-x64/sample/generated.o"
            before = obj.stat().st_mtime_ns
            self.build(directory)
            self.assertEqual(before, obj.stat().st_mtime_ns)
            self.write(directory, "sample/nested/value.h", '#define PCH_VALUE 9\n')
            self.build(directory)
            self.assertNotEqual(before, obj.stat().st_mtime_ns)
            self.assertEqual(subprocess.run([str(exe)], timeout=10).returncode, 9)

    def test_msvc_pch_object_is_a_tracked_library_input(self):
        from SCons.Tool.MSCommon.vc import get_installed_vcs
        if not get_installed_vcs():
            self.skipTest("MSVC is not installed")
        with self.workspace() as directory:
            self.write(directory, "SConstruct", PREAMBLE + """
env = Environment(tools=['default'], ENV=os.environ, TARGET_ARCH='amd64')
env.Replace(tools='default', mode='debug', arch='x64', verbose=False,
            msvcproj=False, pch=True, i18n=False)
dev = Dev(env)
dev.prepare()
library = dev.build('sample/')
Default(env.Program('app', ['main.cpp', library]))
""")
            self.write(directory, "sample/SConscript", """
Import('dev source_path')
env, target, sources = dev.prepare_build(source_path, 'sample', precompiled_header='pch')
ret = dev.build_lib(env, target, sources)
Return('ret')
""")
            self.write(directory, "sample/pch.h", 'int pch_value();\n')
            self.write(directory, "sample/pch.cpp", '#include "pch.h"\nint pch_value() { return 11; }\n')
            self.write(directory, "sample/use.cpp", '#include "pch.h"\nint value() { return pch_value(); }\n')
            self.write(directory, "main.cpp", 'int value(); int main() { return value(); }\n')
            self.build(directory)
            self.assertEqual(subprocess.run([str(directory / "app.exe")], timeout=10).returncode, 11)
            self.write(directory, "sample/pch.cpp", '#include "pch.h"\nint pch_value() { return 12; }\n')
            self.build(directory)
            self.assertEqual(subprocess.run([str(directory / "app.exe")], timeout=10).returncode, 12)

    def test_revision_changes_only_recompile_version(self):
        with self.workspace() as directory:
            self.write(directory, "SConstruct", MINGW + f"""
sys.path.insert(0, {str(ROOT / 'dcpp')!r})
import gen_rev_id
import gen_build_number
from pathlib import Path
gen_rev_id.get_rev_id = lambda env: Path(env.File('#/revision.txt').abspath).read_text()
# Preserve timestamps deliberately, reproducing rapid builds deterministically.
stamp = int(Path('timestamp.txt').read_text())
def fixed_timestamp(generator):
    def generate(target, source, env):
        result = generator(target, source, env)
        os.utime(str(target[0]), (stamp, stamp))
        return result
    return generate
gen_rev_id.gen_rev_id = fixed_timestamp(gen_rev_id.gen_rev_id)
gen_build_number.gen_build_number = fixed_timestamp(gen_build_number.gen_build_number)
dev.tests_only = False
dev.add_dcpp = dev.add_intl = dev.add_openssl = lambda env: None
library = dev.build('dcpp/')
env.AddPostAction(dev.get_build_path('dcpp/version.o'),
    lambda target, source, env: os.utime(str(target[0]), (stamp, stamp)))
env.AddPostAction(library,
    lambda target, source, env: os.utime(str(target[0]), (stamp, stamp)))
Default(env.Program('probe', ['main.cpp', library]))
""")
            self.write(directory, "timestamp.txt", str(int(time.time()) + 60))
            self.write(directory, "main.cpp", '#include <cstdio>\nconst char* revision(); int build();\n'
                       'int main() { std::printf("%s:%d", revision(), build()); }\n')
            self.write(directory, "dcpp/version.cpp",
                       '#include "version-rev-id.inc"\n#include "version-build.inc"\n'
                       'const char* revision() { return DCPP_REVISION; }\n'
                       'int build() { return DCPP_BUILD_NUMBER; }\n')
            self.write(directory, "dcpp/nested/unrelated.cpp", 'int unrelated() { return 1; }\n')
            shutil.copyfile(ROOT / "dcpp/SConscript", directory / "dcpp/SConscript")
            self.write(directory, "revision.txt", "first")
            self.build(directory)
            core = directory / "build/release-mingw-x64/dcpp"
            before = (core / "nested/unrelated.o").stat().st_mtime_ns
            probe = directory / "probe.exe"
            self.assertEqual(subprocess.check_output([str(probe)], text=True), "first:1")
            self.write(directory, "revision.txt", "other")
            self.build(directory)
            self.assertEqual(before, (core / "nested/unrelated.o").stat().st_mtime_ns)
            self.assertEqual(subprocess.check_output([str(probe)], text=True), "other:2")
            self.assertIn('"other"', (directory / "dcpp/version-rev-id.inc").read_text())
            self.assertEqual((directory / ".build-number").read_text().strip(), "2")

    def test_single_file_msvc_projects_use_selected_toolset(self):
        for compiler, studio, toolset in (("14.3", "17.0", "v143"), ("14.5", "18.0", "v145")):
            with self.subTest(studio=studio), self.workspace() as directory:
                self.write(directory, "inc/one.h", "// Header\n")
                self.write(directory, "src/one.cpp", "// Source\n")
                self.write(directory, "SConstruct", PREAMBLE + f"""
env = Environment(tools=['msvs'], ENV=os.environ, MSVS_VERSION={studio!r},
                  MSVC_VERSION={compiler!r})
env.Replace(tools='default', mode='debug', arch='x64', verbose=False, msvcproj=True)
dev = Dev(env)
dev.prepare()
dev.build_lib(env, 'sample', ['src/one.cpp'],
              lambda env: ([env.File('inc/one.h')], [env.File('src/one.cpp')]), 'sample')
Default(env.File(dev.msvcproj_path + 'sample' + env['MSVSPROJECTSUFFIX']))
""")
                self.build(directory)
                project = directory / "msvc/debug-x64/sample.vcxproj"
                document = ET.parse(project)
                ns = {"m": "http://schemas.microsoft.com/developer/msbuild/2003"}
                self.assertEqual([e.text for e in document.findall(".//m:PlatformToolset", ns)], [toolset])
                includes = [e.get("Include", "") for e in document.iter()]
                self.assertTrue(any(p.replace("\\", "/").endswith("inc/one.h") for p in includes))
                self.assertTrue(any(p.replace("\\", "/").endswith("src/one.cpp") for p in includes))
                self.assertFalse(any("dummy" in p for p in includes))


if __name__ == "__main__":
    unittest.main(testRunner=unittest.TextTestRunner(stream=sys.stdout))

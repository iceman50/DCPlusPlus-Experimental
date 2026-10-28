# Copyright (C) 2026 iceman50
"""Exercise the production SCons symbol-splitting rule with a small MinGW program.

Run from any directory: python test/win32/test_debug_companion.py
"""
import hashlib
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib


ROOT = Path(__file__).resolve().parents[2]


def pe_sections(path):
    data = path.read_bytes()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("Not a PE image: " + str(path))
    count = struct.unpack_from("<H", data, pe + 6)[0]
    symbols, symbol_count = struct.unpack_from("<II", data, pe + 12)
    strings = symbols + symbol_count * 18
    start = pe + 24 + struct.unpack_from("<H", data, pe + 20)[0]
    result = {}
    for index in range(count):
        offset = start + index * 40
        name = data[offset:offset + 8].split(b"\0")[0]
        if name.startswith(b"/"):
            name_offset = strings + int(name[1:])
            name = data[name_offset:data.index(b"\0", name_offset)]
        size, pointer = struct.unpack_from("<II", data, offset + 16)
        result[name.decode()] = data[pointer:pointer + size]
    return result


@unittest.skipUnless(sys.platform == "win32", "Requires the Windows MinGW toolchain")
class DebugCompanionTest(unittest.TestCase):
    def test_release_and_debug_outputs_and_recovery(self):
        work = ROOT / "build" / "test-debug-companion"
        work.mkdir(parents=True, exist_ok=True)
        for mode in ("release", "debug"):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory(
                    prefix=mode + " with spaces ", dir=work) as directory:
                directory = Path(directory)
                (directory / "SConstruct").write_text(f"""
import os, sys
sys.path.insert(0, {str(ROOT)!r})
from build_util import Dev
DefaultEnvironment(tools=[])
env = Environment(tools=['mingw'], ENV=os.environ, TARGET_ARCH='amd64')
env['tools'] = 'mingw'
env['mode'] = {mode!r}
env['arch'] = 'x64'
env['verbose'] = False
env['msvcproj'] = False
dev = Dev(env)
dev.prepare()
env.Append(CXXFLAGS=['-gdwarf-5', '-O2' if env['mode'] == 'release' else '-O0'])
linked = env.Program('intermediate/DCPlusPlus-unstripped', 'main.cpp')
Default(dev.split_debug_info(env, '#/bin/DCPlusPlus', linked))
""", encoding="utf-8")
                source = directory / "main.cpp"
                source.write_text("int main() { return 0; }\n", encoding="utf-8")

                def build(*targets):
                    result = subprocess.run([sys.executable, "-m", "SCons", "-Q", "-j4", *targets],
                                            cwd=directory, capture_output=True, timeout=90)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

                exe = directory / "bin" / "DCPlusPlus.exe"
                pdb = exe.with_suffix(".pdb")
                linked = directory / "intermediate" / "DCPlusPlus-unstripped.exe"

                def verify():
                    stripped = pe_sections(exe)
                    debug = pe_sections(pdb)
                    original = pe_sections(linked)
                    self.assertIn(".debug_info", debug)
                    self.assertNotIn(".debug_info", stripped)
                    self.assertFalse(any(name.startswith(".debug") for name in stripped))
                    for name in (".text", ".data", ".rdata", ".pdata", ".xdata"):
                        if name in original:
                            self.assertEqual(original[name], stripped[name], name)
                    link = stripped[".gnu_debuglink"]
                    end = link.index(b"\0")
                    self.assertEqual(link[:end], b"DCPlusPlus.pdb")
                    offset = (end + 4) & ~3
                    self.assertEqual(struct.unpack_from("<I", link, offset)[0],
                                     zlib.crc32(pdb.read_bytes()))
                    self.assertEqual(subprocess.run([str(exe)], timeout=10).returncode, 0)
                    self.assertFalse(exe.with_name("DCPlusPlus-stripped.exe").exists())

                # An explicit request for only the EXE must produce both files.
                build("bin/DCPlusPlus.exe")
                verify()
                timestamps = [p.stat().st_mtime_ns for p in (linked, exe, pdb)]
                build()  # The default target is the same pair, with no extra work.
                self.assertEqual(timestamps, [p.stat().st_mtime_ns for p in (linked, exe, pdb)])

                for missing, requested in ((pdb, "bin/DCPlusPlus.exe"), (exe, "bin/DCPlusPlus.pdb")):
                    missing.unlink()
                    build(requested)
                    verify()
                    self.assertEqual(timestamps[0], linked.stat().st_mtime_ns,
                                     "Recovering a missing output unnecessarily relinked the program")

                previous = hashlib.sha256(pdb.read_bytes()).digest()
                source.write_text("int changed_symbol() { return 0; }\n"
                                  "int main() { return changed_symbol(); }\n", encoding="utf-8")
                build("bin/DCPlusPlus.exe", "bin/DCPlusPlus.pdb")
                verify()
                self.assertNotEqual(previous, hashlib.sha256(pdb.read_bytes()).digest())
                build("-c")
                self.assertFalse(exe.exists())
                self.assertFalse(pdb.exists())


if __name__ == "__main__":
    unittest.main(testRunner=unittest.TextTestRunner(stream=sys.stdout))

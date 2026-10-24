"""Check a built MinGW application's symbols, no-op build, recovery and isolation."""
import argparse
import hashlib
from pathlib import Path
import struct
import subprocess
import sys
import zlib

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "test" / "win32"))
from test_debug_companion import pe_sections


def fingerprint(path):
    if not path.exists():
        return None
    return path.stat().st_mtime_ns, hashlib.sha256(path.read_bytes()).digest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--config", choices=("Debug", "Release"), default="Release")
    args = parser.parse_args()
    directory = args.build_dir.resolve()
    cache = directory / "CMakeCache.txt"
    if not cache.exists() or ("DCPlusPlus_SOURCE_DIR:STATIC=" + (ROOT / "cmake").as_posix()) not in cache.read_text():
        parser.error("expected an isolated DCPlusPlus CMake build directory")
    linked = directory / "linked" / args.config / "DCPlusPlus.exe"
    exe = directory / "bin" / args.config / "DCPlusPlus.exe"
    pdb = exe.with_suffix(".pdb")
    if not all(path.is_file() for path in (linked, exe, pdb)):
        parser.error("build the MinGW application first")
    shared = [ROOT / ".build-number", ROOT / "dcpp/version-rev-id.inc", ROOT / "help/cshelp.h"]
    before = [fingerprint(path) for path in shared]

    def build():
        subprocess.run(["cmake", "--build", str(directory), "--config", args.config,
                        "--target", "application", "--parallel", "4"], check=True)

    def verify():
        stripped, symbols, original = map(pe_sections, (exe, pdb, linked))
        assert ".debug_info" in symbols, "Missing debug information"
        assert not any(name.startswith(".debug") for name in stripped), "EXE is not stripped"
        for name in (".text", ".data", ".rdata", ".pdata", ".xdata", ".rsrc"):
            if name in original:
                assert original[name] == stripped[name], "Changed runtime section: " + name
        link = stripped[".gnu_debuglink"]
        end = link.index(b"\0")
        assert link[:end] == b"DCPlusPlus.pdb", "Wrong debug companion name"
        assert struct.unpack_from("<I", link, (end + 4) & ~3)[0] == zlib.crc32(pdb.read_bytes()), "Wrong companion CRC"

    verify()
    expected = [fingerprint(path) for path in (linked, exe, pdb)]
    build()
    assert expected == [fingerprint(path) for path in (linked, exe, pdb)], "No-op build changed the application"
    for missing in (pdb, exe):
        # These exact generated outputs have been checked above. Preserve the
        # original in a sibling backup until recovery succeeds.
        backup = missing.with_suffix(missing.suffix + ".recovery-backup")
        if backup.exists():
            raise RuntimeError("Unexpected backup: " + str(backup))
        missing.rename(backup)
        try:
            build()
            verify()
            assert fingerprint(linked) == expected[0], "Recovery unnecessarily relinked the application"
        finally:
            if not missing.exists():
                backup.rename(missing)
            else:
                backup.unlink()
    assert before == [fingerprint(path) for path in shared], "CMake changed SCons metadata"
    print("PASS: symbols, runtime sections, no-op build, missing-output recovery, source metadata isolation")


if __name__ == "__main__":
    main()

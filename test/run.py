#!/usr/bin/env python3
"""Build and run component tests from any working directory."""
import argparse
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("components", nargs="*", choices=["all", "dcpp", "dwt", "win32", "dwarf", "zlib"],
                        help="Components to run (default: all automated tests)")
    action = parser.add_mutually_exclusive_group()
    action.add_argument("--build-only", action="store_true", help="Build without running tests")
    action.add_argument("--manual", action="store_true", help="Build interactive DWT demos without launching them")
    action.add_argument("--benchmarks", action="store_true", help="Build opt-in core benchmarks without running them")
    parser.add_argument("--mode", choices=["debug", "release"], default="release")
    parser.add_argument("--arch", choices=["x86", "x64"], default="x64")
    parser.add_argument("--tools", choices=["mingw", "default"], default="mingw" if sys.platform == "win32" else "default")
    parser.add_argument("-j", "--jobs", type=int, default=4)
    parser.add_argument("--filter", default="*", help="GoogleTest filter")
    parser.add_argument("--registry", action="store_true", help="Include temporary HKCU checks in DWT tests")
    parser.add_argument("--timeout", type=int, default=600, help="Timeout in seconds per test executable")
    parser.add_argument("--dry-run", action="store_true", help="Show the build/run actions without executing them")
    options = parser.parse_args()
    if options.jobs < 1 or options.timeout < 1:
        parser.error("jobs and timeout must be positive")
    components = options.components or ["all"]
    if options.manual and any(c not in ("all", "dwt") for c in components):
        parser.error("interactive demos are only available for dwt")
    if options.benchmarks and any(c not in ("all", "dcpp") for c in components):
        parser.error("benchmarks are only available for dcpp")
    prefix = ("test-manual" if options.manual else "test-bench" if options.benchmarks
              else "test-build" if options.build_only else "test")
    targets = [prefix] if "all" in components else [prefix + "/" + c for c in components]
    command = [sys.executable, "-m", "SCons", "-C", str(Path(__file__).resolve().parents[1]),
               "tools=" + options.tools, "mode=" + options.mode, "arch=" + options.arch,
               "-j" + str(options.jobs), "test_filter=" + options.filter,
               "test_timeout=" + str(options.timeout), "test_registry=" + ("yes" if options.registry else "no")]
    if options.dry_run:
        command.append("-n")
    return subprocess.call(command + targets)


if __name__ == "__main__":
    sys.exit(main())

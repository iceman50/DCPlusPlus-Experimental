"""Configure, build and run isolated CMake component tests from the root test tree."""
import argparse
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("components", nargs="+", choices=("dcpp", "dwt", "win32", "zlib", "dwarf"))
    parser.add_argument("--build-dir", type=Path, required=True, help="An already configured CMake build directory")
    parser.add_argument("--config", choices=("Debug", "Release"), default="Release")
    parser.add_argument("-j", "--jobs", type=int, default=4)
    parser.add_argument("--build-only", action="store_true")
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("jobs must be positive")
    directory = args.build_dir.resolve()
    if not (directory / "CMakeCache.txt").is_file():
        parser.error("configure the build directory first using cmake/ presets")
    source = Path(__file__).resolve().parents[2] / "cmake"
    subprocess.run(["cmake", "-S", str(source), "-B", str(directory),
                    "-DDCPP_TEST_COMPONENTS=" + ";".join(dict.fromkeys(args.components))], check=True)
    subprocess.run(["cmake", "--build", str(directory), "--config", args.config,
                    "--target", "component_tests", "--parallel", str(args.jobs)], check=True)
    if not args.build_only:
        subprocess.run(["ctest", "--test-dir", str(directory), "-C", args.config,
                        "--output-on-failure", "--no-tests=error"], check=True)


if __name__ == "__main__":
    main()

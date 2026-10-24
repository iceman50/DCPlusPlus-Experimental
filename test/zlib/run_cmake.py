"""Run the bundled zlib examples in a build-local scratch directory."""
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    name, executable = sys.argv[1:]
    with tempfile.TemporaryDirectory(prefix=name + "-", dir=Path(executable).parent) as directory:
        if name == "example":
            subprocess.run([executable], cwd=directory, check=True, timeout=600)
        else:
            payload = b"hello world\n" * 1024
            compressed = subprocess.run([executable], input=payload, capture_output=True,
                                        cwd=directory, check=True, timeout=600).stdout
            restored = subprocess.run([executable, "-d"], input=compressed, capture_output=True,
                                      cwd=directory, check=True, timeout=600).stdout
            if restored != payload:
                raise RuntimeError("minigzip round trip differs from the input")


if __name__ == "__main__":
    main()

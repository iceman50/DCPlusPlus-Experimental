# Copyright (C) 2026 iceman50
"""Prepare a deterministic local corpus for TransferCompressionBenchmark.

Run from the repository root. Outputs go under the ignored build directory.
Requires the release executable and MinGW objcopy on PATH.
"""

import base64
import gzip
import hashlib
import json
from pathlib import Path
import random
import subprocess

root = Path(__file__).resolve().parent.parent
out = root / "build" / "transfer-compression-benchmark"
out.mkdir(parents=True, exist_ok=True)
inputs = sorted(p for folder in ("dcpp", "win32", "dwt", "help")
                for p in (root / folder).rglob("*")
                if p.suffix in (".cpp", ".h", ".html") and p.is_file())
source = b"".join(p.read_bytes() + b"\n" for p in inputs)
(out / "source.txt").write_bytes(source)
(out / "source.gz").write_bytes(gzip.compress(source, compresslevel=6, mtime=0))
subprocess.run(["objcopy", "--strip-debug",
                str(root / "build/release-mingw-x64/bin/DCPlusPlus.exe"),
                str(out / "application.exe")], check=True)
rng = random.Random(20260921)
noise = rng.randbytes(16 * 1024 * 1024)
(out / "random.bin").write_bytes(noise)
(out / "mixed.bin").write_bytes(noise[:1024 * 1024] + source)
with (out / "filelist.xml").open("w", encoding="utf-8", newline="\n") as stream:
    stream.write('<?xml version="1.0" encoding="utf-8"?>\n<FileListing Version="1" Base="/">\n')
    for directory in range(1000):
        stream.write(f'<Directory Name="Collection {directory}">\n')
        for index in range(100):
            number = directory * 100 + index
            tth = base64.b32encode(rng.randbytes(24)).decode().rstrip("=")
            stream.write(f'<File Name="Track {number:06d} - Recording.flac" '
                         f'Size="{rng.randrange(1000000, 100000000)}" TTH="{tth}"/>\n')
        stream.write('</Directory>\n')
    stream.write('</FileListing>\n')
names = ["source.txt", "application.exe", "filelist.xml", "source.gz", "random.bin", "mixed.bin"]
(out / "manifest.txt").write_text("\n".join(names) + "\n")
metadata = {name: {"bytes": (out / name).stat().st_size,
                   "sha256": hashlib.sha256((out / name).read_bytes()).hexdigest()}
            for name in names}
metadata["source_files"] = [p.relative_to(root).as_posix() for p in inputs]
(out / "corpus.json").write_text(json.dumps(metadata, indent=2) + "\n")
print(json.dumps({name: metadata[name]["bytes"] for name in names}, indent=2))

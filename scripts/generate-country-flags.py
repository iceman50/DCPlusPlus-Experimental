#!/usr/bin/env python3
"""Generate native icon resources and the ISO-code map from GoSquared's flags.

Usage: python scripts/generate-country-flags.py path/to/gosquared-flags
Only repack the 16/24/32/48-pixel ICO images; no image libraries are required.
Update marked sections in resource.h, DCPlusPlus.rc and CountryFlags.cpp.
Existing resource IDs are retained when adding countries.
Copyright (C) 2026 iceman50
"""
from pathlib import Path
import re
import shutil
import struct
import sys

root = Path(__file__).resolve().parents[1]
source = Path(sys.argv[1])
target = root / "res" / "flags"
target.mkdir(parents=True, exist_ok=True)
icons = sorted(p for p in (source / "flags/flags-iso/flat/ico").glob("*.ico")
               if re.fullmatch(r"[A-Z]{2}", p.stem))
if not icons:
    raise SystemExit("No country icons found in flags/flags-iso/flat/ico")

begin = "// BEGIN GENERATED COUNTRY FLAGS (scripts/generate-country-flags.py)"
end = "// END GENERATED COUNTRY FLAGS"
paths = [root / "win32" / name for name in
         ("resource.h", "DCPlusPlus.rc", "appearance/CountryFlags.cpp")]
contents = [path.read_bytes().decode("utf-8") for path in paths]
for path, text in zip(paths, contents):
    if text.count(begin) != 1 or text.count(end) != 1 or text.index(begin) >= text.index(end):
        raise SystemExit(f"Missing or ambiguous country flag section in {path}")
resource_ids = {code: int(value) for code, value in re.findall(
    r"^#define IDI_FLAG_([A-Z]{2}) (\d+)\r?$", contents[0], re.MULTILINE)}
next_id = max(resource_ids.values(), default=1999) + 1
definitions, resources, mapping = [], [], []
for path in icons:
    if path.stem not in resource_ids:
        resource_ids[path.stem] = next_id
        next_id += 1
    resource_id = resource_ids[path.stem]
    if resource_id >= 3000:
        raise SystemExit("Country flag resource IDs exceed their reserved range")
    data = path.read_bytes()
    reserved, kind, count = struct.unpack_from("<HHH", data)
    assert (reserved, kind) == (0, 1)
    selected = []
    for i in range(count):
        fields = struct.unpack_from("<BBBBHHII", data, 6 + 16 * i)
        if fields[0] in (16, 24, 32, 48):
            selected.append((fields, data[fields[7]:fields[7] + fields[6]]))
    assert selected
    header = bytearray(struct.pack("<HHH", 0, 1, len(selected)))
    payload = bytearray()
    for fields, image in selected:
        header.extend(struct.pack("<BBBBHHII", *fields[:6], len(image), 6 + 16 * len(selected) + len(payload)))
        payload.extend(image)
    (target / path.name).write_bytes(header + payload)
    definitions.append(f"#define IDI_FLAG_{path.stem} {resource_id}")
    resources.append(f'IDI_FLAG_{path.stem} ICON "res/flags/{path.name}"')
    mapping.append(f"\tCOUNTRY_FLAG({path.stem}),")
for path, text, lines in zip(paths, contents, (definitions, resources, mapping)):
    prefix, rest = text.split(begin, 1)
    _, suffix = rest.split(end, 1)
    newline = "\r\n" if "\r\n" in text else "\n"
    path.write_bytes((prefix + begin + newline + newline.join(lines) + newline + end + suffix).encode("utf-8"))
shutil.copyfile(source / "LICENSE.txt", target / "LICENSE.txt")
print(f"Generated {len(icons)} country flag resources")

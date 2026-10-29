# Copyright (C) 2026 iceman50
"""Run isolated, opt-in core and transfer-table benchmarks; retain raw timings."""
import argparse
import csv
from datetime import datetime, timezone
import json
import hashlib
import os
from pathlib import Path
import platform
import subprocess
from statistics import median


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--label", required=True)
    parser.add_argument("--build", default="build/release-mingw-x64")
    parser.add_argument("--repeat", type=int, default=3)
    parser.add_argument("--area", choices=("core", "ui", "paint", "all"), default="all")
    args = parser.parse_args()
    if not args.label or any(c not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_" for c in args.label):
        parser.error("label must contain only letters, digits, underscores, and hyphens")
    if not 1 <= args.repeat <= 20:
        parser.error("repeat must be between 1 and 20")
    root = Path(__file__).resolve().parent.parent
    output = root / "build/performance" / args.label
    output.mkdir(parents=True, exist_ok=False)
    build = root / args.build
    commands = {
        "core": [str(build / "test/dcpp/dcpp_benchmarks.exe"),
                 "--gtest_filter=PipelineBenchmark.*", "--gtest_also_run_disabled_tests"],
        "ui": [str(build / "test/win32/TransferPerformanceTest.exe")],
    }
    if args.area == "paint":
        commands = {"paint": commands["ui"]}
    if args.area != "all":
        commands = {args.area: commands[args.area]}
    metadata = dict(timestamp=datetime.now(timezone.utc).isoformat(), platform=platform.platform(),
                    logical_cpus=os.cpu_count(), repeat=args.repeat, commands=commands,
                    note="Warm-cache reads and hidden native transfer-table update/sort timings; excludes visible painting and network traffic.")
    if args.area == "paint":
        metadata["note"] = ("Visible synthetic transfer updates through the production async queue, batch timer, WM_PAINT and DwmFlush. "
                            "Back-to-back batches emphasize timer waiting; excludes network delivery and physical display latency. "
                            "Run on an unlocked interactive desktop with the test window unobscured.")
    metadata["commit"] = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip()
    metadata["status"] = subprocess.check_output(["git", "status", "--short"], cwd=root, text=True)
    metadata["binary_sha256"] = {}
    for name, command in commands.items():
        digest = hashlib.sha256()
        with open(command[0], "rb") as executable:
            for chunk in iter(lambda: executable.read(1024 * 1024), b""):
                digest.update(chunk)
        metadata["binary_sha256"][name] = digest.hexdigest()
    (output / "source.patch").write_bytes(subprocess.check_output(["git", "diff", "HEAD"], cwd=root))
    (output / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    rows = []
    for iteration in range(args.repeat):
        for name, command in commands.items():
            environment = {key: value for key, value in os.environ.items()
                           if key not in ("DCPP_PROFILE", "DCPP_PROFILE_PAINT")}
            environment["DCPP_PROFILE_PAINT" if name == "paint" else "DCPP_PROFILE"] = "1"
            result = subprocess.run(command, cwd=root, env=environment,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=300)
            (output / f"{name}-{iteration}.log").write_bytes(result.stdout)
            result.check_returncode()
            parsed = 0
            for line in result.stdout.decode("utf-8", errors="replace").splitlines():
                if not line.startswith(("ui,", "hash,", "cached_read_new_reader,", "event_to_paint_and_flush,", "wm_paint,")):
                    continue
                fields = line.split(",")
                rows.append(dict(area=fields[0], workload=":".join(fields[1:-2]), run=iteration,
                                 median_ms=float(fields[-2]), max_ms=float(fields[-1])))
                parsed += 1
            if not parsed:
                raise RuntimeError(f"No benchmark rows from {name}; see raw log")
            print(f"{name} run {iteration + 1}: {parsed} measurements", flush=True)
    with (output / "results.csv").open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    for area, workload in sorted({(r["area"], r["workload"]) for r in rows}):
        values = [r["median_ms"] for r in rows if (r["area"], r["workload"]) == (area, workload)]
        print(f"{area} {workload}: {median(values):.3f} ms (median of {len(values)} runs)")


if __name__ == "__main__":
    main()

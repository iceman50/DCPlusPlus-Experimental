"""Summarize the seven timing rounds and estimate ideal pipelined throughput."""

import csv
from collections import defaultdict
from pathlib import Path
from statistics import median

folder = Path(__file__).resolve().parent.parent / "build/transfer-compression-benchmark"
groups = defaultdict(list)
with (folder / "results.csv").open(newline="") as stream:
    for row in csv.DictReader(stream):
        groups[row["dataset"], row["codec"]].append(row)

summary = []
for (dataset, codec), rows in groups.items():
    assert len(rows) == 7, (dataset, codec, len(rows))
    original = int(rows[0]["input_bytes"])
    compressed = int(rows[0]["compressed_bytes"])
    compress_times = [float(row["compress_seconds"]) for row in rows]
    decompress_times = [float(row["decompress_seconds"]) for row in rows]
    compression = original / median(compress_times) / 1e6
    decompression = original / median(decompress_times) / 1e6
    ratio = compressed / original
    result = dict(dataset=dataset, codec=codec, input_bytes=original,
                  compressed_bytes=compressed, percent_original=100 * ratio,
                  compress_MBps=compression, decompress_MBps=decompression,
                  compress_min_MBps=original / max(compress_times) / 1e6,
                  compress_max_MBps=original / min(compress_times) / 1e6,
                  decompress_min_MBps=original / max(decompress_times) / 1e6,
                  decompress_max_MBps=original / min(decompress_times) / 1e6)
    for mbps in (100, 1000, 2500):
        # Ideal steady-state pipeline: compress and decompress on separate peers.
        result[f"modeled_{mbps}Mbps_MBps"] = min(compression, decompression, mbps / 8 / ratio)
    summary.append(result)

with (folder / "summary.csv").open("w", newline="") as stream:
    writer = csv.DictWriter(stream, fieldnames=list(summary[0]))
    writer.writeheader()
    writer.writerows(summary)

lines = ["| Dataset | Codec | Size (% original) | Compress MB/s | Decompress MB/s |",
         "|---|---|---:|---:|---:|"]
for row in summary:
    lines.append(f'| {row["dataset"]} | {row["codec"]} | {row["percent_original"]:.2f} | '
                 f'{row["compress_MBps"]:.1f} | {row["decompress_MBps"]:.1f} |')
lines += ["", "Ideal pipeline estimates (original-data MB/s; not network measurements):",
          "", "| Dataset | Codec | 100 Mbps | 1 Gbps | 2.5 Gbps |",
          "|---|---|---:|---:|---:|"]
for row in summary:
    lines.append(f'| {row["dataset"]} | {row["codec"]} | '
                 f'{row["modeled_100Mbps_MBps"]:.1f} | '
                 f'{row["modeled_1000Mbps_MBps"]:.1f} | '
                 f'{row["modeled_2500Mbps_MBps"]:.1f} |')
report = "\n".join(lines) + "\n"
(folder / "tables.md").write_text(report)
print(report)

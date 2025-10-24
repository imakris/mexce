#!/usr/bin/env python3
"""Parse build/benchmark_results.txt and append summary metrics to CSV."""
from __future__ import annotations

import csv
import re
import sys
from pathlib import Path


def extract_metric(pattern: str, text: str, cast):
    match = re.search(pattern, text)
    if not match:
        raise SystemExit(f"Could not find pattern: {pattern}")
    return cast(match.group(1))


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        raise SystemExit("Usage: record_benchmark_summary.py <branch> <report-path>")

    branch, report_path = argv[1], Path(argv[2])
    text = report_path.read_text(encoding="utf-8")

    avg_compile_us = extract_metric(r"Average compilation time\s+([0-9.]+)\s+us", text, float)
    avg_runtime_ns = extract_metric(r"Average runtime per function\s+([0-9.]+)\s+ns", text, float)
    compiler_runtime_ns = extract_metric(
        r"Average runtime per function\s+[0-9.]+\s+ns\s+([0-9.]+)\s+ns", text, float
    )
    total_compile_sec = extract_metric(r"Total compilation time\s+([0-9.]+)\s+sec", text, float)
    total_exec_ms = extract_metric(r"Total function execution time\s+([0-9.]+)\s+ms", text, float)

    exact = extract_metric(r"0 \(exact\)\s+(\d+)", text, int)
    upto_16 = extract_metric(r"1-16\s+(\d+)", text, int)
    gt_65536 = extract_metric(r">65536\s+(\d+)", text, int)

    out_path = Path("analysis/asmd_optimizer_benchmark_summary.csv")
    file_exists = out_path.exists()

    with out_path.open("a", newline="", encoding="utf-8") as csvfile:
        writer = csv.writer(csvfile)
        if not file_exists:
            writer.writerow(
                [
                    "branch",
                    "avg_compile_us",
                    "avg_runtime_ns",
                    "compiler_runtime_ns",
                    "total_compile_sec",
                    "total_exec_ms",
                    "exact_matches",
                    "matches_1_16",
                    "matches_gt_65536",
                ]
            )
        writer.writerow(
            [
                branch,
                f"{avg_compile_us:.3f}",
                f"{avg_runtime_ns:.3f}",
                f"{compiler_runtime_ns:.3f}",
                f"{total_compile_sec:.6f}",
                f"{total_exec_ms:.6f}",
                exact,
                upto_16,
                gt_65536,
            ]
        )

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))

#!/usr/bin/env python3
"""Render the benchmark summary CSV as a Markdown table."""
from __future__ import annotations

import csv
from pathlib import Path

from typing import List


COLUMNS = [
    "branch",
    "avg_runtime_ns",
    "avg_compile_us",
    "total_exec_ms",
    "exact_matches",
    "matches_1_16",
    "matches_gt_65536",
]


def load_rows(path: Path) -> List[dict]:
    with path.open(newline="", encoding="utf-8") as fh:
        reader = csv.DictReader(fh)
        return list(reader)


def main() -> int:
    csv_path = Path("analysis/asmd_optimizer_benchmark_summary.csv")
    rows = load_rows(csv_path)
    master = next(r for r in rows if r["branch"] == "master")
    master_runtime = float(master["avg_runtime_ns"])
    master_compile = float(master["avg_compile_us"])

    # build markdown table sorted by avg runtime ascending then branch name
    def runtime(row):
        return float(row["avg_runtime_ns"])

    rows_sorted = sorted(rows, key=lambda r: (runtime(r), r["branch"]))

    lines: List[str] = []
    lines.append("| Branch | Avg runtime (ns) | Δ runtime vs master (ns) | Avg compile (µs) | Δ compile vs master (µs) | Total exec (ms) | Exact matches | 1-16 ULP | >65536 ULP |")
    lines.append("| --- | --- | --- | --- | --- | --- | --- | --- | --- |")
    for row in rows_sorted:
        runtime_ns = float(row["avg_runtime_ns"])
        compile_us = float(row["avg_compile_us"])
        delta_runtime = runtime_ns - master_runtime
        delta_compile = compile_us - master_compile
        lines.append(
            "| {branch} | {runtime:.3f} | {dr:+.3f} | {compile:.3f} | {dc:+.3f} | {exec_ms:.3f} | {exact} | {ulp16} | {ulp_gt} |".format(
                branch=row["branch"],
                runtime=runtime_ns,
                dr=delta_runtime,
                compile=compile_us,
                dc=delta_compile,
                exec_ms=float(row["total_exec_ms"]),
                exact=row["exact_matches"],
                ulp16=row["matches_1_16"],
                ulp_gt=row["matches_gt_65536"],
            )
        )

    output_path = Path("analysis/asmd_optimizer_review.md")
    header = "# ASMD optimizer branch benchmarks\n\n"
    with output_path.open("w", encoding="utf-8") as fh:
        fh.write(header)
        fh.write("Benchmarks collected with `run_benchmarks` on the provided branches.\n\n")
        fh.write("\n".join(lines))
        fh.write("\n")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

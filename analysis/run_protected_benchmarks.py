#!/usr/bin/env python3
"""Run protected benchmark phases in isolated processes and retain raw results."""

import argparse
import datetime
import json
import math
import os
import pathlib
import platform
import statistics
import subprocess
import sys
import tempfile
import time


CASE_MODES = (
    "protected-encode",
    "clear-compile",
    "protected-compile",
    "clear-evaluate",
    "protected-evaluate",
)
RESOURCE_MODES = ("maximum-valid", "late-invalid")
EXPECTED_CASE_COUNT = 6
LIMITS = {
    "protected_encode_ns": 1000 * 1000,
    "compile_ratio": 4.0,
    "evaluate_ratio": 1.75,
    "formula_peak_working_set_bytes": 32 * 1024 * 1024,
    "maximum_valid_elapsed_ns": 120 * 1000 * 1000 * 1000,
    "maximum_valid_peak_working_set_bytes": 32 * 1024 * 1024,
    "late_invalid_elapsed_ns": 250 * 1000 * 1000,
    "late_invalid_peak_working_set_bytes": 32 * 1024 * 1024,
}


def invoke(binary, mode, case_index, timeout_seconds, extra_arguments=()):
    command = [str(binary), mode]
    if case_index is not None:
        command.append(str(case_index))
    command.extend(str(argument) for argument in extra_arguments)
    start = time.perf_counter_ns()
    completed = subprocess.run(
        command,
        capture_output=True,
        check=False,
        text=True,
        timeout=timeout_seconds,
    )
    wall_ns = time.perf_counter_ns() - start
    if completed.returncode != 0:
        raise RuntimeError(
            "command failed with exit code {}: {}\nstdout:\n{}\nstderr:\n{}".format(
                completed.returncode,
                " ".join(command),
                completed.stdout,
                completed.stderr,
            )
        )
    lines = [line for line in completed.stdout.splitlines() if line.strip()]
    if len(lines) != 1:
        raise RuntimeError("expected one JSON line from: {}".format(" ".join(command)))
    record = json.loads(lines[0])
    expected_mode = mode.replace("-", "_")
    if record.get("mode") != expected_mode:
        raise RuntimeError("benchmark returned an unexpected mode: {}".format(lines[0]))
    if record.get("correct") is not True:
        raise RuntimeError("benchmark correctness check failed: {}".format(lines[0]))
    if not mode.startswith("prepare-") and mode != "case-count":
        peak = record.get("peak_working_set_bytes")
        if isinstance(peak, bool) or not isinstance(peak, int) or peak <= 0:
            raise RuntimeError("benchmark returned an invalid peak: {}".format(lines[0]))
    record["wall_ns"] = wall_ns
    record["stderr"] = completed.stderr
    return record


def distribution(values):
    median = statistics.median(values)
    deviations = [abs(value - median) for value in values]
    return {
        "median": median,
        "mad": statistics.median(deviations),
        "minimum": min(values),
        "maximum": max(values),
    }


def validate_resource_record(record, requested_mode, late_invalid):
    expected_mode = requested_mode.replace("-", "_")
    if record.get("mode") != expected_mode:
        raise RuntimeError("resource record has an unexpected mode")
    for name, expected in (
        ("record_count", 16384),
        ("artifact_bytes", 802880),
    ):
        value = record.get(name)
        if isinstance(value, bool) or not isinstance(value, int) or value != expected:
            raise RuntimeError("resource record has an invalid {}".format(name))
    if late_invalid is not None:
        accepted = record.get("accepted")
        if not isinstance(accepted, bool) or accepted == late_invalid:
            raise RuntimeError("resource record has invalid acceptance semantics")


def phase_values(records, case_index, mode, field):
    return [
        record[field]
        for record in records
        if record.get("case_index") == case_index and record["mode"] == mode
    ]


def summarize_cases(records, case_count):
    summaries = []
    for case_index in range(case_count):
        protected_encode = phase_values(
            records, case_index, "protected_encode", "per_expression_ns")
        clear_compile = phase_values(
            records, case_index, "clear_compile", "per_expression_ns")
        protected_compile = phase_values(
            records, case_index, "protected_compile", "per_expression_ns")
        clear_evaluate = phase_values(
            records, case_index, "clear_evaluate", "per_expression_ns")
        protected_evaluate = phase_values(
            records, case_index, "protected_evaluate", "per_expression_ns")
        clear_results = phase_values(records, case_index, "clear_evaluate", "result")
        protected_results = phase_values(
            records, case_index, "protected_evaluate", "result")
        backend_values = []
        for mode in (
            "clear_compile",
            "protected_compile",
            "clear_evaluate",
            "protected_evaluate",
        ):
            backend_values.extend(phase_values(
                records, case_index, mode, "backend"))
        phase_lengths = {
            len(values)
            for values in (
                protected_encode,
                clear_compile,
                protected_compile,
                clear_evaluate,
                protected_evaluate,
                clear_results,
                protected_results,
            )
        }
        if len(phase_lengths) != 1 or not clear_results:
            raise RuntimeError("incomplete phase records for case {}".format(case_index))
        if any(
            not math.isclose(clear, protected, rel_tol=1e-12, abs_tol=1e-12)
            for clear, protected in zip(clear_results, protected_results)
        ):
            raise RuntimeError("clear/protected result mismatch for case {}".format(case_index))
        backend_comparable = (
            all(
                not isinstance(backend, bool) and
                isinstance(backend, int) and
                backend > 0
                for backend in backend_values
            ) and
            len(set(backend_values)) == 1
        )

        compile_distribution = distribution(clear_compile)
        protected_compile_distribution = distribution(protected_compile)
        evaluate_distribution = distribution(clear_evaluate)
        protected_evaluate_distribution = distribution(protected_evaluate)
        case_records = [
            record for record in records if record.get("case_index") == case_index
        ]
        summaries.append({
            "case_index": case_index,
            "case": case_records[0]["case"],
            "backend": backend_values[0] if backend_comparable else None,
            "backend_comparable": backend_comparable,
            "protected_encode_ns": distribution(protected_encode),
            "clear_compile_ns": compile_distribution,
            "protected_compile_ns": protected_compile_distribution,
            "compile_ratio": (
                protected_compile_distribution["median"] /
                compile_distribution["median"]
            ),
            "clear_evaluate_ns": evaluate_distribution,
            "protected_evaluate_ns": protected_evaluate_distribution,
            "evaluate_ratio": (
                protected_evaluate_distribution["median"] /
                evaluate_distribution["median"]
            ),
            "peak_working_set_bytes": {
                mode: distribution(phase_values(
                    records, case_index, mode.replace("-", "_"),
                    "peak_working_set_bytes"))
                for mode in CASE_MODES
            },
        })
    return summaries


def acceptance(case_summaries, resource_summary):
    formula_peaks = []
    for summary in case_summaries:
        for phase in summary["peak_working_set_bytes"].values():
            formula_peaks.append(phase["maximum"])
    observed = {
        "backends": {
            summary["case"]: summary["backend"]
            for summary in case_summaries
        },
        "protected_encode_ns": max(
            summary["protected_encode_ns"]["maximum"]
            for summary in case_summaries),
        "compile_ratio": max(
            summary["compile_ratio"] for summary in case_summaries),
        "evaluate_ratio": max(
            summary["evaluate_ratio"] for summary in case_summaries),
        "formula_peak_working_set_bytes": max(formula_peaks),
        "maximum_valid_elapsed_ns":
            resource_summary["maximum-valid"]["elapsed_ns"]["maximum"],
        "maximum_valid_peak_working_set_bytes":
            resource_summary["maximum-valid"]["peak_working_set_bytes"]["maximum"],
        "late_invalid_elapsed_ns":
            resource_summary["late-invalid"]["elapsed_ns"]["maximum"],
        "late_invalid_peak_working_set_bytes":
            resource_summary["late-invalid"]["peak_working_set_bytes"]["maximum"],
    }
    checks = {
        name: observed[name] <= limit
        for name, limit in LIMITS.items()
    }
    checks["backend_comparable"] = all(
        summary["backend_comparable"] for summary in case_summaries)
    return {
        "passed": all(checks.values()),
        "limits": LIMITS,
        "observed": observed,
        "checks": checks,
    }


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path)
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--resource-repeats", type=int, default=1)
    parser.add_argument("--timeout-seconds", type=int, default=180)
    parser.add_argument("--diagnostic-only", action="store_true")
    return parser.parse_args()


def main():
    args = parse_args()
    if args.repeats < 1:
        raise ValueError("--repeats must be positive")
    if args.resource_repeats < 1:
        raise ValueError("--resource-repeats must be positive")
    binary = args.binary.resolve()
    case_count_record = invoke(
        binary, "case-count", None, args.timeout_seconds)
    case_count = case_count_record["count"]
    if (
        isinstance(case_count, bool) or
        not isinstance(case_count, int) or
        case_count != EXPECTED_CASE_COUNT
    ):
        raise RuntimeError(
            "protected benchmark must contain exactly {} cases".format(
                EXPECTED_CASE_COUNT))

    records = []
    preparations = []
    with tempfile.TemporaryDirectory(prefix="mexce-protected-benchmark-") as directory:
        scratch = pathlib.Path(directory)
        os.chmod(scratch, 0o700)
        case_files = {}
        for case_index in range(case_count):
            program = scratch / "case-{}.mxp".format(case_index)
            key = scratch / "case-{}.key".format(case_index)
            preparations.append(invoke(
                binary,
                "prepare-case",
                case_index,
                args.timeout_seconds,
                (program, key),
            ))
            os.chmod(program, 0o600)
            os.chmod(key, 0o600)
            case_files[case_index] = (program, key)

        resource_files = {}
        for mode in RESOURCE_MODES:
            program = scratch / "{}.mxp".format(mode)
            key = scratch / "{}.key".format(mode)
            prepare_mode = "prepare-{}".format(mode)
            preparation = invoke(
                binary,
                prepare_mode,
                None,
                args.timeout_seconds,
                (program, key),
            )
            validate_resource_record(preparation, prepare_mode, None)
            preparations.append(preparation)
            os.chmod(program, 0o600)
            os.chmod(key, 0o600)
            resource_files[mode] = (program, key)

        for repeat in range(args.repeats):
            for case_index in range(case_count):
                encode = invoke(
                    binary, "protected-encode", case_index, args.timeout_seconds)
                encode["repeat"] = repeat
                records.append(encode)

                compile_modes = ["clear-compile", "protected-compile"]
                evaluate_modes = ["clear-evaluate", "protected-evaluate"]
                if repeat % 2:
                    compile_modes.reverse()
                    evaluate_modes.reverse()
                for order, mode in enumerate(compile_modes + evaluate_modes):
                    extra_arguments = (
                        case_files[case_index]
                        if mode.startswith("protected-")
                        else ()
                    )
                    record = invoke(
                        binary,
                        mode,
                        case_index,
                        args.timeout_seconds,
                        extra_arguments,
                    )
                    record["repeat"] = repeat
                    record["order"] = order
                    records.append(record)

        for repeat in range(args.resource_repeats):
            resource_modes = list(RESOURCE_MODES)
            if repeat % 2:
                resource_modes.reverse()
            for order, mode in enumerate(resource_modes):
                record = invoke(
                    binary,
                    mode,
                    None,
                    args.timeout_seconds,
                    resource_files[mode],
                )
                validate_resource_record(record, mode, mode == "late-invalid")
                record["repeat"] = repeat
                record["order"] = order
                records.append(record)

    resource_summary = {}
    for mode in RESOURCE_MODES:
        mode_name = mode.replace("-", "_")
        selected = [record for record in records if record["mode"] == mode_name]
        resource_summary[mode] = {
            "elapsed_ns": distribution([record["elapsed_ns"] for record in selected]),
            "peak_working_set_bytes": distribution([
                record["peak_working_set_bytes"] for record in selected
            ]),
            "record_count": selected[0]["record_count"],
            "artifact_bytes": selected[0]["artifact_bytes"],
        }

    case_summaries = summarize_cases(records, case_count)
    acceptance_result = acceptance(case_summaries, resource_summary)
    report = {
        "schema_version": 1,
        "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "binary": str(binary),
        "platform": platform.platform(),
        "python": platform.python_version(),
        "repeats": args.repeats,
        "resource_repeats": args.resource_repeats,
        "diagnostic_only": args.diagnostic_only,
        "case_count": case_count,
        "case_summaries": case_summaries,
        "resource_summary": resource_summary,
        "acceptance": acceptance_result,
        "preparations": preparations,
        "runs": records,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.output.with_suffix(args.output.suffix + ".tmp")
    temporary.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    os.replace(temporary, args.output)
    print(json.dumps({
        "output": str(args.output),
        "runs": len(records),
        "case_count": case_count,
        "passed": acceptance_result["passed"],
        "limits_enforced": not args.diagnostic_only,
    }))
    if not acceptance_result["checks"]["backend_comparable"]:
        raise RuntimeError("protected benchmark backend comparison failed")
    if not args.diagnostic_only and not acceptance_result["passed"]:
        raise RuntimeError("protected benchmark exceeded an acceptance limit")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired) as error:
        print("protected benchmark failed: {}".format(error), file=sys.stderr)
        sys.exit(1)

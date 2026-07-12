# Performance stabilization runbook

This is the active Batch 6 procedure. The earlier speculative benchmark-gate
proposal remains available in Git history but is not normative: it depended on
unimplemented benchmark flags, host-specific ledgers, and mechanical review
rules that did not produce product evidence.

## Clear baseline and candidate

Use clean snapshots of the adopted baseline and candidate, with build and run
directories outside the repository.

1. Configure Release x64 builds with the same compiler and flags.
2. Compare the emitted compile commands before measuring.
3. Set `OMP_NUM_THREADS=1`.
4. Run the existing SSE2 corpus once per binary and discard both warm-ups.
5. Retain at least seven pairs, alternating baseline-first and candidate-first.
6. Keep every text report and record compile time, evaluate time, process wall
   time, exit code, compiled count, failure count, and backend counts.
7. Report median, median absolute deviation, minimum, maximum, and the
   candidate/baseline median ratio for compile and evaluate.

The release limits and the distributions that selected them are in
`analysis/performance_acceptance.md`. A timing result is invalid if compiler
flags, correctness counts, or backend counts differ between the matched pair.

## Protected formulas and resources

Build `protected_benchmark` in a protected Release configuration, then run:

```text
python analysis/run_protected_benchmarks.py <protected_benchmark> <report.json> \
  --repeats 7 --resource-repeats 3 --timeout-seconds 180
```

The runner first launches benchmark-only producer processes that write matched
programs and 32-byte keys into a private temporary directory. It then launches
encode, clear compile, prepared protected load/compile, clear evaluate, and
prepared protected load/evaluate in separate processes. Resource load processes
consume separately prepared maximum-valid and late-invalid files. The temporary
directory is removed on success or failure, and issuer work never contributes
to load-process peak working set.

The runner alternates clear/protected order, preserves every process record,
checks independent numeric oracles, requires one identical stable backend for
all clear/protected compile/evaluate records in each case, and rejects missing
or non-positive WSS. Maximum-valid and late-invalid must report exactly 16,384
records and 802,880 bytes with the expected acceptance semantics.

The runner writes its report before returning failure for an exceeded limit,
so a failed gate retains its causal evidence. CI uses three formula repeats and
one resource repeat as a continuous release diagnostic; release acceptance uses
the seven/three command above on every supported compiler.

CTest invokes the same preparation flow with one repeat in diagnostic mode.
Correctness, WSS, resource-schema, and backend failures remain fatal there;
numeric distribution ceilings are enforced by CI's three-repeat run and the
seven-repeat release run, not by a single-sample smoke.

## Profiles

Profile only after a repeated distribution exposes material movement.

- Prefer a system sampler that can resolve the affected process and JIT code.
- If system sampling is unavailable, record the failure and use an available
  instrumenting profiler for native compile/load paths.
- Use an evaluation-dominant run for evaluate and a one-iteration corpus run
  for clear compile.
- Profile protected encode, protected load, maximum-valid, and late-invalid in
  distinct processes.
- State explicitly when a profiler cannot symbolize JIT-generated code.

Timing deltas alone do not authorize production changes. A retained
optimization needs a profile-supported cause, a single bounded change,
before/after measurements on the exposing workload, and affected correctness,
security, lifecycle, portability, and packaging gates.

## Evidence hygiene

Raw reports, build trees, profiler output, and scratch snapshots stay outside
the repository. The repository retains the reproducible harness, selected
limits, compact release conclusions, and CI job. Record all timeouts, tool
failures, discarded configurations, and unavailable profilers; baseline
provenance affects disposition but does not erase a failure.

Review and planning metadata are not acceptance evidence. Reviewer counts,
verdict labels, commit trailers, and machine-local evidence paths cannot make a
failing workload pass. Stop an experiment when it does not change a product
conclusion instead of adding another review or bookkeeping layer.

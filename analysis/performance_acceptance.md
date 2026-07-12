# Release performance acceptance

Date: 2026-07-12

Candidate `f868ca40bad8218229f599ecb4fde1bcd058eaec` was compared with baseline
`ea7c1ce` before benchmark-only changes. Builds were Release x64, used identical
flags within each pair, set `OMP_NUM_THREADS=1`, discarded one warm-up, and ran
seven retained pairs in alternating order. Raw reports and structured JSON were
kept outside the repository; they are intentionally transient build evidence.
The durable runner reproduces the protected report shape.

## Clear corpus

The existing 44,229-expression SSE2 corpus was used without modification.
Every retained run compiled all 44,229 expressions with zero compilation
failures. Baseline and candidate backend counts matched on every run:

- MSVC: 42,250 SSE2 and 709 x87;
- GCC and Clang: 42,441 SSE2 and 518 x87; and
- the remaining expressions were constant and selected no executable backend.

| Compiler | Flags | Compile baseline/candidate median | Compile change | Evaluate baseline/candidate median | Evaluate change |
| --- | --- | ---: | ---: | ---: | ---: |
| MSVC 19.44.35228 | `/O2 /W4 /permissive- /bigobj` | 79.424/72.785 us | -8.36% | 7.134/7.592 ns | +6.42% |
| GCC 13.3.0 | `-O3 -DNDEBUG -Wall -Wextra -Wpedantic` | 55.702/54.089 us | -2.90% | 6.942/6.497 ns | -6.41% |
| Clang 18.1.3 | `-O3 -DNDEBUG -Wall -Wextra -Wpedantic` | 57.342/55.957 us | -2.42% | 7.312/7.299 ns | -0.18% |

Median absolute deviations for baseline/candidate were 4.823/8.193 us and
0.292/0.367 ns on MSVC, 5.091/4.034 us and 0.530/0.255 ns on GCC, and
3.133/5.043 us and 0.447/0.426 ns on Clang. The MSVC evaluation interquartile
ranges overlap; the movement does not reproduce on either Linux compiler.

The release limits are a candidate/baseline median ratio of 1.15 for compile
and 1.10 for evaluate, with identical correctness and backend counts. These
limits exceed the observed worst ratios of 0.976 and 1.064 and leave room for
the measured dispersion without accepting a persistent cross-platform
regression.

## Protected formulas

`run_protected_benchmarks.py` runs six formulas covering arithmetic,
transcendentals, powers, rounding, comparison, and mixed libm operations.
Encode, clear compile, protected compile, clear evaluate, and protected
evaluate each run in a separate process. Separate preparation processes write
the matched program and 32-byte key into a private temporary directory; load,
compile, evaluate, and resource processes only consume those prepared files.
Issuer allocation and encoding therefore cannot contribute to their peak
working set. Compile modes contain 25 timed
replacements after a discarded warm-up; evaluate modes contain 100,000 timed
evaluations after 1,000 discarded evaluations. Seven retained process runs per
formula and phase produced 210 formula records per compiler.

All 630 formula records passed their mode checks; the 504 compile/evaluate
records also passed their independent numeric oracle. Within every case, all
retained clear/protected compile/evaluate records selected one stable identical
backend. Median protected/clear ranges were:

| Compiler | Encode observed range | Compile ratio range | Evaluate ratio range | Maximum formula peak |
| --- | ---: | ---: | ---: | ---: |
| MSVC 19.44.35228 | 17.1-30.0 us | 1.32-1.94 | 0.94-1.03 | 4.68 MiB |
| GCC 13.3.0 | 36.4-58.7 us | 2.06-2.61 | 0.95-1.02 | 15.12 MiB |
| Clang 18.1.3 | 40.2-66.3 us | 2.12-2.65 | 0.97-1.00 | 15.12 MiB |

The continuous limits are 1 ms encode time, a 4.0 compile ratio, a 1.75
evaluate ratio, and 32 MiB peak working set. The evaluate allowance accounts
for an early single-run ratio of 1.61 that disappeared in the seven-run
distribution; it is not derived only from the final favorable medians.

## Exact-limit resources

The valid and late-invalid fixtures contain exactly 16,384 authenticated
records and 802,880 bytes. Each mode ran three times in an isolated process.
All valid artifacts loaded; all final-frame corruptions rejected.

| Compiler | Maximum-valid min/median/max | Maximum-valid peak max | Late-invalid median/max | Late-invalid peak max |
| --- | ---: | ---: | ---: | ---: |
| MSVC 19.44.35228 | 32.38/32.44/32.71 s | 21.61 MiB | 31.65/39.93 ms | 14.86 MiB |
| GCC 13.3.0 | 10.49/11.86/11.94 s | 15.12 MiB | 18.64/25.18 ms | 15.12 MiB |
| Clang 18.1.3 | 10.75/11.39/11.97 s | 15.25 MiB | 22.04/26.46 ms | 15.25 MiB |

The limits are 120 seconds and 32 MiB for maximum-valid, and 250 milliseconds
and 32 MiB for late-invalid. The time limits apply to every retained run, not
only the median. They leave roughly 3.7 times the slowest valid observation and
6.3 times the slowest invalid observation while still bounding release work.

## Profiles and optimization decision

GCC 13 `-O3 -pg` profiles used the same optimized sources with profiling
instrumentation. Clear compile self time was distributed across list teardown
(15%), the existing arithmetic optimizer (10%), argument linking (5%),
commutative normalization (5%), and semantic production (5%). Evaluation is
JIT-generated code, which gprof cannot symbolize; the evaluation-dominant run
showed no candidate-owned native hotspot.

The protected profiles were rebuilt from the corrected harness with GCC 13
`-O3 -pg`. Three producer processes prepared the case, maximum-valid, and
late-invalid files. Every gmon file below came from a separate consumer process
and directory, so issuer work is absent from load-process attribution.

- Protected encode took 2.16 ms for 25 encodes and accumulated no 10 ms gprof
  sample. It is below profiler resolution, so no hotspot attribution is made.
- Prepared protected load/compile took 1.77 ms for 25 loads and accumulated no
  sample. This demonstrates no sampled hotspot, not that its native helpers are
  free.
- Prepared protected evaluate took 1.08 ms for 100,000 evaluations and
  accumulated no sample. Its generated code is also outside gprof's symbol
  model, so no native or JIT hotspot is claimed.
- Prepared late-invalid took 31.7 ms and accumulated only two samples. gprof
  assigned one to a function unreachable from that consumer and one to a
  trivial reachable CLI-dispatch string comparison whose 10 ms attribution is
  implausible. The samples are too sparse for attribution; only elapsed time,
  rejection, and peak WSS are retained as evidence.
- Prepared maximum-valid ran long enough to profile. Sampled self time was
  43.79% list teardown, 36.74% list-of-lists insertion, 9.50% commutative
  normalization, and 9.24% function disposal. Secretstream pull accounted for
  0.04%. This independently confirms optimizer/list work as the bounded
  maximum-case cost.

The transient external profile inventory retains the producer and consumer
results, `/usr/bin/time -v` reports, gmon files, gprof reports, commands,
toolchain flags, file sizes, and SHA-256 checksums. These profiles justify no
additional optimization.

A provisional MSVC `__forceinline` annotation for the generated-code call was
tested against the unmodified candidate with discarded warm-ups and seven
alternating pairs. Evaluation changed from 7.610 ns median (0.305 ns MAD) to
8.309 ns (0.641 ns MAD), and compile changed from 70.147 us (4.662 us MAD) to
81.359 us (13.512 us MAD). The annotation was removed. No production
optimization is retained because no causal material regression remains.

## Tool and run failures

- An initial Windows configure selected MinGW for one build and failed to find
  `cl` for the other. Both were discarded; fresh directories pinned MSVC.
- One combined resource command timed out after the old 802,831-byte valid
  fixture completed, before late-invalid ran. Neither result was acceptance
  evidence.
- A four-configuration Linux command timed out after completing the GCC
  configurations. The missing Clang configurations were run separately.
- A GCC build orchestration command timed out, but its child completed. Target
  existence and compile flags were verified before measurement.
- Cloning from a linked Windows worktree failed because its `.git` pointer was
  not portable to WSL. Clean ext4 clones were made from the primary repository.
- Windows Performance Recorder could not enable CPU profiling (`0xc5585011`),
  Linux `perf` was unavailable, and one `dumpbin` symbol scan timed out. GCC
  gprof and `/usr/bin/time -v` were the available profile/resource tools.
- One experimental force-inline candidate failed its A/B gate and was removed.
- The first structured runner stopped on a rounding-oracle mismatch. MEXCE's
  round-to-even result was confirmed, the harness oracle changed to
  `nearbyint`, and the complete run was restarted.
- One protected-profile shell loop lost a variable across PowerShell and Bash;
  it ran no benchmark. Explicit mode commands produced the retained profiles.
- A one-repeat Clang CTest diagnostic observed a 4.18 compile ratio against the
  distribution limit of 4.0. CTest remains fatal for correctness, resource,
  WSS, and backend failures but does not enforce numeric limits with one sample;
  the three-repeat CI and seven-repeat release runners do.
- The first phase-isolated GCC release run observed a 4.011 compile ratio because
  benchmark key allocation was inside the timed compiler call. Key ownership is
  now prepared before timing, matching the public API boundary; all three
  seven-repeat acceptance runs were restarted and passed without changing a
  limit.
- The first durable report relied on a legacy combined-process protected
  load/compile profile. It was not accepted as phase-isolated evidence. The GCC
  profiling target and all protected profiles were rebuilt from the corrected
  prepared-artifact harness; the conclusions above replace the legacy profile.

Inspection-command path and optional-file errors produced no repository
changes and were not used as product evidence.

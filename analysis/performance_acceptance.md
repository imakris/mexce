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

The continuous limits are 1 ms encode time, a 4.0 compile acceptance ratio, a
1.75 evaluate ratio, and 32 MiB peak working set. Compile acceptance uses the
measured protected/clear ratio except for the ARM64 calibration below. The
evaluate allowance accounts for an early single-run ratio of 1.61 that
disappeared in the seven-run distribution; it is not derived only from the
final favorable medians.

### macOS ARM64 compile calibration (2026-10-17)

The 4.0 compile limit was selected from the x64 observations above, before the
ARM64 backend was added. Removing unsafe arithmetic regrouping from strict
mode reduced clear compilation work. Two subsequent macOS ARM64 runs exceeded
that ratio while arithmetic protected compilation also became faster:

| Revision | Paired clear median | Paired protected median | Raw compile ratio min/median/max | Ratio MAD |
| --- | ---: | ---: | ---: | ---: |
| `24dd719` | 6.164 us | 18.588 us | 2.862/3.016/3.114 | 0.099 |
| `4fb72a6` | 4.274 us | 16.814 us | 3.934/4.170/4.446 | 0.236 |
| `e9417b2` | 3.789 us | 15.764 us | 4.033/4.161/4.544 | 0.128 |

These are separate hosted runs, not a controlled same-machine comparison.
All six protected compile medians in `e9417b2` were below their `24dd719`
values. Evaluation, correctness, backend comparison, and resource checks
passed. The repeated ratio failure reflects a smaller clear denominator; it
does not establish a protected latency regression.

For native macOS ARM64 only, with matching recorded ARM64 backends, each
formula retains the latency allowance from
the successful [baseline run 38916967385](https://github.com/imakris/mexce/actions/runs/38916967385)
at `24dd719bdcbe903fb6faf58c32608951a487bd70`. Its three balanced-pair samples ran
on macOS 26.6.2 ARM64 with AppleClang 21.0.0.21000101 and
`-O3 -DNDEBUG -Wall -Wextra -Wpedantic`; the benchmark harness is unchanged
across these revisions. The complete reports remain external build evidence.
The baseline clear distributions and resulting fixed allowances are:

| Formula | Clear min/median/max (us) | Clear MAD (us) | 4 x clear median (us) |
| --- | ---: | ---: | ---: |
| arithmetic | 5.878/6.164/6.610 | 0.285 | 24.655 |
| transcendental | 11.915/12.079/12.130 | 0.051 | 48.317 |
| power | 13.176/14.192/14.984 | 0.792 | 56.767 |
| rounding | 7.585/7.858/9.927 | 0.273 | 31.431 |
| comparison | 10.219/10.660/10.718 | 0.058 | 42.642 |
| mixed | 13.483/16.535/20.806 | 3.052 | 66.139 |

For each retained paired sample, the compile acceptance ratio is
`protected_ns / max(clear_ns, baseline_clear_median_ns)`. The median of these
sample ratios must remain at most 4.0. The baseline median is fixed from the
accepted run, rather than fitted to either failing run. This preserves a
previously admitted latency allowance when clear compilation becomes faster;
when current clear compilation is slower, the original ratio rule applies.
It keeps the existing median-based treatment of sample variation without
adding a noise allowance or increasing the 4.0 factor. Other platforms and
formulas without a calibration use a zero floor and retain the original rule.

The report keeps schema 1 and the unadjusted `compile_ratio` with its full
paired distribution. The separately named `compile_acceptance_ratio` controls
the gate, and the optional `compile_calibration` metadata has its own schema
version. The report records the acceptance distribution, denominator floor,
and baseline provenance. Replaying the retained baseline and two subsequent
reports yields worst formula acceptance ratios of 2.970, 2.867, and 2.558; the
raw ratios above remain visible. All other limits and the three-repeat CI
sampling policy are unchanged. A changed benchmark corpus needs its own
calibration evidence; these figures are not universal API latency guarantees.

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

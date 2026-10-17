# Building and deployment

Ordinary consumers only need `mexce.h`; CMake is for installation, tests,
benchmarks and the optional protected-expression tools. The source project
requires CMake 3.16 or newer and C++11.

The shell recipes below use a native macOS or Linux toolchain and Ninja.
Windows builds follow the [Varinomics Windows toolchain policy](https://github.com/Varinomics/varinomics-standards/blob/master/varinomics_llm_tooling_windows.md).
Use separate build trees for different architectures, generators and compilers.

## Building on Intel macOS

Use Xcode Command Line Tools, CMake, and Ninja on an Intel Mac:

```sh
queued-build --slots 1 -- cmake -S . -B build-intel -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=x86_64
queued-build --slots 3 -- cmake --build build-intel --parallel 3
ctest --test-dir build-intel --output-on-failure
```

Intel macOS supports both x86-64 backends: SSE2 and x87.

## Building on Apple Silicon

Use Xcode Command Line Tools and CMake from a native ARM64 terminal:

```sh
queued-build --slots 1 -- cmake -S . -B build-arm64 -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
queued-build --slots 3 -- cmake --build build-arm64 --parallel 3
ctest --test-dir build-arm64 --output-on-failure
```

Ninja must be installed when using this generator. OpenMP is optional and is
used only as an additional benchmark timer source; the benchmarks also work
with the standard C++ clock supplied by Apple Clang.

Apple Silicon uses native generated code without Rosetta.

## macOS executable memory

Host applications signed with the hardened runtime need the entitlement for
their native architecture:

| Architecture | Required entitlement |
| :--- | :--- |
| Apple Silicon ARM64 | [`com.apple.security.cs.allow-jit`](https://developer.apple.com/documentation/bundleresources/entitlements/com.apple.security.cs.allow-jit) |
| Intel x86-64 | [`com.apple.security.cs.allow-unsigned-executable-memory`](https://developer.apple.com/documentation/bundleresources/entitlements/com.apple.security.cs.allow-unsigned-executable-memory) |

The callback-only `com.apple.security.cs.jit-write-allowlist` entitlement is not
supported. Ordinary command-line builds do not require hardened-runtime signing.

On Apple Silicon, generated code shares one `MAP_JIT` arena that reserves
1 GiB of virtual address space by default. Physical pages are allocated on
demand, and freed code ranges are reused. Each executable buffer occupies whole
operating-system pages, even when its generated code is smaller than one page.
To change the capacity in bytes, define `MEXCE_APPLE_JIT_ARENA_SIZE` before
including `mexce.h`, with the same value in every translation unit. Capacity
is rounded down to whole pages and must hold at least one page.
Compilation throws `std::bad_alloc` when the arena cannot provide a sufficiently
large contiguous range; a failed replacement leaves the evaluator empty.

Hardened Runtime permits one `MAP_JIT` region per process. Apple Silicon
integration requires one shared mexce arena and no independent `MAP_JIT`
allocator in that process. The header does not coordinate arenas with other
JIT runtimes or separately linked copies in libraries with hidden symbol
visibility.

## Protected expressions

Provide libsodium 1.0.22 or newer through the host toolchain's Conan, vcpkg or
pkg-config integration. Pass that toolchain's normal CMake options when
configuring, for example `CMAKE_TOOLCHAIN_FILE` for a generated toolchain.

```sh
queued-build --slots 1 -- cmake -S . -B build-protected -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -DMEXCE_ENABLE_PROTECTED_EXPRESSIONS=ON \
  -DMEXCE_BUILD_ISSUER_TOOLS=ON
queued-build --slots 3 -- cmake --build build-protected --parallel 3
cmake --install build-protected --prefix install
```

Installed protected consumers link `mexce::protected`. Issuance and runtime key
ownership are described in [protected expressions](protected_expressions.md).

## Building the Benchmarks

The benchmark harness is included in the repository and can be run using CMake:

```bash
# Configure and build the project
queued-build --slots 1 -- cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
queued-build --slots 3 -- cmake --build build --parallel 3

# Run quick validation tests
ctest --test-dir build

# Run the standalone benchmark at its default evaluation budget
./build/benchmark 100000 build/benchmark_results-default.txt
```

The standalone [benchmark](../test/benchmark.cpp) defaults to **100,000
evaluations per expression per timing round**. Running it without arguments
uses that budget and writes `benchmark_results.txt` in the working directory.
The explicit command above selects the same budget with a separate report path.
On Windows, invoke the built `benchmark.exe`; a multi-configuration Release
build places it at `build\Release\benchmark.exe`.

Comprehensive mode is the default. It processes the full 44,229-expression
[corpus](../test/benchmark_expressions.h) and reports SSE2, SSE2+fast-math, x87,
and x87+fast-math configurations on x86, or ARM64 and ARM64+fast-math on Apple
Silicon. The report includes native compiler comparisons and per-expression
results. Backend counts distinguish generated code, fallback choices and
constant expressions. Use `--single` only when a deliberately narrower
configuration is wanted.

The [CMake `run_benchmarks` target](../CMakeLists.txt), used by the full
[CI validation workflow](../.github/workflows/validate.yml), explicitly passes
**100** iterations. The `benchmark_quick` and `benchmark_output_first` CTest
smokes pass **5**. These use the same corpus but much shorter timed loops;
neither invokes the standalone default budget. A successful CI diagnostic
does not establish the result of a 100,000-iteration run.

The executable selects the number of timing rounds from the iteration count:

| Evaluations per expression per round | Timing rounds |
| ---: | ---: |
| 1–999 | 7 |
| 1,000–9,999 | 5 |
| 10,000 or more | 3 |

Thus the standalone default uses three rounds; the CI diagnostic and quick
smokes use seven. Increasing iterations across a threshold changes both loop
length and round count. Record both when comparing measurements.

Using a protected build with `BUILD_TESTING=ON`, run the protected
benchmark with:

```bash
ctest --test-dir build-protected -C Release -R mexce_protected_benchmark --verbose
python analysis/run_protected_benchmarks.py \
  build-protected/protected_benchmark protected_benchmark_results.json \
  --repeats 3 --resource-repeats 1
```

The protected runner has its own phase timing and repetition settings; the
ordinary benchmark's iteration argument and adaptive rounds do not configure it.

## Performance evidence

For every change, carefully inspect the full benchmark's numerical accuracy
and performance against a suitable prior baseline. Review every configuration,
including native-compiler and reference comparisons, the complete error
distribution and its tails, per-expression changes, compilation or evaluation
failures, exclusions, timings, and backend coverage. Investigate and explain
regressions; a successful benchmark exit or green CI alone is not numerical
acceptance. Both modes simplify algebraically; default mode retains grouped
complete cancellations, while `fast_math` can remove them and zero identities.
Neither mode is uniformly more accurate. Assess their numerical and performance
tradeoffs together rather than treating either mode's name as an accuracy ranking.

The [benchmark](../test/benchmark.cpp) measures numerical accuracy at one fixed
assignment (`a=1.1`, `b=2.2`, `c=3.3`, `x=4.4`, `y=5.5`, `z=6.6`, `w=7.7`);
increasing timing iterations does not sample more inputs. Its stored
[reference results](../test/benchmark_results.h) use SymPy rational arithmetic
with a NumPy fallback, rather than exact arithmetic over every parsed binary
input. Native compiler output is another comparator. Comparisons where both
values have magnitude at most `1e-12` count as exact, and NaN comparisons are
excluded from ULP totals. Inspect these cases separately; aggregate counts do
not establish correct rounding or behavior over the full input domain.

### Reading timing results

Each expression is compiled once per configuration. Compilation timing covers
`set_expression`, with evaluator construction and variable binding outside
that interval. Evaluation reuses that compiled instance, processing chunks of
up to 1,024 expressions in round-robin order. Each round times the requested
number of `evaluate()` calls for an expression; its median round duration is
the representative evaluation time. These internal rounds are separate from
repeating the executable in independent processes.

In the comprehensive timing table, `Avg Eval` is the arithmetic mean of those
per-expression medians divided by the iteration count. `Avg Compile` averages
the single compilation measurements. Both cover the reported intersection of
expressions with a native implementation that compile in every configuration.
Check exclusions and evaluation failures separately. `Total Eval` sums the
representative durations; it is neither elapsed process time nor the sum of all
timing rounds. The headline `Native` row uses the first configuration's native
measurements, SSE2 or ARM64. Use each configuration's detailed section for its
own native comparison.

The summary retains fractional nanoseconds and displays up to three decimal
places in its chosen unit. Per-expression evaluation times are first rounded
to whole nanoseconds, so averaging or subtracting those displayed rows cannot
reconstruct the fractional summary or attribute a sub-nanosecond difference.
Keep the original reports, use their summary values for aggregate comparisons,
and distinguish measurement precision from extra digits obtained by averaging
rounded summaries.

### Comparing revisions or compilers

Use the standalone 100,000-iteration budget for a comparison of default
benchmark performance. A short-loop diagnostic answers a different question:
clock reads, loop setup and initial execution costs are amortized over fewer
calls. Do not extrapolate its compiler gap to the default budget. When varying
iterations deliberately, account for the adaptive round thresholds as well.

For a repeated comparison:

1. Build baseline and candidate in separate Release trees, without coverage,
   sanitizers or profiling instrumentation. Match architecture, harness,
   corpus, flags, libraries and mexce options; if the compiler is the factor
   under test, record both versions and keep the other inputs matched. Retain
   source revisions, executable identities and actual compile commands.
2. Run on the same machine with a consistent CPU affinity and power policy,
   and avoid concurrent builds or other heavy work. On a virtual machine,
   affinity selects a guest CPU; it does not guarantee an exclusive physical
   core. A hosted runner label alone does not establish matched hardware.
3. Choose the repeat count, order and warm-up policy before measuring, and
   apply that policy consistently to both binaries. If using separate warm-up
   invocations, label and retain their reports separately from the measurement
   set. Run retained measurements in repeated balanced blocks such as
   baseline–candidate–candidate–baseline, using the same iteration count and
   configuration throughout. Write each report to a distinct path and retain
   the progress output, which records the number of rounds.
4. Check the numerical results, corpus intersection and actual backend counts
   before interpreting a speed change. Preserve all completed observations,
   identify failed or incomplete runs, and report the run order and spread
   alongside a stated aggregate such as the median. A best single invocation
   or a small number of samples does not establish a persistent improvement.

The native compiler measurement is a useful control, but it has its own
generated bodies and calling loop. A correlated movement can motivate checking
execution conditions; it does not identify the cause or justify correcting
mexce timings by a universal native ratio.

### Investigating compiler differences

The measured evaluation loop includes the compiler-generated caller as well
as mexce's generated expression. Identical normalized JIT bodies therefore do
not establish equal end-to-end cost: the evaluator wrapper, pointer loads,
indirect call, loop control, code and data placement, and runtime math library
can still differ. Normalizing embedded addresses also hides real placement
differences. Inspect both the emitted body and the host caller before assigning
the gap to either one.

Test a proposed caller change in isolation with matched builds and the full
evaluation budget, and recheck generated code and numerical results. A change
such as hoisting a pointer or forcing inlining can alter surrounding compiler
code as well; its timing difference does not measure the cost of one load.
Likewise, a fit to two iteration counts can suggest an amortized overhead but
cannot establish its cause or predict performance across other workloads.

Attribute comparisons to their exact source, corpus, harness, compiler, and
build options. Reuse retained measurements when those inputs are unchanged,
as with a documentation-only change; otherwise inspect the reports produced
by applicable CI. This requirement does not call for duplicate benchmark runs
with identical measured inputs.

The [release performance acceptance](../analysis/performance_acceptance.md)
records its exact candidate, baseline, compiler inputs, measurement method,
resource checks and limitations. It is evidence for those inputs rather than a
performance guarantee for every expression, machine or backend.

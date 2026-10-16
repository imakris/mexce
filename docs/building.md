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

# Run the full performance benchmark
queued-build --slots 1 -- cmake --build build --target run_benchmarks --parallel 1
```

Using a protected build with `BUILD_TESTING=ON`, run the protected
benchmark with:

```bash
ctest --test-dir build-protected -C Release -R mexce_protected_benchmark --verbose
python analysis/run_protected_benchmarks.py \
  build-protected/protected_benchmark protected_benchmark_results.json \
  --repeats 3 --resource-repeats 1
```

## Performance evidence

For every change, carefully inspect the full benchmark's numerical accuracy
and performance against a suitable prior baseline. Review every configuration,
including native-compiler and reference comparisons, the complete error
distribution and its tails, per-expression changes, compilation or evaluation
failures, exclusions, timings, and backend coverage. Investigate and explain
regressions; a successful benchmark exit or green CI alone is not numerical
acceptance. Default algebraic simplification is part of MEXCE's accuracy
design; `fast_math` adds relaxed special-value assumptions and is not required
to obtain that simplification.

Attribute comparisons to their exact source, corpus, harness, compiler, and
build options. Reuse retained measurements when those inputs are unchanged,
as with a documentation-only change; otherwise inspect the reports produced
by applicable CI. This requirement does not call for duplicate benchmark runs
with identical measured inputs.

The [release performance acceptance](../analysis/performance_acceptance.md)
records its exact candidate, baseline, compiler inputs, measurement method,
resource checks and limitations. It is evidence for those inputs rather than a
performance guarantee for every expression, machine or backend.

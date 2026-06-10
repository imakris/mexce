# mexce Improvement Report

Date: 2026-06-10. Produced by a 35-agent analysis run: branch diff catalog + adversarial review,
full architecture map of mexce.h, benchmark-infrastructure audit, 4 performance-idea lenses with
per-idea adversarial verification against the actual code, reorganization proposal, and an
optimization-process runbook with a completeness-critic pass.

Companion files:
- `analysis/optimization_runbook.md` — the iterative benchmark-driven optimization process (with required amendments appended)
- `analysis/perf_ideas_verified.json` — full detail for every verified performance idea (seed for the runbook's idea bank)

---

## 1. Remote branch: `origin/claude/library-review-AhdW4` (commit d1c012c)

The only remote topic branch. Single commit on top of current master (+512/−116), bundling
12 separable changes. **Verdict: genuinely valuable, but NOT mergeable as-is** — one blocker,
three majors. The adversarial review compiled probe programs against both headers to confirm
findings empirically.

### Harvest as-is (clean cherry-picks)
- `mexce_parsing_exception::position()` accessor (+ tests).
- Platform generalization: `MEXCE_PLATFORM_WIN/POSIX`, macOS-Intel/BSD support, `MAP_ANON` shim,
  `#error` on unsupported platforms (master silently produced UB there). No CI covers the new
  platforms — consider a macos-13 (Intel) job or soften the README claim.
- Portable `get_page_size()` + `round_up_to_page()` for mmap/mprotect/munmap sizing
  (master's `get_page_size()` was dead code).
- `evaluate(const std::string&)` now propagates options to the temp evaluator (one-line bug fix;
  note it is a behavior change for callers with prefer_x87/enable_cse set).
- set_expression exception-safety rework + `evaluate()` null-fptr guard. The null check was
  **measured** (A/B interleaved, 200M evals): 2.436/2.453/2.467 ns master vs 2.434/2.465/2.464 ns
  branch — within noise. Master segfaults on evaluate() after a failed set_expression; the branch
  throws cleanly and also fixes a master bug where a failed SSE2→x87 fallback leaks
  `prefer_x87=true` permanently.
- Parser DoS guards (1 MiB length / 256 nesting). Fix two nits while harvesting: move the
  `MEXCE_MAX_*` macro definitions from mid-function to header top; length check runs after
  `e += " "` (off-by-one).
- Sanitizer CI workflow — but restrict it to `ctest -R mexce_unit_tests`; as written it runs the
  full 44k-expression benchmark twice per compiler under ASan+UBSan on every push.

### Fix before harvesting
1. **BLOCKER — double-free in `scoped_executable_buffer`**: `lock_executable_buffer()` frees the
   buffer before throwing on mprotect/VirtualProtect failure, but `lock()` leaves `m_ptr` set, so
   the destructor frees the same range again. Silent today (munmap of an unmapped range returns 0,
   which is also why the branch's own ASan CI and force-failure test pass), but in a multithreaded
   process it can destroy another thread's fresh mapping. Fix: null `m_ptr` before calling
   `lock_executable_buffer` (~3 lines). The class comment claims the opposite of what the code does.
2. **MAJOR — strtod locale break**: under a comma-decimal `LC_NUMERIC` locale, every fractional
   literal now throws "Malformed numeric literal" (empirically confirmed: master computes
   `1.5 + 1` = 2.0 under German_Germany.1252; branch throws). Master's atof was silently wrong;
   the branch is loudly broken. Needs locale-independent parsing (manual parse / `from_chars` /
   `strtod_l`), keeping the good ERANGE handling.
3. **MAJOR — `enable_cse` forces x87 unconditionally on x64**, even when CSE finds nothing
   (confirmed: `y + 1` compiles to x87 on the branch). Silent per-eval slowdown + bitwise result
   changes for users who left enable_cse on. Better design: fall back to x87 only when CSE
   actually fires, or require explicit `use_x87_backend()`.
4. **MAJOR — `options::use_libm_*` removal is a source-level API break** (master's own test suite
   fails to compile against the branch header). The fields are verified dead, so removal is right
   long-term — but stage it as a deprecation, not folded into a hardening commit.

Minor: failed `set_expression("")` after a constant expression still returns the stale constant
(contradicts the branch's own invalidation contract); CHANGELOG/comment inaccuracies.

### Incidental discovery (file against master separately)
`x-x→0` and `x/x→1` are applied by the asmd_optimizer even in strict (non-fast_math) mode,
contradicting the options documentation (confirmed: strict `x/x` with x=0 returns 1.0; `y-y` with
y=NaN returns 0.0).

---

## 2. Master bugs found during verification (independent of the branch)

File and fix these separately — B1/B3 before baselining any golden-codegen hashes:

- **B1 (miscompilation)**: `emit_sse2_libm_binary_call` arg1_reg==1 branch (mexce.h ~3755–3759)
  clobbers arg1 — the callee receives (arg2, arg2) when a binary libm call happens at virtual-stack
  depth 3. NOTE: `a + logb(2,x)` is NOT a reproducer — the optimizer rewrites it to
  `logb(2,x)+a`, which keeps the call at a safe depth. Verified reproducer (clean master,
  MinGW -O2, a=2 b=4 x=8): `logb(a, b + logb(2,x))` returns 2.321928 instead of 2.807355.
  Credit to external review (ChatGPT) for the correction; confirmed empirically on this machine.
- **B2**: `>` is missing from `is_operator` (~4902) although docs, `get_infix_rank`, and `Gt()`
  all support it — the documented `>` operator throws at parse time.
- **B3 (leak)**: constant folding JIT-compiles each foldable subtree and leaks one executable page
  per fold (`compile_and_finalize_elist` frees the previous buffer only in set_expression and dtor).
- **B4**: SSE4.1 `roundsd` (~3674–3695) is emitted with no CPUID gate — crashes on pre-SSE4.1 CPUs.
- **B5**: strict-mode algebraic transforms (see §1 incidental discovery).
- **B6**: inconsistent `-0.0` handling between `compile_elist` (signbit guard) and
  `emit_load_constant` (emits +0.0 `fldz` for −0.0).

---

## 3. Verified performance ideas (genuine speedups only)

24 ideas from 4 lenses; each adversarially verified against the code and the 44,229-expression
corpus. 22 confirmed real, 2 refuted on impact. Several were found independently by multiple
lenses (strong signal). Ranked by verified expected impact; full details with code locations in
`analysis/perf_ideas_verified.json`.

### Tier 1 — do these
1. **Raise the SSE2 virtual-stack depth cap (+ home the stack in xmm6–15 on Win64)** — ~1% of
   expressions currently fall back to x87 for being "too deep", but they are the expensive tail
   (~190 ns vs ~6 ns average). Migrating them was measured ~17% faster per affected eval →
   **~4–5% aggregate eval-time reduction**, plus ~1% compile time (kills ~80% of the 455
   double-parse fallbacks). The single biggest verified win.
2. **Host-evaluate constant folding instead of JIT-compile-and-execute per fold** — compile-time
   only: **~8% total compile-time reduction** (measured 1573 ms → ~1440 ms on the corpus), ~2×
   for constant-heavy expressions. Verified bit-identical results on all 44,229 expressions.
   Also eliminates the B3 leak by design.
3. **Fuse double-variable loads as memory operands** (`addsd xmm, [rax]` instead of
   `movsd + addsd`) — found independently by 4 lenses. Saves one fused-domain uop + 4–5 bytes per
   binary op with a variable RHS, the dominant pattern. Expect ~1–3% average; small effort, low risk.
4. **xmm register-content tracking** (reuse repeated variable/constant values via register copies
   instead of reloads) — verified 0.5–3% average; biggest on small expressions with repeated
   variables (69% of corpus has repeats).
5. **`movaps` instead of `movsd` for reg-reg copies** (move elimination, kills the false
   dependency through the destination's upper lane) — found by 2 lenses. Trivial, strictly
   non-negative; 3–5% on `x*x`-shaped expressions via the dup peephole, ~0.5% average.

### Tier 2 — small but real, cheap
6. Elide `sub/add rsp,8` prologue/epilogue for call-free SSE2 functions (~0–1.5%, ~20% code-size
   cut on tiny functions).
7. Multi-register address cache (RCX/RDX/R8–R11) instead of the single RAX slot — modest average,
   ~26–28% body shrink on polynomial-class expressions.
8. RIP-relative neg/abs sign masks from a 16-byte constant pool instead of `mov rax,imm64` +
   `xorpd/andpd [rax]` — touches 11% of corpus expressions, ~0.1–0.3% aggregate.
9. **Batch/array evaluate API** (JIT a loop around the body) — measured fixed per-call overhead is
   ~1.0–1.3 ns, so **2–3× on trivial expressions** for users evaluating over arrays; near-zero for
   libm-bound ones. New API, exact results; worth doing for the library's stated use cases
   (simulations, data kernels) even though the current benchmark harness wouldn't show it.

### Tier 3 — real but tiny on this corpus (keep in the idea bank, don't lead with them)
SSE2 integer-pow lowering without whole-expression x87 demotion (only 0.38% of corpus contains
`^`); SSE2 CSE (0.06% of corpus has surviving duplicate subtrees); x87 RAX-cache liveness tweaks;
`cvtsi2sd` for integer variables (zero corpus impact — all benchmark vars are doubles — but real
for int-binding users).

### Refuted (so nobody retries them blind)
- `logb` constant-base strength reduction: mechanism correct, but **zero** corpus occurrences.
  (Its verification uncovered B1, the depth-3 miscompilation — the real treasure.)
- `sin/cos → sincos` fusion: exactly one corpus expression has identical arguments, and it
  constant-folds away; also thread-safety and platform-gating problems as sketched.

A recurring verifier theme: at ~6 ns/eval with SSE2 already beating native, most instruction-level
wins are front-end-bandwidth effects worth ~1% each. The two structural items (depth-cap fallback
elimination, batch API) and compile-time folding are where the meaningful headroom is.

---

## 4. Reorganization plan (containment + coherence, behavior-preserving)

Full proposal with verified line ranges in the workflow output; summary of the 10 steps, in order:

- **P1 — Golden-codegen digest harness first** (hash `get_byte_representation()` per expression ×
  {sse2, x87, x87+cse, fast_math}). Every reorg commit must produce byte-identical digests —
  identical bytes cannot regress eval speed, which collapses the verification burden to compile
  time only. Land B1/B2/B3 fixes BEFORE baselining.
- **P2 — Section banners + decompose** the 630-line `set_expression` into tokenize /
  infix_to_postfix / build_elist / fast_math / fold+dispatch stages, and split `asmd_optimizer`
  into core + two emitters. Pure code motion, digest-verified.
- **P3 — Physical split** into `src/mexce/*.h` (22 modules, dependency-ordered) with a stdlib-only
  `tools/amalgamate.py` generating the committed root `mexce.h` (sqlite model). Verified
  constraint: `conanfile.py` and `ports/mexce/portfile.cmake` both consume root `mexce.h`, so the
  artifact keeps its exact name/path; CI regenerates and fails on `git diff --exit-code mexce.h`.
- **P4 — Named assembler layer** (`emit_movsd_xmm_xmm`, `emit_mov_rax_imm`, …) replacing inline
  hex and the misuse-prone `operator< / operator<<` charstream pair. Biggest long-term
  maintainability gain; incremental, digest-verified per region.
- **P5 — Dedup via small shared utilities** (~600–900 lines: shared RAX-cache struct, repeated-leaf
  peephole, constant classifier, parametrized SSE2 arithmetic emitters, merged asmd halves).
  Explicitly REJECTS a virtual common-emitter interface between x87 and SSE2 — the machines are
  genuinely different (blob-append vs per-node dispatch).
- **P6 — Flagged bug-fix commits** (B1–B3, B4 decision) sequenced before digest baselining.
- **P7 — Function metadata** (op_kind enum + makes_call flag) replacing string-compare dispatch and
  the fragile FF-D0 byte-scan for call-shadow detection.
- **P8 — Hygiene**: `#undef` internal macros at end of header, lean Windows.h, deprecate dead
  `use_libm_*` options, fix stale doc block (still says x87-first).
- **P9 — Test data**: generated 2.4 MB/1.2 MB headers become runtime-loaded `test/data/*.txt`
  (the native-baseline `.inc` stays compiled — it IS the baseline); Python tooling consolidates
  under `tools/`.
- **P10 (deferred)**: Function role separation, O(n²) pass-loop fixes, non-recursive SSE2→x87
  fallback — own design pass once P1–P5 make the code legible.

---

## 5. The optimization process

`analysis/optimization_runbook.md` — a deterministic optimize → measure → record → decide loop
designed so an LLM never improvises. Core design, grounded in the audit of the existing infra:

- The benchmark gains `--json` (atomic write + completion sentinel), median-of-N compile timing
  (today compile time is a SINGLE unrepeated sample — yet it drove the asmd branch ranking),
  warmup rounds, `--filter-file` subset probes, and `--strict` exit codes.
- One entry point (`tools/bench_gate.py`): build → unit tests → ULP fence → benchmark → one
  summary JSON with host/SHA/flags provenance. The Windows dev box is the only timing authority;
  CI is correctness-only.
- Per-host, per-expression committed baselines with measured noise floors; accept iff
  improvement > max(noise_floor, fixed threshold) AND no expression class regresses beyond
  tolerance AND ULP histogram non-regressing. Rejects are recorded with the same rigor — they are
  the "which way makes no sense" data.
- Append-only `analysis/experiments.jsonl` ledger keyed by SHA+host+config (the existing CSV keyed
  by branch name, with 1-ns-quantized values, ranked 13 prior branches on noise — the audit's
  most damning finding).
- `tools/direction_map.py` aggregates the ledger into a pathfinding map: which optimization
  families PAY OFF, which are DEAD ENDS, what to try next.

The critic appendix in that file lists 19 concrete amendments (sign-convention contradiction,
bootstrap deadlocks, per-platform ULP references, fetch-depth, hard-coded host id, etc.) that must
be applied to the spec before first use — each with its exact fix.

---

## 6. Addendum (2026-06-10): external review + local verification of the first patch

An external review (ChatGPT) independently confirmed B1/B2/B3/B5 and produced a patch
(`mexce_review_attempt.diff`) fixing B1, B2, B3, B5 plus two Tier-1/2 perf ideas
(M64FP memory-operand fusion, movaps reg-reg copies). Verified on this machine (MinGW 13.1 -O3):

- **B1 reproducer corrected**: `a + logb(2,x)` is NOT a reproducer (optimizer reorders it to
  `logb(2,x)+a`); `logb(a, b+logb(2,x))` is — clean master returns 2.321928 vs expected 2.807355.
  Patched returns the correct value. §2 above has been corrected accordingly.
- All unit tests pass (incl. 3 new regression tests); probes confirm B2 (`x > 2` works) and B5
  (strict-mode `x-x` with NaN and `x/x` with 0 now stay NaN; fast_math still folds them).
- Full 44,229-expression benchmark, SSE2 single mode, 20k iterations, interleaved A/B
  (contaminated sessions discarded via the native-baseline canary): **zero compile/eval failures**;
  eval 5.16 ns (clean) vs 5.13 ns (patched) — no regression, possibly ~0.5–1% faster, within this
  session's noise floor; compile time statistically unchanged. Needs the runbook's 5-run gated
  protocol on a quiet machine for a definitive perf claim.
- Accuracy shift is the expected B5 effect and is small: exact-vs-reference 17174 → 17006, with
  the difference migrating to the 1–16 ULP bin (partial cancellations like `3x−2x` now computed
  in FP rather than merged). Large-ULP bins unchanged. NOTE: this is a results-change in default
  mode — document in CHANGELOG when committing.
- **New master bug found via the A/B (call it B7)**: fully-constant expressions leave
  `m_backend_used` set to the last fold's temporary backend, so `get_backend()` misreports
  ~1,208 corpus constants as "SSE2". The patch's B3 fix incidentally corrects this
  (clean: 43461 SSE2 / 62 const; patched: 42250 SSE2 / 1270 const — same 44229/44229 compiled).
- Patch status: applied to the working tree, uncommitted. Recommend committing as separate
  logical commits (B1, B2, B3, B5, perf fusions) per the reorg plan's flagged-bug-fix discipline.

# mexce Roadmap

Last updated: 2026-06-10, on branch `claude/verified-fixes-and-sse2-fusion`.

This is the tracking document for the improvement campaign. Detailed evidence lives in:

- [`improvement_report.md`](improvement_report.md) — branch harvest verdict, master bugs B1–B7, verified performance ideas, reorg plan, verification addendum
- [`optimization_runbook.md`](optimization_runbook.md) — the LLM-executable benchmark-driven optimization loop (apply its 19-amendment appendix before first use)
- [`perf_ideas_verified.json`](perf_ideas_verified.json) — all 22 verified performance ideas with code locations, impact estimates, and adversarial verdicts (the idea bank)

Ground truth used throughout: 44,229-expression benchmark corpus; dev box (Windows,
MinGW 13.1 `-O3`) measures ~5.1 ns avg eval / ~33 µs avg compile at 20k iterations;
SSE2 backend already beats the natively compiled baseline. Aggregate effects below
~2% are not measurable on this machine without the Phase 1 infrastructure — two of
six benchmark runs in the verification session were contaminated by background load
(detected via the native-baseline canary jumping 5 → 9 ns).

**The strategic picture:** single-call eval speed has maybe 5–10% left and then it is
physics (~20–25 cycles of call/ret + load chain + FP ops per call). Compile time and
batch throughput are where the multiples live.

---

## Phase 0 — Land what exists

- [ ] Push `claude/verified-fixes-and-sse2-fusion`, open PR, let CI validate the Linux leg
      (six commits: B2 `>` operator, B1 libm arg clobber, B3 fold-buffer leak + B7
      backend misreport, B5 strict-mode cancellations, movaps copies, M64FP
      memory-operand fusion — each builds and passes unit tests individually).
- [ ] Note in the PR: B5 changes default-mode results for cancellation patterns
      (168 corpus expressions move from exact to 1–16 ULP vs reference; 3 expressions
      newly fall back to x87). This is the documented-behavior fix, not a regression.
- [ ] Delete the stale local branch `claude/benchmark-noise-reduction-amOlm`
      (upstream gone; merged as PR #110).
- [ ] Decide the fate of `analysis/asmd_optimizer_review.md` (currently deleted in the
      working tree but present in HEAD; its 1-ns-quantized rankings are known-noise —
      recommend committing the deletion once the ledger replaces it).

## Phase 1 — Measurement infrastructure (prerequisite for all perf work)

Why first: every remaining eval-time idea except the depth-cap item is individually
smaller than the current session noise floor. The previous optimization campaign
(13 `codex/*` branches in `asmd_optimizer_benchmark_summary.csv`) ranked branches on
integer-quantized noise. Do not repeat that.

- [ ] Apply the 19 amendments listed in the appendix of `optimization_runbook.md` to
      the runbook spec (sign convention, bootstrap order, per-platform ULP references,
      `--append` semantics, host-id discovery, etc.).
- [ ] `test/benchmark.cpp`: add `--json` (atomic write + `complete` sentinel),
      `--compile-rounds` (compile time is currently a SINGLE unrepeated sample),
      `--warmup-rounds`, `--filter-file` (targeted subset probes), `--strict`
      (nonzero exit on any failure in every mode), and a `static_assert` that
      `kGoldenResultsCount == kExpressionCount`.
- [ ] `tools/bench_gate.py` — the single entry point: build → unit tests → ULP fence →
      benchmark runs → one machine-readable summary with host/SHA/compiler provenance.
- [ ] `tools/bench_diff.py` — noise-aware accept/reject/inconclusive verdicts.
- [ ] `tools/record_experiment.py` + committed append-only `analysis/experiments.jsonl`
      ledger (negative results recorded with the same rigor — they are the
      "which way makes no sense" data).
- [ ] `tools/direction_map.py` — aggregates the ledger into pays-off / dead-end families.
- [ ] Per-host baselines with measured noise floors (`analysis/baselines/<host_id>/`),
      per-platform ULP references.
- [ ] Golden-codegen digest harness (`test/golden_codegen.cpp`): SHA-256 of
      `get_byte_representation()` per expression × {sse2, x87, x87+cse, fast_math}.
      Identical bytes cannot regress eval speed — this converts refactors and
      risky codegen changes into mechanically verifiable steps.
      **Baseline only after Phase 2 lands** (those fixes change output bytes).

## Phase 2 — Remaining master bugs

Fixed already on the branch: B1, B2, B3, B5, B7. Still open:

- [ ] **B4**: SSE4.1 `roundsd` (mexce.h ~3674–3695) is emitted with no CPUID gate —
      crashes on pre-SSE4.1 CPUs. Decide: runtime CPUID check with x87 fallback for
      the rounding family, or document SSE4.1 as a hard requirement. Either is fine;
      undocumented is not.
- [ ] **B6**: `-0.0` handled inconsistently — `compile_elist` has a signbit guard,
      `emit_load_constant` emits `fldz` (+0.0) for −0.0. Unify (replicate the signbit
      behavior) when introducing the shared constant classifier (Phase 7 / P5).
- [ ] Locale-independent numeric-literal parsing (manual parse or `from_chars`-style;
      keep ERANGE rejection). Supersedes the remote branch's bare-strtod approach,
      which hard-breaks `1.5` under comma-decimal locales.

## Phase 3 — Harvest `origin/claude/library-review-AhdW4`

Full per-change catalog and skeptic findings in `improvement_report.md` §1.
Cherry-pick as-is:

- [ ] `mexce_parsing_exception::position()` accessor (+ tests).
- [ ] Platform generalization (`MEXCE_PLATFORM_WIN/POSIX`, macOS-Intel/BSD, `MAP_ANON`
      shim, `#error` on unknown platforms) — master is silent UB on unknown platforms.
- [ ] Portable `get_page_size()` + `round_up_to_page()` for mmap/mprotect/munmap.
- [ ] Option propagation in single-shot `evaluate(string)` (behavior change: document).
- [ ] `set_expression` exception-safety rework + `evaluate()` null-fptr guard
      (master segfaults on evaluate() after a failed set_expression; the null check
      was measured at zero cost). Also fixes the `prefer_x87=true` leak on a failed
      SSE2→x87 fallback.
- [ ] Parser DoS limits — fix two nits while harvesting: move `MEXCE_MAX_*` macro
      definitions to the header top; length check runs after `e += " "` (off-by-one).
- [ ] Sanitizer CI workflow, restricted to `ctest -R mexce_unit_tests`
      (as written it runs the 44k benchmark twice per compiler under ASan).

Fix before harvesting:

- [ ] Double-free in `scoped_executable_buffer::lock()` failure path (~3-line fix:
      null `m_ptr` before calling `lock_executable_buffer`, which owns cleanup on
      failure). Silent today; dangerous in multithreaded processes.
- [ ] `enable_cse` → x87 forcing: redesign so x87 is forced only when CSE actually
      finds a common subexpression (or require explicit `use_x87_backend()`); the
      branch forces the slower backend even when CSE finds nothing.
- [ ] `use_libm_*` field removal: stage as deprecation (fields are verified dead, but
      removal is a source-level API break — major-version event, not a hardening commit).
- [ ] Stale-constant invalidation: failed `set_expression("")` after a constant
      expression still returns the old constant (one-line fix in the empty branch).
- [ ] Optional: macos-13 (Intel) CI job to back the new platform claims.

## Phase 4 — Performance: eval time (every item gated by Phase 1)

- [ ] **4.1 Raise the SSE2 depth cap / home the value stack in xmm6–15 on Win64.**
      The single biggest remaining eval win: ~1% of expressions fall back to x87 for
      depth, but they are the expensive tail (~190 ns vs 6 ns average); migration
      measured ~17% faster per affected expression → **~4–5% aggregate**, plus ~1%
      compile time (kills ~80% of the 455 double-parse fallbacks). ABI-sensitive
      (callee-saved xmm6–15 on Win64, save/restore in prologue) — requires the golden
      harness and a full gate. Details: `perf_ideas_verified.json`, lens `regalloc`,
      "On Windows, home the SSE2 value stack in callee-saved xmm6-15…".
- [ ] **4.2 Compounding small wins, one runbook experiment each** (each ~0.5–3%,
      individually at or below noise; together plausibly 3–6%):
      - [ ] xmm register-content tracking (reuse repeated variable/constant values
            via register copies; 69% of corpus has repeated variables).
      - [ ] Multi-register address cache (RCX/RDX/R8–R11 instead of single RAX slot;
            ~26–28% code-size cut on polynomial-class expressions).
      - [ ] Elide `sub/add rsp,8` prologue/epilogue for call-free SSE2 functions.
      - [ ] RIP-relative neg/abs sign masks via 16-byte constant-pool entries
            (touches 11% of corpus expressions).
- [ ] **Do NOT retry without new evidence** (refuted or near-zero on this corpus;
      reasons recorded in `improvement_report.md` §3 and the ideas JSON):
      pow integer lowering (0.38% of corpus contains `^`), CSE for SSE2 (0.06% have
      surviving duplicate subtrees), sin/cos → sincos fusion (one expression,
      constant-folded away), logb constant-base strength reduction (zero occurrences),
      x87 RAX-cache liveness tweaks (tiny, x87-only). cvtsi2sd for integer variables
      is real for int-binding users but has zero corpus impact — icebox.

## Phase 5 — Performance: compile time (more headroom than eval time)

- [ ] **Host-evaluate constant folding** instead of JIT-compile-and-execute per fold:
      **~8% total compile time verified, bit-identical on all 44,229 expressions**
      (~2× for constant-heavy expressions). Also structurally eliminates the
      fold-buffer leak class that B3 patches.
- [ ] P7: `Function::op_kind` enum + `makes_call` flag — replaces per-node string
      comparisons on the compile hot path and the fragile FF-D0 byte-scan for
      call-shadow detection (which can false-positive on immediates).
- [ ] Add a compile-time-only benchmark mode (Phase 1 harness) so these are gateable —
      today compile time is a single unrepeated sample per expression.
- [ ] (Deferred, after Phase 7) P10: O(n²) pass loops — `normalize_commutative_operands`
      and fast_math restart the full scan per mutation; `run_cse` builds recursive
      string signatures per node; SSE2→x87 fallback re-parses from scratch.

## Phase 6 — Batch evaluate API (the biggest real-world win)

~1.0–1.3 ns of every call is fixed overhead → **2–3× on trivial expressions** for
array workloads (near-zero for libm-bound ones). Invisible in the current benchmark
(per-call harness) but decisive for the library's stated use cases (simulations,
data kernels). Treat as a feature with a design pass:

- [ ] API shape: `evaluate_batch(double* out, size_t n)` with per-variable stride
      descriptors (SoA and AoS both expressible), or rebind-pointers-per-row.
- [ ] Codegen: JIT a loop around the expression body; variable loads become
      base+index addressing; the loop carries no call/ret per row.
- [ ] Semantics: define behavior for libm calls inside the loop (registers clobbered
      per iteration — same marshalling as today), and for `is_constant_expression`.
- [ ] Benchmark-harness batch mode so the win is measurable and regressions gateable.

## Phase 7 — Reorganization (behavior-preserving; full detail in report §4)

Order matters; every step verified by the golden digest harness (P1, built in Phase 1):

- [ ] P2: section banners + decompose the 630-line `set_expression` and
      `asmd_optimizer` (pure code motion).
- [ ] P3: physical split into `src/mexce/*.h` (22 modules) + stdlib-only
      `tools/amalgamate.py` generating the committed root `mexce.h` (sqlite model;
      conanfile.py and ports/ keep consuming the root header unchanged); CI fails on
      stale amalgamation.
- [ ] P4: named assembler layer (`emit_mov_rax_imm`, `emit_movsd_xmm_rip`, …) replacing
      inline hex and the `operator< / operator<<` charstream pair, region by region.
- [ ] P5: dedup via small shared utilities (~600–900 lines: shared RAX-cache struct,
      repeated-leaf peephole, constant classifier — fold B6 fix in here, parametrized
      SSE2 arithmetic emitters, merged asmd halves). Explicitly NO virtual
      common-emitter interface between x87 and SSE2.
- [ ] P8: preprocessor/API hygiene (`#undef` internal macros at end of header, lean
      Windows.h, refresh the doc block — it still describes the library as x87-first).
- [ ] P9: move generated corpora out of compiled headers into runtime-loaded
      `test/data/*.txt` (the native-baseline `.inc` stays compiled — it IS the
      baseline); consolidate Python tooling under `tools/`.
- [ ] P10: deferred IR/Function role separation (high risk; do after the above makes
      the code legible).

## Icebox

- cvtsi2sd for integer-typed bound variables (helps int users; zero corpus impact).
- x87 RAX-cache liveness across pure-FPU blobs.
- macOS/BSD CI coverage beyond the Phase 3 item.
- Third-party shootout refresh (`test/bench_expr_all_results.txt` is a static,
  non-reproducible snapshot).

## Operating rules (non-negotiable once Phase 1 lands)

1. One hypothesis per experiment; never change `mexce.h` and the harness together.
2. No accept without: improvement > max(noise floor, threshold), unit tests green,
   ULP histogram non-regressing, no expression class regressing beyond tolerance.
3. Rejects and inconclusives go in the ledger too.
4. The Windows dev box is the timing authority; CI is correctness-only.
5. Golden digests must be byte-identical for every pure-refactor commit.

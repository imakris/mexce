# mexce Benchmark-Driven Optimization Runbook

A deterministic optimize → measure → record → decide loop. Every command is copy-pasteable. Every decision is a mechanical rule. Negative results are recorded with the same rigor as positive ones — they are the "which way makes no sense" data.

**Verified ground truth this runbook is built on** (re-checked 2026-06-10):
- `test/benchmark.cpp` — eval timing is round-robin median (chunks of 1024, 3/5/7 adaptive rounds, `select_representative_timing` at line 459); **compile time is a single unrepeated sample** (lines 557–560); comprehensive mode always `return 0` (line 1642); CLI: `[--comprehensive|--single] [--x87|--sse2] [--fast-math] [iterations] [output_file]`, default 100000 iterations, text-only output.
- `analysis/record_benchmark_summary.py` — regexes hardwired to `us`/`ns`/`sec`/`ms` and to **single-mode** report lines; `SystemExit` on any miss; CSV keyed by branch name only. `analysis/asmd_optimizer_benchmark_summary.csv` shows 13 branches separated by 1-ns-quantized values (11.000 vs 12.000) — i.e., the previous campaign ranked branches on noise.
- `CMakeLists.txt` — `/O2` (MSVC) or `-O3 -DNDEBUG` (GCC/Clang), `BENCHMARK_COMPILER[_FLAGS]` defines, `run_benchmarks` target at 100 iterations.
- `.github/workflows/main.yml` — builds, ctests, runs 100-iteration benchmark, uploads artifact; compares nothing.
- `test/benchmark_results.h` has `kGoldenResultsCount = 44229`; nothing asserts it equals `kExpressionCount`.

---

## 0. Roles and principles

1. **The Windows dev box (MSVC Release) is the only timing authority.** CI (ubuntu-latest + windows-latest) is a *correctness* authority only — shared runners are too noisy for ns-scale deltas. Never mix numbers across hosts or compilers.
2. **One metric pipeline.** All decisions read machine-readable JSON emitted by the benchmark binary itself (`--json`). The legacy text report and regex scrapers (`record_benchmark_summary.py`, `render_benchmark_table.py`) are retired from the loop.
3. **No decision without a noise floor.** Every comparison is `delta vs. max(measured_noise, fixed_threshold)`.
4. **Every experiment leaves a ledger row** — accept, reject, or inconclusive.
5. **One hypothesis per experiment.** Never change `mexce.h` and the benchmark harness in the same experiment.

**Metric definitions (used everywhere, no exceptions):**
- `eval_ns_per_call` (per expression) = round-median of `dur_ns` ÷ `iterations`, kept as **double** (the current integer rounding of `avg_ns` is the reason 13 branches were indistinguishable — never round to int).
- `compile_us` (per expression) = **median of `compile_rounds` samples** (new; see §1.2 — single-sample compile timing is fixed before the first experiment).
- Aggregate per config = mean over expressions of the per-expression values.
- Run-level value = the aggregate from one gate benchmark execution; **session value** = median across `--runs` executions.
- `noise_floor_pct(metric)` = `100 × (max − min) / median` of the run-level values across the 5 baseline runs (stored per metric, per config, per class).

---

## 1. ONE-TIME SETUP

### 1.1 File map (new and modified)

| Path | Status | Purpose |
|---|---|---|
| `test/benchmark.cpp` | **modify** | add `--json`, `--compile-rounds`, `--warmup-rounds`, `--filter-file`, `--strict`; add `static_assert`; atomic JSON write |
| `tools/bench_gate.py` | **new** | single entry point: build → unit tests → benchmark runs → gates → ONE summary JSON |
| `tools/bench_diff.py` | **new** | noise-aware baseline-vs-candidate verdict (accept/reject/inconclusive) |
| `tools/record_experiment.py` | **new** | validated append to the experiment ledger |
| `tools/direction_map.py` | **new** | aggregates ledger → `analysis/direction_map.md` |
| `tools/check_integrity.py` | **new** | CI: ledger append-only check, ULP-reference presence, golden-count check |
| `analysis/experiments.jsonl` | **new, committed** | append-only experiment ledger (§2) |
| `analysis/backlog.md` | **new, committed** | prioritized hypothesis backlog (§3, step L2) |
| `analysis/baselines/<host_id>/baseline.json` | **new, committed** | per-host aggregate + per-class baseline with noise floors |
| `analysis/baselines/<host_id>/perexpr.json.gz` | **new, committed** | per-expression baseline medians (for localized-regression diffs) |
| `analysis/ulp_reference.json` | **new, committed** | expected ULP histogram per config — CI correctness fence |
| `analysis/direction_map.md` | **new, generated+committed** | the pathfinding output (§5) |
| `bench_runs/` | **new, gitignored** | raw per-run JSONs and verdicts |
| `.gitignore` | **modify** | add `bench_runs/`, `build-bench*/`, `build-repo-*/` |

The three `build-repo-*-Debug/` IDE trees are never used for measurement (they are Debug). `build-bench/` is the one canonical measurement build.

### 1.2 `test/benchmark.cpp` modifications (spec)

New flags (all coexist with current ones; current behavior unchanged when absent):

```
--json <path>            Write machine-readable JSON (in addition to the text report).
                         Written to <path>.tmp, fsync'd, then renamed to <path>.
                         Top-level field "complete": true is the completion sentinel;
                         a crashed run leaves only the .tmp file.
--compile-rounds <N>     Default 5. Compile timing becomes median-of-N: each expression's
                         set_expression() is re-run on a fresh evaluator N times,
                         interleaved round-robin across the 1024-expression chunk
                         (same decorrelation as eval timing). The original
                         metadata-producing compile serves as warmup and is NOT timed.
--warmup-rounds <N>      Default 1. N extra eval timing rounds run first and discarded
                         (fixes cold-cache round adjacent to a 3-round median).
--filter-file <path>     Text file, one 0-based expression index per line; only these
                         expressions are benchmarked. Deterministic subset mode for
                         fast targeted probes.
--strict                 Nonzero exit (1) on any compile/eval failure in ANY mode,
                         including comprehensive (today comprehensive always returns 0).
```

One-line correctness fix, added near the top of `main`-adjacent code:

```cpp
static_assert(mexce::benchmark_data::kGoldenResultsCount == mexce::benchmark_data::kExpressionCount,
              "benchmark_results.h is stale: rerun test/result_generator.py");
```

**`--json` schema (schema_version 1):**

```json
{
  "schema_version": 1,
  "complete": true,
  "timestamp_utc": "2026-06-10T09:42:11Z",
  "mode": "comprehensive",
  "iterations": 100000,
  "eval_rounds": 3, "compile_rounds": 5, "warmup_rounds": 1,
  "compiler": "<BENCHMARK_COMPILER>",
  "compiler_flags": "<BENCHMARK_COMPILER_FLAGS>",
  "expression_count": 44229,
  "configs": [{
    "name": "sse2",                  // sse2 | sse2_fm | x87 | x87_fm
    "prefer_x87": false, "fast_math": false,
    "compiled_count": 44229, "compile_fail_count": 0, "eval_fail_count": 0,
    "sse2_backend_count": 44229, "x87_backend_count": 0,
    "eval_ns_per_call_avg": 6.213,   // double, NEVER integer-rounded
    "compile_us_avg": 174.31,
    "native_eval_ns_per_call_avg": 7.018,
    "ulp_bins_vs_ref":  {"exact": 20164, "b16": 23494, "b32": 0, "...": 0, "gt65536": 15},
    "ulp_bins_vs_native": { "...": 0 },
    "expressions": [
      { "i": 0,
        "eval_ns_rounds": [612345, 609888, 615002],   // raw dur_ns per round
        "eval_ns_per_call": 6.10,
        "compile_ns_samples": [171002, 168455, 170100, 169322, 172800],
        "compile_ns_median": 170100,
        "native_ns_per_call": 6.95,
        "ulp_ref": 0, "backend": "sse2", "ok": true, "error": null }
    ]
  }]
}
```

Exit codes: `0` ok, `1` correctness failure under `--strict`, `2` bad arguments.

### 1.3 `tools/bench_gate.py` — the single entry point (spec)

```
python tools/bench_gate.py
  --build-dir DIR            required; canonical build dir (build-bench locally, build in CI)
  --cmake-config CFG         default Release (multi-config generators / MSVC)
  --runs N                   default 3; full benchmark repetitions in this session
  --iterations N             default 100000
  --mode full|quick|smoke    default full
                             full : comprehensive, all 4 configs, all expressions
                             quick: --single one config, optional --filter-class, 20000 iters default
                             smoke: all configs, --iterations 200, --strict, ULP fence only (CI)
  --config NAME              quick mode only: sse2|sse2_fm|x87|x87_fm (default sse2)
  --filter-class NAME        quick mode only: restrict to one expression class (§1.6);
                             gate writes the index list to bench_runs/filters/<NAME>.idx
                             and passes it as --filter-file
  --baseline                 after the runs, write analysis/baselines/<host_id>/
                             (refuses if git tree is dirty); implies --runs 5 unless overridden
  --update-ulp-reference     rewrite analysis/ulp_reference.json from this run (clean tree only)
  --label TEXT               free-text tag stored in the summary
  --allow-any-power          skip the Windows power-plan check (never use for recorded runs)
  --out DIR                  default bench_runs/
```

**Behavior, in order (all steps logged):**
1. **Preflight**: record git SHA, branch, dirty flag (`git status --porcelain`); hostname; CPU model (`Get-CimInstance Win32_Processor` on Windows / `lscpu` on Linux); logical cores; OS; active power scheme (`powercfg /getactivescheme`). On Windows, **hard-fail (exit 4)** if the scheme is not High performance/Ultimate unless `--allow-any-power`.
2. **Build**: `cmake --build <build-dir> --config <cfg> --target benchmark unit_tests`. Fail → exit 5.
3. **Flags fence**: extract `BENCHMARK_COMPILER`/`BENCHMARK_COMPILER_FLAGS` from a `--json` probe run (`benchmark --json tmp --filter-file <empty-3-expr-file> 5`); the flags string must contain `/O2` or `-O3`; in full/quick mode it must exactly match the host baseline's string. Mismatch → exit 4. (This makes Debug-tree measurement impossible.)
4. **Unit tests**: run `unit_tests` binary directly. Fail → exit 5.
5. **Benchmark runs**: launch `benchmark` `--runs` times sequentially, each with `--json bench_runs/<host_id>/<runid>_rN.json --strict --warmup-rounds 1 --compile-rounds 5`, at HIGH priority class (psutil; fallback: normal, recorded). `host_id = <hostname>_<compilerid-version>_<O-flag>`, e.g. `IMAK-PC_msvc-19.40_O2`.
6. **Canary check**: a fixed canary set (expression indices `0, 443, 886, …` — every 443rd index, 100 total, committed inside bench_gate.py as a literal list) is timed in a `--filter-file` micro-run before run 1 and after run N. If `|after/before − 1| > 3%` → session unstable, exit 3, nothing recorded.
7. **Merge** the N run JSONs into ONE gate summary `bench_runs/<host_id>/<UTCstamp>_<sha7>[_dirty].json`:

```json
{
  "schema_version": 1, "complete": true, "gate_version": "1.0.0",
  "run_id": "20260610T094211Z-a1b2c3d",
  "host": {"host_id": "IMAK-PC_msvc-19.40_O2", "hostname": "IMAK-PC",
            "cpu_model": "13th Gen Intel(R) Core(TM) i9-13900K", "logical_cores": 32,
            "os": "Windows 11 10.0.22631", "power_scheme": "High performance",
            "priority_boost": true},
  "git": {"commit": "<full sha>", "branch": "exp/EXP-0007-rax-cache", "dirty": false},
  "build": {"compiler": "MSVC 19.40.33812", "flags": "/O2 /W4 /permissive- /bigobj",
             "build_dir": "build-bench", "cmake_config": "Release"},
  "params": {"mode": "full", "runs": 3, "iterations": 100000,
              "eval_rounds": 3, "compile_rounds": 5, "warmup_rounds": 1, "filter_class": null},
  "gates": {"unit_tests": "pass", "compile_failures": 0, "eval_failures": 0,
             "ulp_fence": "pass", "canary_drift_pct": 0.8, "flags_match_baseline": true},
  "configs": {
    "sse2": {
      "eval_ns_per_call_runs": [6.23, 6.21, 6.19], "eval_ns_per_call": 6.21,
      "compile_us_runs": [175.1, 174.3, 173.9],   "compile_us": 174.3,
      "native_eval_ns_per_call": 7.02,
      "ulp_bins": {"exact": 20164, "b16": 23494, "gt65536": 15},
      "classes": {
        "asmd":           {"count": 18234, "eval_ns_per_call": 3.12, "compile_us": 121.4},
        "transcendental": {"count": 14102, "eval_ns_per_call": 11.8, "compile_us": 233.0},
        "power_root":     {"count": 6120,  "eval_ns_per_call": 8.4,  "compile_us": 190.2},
        "logic_compare":  {"count": 2410,  "eval_ns_per_call": 4.9,  "compile_us": 150.7},
        "other_func":     {"count": 2105,  "eval_ns_per_call": 7.7,  "compile_us": 181.3},
        "constant_only":  {"count": 1258,  "eval_ns_per_call": 1.4,  "compile_us": 96.0}
      }
    },
    "sse2_fm": {"...": 0}, "x87": {"...": 0}, "x87_fm": {"...": 0}
  },
  "perexpr_file": "bench_runs/IMAK-PC_msvc-19.40_O2/20260610T094211Z-a1b2c3d_perexpr.json.gz"
}
```

(Per-class counts above are illustrative; real counts come from the classifier in §1.6.)
8. **Gates**: ULP fence = ulp_bins must equal `analysis/ulp_reference.json` per config exactly (unless `--update-ulp-reference`). Any compile/eval failure count > 0 → exit 2.
9. With `--baseline`: write `analysis/baselines/<host_id>/baseline.json` = the gate summary plus `noise_floor_pct` per (config × metric) and per (config × class × metric), computed from the 5 runs; write `perexpr.json.gz` with per-expression median-of-runs `eval_ns_per_call` and `compile_ns` arrays.

**Gate exit codes**: 0 pass · 2 correctness gate failed · 3 noisy session (canary) · 4 environment/flags mismatch · 5 build/test failure.

### 1.4 `tools/bench_diff.py` (spec)

```
python tools/bench_diff.py
  --baseline analysis/baselines/<host_id>/baseline.json
  --candidate bench_runs/<host_id>/<runid>.json
  --primary  <config>:<metric>        e.g. sse2:eval  or  sse2:compile  (required)
  --budget   <config>:<metric>:<+pct> repeatable; declared allowed regressions,
                                      e.g. --budget sse2:compile:+5
  --ulp-budget <config>:<field>:<n>   repeatable; declared accuracy change for
                                      fast-math experiments only, e.g. sse2_fm:exact:-100
  --out <verdict.json>
```

Decision algorithm (mechanical, in order; first failure wins):
1. Candidate `gates.*` all pass and `flags_match_baseline` → else **reject (reason: gate)**.
2. Candidate internal spread check: for the primary metric, `(max−min)/median` of the candidate's run values must be ≤ 2 × baseline `noise_floor_pct` for that metric → else **noisy-session** (exit 3; re-run, do not decide).
3. ULP: for every config, bins must be identical to baseline unless covered by an explicit `--ulp-budget` → else **reject (reason: accuracy)**.
4. Per-class guardrail: for every config × class, regression must be ≤ `max(class noise_floor_pct, 3.0%)` → else **reject (reason: class-regression)**, naming the worst offender.
5. Secondary metrics: every non-primary (config × metric) aggregate may regress at most `max(its noise_floor_pct, 1.0%)` plus any declared `--budget` → else **reject (reason: undeclared-tradeoff)**.
6. Primary metric: `improvement_pct = 100 × (baseline − candidate)/baseline`.
   - `improvement_pct > max(noise_floor_pct, X)` where **X = 1.0 for eval, X = 2.0 for compile** → **accept**.
   - `improvement_pct < −max(noise_floor_pct, X)` → **reject (reason: slower)**.
   - otherwise → **inconclusive**.

Output `verdict.json`: `{decision, reasons[], primary:{metric, before, after, delta_pct, threshold_pct}, secondary_deltas{}, worst_class_regression:{config,class,pct}, top_regressed_expressions:[{i,expr,before_ns,after_ns}] (top 20 from perexpr diff), top_improved_expressions:[...]}` plus a printed Markdown table. Exit codes: 0 accept · 1 reject · 2 inconclusive · 3 noisy.

### 1.5 `tools/record_experiment.py` and `tools/check_integrity.py` (spec)

```
python tools/record_experiment.py --id EXP-0007 --backlog B-012 --family rax-cache \
  --hypothesis "..." --change-summary "..." --decision accept --rationale "..." \
  --baseline <baseline.json> --candidate <gate.json> --verdict <verdict.json>
```
Validates: id matches `EXP-\d{4}`, strictly greater than the last id in the ledger; decision ∈ {accept, reject, inconclusive}; pulls metrics/noise/deltas from the three JSONs (no hand-typed numbers); appends exactly one line to `analysis/experiments.jsonl`; refuses duplicates.

`tools/check_integrity.py` (CI): (a) `git diff --unified=0 origin/master...HEAD -- analysis/experiments.jsonl` must contain only `+` lines (append-only fence); (b) `analysis/ulp_reference.json` exists and parses; (c) greps `kGoldenResultsCount` and `kExpressionCount` values from the headers and asserts equality (belt-and-suspenders for the static_assert). Nonzero exit on any violation.

### 1.6 Expression classifier (spec — lives in `tools/bench_gate.py`)

Deterministic, first-match-wins on the raw expression string; function tokens matched as `\b<name>\s*\(`:
1. contains any of `sin cos tan asin acos atan sinh cosh tanh exp log ln log2 log10` → **transcendental**
2. contains `^` or any of `pow sqrt cbrt hypot` → **power_root**
3. contains any other `identifier(` call → **other_func**
4. contains any of `< > = !` → **logic_compare**
5. contains none of the standalone variables `a b c x y z w` → **constant_only**
6. else (only `+ - * /`, digits, parens, variables — the asmd_optimizer's target) → **asmd**

Secondary size bucket (reported as a second table): operator+function count 1–2 **tiny**, 3–6 **small**, 7–15 **medium**, >15 **large**.

### 1.7 Windows setup (the measurement host) — run once

```powershell
cd C:\plms\bsd_licensed\mexce
cmake -S . -B build-bench -G "Visual Studio 17 2022" -A x64
cmake --build build-bench --config Release --target benchmark unit_tests
powercfg /getactivescheme
powercfg /setactive 8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c   # High performance
pip install psutil
git checkout master
python tools\bench_gate.py --build-dir build-bench --cmake-config Release --baseline --runs 5 --iterations 100000
git add analysis\baselines analysis\ulp_reference.json
git commit -m "Record benchmark baseline for IMAK-PC_msvc-19.40_O2 at $(git rev-parse --short HEAD)"
```

MinGW/llvm-mingw builds are permitted only as **separate host_ids** with their own baselines; their numbers are never compared against MSVC numbers.

### 1.8 Linux CI changes — append to `.github/workflows/main.yml` after the ctest step

```yaml
      - name: Integrity checks (ledger append-only, golden counts, ULP reference)
        run: python tools/check_integrity.py

      - name: Correctness gate (smoke, no timing authority)
        run: python tools/bench_gate.py --build-dir build --cmake-config Release --mode smoke --iterations 200 --allow-any-power
```

Smoke mode asserts: unit tests pass, zero compile/eval failures across all 44229 expressions × 4 configs (`--strict`), and ULP bins identical to `analysis/ulp_reference.json`. CI **never** produces accept/reject timing verdicts. Any PR that intentionally changes accuracy must update `analysis/ulp_reference.json` in the same commit (via `--update-ulp-reference` locally) or CI fails — this closes the "row ykpt1y changed accuracy and nothing flagged it" hole.

---

## 2. EXPERIMENT LEDGER

**Path:** `analysis/experiments.jsonl` — committed, append-only, one JSON object per line (JSONL, not CSV: hypotheses contain commas; metrics are nested). Written only by `tools/record_experiment.py`; CI enforces append-only.

**Fields:** `id, date_utc, host_id, backlog_id, family, hypothesis, change_summary, branch, commit, baseline_commit, baseline_run_id, candidate_run_id, accuracy_affecting, declared_budgets, params {iterations, runs_baseline, runs_candidate, escalated}, metrics_before, metrics_after, noise_floor_pct, delta_pct, worst_class_regression, ulp_delta, decision, rationale, verdict_file`.

**Filled example row** (stored as one line; pretty-printed here):

```json
{"id":"EXP-0007","date_utc":"2026-06-10T09:42:11Z","host_id":"IMAK-PC_msvc-19.40_O2",
 "backlog_id":"B-012","family":"rax-cache",
 "hypothesis":"Extending the shared RAX cache across mul/div node boundaries in asmd_optimizer removes one mov per chained term; predict >=2% eval improvement on class asmd under sse2, compile cost <=5%.",
 "change_summary":"mexce.h: asmd_optimizer keeps RAX validity across compile_elist for mul/div nodes (+38/-7 lines)",
 "branch":"exp/EXP-0007-rax-cache-muldiv","commit":"a1b2c3d4...","baseline_commit":"133abdd...",
 "baseline_run_id":"20260608T071000Z-133abdd","candidate_run_id":"20260610T094211Z-a1b2c3d",
 "accuracy_affecting":false,"declared_budgets":{"sse2:compile":"+5%"},
 "params":{"iterations":100000,"runs_baseline":5,"runs_candidate":3,"escalated":false},
 "metrics_before":{"sse2":{"eval_ns":6.21,"compile_us":174.3},"sse2_fm":{"eval_ns":5.94,"compile_us":171.0},
                   "x87":{"eval_ns":7.85,"compile_us":188.2},"x87_fm":{"eval_ns":7.42,"compile_us":186.5},
                   "ulp_exact_sse2":20164},
 "metrics_after":{"sse2":{"eval_ns":6.05,"compile_us":177.9},"sse2_fm":{"eval_ns":5.83,"compile_us":174.1},
                  "x87":{"eval_ns":7.86,"compile_us":190.0},"x87_fm":{"eval_ns":7.44,"compile_us":188.1},
                  "ulp_exact_sse2":20164},
 "noise_floor_pct":{"sse2:eval":0.7,"sse2:compile":1.8},
 "delta_pct":{"sse2:eval":-2.58,"sse2:compile":+2.07,"asmd@sse2:eval":-5.9},
 "worst_class_regression":{"config":"x87","class":"transcendental","pct":+0.4},
 "ulp_delta":"none",
 "decision":"accept",
 "rationale":"sse2 eval -2.58% > max(0.7% noise, 1.0% threshold); compile +2.07% within declared +5% budget; ULP bins identical in all 4 configs; worst class regression +0.4% < 3%; unit tests green.",
 "verdict_file":"bench_runs/IMAK-PC_msvc-19.40_O2/20260610T094211Z-a1b2c3d_verdict.json"}
```

---

## 3. THE LOOP (mechanical algorithm)

**Backlog file:** `analysis/backlog.md` — committed, a strict Markdown table; the LLM picks rows mechanically:

```markdown
| ID    | Pri | Family            | Hypothesis (falsifiable: predicted Δ, target class, target config, budget) | Status |
|-------|-----|-------------------|------------------------------------------------------------------------------|--------|
| B-012 | 1   | rax-cache         | RAX cache across mul/div boundaries: >=2% eval on asmd@sse2, compile <=+5%     | done-accepted(EXP-0007) |
| B-013 | 1   | libm-call-overhead| Inline sin/cos range reduction stub: >=3% eval on transcendental@sse2          | open   |
| B-014 | 2   | compile-speed     | Arena-allocate parse nodes: >=5% compile_us all configs, eval unchanged        | open   |
```
`Pri` ∈ {1,2,3}. `Status` ∈ {open, in-progress(EXP-id), done-accepted(EXP-id), done-rejected(EXP-id), blocked(reason)}. Every hypothesis MUST name: predicted direction+magnitude, primary `<config>:<metric>`, target class, and budgets.

### The algorithm

- **L0 — Preflight.** On master, clean tree. Run §1.7 build commands. `powercfg /getactivescheme` must report High performance.
- **L1 — Baseline validity check.** A baseline is **stale** if any is true: no `analysis/baselines/<host_id>/` exists; `baseline_commit ≠ current master HEAD` for any commit that touched `mexce.h`; compiler/flags string differs; older than 14 days. If stale:
  ```powershell
  python tools\bench_gate.py --build-dir build-bench --baseline --runs 5 --iterations 100000
  ```
  Commit the updated `analysis/baselines/` files.
- **L2 — Pick ONE hypothesis.** From `analysis/backlog.md`: the `open` row with lowest `Pri`, ties broken by lowest `ID`, **skipping any row whose Family is marked DEAD_END in `analysis/direction_map.md`**. Set status `in-progress(EXP-NNNN)` where NNNN = last ledger id + 1. If the backlog is empty → run §5 direction map and add ≥3 new rows in PAYS_OFF or UNEXPLORED families before continuing.
- **L3 — Implement minimal patch.** `git checkout -b exp/EXP-NNNN-<slug>`. The diff touches `mexce.h` only (or harness only — never both). One conceptual change.
- **L4 — Quick probe (cheap early kill, ~30 s).**
  ```powershell
  python tools\bench_gate.py --build-dir build-bench --mode quick --config sse2 --filter-class asmd --iterations 20000 --runs 1 --label EXP-NNNN-probe
  ```
  If the target class regresses > 5% vs its baseline value → go to L7 with `decision=reject`, evidence = probe (still record!). Probe results never justify *accept*.
- **L5 — Full gate.**
  ```powershell
  python tools\bench_gate.py --build-dir build-bench --runs 3 --iterations 100000 --label EXP-NNNN
  ```
  Exit 3 (noisy/canary) → fix environment, re-run once; exit 3 again → record `inconclusive` with rationale "environment unstable", stop for the session.
- **L6 — Verdict.**
  ```powershell
  python tools\bench_diff.py --baseline analysis\baselines\IMAK-PC_msvc-19.40_O2\baseline.json --candidate bench_runs\IMAK-PC_msvc-19.40_O2\<runid>.json --primary sse2:eval --budget sse2:compile:+5 --out bench_runs\IMAK-PC_msvc-19.40_O2\<runid>_verdict.json
  ```
  (`--primary` and `--budget` come verbatim from the backlog row.) Accept rule, restated: **accept iff** `improvement > max(noise_floor, X)` [X=1.0% eval / 2.0% compile] **AND** unit tests green **AND** zero new compile/eval failures **AND** ULP bins unchanged (or within declared `--ulp-budget` for fast-math-only experiments) **AND** no class regresses > max(class noise floor, 3%) **AND** no undeclared secondary regression > max(noise floor, 1%).
- **L7 — Act on the verdict.**
  - **accept** → record ledger row; merge to master (`git checkout master && git merge --no-ff exp/EXP-NNNN-<slug>`); set backlog status `done-accepted(EXP-NNNN)`; **re-baseline** (L1 command, commit baselines); run §5.
  - **reject** → record ledger row (full metrics — this is the valuable "doesn't work" data); set status `done-rejected(EXP-NNNN)`; `git checkout master` (keep the branch unmerged for reference); do NOT re-baseline.
  - **inconclusive** → escalate **exactly once**: re-run gate with `--runs 6`, and extend the baseline with 2 extra runs (gate supports re-running `--baseline --runs 2 --append`; noise floors recomputed over all 7). Re-run bench_diff. If still inconclusive → record as **reject** with rationale "no measurable effect at N=6/7, threshold X%, noise floor F%". An idea that can't beat a 1% fence after 13 runs is, for pathfinding purposes, a dead direction.
- **L8 — Housekeeping.** After every accept and every 10 experiments: `python tools\direction_map.py`; commit `analysis/direction_map.md`, `analysis/experiments.jsonl`, `analysis/backlog.md` together.

---

## 4. GUARDRAILS

1. **Never tune to one class.** bench_diff rule 4 is non-overridable: no (config × class) eval or compile regression beyond `max(class_noise_floor, 3%)`, even if the global average improves. The verdict names the worst offender and the top-20 regressed expressions from the per-expression diff so the follow-up hypothesis is targeted, not guessed.
2. **CPU frequency / power.** Windows: gate refuses to run on Balanced (`powercfg /getactivescheme` checked every session; set with `powercfg /setactive 8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c`). Desktop on AC. Don't run builds, browsers with video, or other benchmarks concurrently with the timing phase. The gate runs the binary at HIGH priority.
3. **Build-flags consistency.** The flags fence (§1.3 step 3) hard-fails if `BENCHMARK_COMPILER_FLAGS` differs from the baseline or lacks `/O2`//`-O3`. Debug IDE trees (`build-repo-*-Debug/`) physically cannot produce a recordable run. Cross-compiler comparisons are structurally impossible because baselines are keyed by `host_id` which embeds the compiler.
4. **Noisy-session detection and discard.** Three independent tripwires: (a) canary drift > 3% between session start/end → exit 3, session discarded; (b) candidate run spread > 2× baseline noise floor on the primary metric → no decision, re-run; (c) ULP fence and `--strict` failure counts make a corrupted run un-recordable. A discarded session is never written to the ledger as a decision — at most a note.
5. **Truncated-output hazard is closed** by the tmp+rename write and the `"complete": true` sentinel; bench_gate refuses any JSON lacking the sentinel (this retires the 648-byte-stub failure mode of `benchmark_results.txt`).
6. **Harness changes are experiments too.** Any edit to `test/benchmark.cpp`, the gate, or the classifier invalidates all baselines: bump `gate_version`, re-baseline before the next library experiment, and never land a harness change inside a library experiment branch.
7. **Golden data integrity.** The `static_assert` plus `check_integrity.py` in CI; if `test/benchmark_expressions.h` changes, `test/result_generator.py` must be re-run and `analysis/ulp_reference.json` regenerated in the same commit, or CI fails.

---

## 5. PATHFINDING — the direction map

**Command:** `python tools/direction_map.py` (no args). Reads `analysis/experiments.jsonl`, writes `analysis/direction_map.md` (committed).

**Exact aggregation behavior:**
1. Group ledger rows by `family`.
2. Per family compute: `n_experiments`, `n_accept`, `n_reject`, `n_inconclusive`; `cumulative_accepted_improvement_pct` (sum of accepted primary-metric deltas — meaningful because each accept was measured against a fresh post-merge baseline); `best_single_delta_pct`; `mean_attempted_delta_pct` (all rows, including rejects — shows whether the family even moves the needle in either direction); `last_experiment_date`.
3. Verdict per family (mechanical):
   - **PAYS_OFF**: `n_accept ≥ 2` AND `cumulative_accepted_improvement_pct > 1.0`.
   - **DEAD_END**: `n_experiments ≥ 3` AND `n_accept = 0` AND `best_single_delta_pct ≤ 0` (i.e., three honest attempts, none ever beat noise in the right direction).
   - **MIXED**: at least one accept and at least one reject, otherwise.
   - **UNEXPLORED**: `n_experiments < 2`.
4. Emit `analysis/direction_map.md`: (a) family table sorted by `cumulative_accepted_improvement_pct` descending; (b) the last 10 ledger rows as a digest (id, family, primary delta vs threshold, decision); (c) a **"Recommended next"** section: the top-3 `open` backlog rows whose family verdict is PAYS_OFF or UNEXPLORED, in backlog order — this is the input to loop step L2.

Example output table:

```markdown
| Family             | Verdict   | Exp | Acc | Rej | Inc | Cum. accepted Δ | Best Δ | Mean attempted Δ |
|--------------------|-----------|-----|-----|-----|-----|-----------------|--------|------------------|
| rax-cache          | PAYS_OFF  | 4   | 3   | 1   | 0   | -5.8%           | -2.6%  | -1.9%            |
| compile-speed      | MIXED     | 3   | 1   | 2   | 0   | -4.1% (compile) | -4.1%  | -1.2%            |
| libm-call-overhead | UNEXPLORED| 1   | 0   | 1   | 0   | 0.0%            | -0.4%  | -0.4%            |
| x87-scheduling     | DEAD_END  | 3   | 0   | 3   | 0   | 0.0%            | +0.1%  | +0.6%            |
```

DEAD_END families are skipped by L2 until a backlog row explicitly argues new information (set `Pri 3`, note "revisits DEAD_END because <reason>") — dead ends stay visible and falsifiable, never silently retried.

---

### Initial seeding (first session checklist)

1. Implement §1.2 benchmark.cpp flags + static_assert; implement the five `tools/*.py` scripts to the specs above; add `.gitignore` entries — one PR, CI green.
2. Run §1.7 (build, power plan, 5-run baseline, `--update-ulp-reference`), commit baselines + reference.
3. Create `analysis/backlog.md` seeded from the asmd_optimizer campaign's open questions (families: `rax-cache`, `instruction-selection`, `constant-folding`, `compile-speed`, `libm-call-overhead`, `cse-x87`, `codegen-buffer`), and an empty `analysis/experiments.jsonl`.
4. Retroactively back-fill one ledger row per surviving `codex/*` branch from `analysis/asmd_optimizer_benchmark_summary.csv` with `decision: "inconclusive"` and rationale "pre-runbook measurement: 1-ns quantization, no noise floor" — so the direction map starts with honest history instead of false precision.
5. Enter the loop at L1.



---

# APPENDIX: Required amendments before first use (completeness-critic findings)

A critic agent checked every command and rule in this runbook against the actual repo. The runbook's factual ground truth verified clean, but the following 19 defects must be applied to the spec before an LLM executes it. Each entry is issue -> concrete fix.

## A1

**Issue:** Sign-convention contradiction across the three decision surfaces. §1.4 defines improvement_pct = 100×(baseline−candidate)/baseline (positive = better). The §2 ledger example uses delta_pct with negative = better ('sse2:eval':-2.58 is the accepted improvement). §5's DEAD_END rule says 'best_single_delta_pct ≤ 0 (none ever beat noise in the right direction)' and PAYS_OFF requires 'cumulative_accepted_improvement_pct > 1.0' — but the §5 example table shows rax-cache as PAYS_OFF with Cum Δ = -5.8% (fails '> 1.0' under either convention) and x87-scheduling as DEAD_END with Best Δ = +0.1% (fails '≤ 0' under the ledger convention). An LLM implementing direction_map.py cannot satisfy both the rules and the example.

**Fix:** Pick one canonical signed quantity — recommend improvement_pct (positive = better) — use it in verdict.json, ledger delta_pct, and direction_map rules alike. Restate: PAYS_OFF iff n_accept ≥ 2 AND cumulative_accepted_improvement_pct > 1.0; DEAD_END iff n_experiments ≥ 3 AND n_accept = 0 AND best_single_improvement_pct ≤ 0. Regenerate both the §2 ledger example and the §5 example table with consistent signs (rax-cache Cum +5.8, x87-scheduling Best −0.1).

## A2

**Issue:** Hard-coded host_id 'IMAK-PC_msvc-19.40_O2' is factually wrong for the actual measurement host: `hostname` on this machine returns ANDREAS. The 'copy-pasteable' commands in §1.7 (commit message) and §3 L6 (--baseline/--candidate paths) embed IMAK-PC, and L6 also contains an unresolved <runid> placeholder with no specified way to obtain it, so the LLM must improvise both.

**Fix:** Remove all literal host_ids from commands. Specify that bench_gate.py prints exactly two final stdout lines: 'HOST_ID=<id>' and 'SUMMARY=<absolute path to gate summary json>', and rewrite L6 as: run bench_diff with --baseline analysis/baselines/$HOST_ID/baseline.json --candidate $SUMMARY, where both values are taken verbatim from the gate's last output lines.

## A3

**Issue:** Initial-seeding step 4 is unexecutable as written. (a) `git branch -a` shows zero surviving codex/* branches (only master, claude/benchmark-noise-reduction-amOlm, origin/claude/library-review-AhdW4) — the 13 codex rows exist only in analysis/asmd_optimizer_benchmark_summary.csv with no commits, branches, or run JSONs. (b) record_experiment.py is specified to pull all metrics from three JSON files (baseline/candidate/verdict, 'no hand-typed numbers') and to validate commit/branch fields — none of which exist for the CSV rows. The tool as specified refuses the very rows step 4 demands.

**Fix:** Either drop step 4 and have direction_map.py emit a static 'pre-runbook history' appendix read directly from the CSV, or add an explicit `record_experiment.py --backfill-csv analysis/asmd_optimizer_benchmark_summary.csv` mode that creates one row per CSV line (keyed by the branch-name column, not git branches) with commit/baseline_commit/run_id set to null, decision 'inconclusive', and family 'rax-cache', written before the append-only fence baseline is taken.

## A4

**Issue:** First-baseline bootstrap deadlock in §1.7: gate step 8 says the ULP fence compares bins against analysis/ulp_reference.json 'unless --update-ulp-reference', but the §1.7 baseline command omits --update-ulp-reference and the reference file does not exist yet (analysis/ contains only the CSV and two legacy .py scripts). The very first run either fails the fence or the LLM must invent behavior for a missing reference; yet §1.7's git add stages analysis\ulp_reference.json.

**Fix:** Add --update-ulp-reference to the §1.7 bench_gate command, and specify gate step 8 explicitly: 'if analysis/ulp_reference.json is absent and --update-ulp-reference was not passed → exit 4 with message; if --update-ulp-reference passed → write it and skip the comparison.'

## A5

**Issue:** Seeding step 1 ('one PR, CI green') cannot be green: that PR adds the CI steps `python tools/check_integrity.py` (which requires analysis/ulp_reference.json to exist and parse) and the smoke gate (which compares against the same file), but the reference is only created in step 2 on the dev box. The first PR's CI necessarily fails its own new checks.

**Fix:** Either generate and commit analysis/ulp_reference.json (and an initial empty analysis/experiments.jsonl) inside the step-1 PR by running the new binary locally before pushing, or specify bootstrap tolerance: check_integrity.py and smoke mode emit a warning and pass when ulp_reference.json is absent AND analysis/experiments.jsonl is empty, hard-failing once either exists.

## A6

**Issue:** A single committed analysis/ulp_reference.json cannot be 'identical' on both CI matrix legs. The SSE2 backend calls libm for transcendentals (confirmed in CMakeLists/benchmark notes), so mexce-vs-reference ULP bins differ between glibc (ubuntu-latest) and MSVC UCRT (windows-latest and the dev box); additionally x87-config bins depend on hardware fsin/fcos/fpatan, which differ between the Intel and AMD CPUs GitHub assigns to runners. The §1.8 smoke fence 'ULP bins identical to analysis/ulp_reference.json' will be permanently red on at least one leg, and x87 rows can flap run-to-run on Linux.

**Fix:** Key references per platform: analysis/ulp_reference/<platform>.json where platform ∈ {windows-msvc, linux-gnu} is derived from the compiler define; let smoke compare only against its own platform's file and bootstrap it via --update-ulp-reference in a one-time CI commit. For x87 configs on CI, either pin exactness only for sse2/sse2_fm and assert 'gt65536 count unchanged' for x87/x87_fm, or run the CI fence on sse2 configs only. Keep full 4-config exactness only on the dev-box host_id.

## A7

**Issue:** check_integrity.py's append-only check runs `git diff origin/master...HEAD` in CI, but .github/workflows/main.yml uses actions/checkout@v4 with default fetch-depth 1 (verified: no fetch-depth key in the file), so origin/master is not available on PR builds and the diff command fails.

**Fix:** The §1.8 workflow modification must also change the checkout step to `with: fetch-depth: 0` (or add an explicit `git fetch origin master` before the integrity step), and the spec should state the check is skipped when HEAD == origin/master (push-to-master builds).

## A8

**Issue:** L7's inconclusive-escalation path invokes `bench_gate.py --baseline --runs 2 --append`, but §1.3's CLI spec defines no --append flag, and --baseline as specified overwrites analysis/baselines/<host_id>/ and 'refuses if git tree is dirty' — semantics for merging 2 new runs into existing 5-run noise floors are nowhere defined.

**Fix:** Add --append to the §1.3 CLI table: 'valid only with --baseline; requires an existing baseline.json for this host_id at the same git commit and gate_version; appends the new run-level values to the stored per-run arrays and recomputes all noise_floor_pct fields over the union (5+2=7 runs); exit 4 if commit or gate_version differs.'

## A9

**Issue:** bench_diff exit code 3 (noisy candidate spread, rule 2) is unhandled by the loop: L7 branches only on accept/reject/inconclusive, and L5's retry rule covers only gate exit 3 (canary). If bench_diff returns 3 the LLM has no rule to follow.

**Fix:** Add to L6: 'bench_diff exit 3 → re-run L5 (full gate) once and re-run L6; if bench_diff exits 3 again → record decision=inconclusive with rationale "candidate spread exceeded 2× baseline noise floor twice", set backlog status blocked(noisy-host), stop for the session.'

## A10

**Issue:** The canary check (gate step 6) is underspecified: the micro-run's config, mode, iteration count, and round count are not given ('a --filter-file micro-run' is all the spec says), and drift is defined on an unstated aggregate. Also smoke mode is not exempted, so a 3% canary drift on shared CI runners — practically guaranteed — would fail CI with exit 3 even though CI is declared a correctness-only authority; and smoke inherits --runs default 3, tripling a run that needs N=1.

**Fix:** Specify the canary run exactly: `benchmark --single --sse2 --json <out> --filter-file bench_runs/canary.idx 20000` (canary.idx = the 100 committed indices), drift = |mean(eval_ns_per_call over canary set, after) / mean(before) − 1|. State that smoke mode skips step 6 entirely and forces --runs 1 regardless of the flag.

## A11

**Issue:** Binary path resolution is never specified, but it differs by generator: with the prescribed VS generator the executables are build-bench/Release/benchmark.exe and build-bench/Release/unit_tests.exe, while CI's Linux Makefile build puts them at build/benchmark and build/unit_tests. Gate steps 3-6 ('run unit_tests binary directly', 'launch benchmark') force the implementing LLM to improvise the lookup.

**Fix:** Add to §1.3: 'executable resolution: try <build-dir>/<cmake-config>/<name>[.exe], then <build-dir>/<name>[.exe]; exit 5 listing both attempted paths if neither exists.'

## A12

**Issue:** The dirty-tree rule will misfire on this repo: preflight defines dirty via `git status --porcelain`, which includes untracked entries — the working tree currently has untracked .claude/ (and the build-repo-* dirs until the .gitignore PR lands), so every run is flagged dirty and `--baseline` ('refuses if git tree is dirty') permanently refuses.

**Fix:** Define dirty as `git status --porcelain --untracked-files=no` producing any output. Record the untracked-file list separately as informational.

## A13

**Issue:** check_integrity.py spec says it 'greps kGoldenResultsCount and kExpressionCount values from the headers and asserts equality', but kExpressionCount has no literal value: test/benchmark_expressions.h line 44242 defines it as sizeof(kExpressions)/sizeof(kExpressions[0]). A grep-based comparison is impossible as specified.

**Fix:** Respecify: parse kGoldenResultsCount's literal (benchmark_results.h line 44246) and compare it to the count of initializer entries in the kExpressions array (count lines matching the entry regex between the array's braces), or drop this check and rely on the §1.2 static_assert, with CI's build step as the enforcing gate (the assert text 'mexce::benchmark_data::...' is namespace-correct — verified both headers use namespace mexce::benchmark_data).

## A14

**Issue:** The flags-fence probe command is ambiguous: 'benchmark --json tmp --filter-file <empty-3-expr-file> 5' — 'empty-3-expr-file' could mean an empty file or a file with 3 indices, the file's location/creation is unspecified, and behavior of --filter-file with an empty index list is undefined in §1.2.

**Fix:** Specify: gate writes bench_runs/filters/probe.idx containing the three lines '0', '1', '2' before the probe, runs `benchmark --single --sse2 --json <tmp> --filter-file bench_runs/filters/probe.idx 5`, and §1.2 must state that an empty --filter-file is an error (exit 2).

## A15

**Issue:** L1's staleness rule is ambiguous and incomplete: 'baseline_commit ≠ current master HEAD for any commit that touched mexce.h' does not parse into a procedure, and it omits harness files even though guardrail 6 says any change to test/benchmark.cpp, the gate, or the classifier invalidates all baselines.

**Fix:** Restate mechanically: stale iff (a) analysis/baselines/<host_id>/ missing; OR (b) `git diff --name-only <baseline_commit>..HEAD -- mexce.h test/benchmark.cpp test/benchmark_expressions.h test/benchmark_results.h tools/ CMakeLists.txt` is non-empty; OR (c) baseline gate_version ≠ current gate_version; OR (d) compiler/flags string differs; OR (e) baseline timestamp older than 14 days.

## A16

**Issue:** Power-plan gate matches the localized display name ('High performance/Ultimate') from `powercfg /getactivescheme`, which fails on non-English Windows and on OEM-renamed schemes even when the GUID is correct.

**Fix:** Match the GUID instead: parse the GUID from powercfg output and accept {8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c, e9a42b02-d5df-448d-aa00-03f14749eb61}; treat any other GUID as exit 4 unless --allow-any-power.

## A17

**Issue:** The --json schema leaves the ULP bin keys as placeholders ('b16', 'b32', '...': 0) while the binary actually has 15 buckets — exact, then thresholds {16,32,64,128,256,512,1024,2048,4096,8192,16384,32768,65536} plus an overflow bucket (benchmark.cpp lines 339-343). The exact-equality ULP fence needs byte-stable key names, so an implementer must not guess them.

**Fix:** Enumerate the canonical key list in the schema: ["exact","le16","le32","le64","le128","le256","le512","le1024","le2048","le4096","le8192","le16384","le32768","le65536","gt65536"], and state that every key is always present (zero-filled) in both the benchmark JSON and ulp_reference.json.

## A18

**Issue:** L2's empty-backlog branch ('run §5 and add ≥3 new rows in PAYS_OFF or UNEXPLORED families') is the one step with genuinely unbounded improvisation: the LLM must invent hypotheses, predicted magnitudes, and budgets with no source of candidates once the direction map's 'Recommended next' section is empty (which is exactly when this branch triggers).

**Fix:** Bound it: maintain a committed analysis/idea_bank.md of pre-written candidate hypotheses (seeded by a human alongside seeding step 3); the rule becomes 'promote the top ≥3 idea-bank entries whose family is not DEAD_END into backlog rows verbatim; if the idea bank is empty, stop and emit REQUEST_HUMAN_INPUT instead of continuing the loop.'

## A19

**Issue:** Minor spec bug in the append-only fence: 'the diff must contain only + lines' is unsatisfiable literally — unified diff output always contains header lines (diff --git, index, +++, ---, @@), so a naive implementation would always fail or the implementer must improvise the filter.

**Fix:** Respecify: in the output of `git diff --unified=0 origin/master...HEAD -- analysis/experiments.jsonl`, there must be no line starting with '-' except the '--- ' file header, and every '+' content line must parse as JSON with an id greater than all prior ids.

## Critic overall assessment

The runbook's factual foundation is solid: every claim in its 'verified ground truth' section checks out against the repo (benchmark.cpp line numbers 459/557-560/~1643, CLI flags and 100000-iteration default, adaptive 3/5/7 rounds with 1024-expression chunks, kGoldenResultsCount=44229 with sizeof-derived kExpressionCount, CMakeLists target names benchmark/unit_tests/run_benchmarks with /O2 and -O3 -DNDEBUG and the BENCHMARK_COMPILER[_FLAGS] defines, the CI workflow contents, record_benchmark_summary.py's brittle regexes, and the CSV's 13 codex branches with 11/12-ns quantization — including the ykpt1y accuracy shift 20164→20325 it calls out). The CMake build commands are valid for this repo (cmake_minimum 3.16 permits multi-target --target; the explicit target_compile_options make the flags fence work even in CI's typeless configure), the powercfg GUID is the real High-performance GUID, the classifier's variable list matches the benchmark's bind() call, the decision thresholds are all concrete numbers (1.0%/2.0% primary, 3% class, 1% secondary, 2× spread, 3% canary, 5% probe, 14 days), and the ledger is fully specified with field list plus a filled example. However, the runbook is not yet executable without improvisation. The blocking defects are: a sign-convention contradiction that makes the direction-map rules and their own worked example mutually unsatisfiable; 'copy-pasteable' commands hard-coding host_id IMAK-PC when the actual host is ANDREAS, plus an unresolved <runid> placeholder; a seeding step that requires ledger rows for codex/* branches that no longer exist via a tool specified to refuse exactly that input; a first-baseline/first-PR bootstrap deadlock around the not-yet-existing ulp_reference.json; a single ULP reference that physically cannot match on both CI OSes (glibc vs UCRT libm, plus Intel-vs-AMD x87 microcode); a fetch-depth-1 checkout that breaks the append-only git check; and an --append flag invoked in the loop but absent from the gate's CLI spec. A second tier of underspecification (binary path resolution across multi/single-config generators, canary-run parameters, smoke-mode exemptions, dirty-tree definition that currently trips on the untracked .claude/ directory, the impossible grep of a sizeof-derived constant, ULP bin key names, and the unbounded empty-backlog hypothesis-generation branch) would each force an implementing LLM to make a judgment call. All are fixable with the concrete amendments listed; with them applied, the runbook would meet its no-improvisation bar.


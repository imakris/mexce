# ASMD optimizer branch benchmarks

Benchmarks collected with `run_benchmarks` on the provided branches.

| Branch | Avg runtime (ns) | Δ runtime vs master (ns) | Avg compile (µs) | Δ compile vs master (µs) | Total exec (ms) | Exact matches | 1-16 ULP | >65536 ULP |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| codex/improve-asmd-optimizer-and-benchmark-s6rmtm | 11.000 | -1.000 | 198.835 | -7.874 | 50.341 | 20164 | 23494 | 15 |
| codex/improve-asmd-optimizer-and-run-benchmark-w6gusv | 11.000 | -1.000 | 198.515 | -8.194 | 50.472 | 20164 | 23494 | 15 |
| codex/optimize-asmd-and-run-benchmark-2ut8ap | 11.000 | -1.000 | 192.650 | -14.059 | 49.085 | 20164 | 23494 | 15 |
| codex/optimize-asmd-and-run-benchmark-ykpt1y | 11.000 | -1.000 | 199.410 | -7.299 | 50.321 | 20325 | 23345 | 15 |
| codex/improve-asmd-optimizer-and-benchmark | 12.000 | +0.000 | 197.202 | -9.507 | 51.486 | 20164 | 23494 | 15 |
| codex/improve-asmd-optimizer-and-benchmark-gw5o3x | 12.000 | +0.000 | 199.552 | -7.157 | 51.709 | 20164 | 23494 | 15 |
| codex/improve-asmd-optimizer-and-benchmark-qwj19s | 12.000 | +0.000 | 211.625 | +4.916 | 54.794 | 20164 | 23494 | 15 |
| codex/improve-asmd-optimizer-and-run-benchmark | 12.000 | +0.000 | 196.460 | -10.249 | 51.387 | 20164 | 23494 | 15 |
| codex/improve-asmd-optimizer-and-run-benchmark-shydiw | 12.000 | +0.000 | 201.476 | -5.233 | 50.883 | 20164 | 23494 | 15 |
| codex/improve-asmd-optimizer-and-run-benchmark-unsjv5 | 12.000 | +0.000 | 208.978 | +2.269 | 52.246 | 20164 | 23494 | 15 |
| codex/optimize-asmd-and-run-benchmark | 12.000 | +0.000 | 203.880 | -2.829 | 53.024 | 20164 | 23494 | 15 |
| codex/optimize-asmd-and-run-benchmark-stlq24 | 12.000 | +0.000 | 209.213 | +2.504 | 52.913 | 20164 | 23494 | 15 |
| master | 12.000 | +0.000 | 206.709 | +0.000 | 52.851 | 20164 | 23494 | 15 |

## Branch assessments

- **codex/optimize-asmd-and-run-benchmark-2ut8ap** – replaces the associative map accumulation with a vector that is sorted and coalesced, cutting average runtime by 1 ns while preserving accuracy.
- **codex/optimize-asmd-and-run-benchmark-ykpt1y** – adds iterator-based helpers that can operate on both vector and map term collections, uses long double for intermediate constant reduction, and selects between vector and map paths based on term count. This yields faster code and slightly more exact matches.
- **codex/improve-asmd-optimizer-and-benchmark-s6rmtm** – coalesces like terms with a sorted vector before emitting code, reducing map churn and improving runtime.
- **codex/improve-asmd-optimizer-and-run-benchmark-w6gusv** – keeps the map but adds inline exponent handling for small integers, avoiding expensive `pow` calls and improving speed.
- The remaining branches either change code structure without measurable runtime gains (`codex/optimize-asmd-and-run-benchmark-stlq24`, `codex/optimize-asmd-and-run-benchmark`, `codex/improve-asmd-optimizer-and-benchmark[-gw5o3x|-qwj19s]`, `codex/improve-asmd-optimizer-and-run-benchmark[-unsjv5|-shydiw]`) or only tweak aggregation without a net benefit.

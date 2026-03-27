#include "benchmark_expressions.h"
#include "benchmark_results.h"
#include "benchmark_native_expressions.h"

#ifdef _MSC_VER
#  pragma warning(push)
#  pragma warning(disable:4100 4189 4456)
#endif
#include "mexce.h"
#ifdef _MSC_VER
#  pragma warning(pop)
#endif

#include "get_wtime.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#  include <direct.h>
#  include <stdlib.h>
#else
#  include <limits.h>
#  include <unistd.h>
#endif

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

// ----------------------------- Utils ---------------------------------

// Human-readable ns formatter (no chrono)
static std::string format_ns(double ns)
{
    struct unit_t { const char* name; double factor; };
    static const unit_t s_units[] = {
        {"sec", 1000000000.0},
        {"ms",     1000000.0},
        {"us",        1000.0},
        {"ns",           1.0}
    };

    if (ns == 0.0) {
        return "0.0 ns";
    }

    const unit_t* chosen = &s_units[3];
    for (size_t i = 0; i < sizeof(s_units) / sizeof(s_units[0]); ++i) {
        double value = ns / s_units[i].factor;
        if (value >= 1.0 && value < 1000.0) {
            chosen = &s_units[i];
            break;
        }
    }
    if (ns / s_units[0].factor >= 1000.0) {
        chosen = &s_units[0];
    }

    double value = ns / chosen->factor;

    // Format with up to 3 decimal places, removing trailing zeros
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(3) << value;
    std::string out = oss.str();

    // Remove trailing zeros after decimal point, but keep at least one digit
    size_t dot_pos = out.find('.');
    if (dot_pos != std::string::npos) {
        size_t last_nonzero = out.find_last_not_of('0');
        if (last_nonzero != std::string::npos && last_nonzero > dot_pos) {
            out.erase(last_nonzero + 1);
        } else if (last_nonzero == dot_pos) {
            out.erase(dot_pos + 2);  // Keep ".0"
        }
    }

    out += ' ';
    out += chosen->name;
    return out;
}

enum Iteration_parse_result {
    success,
    not_numeric,
    invalid_range
};

static Iteration_parse_result parse_iterations(const char* text, int* value)
{
    try {
        int parsed = std::stoi(text);
        if (parsed <= 0) {
            return invalid_range;
        }
        *value = parsed;
        return success;
    }
    catch (const std::invalid_argument&) {
        return not_numeric;
    }
    catch (const std::out_of_range&) {
        return invalid_range;
    }
}

static std::string resolve_full_path(const std::string& path)
{
#ifdef _WIN32
    char absbuf[_MAX_PATH];
    if (_fullpath(absbuf, path.c_str(), _MAX_PATH)) {
        return std::string(absbuf);
    }
    char cwdbuf[_MAX_PATH];
    if (_getcwd(cwdbuf, _MAX_PATH)) {
        std::string s = cwdbuf;
        if (!s.empty() && s.back() != '\\') {
            s += '\\';
        }
        s += path;
        return s;
    }
    return path;
#else
    char* rp = realpath(path.c_str(), NULL);
    if (rp) {
        std::string s(rp);
        free(rp);
        return s;
    }
    char cwdbuf[PATH_MAX];
    if (getcwd(cwdbuf, sizeof(cwdbuf))) {
        std::string s = cwdbuf;
        if (!s.empty() && s.back() != '/') {
            s += '/';
        }
        s += path;
        return s;
    }
    return path;
#endif
}

// ----------------------- Comparison (test) ---------------------------

static uint64_t ulp_distance(double a, double b)
{
    if (std::isnan(a) || std::isnan(b)) {
        return UINT64_MAX;
    }

    int64_t ai = 0, bi = 0;
    std::memcpy(&ai, &a,  sizeof(double));
    std::memcpy(&bi, &b,  sizeof(double));

    if (ai < 0) {
        ai = (int64_t)0x8000000000000000ULL - ai;
    }
    if (bi < 0) {
        bi = (int64_t)0x8000000000000000ULL - bi;
    }

    const uint64_t ua = (uint64_t)ai, ub = (uint64_t)bi;
    return (ua > ub) ? (ua - ub) : (ub - ua);
}

struct ulp_sum_t {
    uint64_t hi;
    uint64_t lo;

    ulp_sum_t() : hi(0), lo(0) {}

    void add(uint64_t value)
    {
        if (value == UINT64_MAX) {
            return;
        }
        uint64_t old = lo;
        lo += value;
        if (lo < old) {
            ++hi;
        }
    }
};

static std::string to_decimal_u128(uint64_t hi, uint64_t lo)
{
    if (hi == 0) {
        return std::to_string(lo);
    }

    uint32_t words[4];
    words[0] = (uint32_t)(hi >> 32);
    words[1] = (uint32_t)hi;
    words[2] = (uint32_t)(lo >> 32);
    words[3] = (uint32_t)lo;

    std::string digits;
    digits.reserve(39);

    while (words[0] || words[1] || words[2] || words[3]) {
        uint64_t rem = 0;
        for (int i = 0; i < 4; ++i) {
            uint64_t cur = (rem << 32) | words[i];
            words[i] = (uint32_t)(cur / 10);
            rem = cur % 10;
        }
        digits.push_back((char)('0' + rem));
    }

    std::reverse(digits.begin(), digits.end());
    return digits;
}

// ------------------------------ Main ---------------------------------

static void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " [options] [iterations] [output_file]\n\n";
    std::cerr << "Options:\n";
    std::cerr << "  --comprehensive, -c   Run comprehensive comparison (default)\n";
    std::cerr << "  --single              Run single-configuration benchmark\n";
    std::cerr << "  --x87                 Force x87 backend (with --single)\n";
    std::cerr << "  --sse2                Force SSE2 backend (with --single, default)\n";
    std::cerr << "  --fast-math           Enable fast-math optimizations (with --single)\n";
    std::cerr << "  --help, -h            Show this help message\n";
    std::cerr << "\n";
    std::cerr << "By default, runs comprehensive comparison across all configurations.\n";
    std::cerr << "Use --single to run a single-configuration benchmark with detailed output.\n";
}

struct benchmark_config {
    int iterations = 100000;
    std::string output_path = "benchmark_results.txt";
    bool comprehensive = true;  // Default to comprehensive mode
    bool force_x87 = false;
    bool force_sse2 = false;
    bool fast_math = false;
};

static bool parse_args(int argc, char* argv[], benchmark_config& config)
{
    bool iterations_set = false;
    bool output_set = false;
    bool explicit_comprehensive = false;
    bool explicit_single = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return false;
        }
        if (arg == "--comprehensive" || arg == "-c") {
            explicit_comprehensive = true;
            config.comprehensive = true;
            continue;
        }
        if (arg == "--single") {
            explicit_single = true;
            config.comprehensive = false;
            continue;
        }
        if (arg == "--x87") {
            config.force_x87 = true;
            config.comprehensive = false;  // Backend options imply single mode
            continue;
        }
        if (arg == "--sse2") {
            config.force_sse2 = true;
            config.comprehensive = false;  // Backend options imply single mode
            continue;
        }
        if (arg == "--fast-math") {
            config.fast_math = true;
            config.comprehensive = false;  // fast-math implies single mode
            continue;
        }

        // Try to parse as iteration count
        int parsed = 0;
        Iteration_parse_result pr = parse_iterations(argv[i], &parsed);
        if (pr == success) {
            if (iterations_set) {
                std::cerr << "Iteration count specified twice." << std::endl;
                return false;
            }
            config.iterations = parsed;
            iterations_set = true;
        }
        else if (pr == not_numeric) {
            if (output_set) {
                std::cerr << "Unknown option or extra argument: " << arg << std::endl;
                return false;
            }
            config.output_path = arg;
            output_set = true;
        }
        else {
            std::cerr << "Iteration count must be a positive integer." << std::endl;
            return false;
        }
    }

    if (config.force_x87 && config.force_sse2) {
        std::cerr << "Cannot specify both --x87 and --sse2." << std::endl;
        return false;
    }

    if (explicit_comprehensive && explicit_single) {
        std::cerr << "Cannot specify both --comprehensive and --single." << std::endl;
        return false;
    }

    // If explicit comprehensive requested but backend options given, warn and use comprehensive
    if (explicit_comprehensive && (config.force_x87 || config.force_sse2 || config.fast_math)) {
        std::cerr << "Note: --x87, --sse2, --fast-math are ignored in comprehensive mode." << std::endl;
        config.comprehensive = true;
    }

    return true;
}

// ULP bin thresholds for precision analysis
static constexpr uint64_t k_ulp_bin_thresholds[] = {
    16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536
};
static constexpr size_t k_ulp_num_bins = sizeof(k_ulp_bin_thresholds) / sizeof(k_ulp_bin_thresholds[0]);
static constexpr long double k_zero_abs_tol = 1e-12L;

// Configuration definition for benchmark
struct mexce_config {
    std::string name;
    bool prefer_x87;
    bool fast_math;
};

// Unified per-expression record (used by both single and comprehensive modes)
struct benchmark_record {
    std::string expr;
    std::string optimized_expr;
    std::string bytes_expr;
    bool compiled = false;
    bool eval_ok = false;
    bool native_available = false;
    bool native_eval_ok = false;
    long double expected = 0;
    double mexce_result = 0;
    double native_result = 0;
    uint64_t ulp_mexce_vs_compiler = UINT64_MAX;
    uint64_t ulp_mexce_vs_reference = UINT64_MAX;
    uint64_t ulp_compiler_vs_reference = UINT64_MAX;
    uint64_t compile_ns = 0;
    uint64_t avg_ns = 0;
    long long dur_ns = 0;
    uint64_t native_avg_ns = 0;
    long long native_dur_ns = 0;
    std::string error;
    mexce::backend_type backend_used = mexce::backend_type::none;
};

// Results for a single configuration (summary statistics)
struct benchmark_result {
    std::string config_name;
    std::vector<benchmark_record> records;

    // Summary stats
    size_t expressions_tested = 0;
    size_t compiled_count = 0;
    size_t eval_count = 0;
    size_t compile_fail_count = 0;
    size_t eval_fail_count = 0;
    size_t sse2_backend_count = 0;  // Expressions that actually used SSE2 backend
    size_t x87_backend_count = 0;   // Expressions that actually used x87 backend

    // Timing stats
    long long total_compile_ns = 0;
    long long total_eval_ns = 0;
    double avg_compile_ns = 0.0;
    double avg_eval_ns = 0.0;

    // Timing stats for expressions using the actual requested backend only
    // (excludes expressions that fell back to a different backend)
    long long total_eval_ns_actual_backend = 0;
    size_t eval_count_actual_backend = 0;

    // Native timing for expressions that used the actual requested backend AND have native
    // This enables fair 1:1 comparison between mexce and native for the same expression set
    long long total_native_ns_actual_backend = 0;
    size_t native_count_actual_backend = 0;

    uint64_t compile_min_ns = std::numeric_limits<uint64_t>::max();
    uint64_t compile_max_ns = 0;
    std::vector<uint64_t> compile_times;

    // Native timing (all native expressions, for reference)
    long long total_native_ns = 0;
    size_t benchmarked_native_count = 0;

    // ULP stats for mexce vs reference
    size_t exact_zero_count_mexce_ref = 0;
    std::vector<size_t> ulp_bins_mexce_ref;
    ulp_sum_t ulp_sum_mexce_ref;
    size_t comparisons_mexce_ref = 0;

    // ULP stats for compiler vs reference
    size_t exact_zero_count_comp_ref = 0;
    std::vector<size_t> ulp_bins_comp_ref;
    ulp_sum_t ulp_sum_comp_ref;
    size_t comparisons_comp_ref = 0;

    // ULP stats for mexce vs compiler
    size_t exact_zero_count_mexce_comp = 0;
    std::vector<size_t> ulp_bins_mexce_comp;
    ulp_sum_t ulp_sum_mexce_comp;
    size_t comparisons_mexce_comp = 0;

    benchmark_result()
        : ulp_bins_mexce_ref(k_ulp_num_bins + 1, 0)
        , ulp_bins_comp_ref(k_ulp_num_bins + 1, 0)
        , ulp_bins_mexce_comp(k_ulp_num_bins + 1, 0)
    {}
};

static void update_ulp_bins(uint64_t ulp, size_t& exact_zero_count, std::vector<size_t>& bins, size_t& comparisons) {
    if (ulp == UINT64_MAX) {
        return;
    }
    ++comparisons;
    if (ulp == 0) {
        ++exact_zero_count;
        return;
    }
    size_t bin_idx = k_ulp_num_bins;
    for (size_t bin_i = 0; bin_i < k_ulp_num_bins; ++bin_i) {
        if (ulp <= k_ulp_bin_thresholds[bin_i]) {
            bin_idx = bin_i;
            break;
        }
    }
    ++bins[bin_idx];
}

// Run benchmark for a single configuration with full per-expression records
// This is the core benchmark function used by both single and comprehensive modes
//
// Noise reduction strategy: Round-robin timing
// Instead of running all timing trials for one expression back-to-back (where a
// single VM/container scheduling hiccup can inflate ALL samples), we spread timing
// across multiple rounds. In each round, every expression is timed once. Between
// rounds, significant wall time passes (processing all other expressions), so a
// hiccup in one round only affects that round's measurement. The minimum across
// rounds is taken as the result, since noise only adds latency, never removes it.
static benchmark_result run_benchmark(
    const mexce_config& cfg,
    int iterations,
    std::ostream& progress_out)
{
    benchmark_result result;
    result.config_name = cfg.name;

    mexce::evaluator eval;
    eval.opts().prefer_x87 = cfg.prefer_x87;
    eval.opts().fast_math = cfg.fast_math;

    double a = 1.1, b = 2.2, c = 3.3, x = 4.4, y = 5.5, z = 6.6, w = 7.7;
    eval.bind(a, "a", b, "b", c, "c", x, "x", y, "y", z, "z", w, "w");
    mexce::benchmark_data::NativeContext native_ctx{};

    const size_t total_expressions = mexce::benchmark_data::kExpressionCount;
    const size_t iterations_u = static_cast<size_t>(iterations);

    // Adaptive round count: more rounds for fewer iterations (higher variance)
    const int num_rounds =
        (iterations >= 10000) ? 3 :
        (iterations >= 1000)  ? 5 : 7;

    result.records.reserve(total_expressions);
    result.compile_times.reserve(total_expressions);

    progress_out << "  Testing " << cfg.name << "..." << std::flush;

    // ===== Phase 1: Compilation + Precision (single pass) =====
    // Compile each expression, evaluate once for correctness/precision.
    // No timing here - that's done in Phase 2 with round-robin.

    for (size_t idx = 0; idx < total_expressions; ++idx) {
        const std::string expr = mexce::benchmark_data::kExpressions[idx];
        const long double golden = mexce::benchmark_data::kGoldenResults[idx];
        const double golden_d = static_cast<double>(golden);

        a = 1.1; b = 2.2; c = 3.3; x = 4.4; y = 5.5; z = 6.6; w = 7.7;

        benchmark_record rec;
        rec.expr = expr;
        rec.expected = golden;
        rec.native_available = idx < mexce::benchmark_data::kNativeExpressionsCount;
        rec.mexce_result = std::numeric_limits<double>::quiet_NaN();
        rec.native_result = std::numeric_limits<double>::quiet_NaN();

        result.expressions_tested++;

        // Native evaluation (for comparison)
        if (rec.native_available) {
            native_ctx.a = a; native_ctx.b = b; native_ctx.c = c;
            native_ctx.x = x; native_ctx.y = y; native_ctx.z = z; native_ctx.w = w;

            rec.native_result = mexce::benchmark_data::kNativeExpressions[idx](native_ctx);
            rec.native_eval_ok = true;

            const bool native_zero = std::fabs(rec.native_result) <= (double)k_zero_abs_tol;
            const bool golden_zero = std::abs(golden) <= k_zero_abs_tol;
            rec.ulp_compiler_vs_reference = (native_zero && golden_zero) ? 0 : ulp_distance(rec.native_result, golden_d);

            result.ulp_sum_comp_ref.add(rec.ulp_compiler_vs_reference);
            update_ulp_bins(rec.ulp_compiler_vs_reference, result.exact_zero_count_comp_ref,
                           result.ulp_bins_comp_ref, result.comparisons_comp_ref);
        }

        // Mexce compilation
        mexce::stopwatch compile_timer;
        try {
            eval.set_expression(expr);
            rec.compile_ns = (uint64_t)compile_timer.elapsed_nanoseconds();
            rec.optimized_expr = eval.get_optimized_expression();
            rec.bytes_expr = eval.get_byte_representation();
            rec.compiled = true;
            rec.backend_used = eval.get_backend();

            result.compiled_count++;
            result.total_compile_ns += (long long)rec.compile_ns;
            result.compile_min_ns = std::min(result.compile_min_ns, rec.compile_ns);
            result.compile_max_ns = std::max(result.compile_max_ns, rec.compile_ns);
            result.compile_times.push_back(rec.compile_ns);

            // Track which backend was actually used
            if (rec.backend_used == mexce::backend_type::sse2) {
                result.sse2_backend_count++;
            } else if (rec.backend_used == mexce::backend_type::x87) {
                result.x87_backend_count++;
            }
        }
        catch (const std::exception& e) {
            rec.compile_ns = (uint64_t)compile_timer.elapsed_nanoseconds();
            result.compile_fail_count++;
            rec.error = std::string("compile: ") + e.what();
            result.records.push_back(rec);
            continue;
        }

        // Mexce evaluation (once for precision)
        try {
            rec.mexce_result = eval.evaluate();
            rec.eval_ok = true;
            result.eval_count++;
        }
        catch (const std::exception& e) {
            result.eval_fail_count++;
            rec.error = std::string("evaluate: ") + e.what();
            result.records.push_back(rec);
            continue;
        }

        // ULP calculations
        const bool mexce_zero = std::fabs(rec.mexce_result) <= (double)k_zero_abs_tol;
        const bool golden_zero = std::abs(golden) <= k_zero_abs_tol;
        rec.ulp_mexce_vs_reference = (mexce_zero && golden_zero) ? 0 : ulp_distance(rec.mexce_result, golden_d);

        result.ulp_sum_mexce_ref.add(rec.ulp_mexce_vs_reference);
        update_ulp_bins(rec.ulp_mexce_vs_reference, result.exact_zero_count_mexce_ref,
                       result.ulp_bins_mexce_ref, result.comparisons_mexce_ref);

        if (rec.native_eval_ok) {
            const bool native_zero = std::fabs(rec.native_result) <= (double)k_zero_abs_tol;
            rec.ulp_mexce_vs_compiler = (mexce_zero && native_zero) ? 0 : ulp_distance(rec.mexce_result, rec.native_result);

            result.ulp_sum_mexce_comp.add(rec.ulp_mexce_vs_compiler);
            update_ulp_bins(rec.ulp_mexce_vs_compiler, result.exact_zero_count_mexce_comp,
                           result.ulp_bins_mexce_comp, result.comparisons_mexce_comp);
        }

        result.records.push_back(rec);
    }

    // ===== Phase 2: Round-robin timing for mexce =====
    // Collect indices of expressions that compiled and evaluated successfully
    std::vector<size_t> timing_indices;
    timing_indices.reserve(result.eval_count);
    for (size_t idx = 0; idx < result.records.size(); ++idx) {
        if (result.records[idx].compiled && result.records[idx].eval_ok) {
            timing_indices.push_back(idx);
        }
    }

    // Per-expression timing samples across rounds
    std::vector<std::vector<long long>> round_timings(result.records.size());
    for (size_t idx : timing_indices) {
        round_timings[idx].reserve(static_cast<size_t>(num_rounds));
    }

    for (int round = 0; round < num_rounds; ++round) {
        for (size_t idx : timing_indices) {
            a = 1.1; b = 2.2; c = 3.3; x = 4.4; y = 5.5; z = 6.6; w = 7.7;
            eval.set_expression(result.records[idx].expr);

            mexce::stopwatch timer;
            for (size_t i = 0; i < iterations_u; ++i) {
                (void)eval.evaluate();
            }
            round_timings[idx].push_back(timer.elapsed_nanoseconds());
        }
    }

    // Select best timing: take the minimum across rounds.
    // Noise (scheduling, interrupts, cache misses) only adds latency, so the
    // fastest observed run is the closest to the true execution time.
    for (size_t idx : timing_indices) {
        long long best = *std::min_element(round_timings[idx].begin(),
                                           round_timings[idx].end());

        result.records[idx].dur_ns = best;
        result.records[idx].avg_ns = (uint64_t)((long double)best / (long double)iterations_u + 0.5L);

        result.total_eval_ns += best;

        bool used_requested_backend = (cfg.prefer_x87 && result.records[idx].backend_used == mexce::backend_type::x87) ||
                                      (!cfg.prefer_x87 && result.records[idx].backend_used == mexce::backend_type::sse2);
        if (used_requested_backend) {
            result.total_eval_ns_actual_backend += best;
            result.eval_count_actual_backend++;
        }
    }

    // ===== Phase 3: Round-robin timing for native expressions =====
    const size_t native_count = mexce::benchmark_data::kNativeExpressionsCount;

    std::vector<std::vector<long long>> native_round_timings(native_count);
    for (size_t idx = 0; idx < native_count; ++idx) {
        native_round_timings[idx].reserve(static_cast<size_t>(num_rounds));
    }

    for (int round = 0; round < num_rounds; ++round) {
        for (size_t idx = 0; idx < native_count; ++idx) {
            native_ctx.a = 1.1; native_ctx.b = 2.2; native_ctx.c = 3.3;
            native_ctx.x = 4.4; native_ctx.y = 5.5; native_ctx.z = 6.6; native_ctx.w = 7.7;

            mexce::stopwatch timer;
            for (size_t i = 0; i < iterations_u; ++i) {
                (void)mexce::benchmark_data::kNativeExpressions[idx](native_ctx);
            }
            native_round_timings[idx].push_back(timer.elapsed_nanoseconds());
        }
    }

    // Select best native timing (minimum across rounds)
    for (size_t idx = 0; idx < native_count; ++idx) {
        long long native_dur = *std::min_element(native_round_timings[idx].begin(),
                                                  native_round_timings[idx].end());

        result.total_native_ns += native_dur;
        result.benchmarked_native_count++;

        if (idx < result.records.size()) {
            result.records[idx].native_dur_ns = native_dur;
            result.records[idx].native_avg_ns = (uint64_t)((long double)native_dur / (long double)iterations_u + 0.5L);

            const auto& rec = result.records[idx];
            bool used_requested_backend = (cfg.prefer_x87 && rec.backend_used == mexce::backend_type::x87) ||
                                          (!cfg.prefer_x87 && rec.backend_used == mexce::backend_type::sse2);
            if (used_requested_backend && rec.native_available) {
                result.total_native_ns_actual_backend += native_dur;
                result.native_count_actual_backend++;
            }
        }
    }

    // Compute averages
    if (result.compiled_count > 0) {
        result.avg_compile_ns = (double)result.total_compile_ns /
            (double)result.compiled_count;
    }
    if (result.eval_count > 0) {
        result.avg_eval_ns = (double)result.total_eval_ns /
            ((double)result.eval_count * (double)iterations);
    }

    // Show compilation stats including backend breakdown
    progress_out << " done (" << result.compiled_count << "/" << total_expressions << " compiled";
    if (result.sse2_backend_count > 0 && result.x87_backend_count > 0) {
        progress_out << ": " << result.sse2_backend_count << " SSE2, " << result.x87_backend_count << " x87";
    } else if (result.sse2_backend_count > 0) {
        progress_out << ", all SSE2";
    } else if (result.x87_backend_count > 0) {
        progress_out << ", all x87";
    }
    progress_out << ", " << num_rounds << " timing rounds)\n";
    return result;
}

// Print explanatory notes about backends and options
static void print_backend_notes(std::ostream& out) {
    out << "\nBackend notes:\n";
    out << "  SSE2:      Uses SSE2 scalar instructions (addsd, mulsd, etc.) for arithmetic,\n";
    out << "             plus SSE4.1 roundsd for rounding functions (floor, ceil, round, trunc).\n";
    out << "  x87:       Uses the legacy x87 FPU stack. May produce different precision results\n";
    out << "             due to 80-bit extended precision intermediate calculations.\n";
    out << "  fast-math: Enables algebraic optimizations (e.g., x*0=0, x+0=x) that assume\n";
    out << "             no NaN/Inf values, similar to compiler -ffast-math behavior.\n";
    out << "  Native:    Compiler-generated code using the same flags shown above.\n";
    out << std::endl;  // flush to ensure notes appear before progress output
}

// Print detailed per-expression report from benchmark result
static void print_detailed_report(
    const benchmark_result& result,
    std::ostream& out)
{
    const std::string line(78, '=');

    out << "\n" << line << "\n";
    out << "DETAILED PER-EXPRESSION REPORT (" << result.config_name << ")\n";
    out << line << "\n\n";

    // Sort by speedup (problematic regressions first)
    std::vector<const benchmark_record*> sorted_records;
    sorted_records.reserve(result.records.size());
    for (const auto& rec : result.records) {
        sorted_records.push_back(&rec);
    }
    std::sort(sorted_records.begin(), sorted_records.end(), [](const benchmark_record* a, const benchmark_record* b) {
        if (!a->eval_ok && b->eval_ok) return true;
        if (a->eval_ok && !b->eval_ok) return false;
        if (!a->compiled && b->compiled) return true;
        if (a->compiled && !b->compiled) return false;
        if (!a->native_eval_ok && b->native_eval_ok) return false;
        if (a->native_eval_ok && !b->native_eval_ok) return true;
        if (!a->native_eval_ok && !b->native_eval_ok) return a->expr < b->expr;
        double speedup_a = (double)a->native_avg_ns / std::max(1.0, (double)a->avg_ns);
        double speedup_b = (double)b->native_avg_ns / std::max(1.0, (double)b->avg_ns);
        if (std::abs(speedup_a - speedup_b) > 1e-6) return speedup_a < speedup_b;
        return a->expr < b->expr;
    });

    auto format_ulp = [](uint64_t v) -> std::string { return v == UINT64_MAX ? "-" : std::to_string(v); };
    auto format_speedup = [](uint64_t native_ns, uint64_t mexce_ns) -> std::string {
        if (native_ns == 0) return "-";
        double sp = (double)native_ns / std::max(1.0, (double)mexce_ns);
        std::stringstream ss;
        ss << std::fixed << std::setprecision(2) << sp << "x";
        return ss.str();
    };

    out << "Status      ULP(Mx-Cp)  ULP(Mx-Ref)  ULP(Cp-Ref)  Compile       Avg(Mx)       Avg(Cp)       Speedup   Expression : Optimized : Bytes\n";
    out << "----------  ----------  -----------  -----------  ------------  ------------  ------------  --------  ----------------------------------------\n";

    for (const auto* rec : sorted_records) {
        std::string status = rec->compiled ? (rec->eval_ok ? "ok" : "eval") : "compile";
        out << std::left << std::setw(10) << status << "  "
            << std::setw(10) << format_ulp(rec->ulp_mexce_vs_compiler) << "  "
            << std::setw(11) << format_ulp(rec->ulp_mexce_vs_reference) << "  "
            << std::setw(11) << format_ulp(rec->ulp_compiler_vs_reference) << "  "
            << std::setw(12) << format_ns(rec->compile_ns) << "  "
            << std::setw(12) << (rec->eval_ok ? format_ns(rec->avg_ns) : "-") << "  "
            << std::setw(12) << (rec->native_eval_ok ? format_ns(rec->native_avg_ns) : "-") << "  "
            << std::setw(8) << (rec->native_eval_ok && rec->eval_ok ? format_speedup(rec->native_avg_ns, rec->avg_ns) : "-") << "  "
            << rec->expr;
        if (rec->compiled) {
            out << " : " << rec->optimized_expr << " : " << rec->bytes_expr;
        }
        out << "\n";
        if (!rec->error.empty()) {
            out << "    note: " << rec->error << "\n";
        }
    }
}

// Print single-mode benchmark report (more detailed than comprehensive mode)
static int print_single_mode_report(
    const benchmark_result& result,
    int iterations,
    std::ostream& out)
{
    const std::string line = std::string(65, '-');
    auto print_kv = [&out](const std::string& k, const std::string& v) {
        out << std::left << std::setw(40) << k << v << "\n";
    };

    // ========== TEST SUMMARY ==========
    out << line << "\n" << "TEST SUMMARY" << "\n" << line << "\n";
    print_kv("Compiled", std::to_string(result.compiled_count));
    print_kv("Failed to compile", std::to_string(result.compile_fail_count));
    if (result.eval_fail_count > 0) {
        print_kv("Evaluation failures", std::to_string(result.eval_fail_count));
    }

    const size_t native_covered = std::min(result.expressions_tested,
        mexce::benchmark_data::kNativeExpressionsCount);
    print_kv("Native expressions covered", std::to_string(native_covered));

    // ========== ACCURACY DISTRIBUTION (ULP) ==========
    out << "\nAccuracy distribution (ULP):\n";

    struct Distribution_column {
        std::string title;
        size_t comparisons;
        size_t exact_zero_count;
        const std::vector<size_t>* bins;
    };

    const Distribution_column distribution_columns[] = {
        {"Mexce vs Reference",    result.comparisons_mexce_ref, result.exact_zero_count_mexce_ref, &result.ulp_bins_mexce_ref},
        {"Compiler vs Reference", result.comparisons_comp_ref,  result.exact_zero_count_comp_ref,  &result.ulp_bins_comp_ref},
        {"Mexce vs Compiler",     result.comparisons_mexce_comp, result.exact_zero_count_mexce_comp, &result.ulp_bins_mexce_comp}
    };

    std::vector<std::string> row_labels;
    row_labels.reserve(k_ulp_num_bins + 2);
    row_labels.emplace_back("0 (exact)");
    for (size_t bin_idx = 0; bin_idx < k_ulp_num_bins; ++bin_idx) {
        char buf[32];
        uint64_t lo = (bin_idx == 0 ? 1 : (k_ulp_bin_thresholds[bin_idx - 1] + 1));
        uint64_t hi = k_ulp_bin_thresholds[bin_idx];
        std::snprintf(buf, sizeof(buf), "%llu-%llu", (unsigned long long)lo, (unsigned long long)hi);
        row_labels.emplace_back(buf);
    }
    row_labels.emplace_back(">65536");

    using std::max;

    const std::string label_header = "Metric";
    size_t label_width = label_header.size();
    for (const auto& label : row_labels) {
        label_width = max(label_width, label.size());
    }

    std::vector<std::vector<std::string>> column_values(3);
    for (size_t column_idx = 0; column_idx < 3; ++column_idx) {
        const Distribution_column& column = distribution_columns[column_idx];
        std::vector<std::string>& values = column_values[column_idx];
        values.reserve(row_labels.size());
        if (column.comparisons == 0) {
            values.emplace_back("-");
            for (size_t bin_idx = 0; bin_idx < k_ulp_num_bins + 1; ++bin_idx) {
                values.emplace_back("-");
            }
        }
        else {
            values.emplace_back(std::to_string(column.exact_zero_count));
            for (size_t bin_idx = 0; bin_idx < k_ulp_num_bins; ++bin_idx) {
                values.emplace_back(std::to_string((*column.bins)[bin_idx]));
            }
            values.emplace_back(std::to_string((*column.bins)[k_ulp_num_bins]));
        }
    }

    std::vector<size_t> column_widths(3, 0);
    for (size_t column_idx = 0; column_idx < 3; ++column_idx) {
        const Distribution_column& column = distribution_columns[column_idx];
        column_widths[column_idx] = column.title.size();
        for (const std::string& value : column_values[column_idx]) {
            column_widths[column_idx] = max(column_widths[column_idx], value.size());
        }
    }

    const auto print_table_row = [&](const std::string& label, size_t row_index) {
        out << std::left << std::setw((int)label_width) << label << "  ";
        for (size_t column_idx = 0; column_idx < 3; ++column_idx) {
            out << std::setw((int)column_widths[column_idx]) << column_values[column_idx][row_index];
            if (column_idx + 1 != 3) {
                out << "  ";
            }
        }
        out << "\n";
    };

    out << std::left << std::setw((int)label_width) << label_header << "  ";
    for (size_t column_idx = 0; column_idx < 3; ++column_idx) {
        out << std::setw((int)column_widths[column_idx]) << distribution_columns[column_idx].title;
        if (column_idx + 1 != 3) {
            out << "  ";
        }
    }
    out << "\n";

    out << std::string((int)label_width, '-') << "  ";
    for (size_t column_idx = 0; column_idx < 3; ++column_idx) {
        out << std::string((int)column_widths[column_idx], '-');
        if (column_idx + 1 != 3) {
            out << "  ";
        }
    }
    out << "\n";

    for (size_t row_idx = 0; row_idx < row_labels.size(); ++row_idx) {
        print_table_row(row_labels[row_idx], row_idx);
    }

    out << "\n";

    // ========== ACCUMULATED ULP DISTANCE ==========
    out << "Accumulated ULP distance (sum):\n";
    print_kv("Mexce vs Reference", result.comparisons_mexce_ref == 0 ? "-" :
        to_decimal_u128(result.ulp_sum_mexce_ref.hi, result.ulp_sum_mexce_ref.lo));
    print_kv("Compiler vs Reference", result.comparisons_comp_ref == 0 ? "-" :
        to_decimal_u128(result.ulp_sum_comp_ref.hi, result.ulp_sum_comp_ref.lo));
    print_kv("Mexce vs Compiler", result.comparisons_mexce_comp == 0 ? "-" :
        to_decimal_u128(result.ulp_sum_mexce_comp.hi, result.ulp_sum_mexce_comp.lo));
    out << "\n";

    // ========== COMPILATION TIME HISTOGRAM ==========
    out << "Compilation time histogram:" << "\n";
    if (result.compile_times.empty()) {
        out << "No successfully compiled expressions." << "\n";
    }
    else {
        constexpr size_t kCompileBins = 10;
        std::vector<size_t> compile_hist(kCompileBins, 0);

        uint64_t log_min_ns = result.compile_min_ns;
        uint64_t log_max_ns = result.compile_max_ns;

        if (log_min_ns == 0) {
            log_min_ns = std::numeric_limits<uint64_t>::max();
            for (uint64_t ns : result.compile_times) {
                if (ns > 0 && ns < log_min_ns) {
                    log_min_ns = ns;
                }
            }
            if (log_min_ns == std::numeric_limits<uint64_t>::max()) {
                log_min_ns = 1;
            }
        }
        if (log_max_ns < log_min_ns) {
            log_max_ns = log_min_ns;
        }

        const long double log_min = std::log10((long double)log_min_ns);
        const long double log_max = std::log10((long double)log_max_ns);
        const long double log_range = (log_max > log_min) ? (log_max - log_min) : 1.0L;

        for (uint64_t ns : result.compile_times) {
            size_t bin_idx = 0;
            if (log_max > log_min && ns > 0) {
                const long double x = std::log10((long double)ns);
                long double t = (x - log_min) / log_range;
                if (t < 0.0L) t = 0.0L;
                if (t > 1.0L) t = 1.0L;
                bin_idx = (size_t)(t * kCompileBins);
                if (bin_idx >= kCompileBins) {
                    bin_idx = kCompileBins - 1;
                }
            }
            compile_hist[bin_idx]++;
        }

        const std::string range_header = "Range";
        const std::string count_header = "Count";
        size_t range_width = range_header.size();
        size_t count_width = count_header.size();
        std::vector<std::string> ranges;
        std::vector<std::string> counts;
        ranges.reserve(kCompileBins);
        counts.reserve(kCompileBins);

        for (size_t bin_idx = 0; bin_idx < kCompileBins; ++bin_idx) {
            uint64_t start_ns = 0;
            uint64_t end_ns = 0;

            if (log_max <= log_min) {
                start_ns = result.compile_min_ns;
                end_ns = result.compile_max_ns;
            } else {
                const long double start_ratio = (long double)bin_idx / (long double)kCompileBins;
                const long double end_ratio = (long double)(bin_idx + 1) / (long double)kCompileBins;

                const long double start_log = log_min + start_ratio * log_range;
                const long double end_log = log_min + end_ratio * log_range;

                start_ns = (uint64_t)std::floor(std::pow((long double)10.0, start_log));
                end_ns   = (uint64_t)std::floor(std::pow((long double)10.0, end_log));

                if (start_ns < result.compile_min_ns) start_ns = result.compile_min_ns;
                if (end_ns > result.compile_max_ns)   end_ns = result.compile_max_ns;
                if (end_ns < start_ns)         end_ns = start_ns;

                if (bin_idx == kCompileBins - 1) {
                    end_ns = result.compile_max_ns;
                }
            }

            std::string label;
            if (start_ns == end_ns) {
                label = format_ns(start_ns);
            }
            else {
                label = format_ns(start_ns) + " - " + format_ns(end_ns);
            }
            ranges.push_back(label);
            counts.push_back(std::to_string(compile_hist[bin_idx]));
            range_width = std::max(range_width, label.size());
            count_width = std::max(count_width, counts.back().size());
        }

        out << std::left << std::setw((int)range_width) << range_header << "  "
            << std::setw((int)count_width) << count_header << "\n";
        out << std::string(range_width, '-') << "  " << std::string(count_width, '-') << "\n";
        for (size_t bin_idx = 0; bin_idx < kCompileBins; ++bin_idx) {
            out << std::left << std::setw((int)range_width) << ranges[bin_idx] << "  "
                << std::setw((int)count_width) << counts[bin_idx] << "\n";
        }
    }

    out << "\n";

    // ========== BENCHMARK SUMMARY ==========
    out << line << "\n" << "BENCHMARK SUMMARY" << "\n" << line << "\n";

    struct Summary_column {
        std::string title;
        std::vector<std::string> values;
    };

    const std::vector<std::string> summary_rows = {
        "Functions benchmarked",
        "Average compilation time",
        "Average runtime per function",
        "Total compilation time",
        "Total function execution time"
    };

    std::vector<Summary_column> summary_columns;
    summary_columns.reserve(2);

    {
        Summary_column mexce_column;
        mexce_column.title = "Mexce";
        mexce_column.values.assign(summary_rows.size(), "-");
        mexce_column.values[0] = std::to_string(result.eval_count);
        if (result.compiled_count > 0) {
            mexce_column.values[1] = format_ns(result.avg_compile_ns);
            mexce_column.values[3] = format_ns((uint64_t)result.total_compile_ns);
        }
        if (result.eval_count > 0) {
            mexce_column.values[2] = format_ns(result.avg_eval_ns);
            mexce_column.values[4] = format_ns((uint64_t)result.total_eval_ns);
        }
        summary_columns.push_back(std::move(mexce_column));
    }

    {
        Summary_column compiler_column;
        compiler_column.title = "Compiler";
        compiler_column.values.assign(summary_rows.size(), "-");
        compiler_column.values[0] = std::to_string(result.benchmarked_native_count);
        if (result.benchmarked_native_count > 0) {
            const uint64_t total_native_calls = (uint64_t)result.benchmarked_native_count * (uint64_t)iterations;
            const uint64_t avg_native_ns = (total_native_calls > 0)
                ? (uint64_t)((double)result.total_native_ns / (double)total_native_calls + 0.5)
                : 0;
            compiler_column.values[2] = format_ns(avg_native_ns);
            compiler_column.values[4] = format_ns((uint64_t)result.total_native_ns);
        }
        summary_columns.push_back(std::move(compiler_column));
    }

    label_width = std::string("Metric").size();
    for (const std::string& row_label : summary_rows) {
        label_width = max(label_width, row_label.size());
    }

    column_widths = std::vector<size_t>(summary_columns.size(), 0);
    for (size_t column_idx = 0; column_idx < summary_columns.size(); ++column_idx) {
        Summary_column& column = summary_columns[column_idx];
        column_widths[column_idx] = column.title.size();
        for (const std::string& value : column.values) {
            column_widths[column_idx] = max(column_widths[column_idx], value.size());
        }
    }

    auto print_summary_row = [&](const std::string& label, size_t row_idx) {
        out << std::left << std::setw((int)label_width) << label << "  ";
        for (size_t column_idx = 0; column_idx < summary_columns.size(); ++column_idx) {
            out << std::setw((int)column_widths[column_idx]) << summary_columns[column_idx].values[row_idx];
            if (column_idx + 1 != summary_columns.size()) {
                out << "  ";
            }
        }
        out << "\n";
    };

    out << std::left << std::setw((int)label_width) << "Metric" << "  ";
    for (size_t column_idx = 0; column_idx < summary_columns.size(); ++column_idx) {
        out << std::setw((int)column_widths[column_idx]) << summary_columns[column_idx].title;
        if (column_idx + 1 != summary_columns.size()) {
            out << "  ";
        }
    }
    out << "\n";

    out << std::string((int)label_width, '-') << "  ";
    for (size_t column_idx = 0; column_idx < summary_columns.size(); ++column_idx) {
        out << std::string((int)column_widths[column_idx], '-');
        if (column_idx + 1 != summary_columns.size()) {
            out << "  ";
        }
    }
    out << "\n";

    for (size_t row_idx = 0; row_idx < summary_rows.size(); ++row_idx) {
        print_summary_row(summary_rows[row_idx], row_idx);
    }

    // ========== DETAILED REPORT ==========
    out << "\n" << line << "\n" << "DETAILED REPORT" << "\n" << line << "\n";

    const char* kHeaderMexceComp = "ULP(Mx-Cp)";
    const char* kHeaderMexceRef = "ULP(Mx-Ref)";
    const char* kHeaderCompRef = "ULP(Cp-Ref)";

    size_t max_ulp_len_mexce_comp = std::strlen(kHeaderMexceComp);
    size_t max_ulp_len_mexce_ref = std::strlen(kHeaderMexceRef);
    size_t max_ulp_len_comp_ref = std::strlen(kHeaderCompRef);

    auto consider_len = [](uint64_t value, size_t& max_len) {
        if (value == UINT64_MAX) {
            return;
        }
        const size_t len = std::to_string(value).size();
        if (len > max_len) {
            max_len = len;
        }
    };

    for (const auto& r : result.records) {
        consider_len(r.ulp_mexce_vs_compiler, max_ulp_len_mexce_comp);
        consider_len(r.ulp_mexce_vs_reference, max_ulp_len_mexce_ref);
        consider_len(r.ulp_compiler_vs_reference, max_ulp_len_comp_ref);
    }

    auto format_ulp_value = [](uint64_t value) -> std::string {
        return (value == UINT64_MAX) ? "-" : std::to_string(value);
    };

    auto format_double_2d = [](double val) {
        std::stringstream ss;
        ss << std::fixed << std::setprecision(2) << val;
        return ss.str();
    };

    auto print_row_header = [&] {
        out << std::left << std::setw(10) << "Status"
            << "  " << std::setw((int)max_ulp_len_mexce_comp) << kHeaderMexceComp
            << "  " << std::setw((int)max_ulp_len_mexce_ref) << kHeaderMexceRef
            << "  " << std::setw((int)max_ulp_len_comp_ref) << kHeaderCompRef
            << "  " << std::setw(16) << "Compile"
            << "  " << std::setw(16) << "Avg/Call (Mx)"
            << "  " << std::setw(16) << "Avg/Call (Cp)"
            << "  " << std::setw(10) << "Speedup"
            << "  " << "Expression" << " : " << "Optimized Expression" << "\n";
        out << std::string(10, '-')
            << "  " << std::string((int)max_ulp_len_mexce_comp, '-')
            << "  " << std::string((int)max_ulp_len_mexce_ref, '-')
            << "  " << std::string((int)max_ulp_len_comp_ref, '-')
            << "  " << std::string(16, '-')
            << "  " << std::string(16, '-')
            << "  " << std::string(16, '-')
            << "  " << std::string(10, '-')
            << "  " << std::string(40, '-') << " : " << std::string(40, '-') << "\n";
    };

    // Create bins for sorting
    std::vector<const benchmark_record*> compile_failures;
    std::vector<const benchmark_record*> eval_failures;
    std::vector<const benchmark_record*> passed;

    for (const auto& rec : result.records) {
        if (!rec.compiled) {
            compile_failures.push_back(&rec);
        }
        else if (!rec.eval_ok) {
            eval_failures.push_back(&rec);
        }
        else {
            passed.push_back(&rec);
        }
    }

    // Sort bins
    auto sort_by_expression = [](const benchmark_record* a, const benchmark_record* b) {
        return a->expr < b->expr;
    };
    std::sort(compile_failures.begin(), compile_failures.end(), sort_by_expression);
    std::sort(eval_failures.begin(), eval_failures.end(), sort_by_expression);

    std::sort(passed.begin(), passed.end(), [](const benchmark_record* a, const benchmark_record* b) {
        if (!a->native_eval_ok && b->native_eval_ok) return false;
        if (a->native_eval_ok && !b->native_eval_ok) return true;
        if (!a->native_eval_ok && !b->native_eval_ok) return a->expr < b->expr;

        double speedup_a = (double)a->native_avg_ns / (std::max)(1.0, (double)a->avg_ns);
        double speedup_b = (double)b->native_avg_ns / (std::max)(1.0, (double)b->avg_ns);

        if (std::abs(speedup_a - speedup_b) > 1e-6) {
            return speedup_a < speedup_b;
        }
        return a->expr < b->expr;
    });

    // Print sorted bins
    if (!compile_failures.empty()) {
        out << "Compile Failures (sorted alphabetically):" << "\n";
        print_row_header();
        for (const auto* r_ptr : compile_failures) {
            const benchmark_record& r = *r_ptr;
            out << std::left << std::setw(10) << "compile"
                << "  " << std::setw((int)max_ulp_len_mexce_comp) << format_ulp_value(r.ulp_mexce_vs_compiler)
                << "  " << std::setw((int)max_ulp_len_mexce_ref) << format_ulp_value(r.ulp_mexce_vs_reference)
                << "  " << std::setw((int)max_ulp_len_comp_ref) << format_ulp_value(r.ulp_compiler_vs_reference)
                << "  " << std::setw(16) << format_ns(r.compile_ns)
                << "  " << std::setw(16) << "-"
                << "  " << std::setw(16) << "-"
                << "  " << std::setw(10) << "-"
                << "  " << r.expr << "\n";
            if (!r.error.empty()) {
                out << "    note: " << r.error << "\n";
            }
        }
        out << "\n";
    }

    if (!eval_failures.empty()) {
        out << "Evaluation Failures (sorted alphabetically):" << "\n";
        print_row_header();
        for (const auto* r_ptr : eval_failures) {
            const benchmark_record& r = *r_ptr;
            out << std::left << std::setw(10) << "eval"
                << "  " << std::setw((int)max_ulp_len_mexce_comp) << format_ulp_value(r.ulp_mexce_vs_compiler)
                << "  " << std::setw((int)max_ulp_len_mexce_ref) << format_ulp_value(r.ulp_mexce_vs_reference)
                << "  " << std::setw((int)max_ulp_len_comp_ref) << format_ulp_value(r.ulp_compiler_vs_reference)
                << "  " << std::setw(16) << format_ns(r.compile_ns)
                << "  " << std::setw(16) << "-"
                << "  " << std::setw(16) << "-"
                << "  " << std::setw(10) << "-"
                << "  " << r.expr << " : " << r.optimized_expr << " : " << r.bytes_expr << "\n";
            if (!r.error.empty()) {
                out << "    note: " << r.error << "\n";
            }
        }
        out << "\n";
    }

    if (!passed.empty()) {
        out << "Passed (sorted by Speedup [Native/Mexce] ASC - Problematic regressions first):" << "\n";
        print_row_header();
        auto ulp_exceeds = [](uint64_t value) { return value != UINT64_MAX && value > 8192; };
        for (const auto* r_ptr : passed) {
            const benchmark_record& r = *r_ptr;

            const bool highlight = ulp_exceeds(r.ulp_mexce_vs_reference) ||
                ulp_exceeds(r.ulp_mexce_vs_compiler) ||
                ulp_exceeds(r.ulp_compiler_vs_reference);

            if (highlight) {
                out << "\n";
            }

            std::string speedup_str = "-";
            if (r.native_eval_ok) {
                double speedup = (double)r.native_avg_ns / (std::max)(1.0, (double)r.avg_ns);
                speedup_str = format_double_2d(speedup) + "x";
            }

            out << std::left << std::setw(10) << "ok"
                << "  " << std::setw((int)max_ulp_len_mexce_comp) << format_ulp_value(r.ulp_mexce_vs_compiler)
                << "  " << std::setw((int)max_ulp_len_mexce_ref) << format_ulp_value(r.ulp_mexce_vs_reference)
                << "  " << std::setw((int)max_ulp_len_comp_ref) << format_ulp_value(r.ulp_compiler_vs_reference)
                << "  " << std::setw(16) << format_ns(r.compile_ns)
                << "  " << std::setw(16) << format_ns(r.avg_ns)
                << "  " << std::setw(16) << (r.native_eval_ok ? format_ns(r.native_avg_ns) : "-")
                << "  " << std::setw(10) << speedup_str
                << "  " << r.expr << " : " << r.optimized_expr << " : " << r.bytes_expr << "\n";

            if (highlight) {
                std::ios_base::fmtflags original_flags = out.flags();
                auto original_precision = out.precision();

                out << std::fixed << std::setprecision(17);
                out << "    Mexce:    " << r.mexce_result << "\n";
                if (r.native_eval_ok) {
                    out << "    Compiler: " << r.native_result << "\n";
                }
                out << "    Reference: " << static_cast<double>(r.expected) << "\n";

                out.flags(original_flags);
                out.precision(original_precision);
            }
        }
    }

    return (result.compile_fail_count == 0 && result.eval_fail_count == 0) ? 0 : 1;
}

static int run_comprehensive_benchmark(const benchmark_config& config)
{
    // Set up output stream (file or stdout)
    std::ofstream file_output;
    std::ostream* output_stream = &std::cout;
    if (!config.output_path.empty()) {
        file_output.open(config.output_path.c_str());
        if (!file_output) {
            std::cerr << "Failed to open output file: " << config.output_path << std::endl;
            return 1;
        }
        output_stream = &file_output;
        std::cout << "Results will be written to " << resolve_full_path(config.output_path) << std::endl;
    }
    std::ostream& out = *output_stream;

    const std::string line(78, '=');

    out << "\n" << line << "\n";
    out << "COMPREHENSIVE CONFIGURATION COMPARISON\n";
    out << line << "\n\n";

#ifdef BENCHMARK_COMPILER
    out << "Compiler: " << BENCHMARK_COMPILER << "\n";
#endif
#ifdef BENCHMARK_COMPILER_FLAGS
    out << "Compiler flags: " << BENCHMARK_COMPILER_FLAGS << "\n";
#endif

    print_backend_notes(out);

    out << "Iterations per expression: " << config.iterations << "\n";
    out << "Total expressions: " << mexce::benchmark_data::kExpressionCount << "\n\n";

    // Define configurations to test (same configs used in both modes)
    std::vector<mexce_config> configs = {
        {"SSE2",            false, false},
        {"SSE2+fast-math",  false, true},
        {"x87",             true,  false},
        {"x87+fast-math",   true,  true}
    };

    std::vector<benchmark_result> results;
    results.reserve(configs.size());

    // Run all mexce configurations using the same core benchmark function
    for (const auto& cfg : configs) {
        results.push_back(run_benchmark(cfg, config.iterations, std::cout));
    }

    // ========== TIMING COMPARISON ==========
    out << "\n" << line << "\n";
    out << "TIMING COMPARISON\n";
    out << line << "\n\n";

    // Find the intersection of expressions that work for ALL backends
    // This ensures a fair 1:1 comparison across Native and all mexce configs
    const size_t total_expr = results.empty() ? 0 : results[0].records.size();
    std::vector<bool> in_intersection(total_expr, false);
    size_t intersection_count = 0;

    for (size_t idx = 0; idx < total_expr; ++idx) {
        // Must have native available
        if (!results[0].records[idx].native_available) continue;

        // Must have compiled successfully in ALL configs
        bool all_compiled = true;
        for (const auto& r : results) {
            if (idx >= r.records.size() || !r.records[idx].compiled) {
                all_compiled = false;
                break;
            }
        }
        if (!all_compiled) continue;

        in_intersection[idx] = true;
        intersection_count++;
    }

    // Compute timing for the intersection set only
    struct intersection_timing {
        long long total_eval_ns = 0;
        long long total_native_ns = 0;
        long long total_compile_ns = 0;
        size_t count = 0;
    };

    // Native timing for intersection
    long long native_total_ns = 0;
    for (size_t idx = 0; idx < total_expr && idx < results[0].records.size(); ++idx) {
        if (in_intersection[idx]) {
            native_total_ns += results[0].records[idx].native_dur_ns;
        }
    }

    // Per-config timing for intersection
    std::vector<intersection_timing> intersection_timings(results.size());
    for (size_t cfg_idx = 0; cfg_idx < results.size(); ++cfg_idx) {
        const auto& r = results[cfg_idx];
        auto& timing = intersection_timings[cfg_idx];
        for (size_t idx = 0; idx < total_expr && idx < r.records.size(); ++idx) {
            if (in_intersection[idx]) {
                timing.total_eval_ns += r.records[idx].dur_ns;
                timing.total_native_ns += r.records[idx].native_dur_ns;
                timing.total_compile_ns += (long long)r.records[idx].compile_ns;
                timing.count++;
            }
        }
    }

    // Find column widths
    size_t name_width = std::string("Configuration").size();
    for (const auto& r : results) {
        name_width = std::max(name_width, r.config_name.size());
    }
    name_width = std::max(name_width, std::string("Native").size());

    const size_t col_width = 15;

    out << std::left << std::setw((int)name_width) << "Configuration" << "  "
        << std::setw((int)col_width) << "Avg Compile" << "  "
        << std::setw((int)col_width) << "Avg Eval" << "  "
        << std::setw((int)col_width) << "Total Eval" << "\n";

    out << std::string(name_width, '-') << "  "
        << std::string(col_width, '-') << "  "
        << std::string(col_width, '-') << "  "
        << std::string(col_width, '-') << "\n";

    // Print Native row first
    if (intersection_count > 0) {
        double native_avg = (double)native_total_ns / ((double)intersection_count * (double)config.iterations);
        out << std::left << std::setw((int)name_width) << "Native" << "  "
            << std::setw((int)col_width) << "-" << "  "
            << std::setw((int)col_width) << format_ns(native_avg) << "  "
            << std::setw((int)col_width) << format_ns((uint64_t)native_total_ns) << "\n";
    }

    // Print each mexce config
    for (size_t cfg_idx = 0; cfg_idx < results.size(); ++cfg_idx) {
        const auto& r = results[cfg_idx];
        const auto& timing = intersection_timings[cfg_idx];

        double avg_compile = (timing.count > 0) ? (double)timing.total_compile_ns / (double)timing.count : 0.0;
        double avg_eval = (timing.count > 0) ? (double)timing.total_eval_ns / ((double)timing.count * (double)config.iterations) : 0.0;

        out << std::left << std::setw((int)name_width) << r.config_name << "  "
            << std::setw((int)col_width) << format_ns(avg_compile) << "  "
            << std::setw((int)col_width) << format_ns(avg_eval) << "  "
            << std::setw((int)col_width) << format_ns((uint64_t)timing.total_eval_ns) << "\n";
    }

    out << "\nExpressions in intersection: " << intersection_count << " (of " << total_expr << " total)\n";

    // Find fastest mexce config (using intersection timing)
    if (!intersection_timings.empty() && intersection_count > 0) {
        size_t fastest_idx = 0;
        double fastest_avg = std::numeric_limits<double>::max();
        for (size_t i = 0; i < intersection_timings.size(); ++i) {
            double avg = (double)intersection_timings[i].total_eval_ns /
                ((double)intersection_count * (double)config.iterations);
            if (avg < fastest_avg) {
                fastest_avg = avg;
                fastest_idx = i;
            }
        }
        out << "\nFastest: " << results[fastest_idx].config_name
            << " (" << format_ns(fastest_avg) << " per call)\n";
    }

    // ========== PRECISION COMPARISON ==========
    out << "\n" << line << "\n";
    out << "PRECISION COMPARISON (ULP vs Reference)\n";
    out << line << "\n\n";

    // Build row labels
    std::vector<std::string> row_labels;
    row_labels.reserve(k_ulp_num_bins + 2);
    row_labels.emplace_back("0 (exact)");
    for (size_t bin_idx = 0; bin_idx < k_ulp_num_bins; ++bin_idx) {
        char buf[32];
        uint64_t lo = (bin_idx == 0 ? 1 : (k_ulp_bin_thresholds[bin_idx - 1] + 1));
        uint64_t hi = k_ulp_bin_thresholds[bin_idx];
        std::snprintf(buf, sizeof(buf), "%llu-%llu", (unsigned long long)lo, (unsigned long long)hi);
        row_labels.emplace_back(buf);
    }
    row_labels.emplace_back(">65536");

    // Find label width
    size_t label_width = std::string("ULP Range").size();
    for (const auto& label : row_labels) {
        label_width = std::max(label_width, label.size());
    }

    // Build column values (include Native as first column from comp_ref stats)
    size_t num_columns = results.size() + 1;  // +1 for Native
    std::vector<std::vector<std::string>> column_values(num_columns);
    std::vector<size_t> column_widths(num_columns);
    std::vector<std::string> column_names(num_columns);

    // Native column (using compiler vs reference ULP from first result)
    column_names[0] = "Native";
    if (!results.empty()) {
        column_values[0].emplace_back(std::to_string(results[0].exact_zero_count_comp_ref));
        for (size_t bin_idx = 0; bin_idx < k_ulp_num_bins; ++bin_idx) {
            column_values[0].emplace_back(std::to_string(results[0].ulp_bins_comp_ref[bin_idx]));
        }
        column_values[0].emplace_back(std::to_string(results[0].ulp_bins_comp_ref[k_ulp_num_bins]));
    }
    column_widths[0] = column_names[0].size();
    for (const std::string& value : column_values[0]) {
        column_widths[0] = std::max(column_widths[0], value.size());
    }

    // Mexce config columns (using mexce vs reference ULP)
    for (size_t col = 0; col < results.size(); ++col) {
        const benchmark_result& r = results[col];
        size_t col_idx = col + 1;
        column_names[col_idx] = r.config_name;
        std::vector<std::string>& values = column_values[col_idx];
        values.reserve(row_labels.size());

        values.emplace_back(std::to_string(r.exact_zero_count_mexce_ref));
        for (size_t bin_idx = 0; bin_idx < k_ulp_num_bins; ++bin_idx) {
            values.emplace_back(std::to_string(r.ulp_bins_mexce_ref[bin_idx]));
        }
        values.emplace_back(std::to_string(r.ulp_bins_mexce_ref[k_ulp_num_bins]));

        // Column width
        column_widths[col_idx] = r.config_name.size();
        for (const std::string& value : values) {
            column_widths[col_idx] = std::max(column_widths[col_idx], value.size());
        }
    }

    // Print header
    out << std::left << std::setw((int)label_width) << "ULP Range" << "  ";
    for (size_t col = 0; col < num_columns; ++col) {
        out << std::setw((int)column_widths[col]) << column_names[col];
        if (col + 1 != num_columns) out << "  ";
    }
    out << "\n";

    // Print separator
    out << std::string(label_width, '-') << "  ";
    for (size_t col = 0; col < num_columns; ++col) {
        out << std::string(column_widths[col], '-');
        if (col + 1 != num_columns) out << "  ";
    }
    out << "\n";

    // Print rows
    for (size_t row = 0; row < row_labels.size(); ++row) {
        out << std::left << std::setw((int)label_width) << row_labels[row] << "  ";
        for (size_t col = 0; col < num_columns; ++col) {
            out << std::setw((int)column_widths[col]) << column_values[col][row];
            if (col + 1 != num_columns) out << "  ";
        }
        out << "\n";
    }

    // Print totals
    out << "\n";
    out << std::left << std::setw((int)label_width) << "Total ULP sum" << "  ";
    // Native ULP sum
    if (!results.empty()) {
        std::string sum_str = to_decimal_u128(results[0].ulp_sum_comp_ref.hi, results[0].ulp_sum_comp_ref.lo);
        out << std::setw((int)column_widths[0]) << sum_str << "  ";
    }
    for (size_t col = 0; col < results.size(); ++col) {
        std::string sum_str = to_decimal_u128(results[col].ulp_sum_mexce_ref.hi, results[col].ulp_sum_mexce_ref.lo);
        out << std::setw((int)column_widths[col + 1]) << sum_str;
        if (col + 1 != results.size()) out << "  ";
    }
    out << "\n";

    out << std::left << std::setw((int)label_width) << "Evaluated" << "  ";
    // Native evaluated count
    if (!results.empty()) {
        out << std::setw((int)column_widths[0]) << results[0].comparisons_comp_ref << "  ";
    }
    for (size_t col = 0; col < results.size(); ++col) {
        out << std::setw((int)column_widths[col + 1]) << results[col].eval_count;
        if (col + 1 != results.size()) out << "  ";
    }
    out << "\n";

    // Find most precise (lowest total ULP among mexce configs)
    if (!results.empty()) {
        auto most_precise_it = std::min_element(results.begin(), results.end(),
            [](const benchmark_result& a, const benchmark_result& b) {
                if (a.ulp_sum_mexce_ref.hi != b.ulp_sum_mexce_ref.hi)
                    return a.ulp_sum_mexce_ref.hi < b.ulp_sum_mexce_ref.hi;
                return a.ulp_sum_mexce_ref.lo < b.ulp_sum_mexce_ref.lo;
            });
        out << "\nMost precise: " << most_precise_it->config_name
            << " (total ULP: " << to_decimal_u128(most_precise_it->ulp_sum_mexce_ref.hi,
                                                   most_precise_it->ulp_sum_mexce_ref.lo) << ")\n";
    }

    out << "\n" << line << "\n";
    out << "Notes:\n";
    out << "  Native:    Compiler-generated code (baseline for comparison)\n";
    out << "  SSE2:      Uses SSE2 for basic arithmetic, libm for transcendentals\n";
    out << "  x87:       Uses x87 FPU for all operations (80-bit internal precision)\n";
    out << "  fast-math: Enables algebraic simplifications (x-x=0, x/x=1, etc.)\n";
    out << line << "\n";

    // ========== DETAILED PER-EXPRESSION REPORTS FOR ALL CONFIGURATIONS ==========
    // Use the records already collected during the benchmark run
    for (const auto& result : results) {
        print_detailed_report(result, out);
    }

    out << "\n" << line << "\n";
    out << "END OF COMPREHENSIVE BENCHMARK REPORT\n";
    out << line << "\n";

    return 0;
}

int main(int argc, char* argv[])
{
    benchmark_config config;
    if (!parse_args(argc, argv, config)) {
        return 1;
    }

    if (config.comprehensive) {
        return run_comprehensive_benchmark(config);
    }

    // Single-mode benchmark: run one configuration using the shared run_benchmark function

    // Build configuration name
    std::string config_name;
    bool prefer_x87 = config.force_x87;
    if (!config.force_x87 && !config.force_sse2) {
        prefer_x87 = false;  // Default to SSE2
    }

    if (prefer_x87) {
        config_name = config.fast_math ? "x87+fast-math" : "x87";
    } else {
        config_name = config.fast_math ? "SSE2+fast-math" : "SSE2";
    }

    const std::string resolved_output = config.output_path.empty() ? std::string() : resolve_full_path(config.output_path);

    if (argc == 1) {
        std::cout << "No commandline arguments provided." << std::endl;
    }
    std::cout << "Running " << config.iterations << " iterations." << std::endl;
    if (!config.output_path.empty()) {
        std::cout << "Results will be written to " << resolved_output << std::endl;
    }
    else {
        std::cout << "Results will be written to standard output" << std::endl;
    }

    std::cout << "Configuration:" << std::endl;
    std::cout << "  Backend: " << (prefer_x87 ? "x87" : "SSE2") << std::endl;
    std::cout << "  Fast-math: " << (config.fast_math ? "enabled" : "disabled") << std::endl;

    // Set up output stream
    std::ofstream file_output;
    std::ostream* output_stream = &std::cout;
    if (!config.output_path.empty()) {
        file_output.open(config.output_path.c_str());
        if (!file_output) {
            std::cerr << "Failed to open output file: " << config.output_path << std::endl;
            return 1;
        }
        output_stream = &file_output;
    }
    std::ostream& out = *output_stream;

#ifdef BENCHMARK_COMPILER
    out << "Compiler: " << BENCHMARK_COMPILER << std::endl;
#endif
#ifdef BENCHMARK_COMPILER_FLAGS
    out << "Compiler flags: " << BENCHMARK_COMPILER_FLAGS << std::endl;
#endif

    print_backend_notes(out);

    if (mexce::benchmark_data::kExpressionCount == 0) {
        out << "No expressions available for benchmarking." << std::endl;
        return 0;
    }

    // Create config and run the shared benchmark function
    mexce_config cfg{config_name, prefer_x87, config.fast_math};
    benchmark_result result = run_benchmark(cfg, config.iterations, std::cout);

    // Print the single-mode report
    return print_single_mode_report(result, config.iterations, out);
}

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
static std::string format_ns(uint64_t ns)
{
    struct unit_t { const char* name; uint64_t factor; };
    static const unit_t s_units[] = {
        {"sec", 1000000000ULL},
        {"ms",     1000000ULL},
        {"us",        1000ULL},
        {"ns",           1ULL}
    };

    if (ns == 0) {
        return "0.0 ns";
    }

    const unit_t* chosen = &s_units[3];
    for (size_t i = 0; i < sizeof(s_units) / sizeof(s_units[0]); ++i) {
        uint64_t integer = ns / s_units[i].factor;
        if (integer >= 1 && integer <= 999) {
            chosen = &s_units[i];
            break;
        }
    }
    if (ns / s_units[0].factor >= 1000) {
        chosen = &s_units[0];
    }

    uint64_t integer = ns / chosen->factor;
    uint64_t frac = ns % chosen->factor;
    unsigned width = 0;
    for (uint64_t f = chosen->factor; f > 1; f /= 10) {
        ++width;
    }

    std::string out;
    out.reserve(32);
    out += std::to_string(integer);

    if (width > 0) {
        std::string frac_str(width, '0');
        for (int i = (int)width - 1; i >= 0 && frac > 0; --i) {
            frac_str[(size_t)i] = char('0' + (frac % 10));
            frac /= 10;
        }
        while (!frac_str.empty() && frac_str.back() == '0') {
            frac_str.pop_back();
        }
        out += '.';
        out += frac_str.empty() ? "0" : frac_str;
    }
    else {
        out += ".0";
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

// Configuration definition for comprehensive benchmark
struct mexce_config {
    std::string name;
    bool prefer_x87;
    bool fast_math;
};

// Results for a single configuration
struct config_result {
    std::string config_name;
    size_t expressions_tested = 0;
    size_t expressions_compiled = 0;
    size_t expressions_evaluated = 0;
    long long total_compile_ns = 0;
    long long total_eval_ns = 0;
    uint64_t avg_compile_ns = 0;
    uint64_t avg_eval_ns = 0;

    // Precision stats
    size_t exact_zero_count = 0;
    std::vector<size_t> ulp_bins;
    ulp_sum_t ulp_sum;

    config_result() : ulp_bins(k_ulp_num_bins + 1, 0) {}
};

// Detailed per-expression record for comprehensive output
struct detail_record_t {
    size_t original_idx = 0;  // Index into benchmark_data arrays
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
    uint64_t native_avg_ns = 0;
    std::string error;
};

static void update_ulp_bins(uint64_t ulp, size_t& exact_zero_count, std::vector<size_t>& bins) {
    if (ulp == UINT64_MAX) {
        return;
    }
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

// Run benchmark for a single configuration with timing and precision
static config_result run_config_benchmark(
    const mexce_config& cfg,
    int iterations,
    std::ostream& progress_out)
{
    config_result result;
    result.config_name = cfg.name;

    mexce::evaluator eval;
    eval.opts().prefer_x87 = cfg.prefer_x87;
    eval.opts().fast_math = cfg.fast_math;

    double a = 1.1, b = 2.2, c = 3.3, x = 4.4, y = 5.5, z = 6.6, w = 7.7;
    eval.bind(a, "a", b, "b", c, "c", x, "x", y, "y", z, "z", w, "w");

    const size_t total = mexce::benchmark_data::kExpressionCount;
    const size_t iterations_u = static_cast<size_t>(iterations);

    progress_out << "  Testing " << cfg.name << "..." << std::flush;

    for (size_t idx = 0; idx < total; ++idx) {
        const std::string expr = mexce::benchmark_data::kExpressions[idx];
        const long double golden = mexce::benchmark_data::kGoldenResults[idx];
        const double golden_d = static_cast<double>(golden);

        a = 1.1; b = 2.2; c = 3.3; x = 4.4; y = 5.5; z = 6.6; w = 7.7;
        result.expressions_tested++;

        // Compile
        mexce::stopwatch compile_timer;
        try {
            eval.set_expression(expr);
            result.total_compile_ns += compile_timer.elapsed_nanoseconds();
            result.expressions_compiled++;
        }
        catch (...) {
            continue;
        }

        // Evaluate once for precision
        double mexce_result;
        try {
            mexce_result = eval.evaluate();
            result.expressions_evaluated++;
        }
        catch (...) {
            continue;
        }

        // Compute ULP distance vs reference
        const bool mexce_zero = std::fabs(mexce_result) <= (double)k_zero_abs_tol;
        const bool golden_zero = std::abs(golden) <= k_zero_abs_tol;
        uint64_t ulp;
        if (mexce_zero && golden_zero) {
            ulp = 0;
        } else {
            ulp = ulp_distance(mexce_result, golden_d);
        }
        result.ulp_sum.add(ulp);
        update_ulp_bins(ulp, result.exact_zero_count, result.ulp_bins);

        // Timing benchmark
        mexce::stopwatch eval_timer;
        for (size_t i = 0; i < iterations_u; ++i) {
            (void)eval.evaluate();
        }
        result.total_eval_ns += eval_timer.elapsed_nanoseconds();
    }

    if (result.expressions_compiled > 0) {
        result.avg_compile_ns = (uint64_t)((double)result.total_compile_ns /
            (double)result.expressions_compiled + 0.5);
    }
    if (result.expressions_evaluated > 0) {
        result.avg_eval_ns = (uint64_t)((double)result.total_eval_ns /
            ((double)result.expressions_evaluated * (double)iterations) + 0.5);
    }

    progress_out << " done (" << result.expressions_compiled << "/" << total << " compiled)\n";
    return result;
}

// Run benchmark for native (compiler-generated) expressions
static config_result run_native_benchmark(
    int iterations,
    std::ostream& progress_out)
{
    config_result result;
    result.config_name = "Native";

    const size_t total = mexce::benchmark_data::kExpressionCount;
    const size_t native_count = mexce::benchmark_data::kNativeExpressionsCount;
    const size_t iterations_u = static_cast<size_t>(iterations);

    mexce::benchmark_data::NativeContext native_ctx{};

    progress_out << "  Testing Native..." << std::flush;

    for (size_t idx = 0; idx < total; ++idx) {
        const long double golden = mexce::benchmark_data::kGoldenResults[idx];
        const double golden_d = static_cast<double>(golden);

        result.expressions_tested++;

        // Check if native expression is available for this index
        if (idx >= native_count) {
            continue;
        }

        // Evaluate once for precision
        native_ctx.a = 1.1;
        native_ctx.b = 2.2;
        native_ctx.c = 3.3;
        native_ctx.x = 4.4;
        native_ctx.y = 5.5;
        native_ctx.z = 6.6;
        native_ctx.w = 7.7;

        double native_result;
        try {
            native_result = mexce::benchmark_data::kNativeExpressions[idx](native_ctx);
            result.expressions_evaluated++;
        }
        catch (...) {
            continue;
        }
        result.expressions_compiled++;  // Native is always "compiled" if available

        // Compute ULP distance vs reference
        const bool native_zero = std::fabs(native_result) <= (double)k_zero_abs_tol;
        const bool golden_zero = std::abs(golden) <= k_zero_abs_tol;
        uint64_t ulp;
        if (native_zero && golden_zero) {
            ulp = 0;
        } else {
            ulp = ulp_distance(native_result, golden_d);
        }
        result.ulp_sum.add(ulp);
        update_ulp_bins(ulp, result.exact_zero_count, result.ulp_bins);

        // Timing benchmark
        mexce::stopwatch eval_timer;
        for (size_t i = 0; i < iterations_u; ++i) {
            (void)mexce::benchmark_data::kNativeExpressions[idx](native_ctx);
        }
        result.total_eval_ns += eval_timer.elapsed_nanoseconds();
    }

    // No compile time for native
    result.avg_compile_ns = 0;
    result.total_compile_ns = 0;

    if (result.expressions_evaluated > 0) {
        result.avg_eval_ns = (uint64_t)((double)result.total_eval_ns /
            ((double)result.expressions_evaluated * (double)iterations) + 0.5);
    }

    progress_out << " done (" << result.expressions_evaluated << "/" << native_count << " available)\n";
    return result;
}

// Generate detailed per-expression report for a single configuration
static void generate_detailed_report(
    const mexce_config& cfg,
    int iterations,
    std::ostream& out,
    std::ostream& progress_out)
{
    const std::string line(78, '=');

    out << "\n" << line << "\n";
    out << "DETAILED PER-EXPRESSION REPORT (" << cfg.name << ")\n";
    out << line << "\n\n";

    double a = 1.1, b = 2.2, c = 3.3, x = 4.4, y = 5.5, z = 6.6, w = 7.7;
    mexce::benchmark_data::NativeContext native_ctx{};

    const size_t total_expressions = mexce::benchmark_data::kExpressionCount;
    const size_t iterations_u = static_cast<size_t>(iterations);

    // Number of timing passes - spread trials apart to avoid sustained OS scheduling effects
    // Use fewer trials when iterations is high (timing is more stable)
    const int k_timing_passes = (iterations >= 10000) ? 5 :
                                (iterations >= 1000)  ? 7 : 10;

    std::vector<detail_record_t> detail_records;
    detail_records.reserve(total_expressions);

    // Store compiled evaluators to avoid recompilation in timing passes
    // Each evaluator is bound to the same variables (by reference)
    std::vector<std::unique_ptr<mexce::evaluator>> compiled_evals;
    compiled_evals.reserve(total_expressions);

    // ========== Pass 1: Compile, evaluate, and collect metadata ==========
    progress_out << "  Compiling " << cfg.name << "..." << std::flush;

    for (size_t idx = 0; idx < total_expressions; ++idx) {
        const std::string expr = mexce::benchmark_data::kExpressions[idx];
        const long double golden = mexce::benchmark_data::kGoldenResults[idx];
        const double golden_d = static_cast<double>(golden);

        a = 1.1; b = 2.2; c = 3.3; x = 4.4; y = 5.5; z = 6.6; w = 7.7;

        detail_record_t rec;
        rec.original_idx = idx;
        rec.expr = expr;
        rec.expected = golden;
        rec.native_available = idx < mexce::benchmark_data::kNativeExpressionsCount;
        rec.mexce_result = std::numeric_limits<double>::quiet_NaN();
        rec.native_result = std::numeric_limits<double>::quiet_NaN();
        rec.avg_ns = std::numeric_limits<uint64_t>::max();
        rec.native_avg_ns = std::numeric_limits<uint64_t>::max();

        // Native evaluation
        if (rec.native_available) {
            native_ctx.a = a; native_ctx.b = b; native_ctx.c = c;
            native_ctx.x = x; native_ctx.y = y; native_ctx.z = z; native_ctx.w = w;
            try {
                rec.native_result = mexce::benchmark_data::kNativeExpressions[idx](native_ctx);
                rec.native_eval_ok = true;
                const bool native_zero = std::fabs(rec.native_result) <= (double)k_zero_abs_tol;
                const bool golden_zero = std::abs(golden) <= k_zero_abs_tol;
                rec.ulp_compiler_vs_reference = (native_zero && golden_zero) ? 0 : ulp_distance(rec.native_result, golden_d);
            } catch (...) {}
        }

        // Create a new evaluator for this expression
        std::unique_ptr<mexce::evaluator> expr_eval(new mexce::evaluator());
        expr_eval->opts().prefer_x87 = cfg.prefer_x87;
        expr_eval->opts().fast_math = cfg.fast_math;
        expr_eval->bind(a, "a", b, "b", c, "c", x, "x", y, "y", z, "z", w, "w");

        // Mexce compilation
        mexce::stopwatch compile_timer;
        try {
            expr_eval->set_expression(expr);
            rec.compile_ns = (uint64_t)compile_timer.elapsed_nanoseconds();
            rec.optimized_expr = expr_eval->get_optimized_expression();
            rec.bytes_expr = expr_eval->get_byte_representation();
            rec.compiled = true;
        } catch (const std::exception& e) {
            rec.compile_ns = (uint64_t)compile_timer.elapsed_nanoseconds();
            rec.error = std::string("compile: ") + e.what();
            detail_records.push_back(rec);
            compiled_evals.push_back(nullptr);
            continue;
        }

        // Mexce evaluation
        try {
            rec.mexce_result = expr_eval->evaluate();
            rec.eval_ok = true;
        } catch (const std::exception& e) {
            rec.error = std::string("evaluate: ") + e.what();
            detail_records.push_back(rec);
            compiled_evals.push_back(nullptr);
            continue;
        }

        // ULP calculations
        const bool mexce_zero = std::fabs(rec.mexce_result) <= (double)k_zero_abs_tol;
        const bool golden_zero = std::abs(golden) <= k_zero_abs_tol;
        rec.ulp_mexce_vs_reference = (mexce_zero && golden_zero) ? 0 : ulp_distance(rec.mexce_result, golden_d);
        if (rec.native_eval_ok) {
            const bool native_zero = std::fabs(rec.native_result) <= (double)k_zero_abs_tol;
            rec.ulp_mexce_vs_compiler = (mexce_zero && native_zero) ? 0 : ulp_distance(rec.mexce_result, rec.native_result);
        }

        detail_records.push_back(rec);
        compiled_evals.push_back(std::move(expr_eval));
    }
    progress_out << " done\n";

    // ========== Timing passes: spread trials apart to reduce OS scheduling noise ==========
    // Running multiple complete passes through all expressions spreads the timing trials
    // far apart in time, so a scheduling hiccup in one pass won't affect all trials.
    // Uses pre-compiled evaluators to avoid recompilation overhead.
    progress_out << "  Timing " << cfg.name << " (" << k_timing_passes << " passes)..." << std::flush;

    for (int pass = 0; pass < k_timing_passes; ++pass) {
        // Reset variables for consistent state
        a = 1.1; b = 2.2; c = 3.3; x = 4.4; y = 5.5; z = 6.6; w = 7.7;
        native_ctx.a = a; native_ctx.b = b; native_ctx.c = c;
        native_ctx.x = x; native_ctx.y = y; native_ctx.z = z; native_ctx.w = w;

        for (size_t idx = 0; idx < detail_records.size(); ++idx) {
            detail_record_t& rec = detail_records[idx];
            if (!rec.eval_ok || !compiled_evals[idx]) continue;

            // Time mexce using pre-compiled evaluator
            mexce::stopwatch eval_timer;
            for (size_t i = 0; i < iterations_u; ++i) {
                (void)compiled_evals[idx]->evaluate();
            }
            uint64_t trial_ns = (uint64_t)((double)eval_timer.elapsed_nanoseconds() / (double)iterations_u + 0.5);
            if (trial_ns < rec.avg_ns) rec.avg_ns = trial_ns;

            // Time native
            if (rec.native_eval_ok && rec.original_idx < mexce::benchmark_data::kNativeExpressionsCount) {
                mexce::stopwatch native_timer;
                for (size_t i = 0; i < iterations_u; ++i) {
                    (void)mexce::benchmark_data::kNativeExpressions[rec.original_idx](native_ctx);
                }
                uint64_t native_trial_ns = (uint64_t)((double)native_timer.elapsed_nanoseconds() / (double)iterations_u + 0.5);
                if (native_trial_ns < rec.native_avg_ns) rec.native_avg_ns = native_trial_ns;
            }
        }
    }
    progress_out << " done\n";

    // Sort by speedup (problematic regressions first)
    std::vector<const detail_record_t*> sorted_records;
    sorted_records.reserve(detail_records.size());
    for (const auto& rec : detail_records) {
        sorted_records.push_back(&rec);
    }
    std::sort(sorted_records.begin(), sorted_records.end(), [](const detail_record_t* a, const detail_record_t* b) {
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

    out << "Iterations per expression: " << config.iterations << "\n";
    out << "Total expressions: " << mexce::benchmark_data::kExpressionCount << "\n\n";

    // Define configurations to test
    std::vector<mexce_config> configs = {
        {"SSE2",            false, false},
        {"SSE2+fast-math",  false, true},
        {"x87",             true,  false},
        {"x87+fast-math",   true,  true}
    };

    std::vector<config_result> results;
    results.reserve(configs.size() + 1);  // +1 for native

    // Run native benchmark first (as baseline)
    results.push_back(run_native_benchmark(config.iterations, std::cout));

    // Run mexce configurations
    for (const auto& cfg : configs) {
        results.push_back(run_config_benchmark(cfg, config.iterations, std::cout));
    }

    // ========== TIMING COMPARISON ==========
    out << "\n" << line << "\n";
    out << "TIMING COMPARISON\n";
    out << line << "\n\n";

    // Find column widths
    size_t name_width = std::string("Configuration").size();
    for (const auto& r : results) {
        name_width = std::max(name_width, r.config_name.size());
    }

    const size_t col_width = 15;

    out << std::left << std::setw((int)name_width) << "Configuration" << "  "
        << std::setw((int)col_width) << "Avg Compile" << "  "
        << std::setw((int)col_width) << "Avg Eval" << "  "
        << std::setw((int)col_width) << "Total Eval" << "\n";

    out << std::string(name_width, '-') << "  "
        << std::string(col_width, '-') << "  "
        << std::string(col_width, '-') << "  "
        << std::string(col_width, '-') << "\n";

    for (const auto& r : results) {
        out << std::left << std::setw((int)name_width) << r.config_name << "  "
            << std::setw((int)col_width) << format_ns(r.avg_compile_ns) << "  "
            << std::setw((int)col_width) << format_ns(r.avg_eval_ns) << "  "
            << std::setw((int)col_width) << format_ns((uint64_t)r.total_eval_ns) << "\n";
    }

    // Find fastest
    if (!results.empty()) {
        auto fastest_it = std::min_element(results.begin(), results.end(),
            [](const config_result& a, const config_result& b) {
                return a.avg_eval_ns < b.avg_eval_ns;
            });
        out << "\nFastest: " << fastest_it->config_name
            << " (" << format_ns(fastest_it->avg_eval_ns) << " per call)\n";
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

    // Build column values
    std::vector<std::vector<std::string>> column_values(results.size());
    std::vector<size_t> column_widths(results.size());

    for (size_t col = 0; col < results.size(); ++col) {
        const config_result& r = results[col];
        std::vector<std::string>& values = column_values[col];
        values.reserve(row_labels.size());

        values.emplace_back(std::to_string(r.exact_zero_count));
        for (size_t bin_idx = 0; bin_idx < k_ulp_num_bins; ++bin_idx) {
            values.emplace_back(std::to_string(r.ulp_bins[bin_idx]));
        }
        values.emplace_back(std::to_string(r.ulp_bins[k_ulp_num_bins]));

        // Column width
        column_widths[col] = r.config_name.size();
        for (const std::string& value : values) {
            column_widths[col] = std::max(column_widths[col], value.size());
        }
    }

    // Print header
    out << std::left << std::setw((int)label_width) << "ULP Range" << "  ";
    for (size_t col = 0; col < results.size(); ++col) {
        out << std::setw((int)column_widths[col]) << results[col].config_name;
        if (col + 1 != results.size()) out << "  ";
    }
    out << "\n";

    // Print separator
    out << std::string(label_width, '-') << "  ";
    for (size_t col = 0; col < results.size(); ++col) {
        out << std::string(column_widths[col], '-');
        if (col + 1 != results.size()) out << "  ";
    }
    out << "\n";

    // Print rows
    for (size_t row = 0; row < row_labels.size(); ++row) {
        out << std::left << std::setw((int)label_width) << row_labels[row] << "  ";
        for (size_t col = 0; col < results.size(); ++col) {
            out << std::setw((int)column_widths[col]) << column_values[col][row];
            if (col + 1 != results.size()) out << "  ";
        }
        out << "\n";
    }

    // Print totals
    out << "\n";
    out << std::left << std::setw((int)label_width) << "Total ULP sum" << "  ";
    for (size_t col = 0; col < results.size(); ++col) {
        std::string sum_str = to_decimal_u128(results[col].ulp_sum.hi, results[col].ulp_sum.lo);
        out << std::setw((int)column_widths[col]) << sum_str;
        if (col + 1 != results.size()) out << "  ";
    }
    out << "\n";

    out << std::left << std::setw((int)label_width) << "Evaluated" << "  ";
    for (size_t col = 0; col < results.size(); ++col) {
        out << std::setw((int)column_widths[col]) << results[col].expressions_evaluated;
        if (col + 1 != results.size()) out << "  ";
    }
    out << "\n";

    // Find most precise (lowest total ULP)
    if (!results.empty()) {
        auto most_precise_it = std::min_element(results.begin(), results.end(),
            [](const config_result& a, const config_result& b) {
                if (a.ulp_sum.hi != b.ulp_sum.hi) return a.ulp_sum.hi < b.ulp_sum.hi;
                return a.ulp_sum.lo < b.ulp_sum.lo;
            });
        out << "\nMost precise: " << most_precise_it->config_name
            << " (total ULP: " << to_decimal_u128(most_precise_it->ulp_sum.hi, most_precise_it->ulp_sum.lo) << ")\n";
    }

    out << "\n" << line << "\n";
    out << "Notes:\n";
    out << "  Native:    Compiler-generated code (baseline for comparison)\n";
    out << "  SSE2:      Uses SSE2 for basic arithmetic, libm for transcendentals\n";
    out << "  x87:       Uses x87 FPU for all operations (80-bit internal precision)\n";
    out << "  fast-math: Enables algebraic simplifications (x-x=0, x/x=1, etc.)\n";
    out << line << "\n";

    // ========== DETAILED PER-EXPRESSION REPORTS FOR ALL CONFIGURATIONS ==========
    // Generate detailed reports for all 4 mexce configurations
    for (const auto& cfg : configs) {
        generate_detailed_report(cfg, config.iterations, out, std::cout);
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

    int iterations = config.iterations;
    std::string output_path = config.output_path;

    const std::string resolved_output = output_path.empty() ? std::string() : resolve_full_path(output_path);

    if (argc == 1) {
        std::cout << "No commandline arguments provided." << std::endl;
    }
    std::cout << "Running " << iterations << " iterations." << std::endl;
    if (!output_path.empty()) {
        std::cout << "Results will be written to " << resolved_output << std::endl;
    }
    else {
        std::cout << "Results will be written to standard output" << std::endl;
    }

    mexce::evaluator eval;
    // Apply configuration options
    if (config.force_x87) {
        eval.opts().prefer_x87 = true;
    }
    else if (config.force_sse2) {
        eval.opts().prefer_x87 = false;
    }
    if (config.fast_math) {
        eval.opts().fast_math = true;
    }

    // Always show configuration
    std::cout << "Configuration:" << std::endl;
    std::cout << "  Backend: " << (eval.opts().prefer_x87 ? "x87" : "SSE2") << std::endl;
    std::cout << "  Fast-math: " << (eval.opts().fast_math ? "enabled" : "disabled") << std::endl;

    double a = 1.1, b = 2.2, c = 3.3, x = 4.4, y = 5.5, z = 6.6, w = 7.7;
    eval.bind(a, "a", b, "b", c, "c", x, "x", y, "y", z, "z", w, "w");
    mexce::benchmark_data::NativeContext native_ctx{};

    std::ofstream file_output;
    std::ostream* output_stream = &std::cout;
    if (!output_path.empty()) {
        file_output.open(output_path.c_str());
        if (!file_output) {
            std::cerr << "Failed to open output file: " << output_path << std::endl;
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

    const volatile std::size_t total_expressions = mexce::benchmark_data::kExpressionCount; // volatile, only for MSVC to shut up
    if (total_expressions == 0) {
        out << "No expressions available for benchmarking." << std::endl;
        return 0;
    }

    struct record_t {
        std::string expr;
        std::string optimized_expr;
        std::string bytes_expr;
        bool compiled;
        bool eval_ok;
        bool native_available;
        bool native_eval_ok;
        long double expected;
        double mexce_result;
        double native_result;
        uint64_t ulp_mexce_vs_compiler;
        uint64_t ulp_mexce_vs_reference;
        uint64_t ulp_compiler_vs_reference;
        uint64_t compile_ns;
        uint64_t avg_ns;
        long long dur_ns;
        uint64_t native_avg_ns;
        long long native_dur_ns;
        std::string error;
    };

    std::vector<record_t> records;
    records.reserve(total_expressions);

    constexpr uint64_t k_bin_thresholds[] = {
        16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536
    };
    constexpr size_t k_num_bins = sizeof(k_bin_thresholds) / sizeof(k_bin_thresholds[0]);

    size_t compiled_count = 0, compile_fail_count = 0, eval_fail_count = 0;
    size_t exact_zero_count_mexce_ref = 0;
    size_t exact_zero_count_mexce_comp = 0;
    size_t exact_zero_count_comp_ref = 0;
    std::vector<size_t> bin_counts_mexce_ref(k_num_bins + 1, 0);
    std::vector<size_t> bin_counts_mexce_comp(k_num_bins + 1, 0);
    std::vector<size_t> bin_counts_comp_ref(k_num_bins + 1, 0);
    size_t comparisons_mexce_ref = 0;
    size_t comparisons_mexce_comp = 0;
    size_t comparisons_comp_ref = 0;
    ulp_sum_t ulp_sum_mexce_ref;
    ulp_sum_t ulp_sum_mexce_comp;
    ulp_sum_t ulp_sum_comp_ref;
    constexpr long double k_zero_abs_tol = 1e-12L;

    auto update_bin_counts = [&](uint64_t ulp, size_t& exact_zero_count, std::vector<size_t>& bins, size_t& comparisons) {
        if (ulp == UINT64_MAX) {
            return;
        }
        ++comparisons;
        if (ulp == 0) {
            ++exact_zero_count;
            return;
        }
        size_t bin_idx = k_num_bins;
        for (size_t bin_i = 0; bin_i < k_num_bins; ++bin_i) {
            if (ulp <= k_bin_thresholds[bin_i]) {
                bin_idx = bin_i;
                break;
            }
        }
        ++bins[bin_idx];
    };

    long long total_compile_duration_ns = 0;
    long double sum_compile_ns = 0.0L;
    uint64_t compile_min_ns = std::numeric_limits<uint64_t>::max();
    uint64_t compile_max_ns = 0;
    std::vector<uint64_t> compile_times;
    compile_times.reserve(total_expressions);

    long long total_duration_ns = 0;
    size_t benchmarked_functions = 0;
    long long total_native_duration_ns = 0;
    size_t benchmarked_native_functions = 0;

    const std::size_t iterations_u = static_cast<std::size_t>(iterations);
    const int timing_trials =
        (iterations >= 10000) ? 1 :
        (iterations >= 1000) ? 5 :
        11;
    std::vector<long long> timing_samples;
    std::vector<long long> native_timing_samples;
    timing_samples.reserve(static_cast<size_t>(timing_trials));
    native_timing_samples.reserve(static_cast<size_t>(timing_trials));

    for (std::size_t idx = 0; idx < total_expressions; ++idx) {
        const std::string expr = mexce::benchmark_data::kExpressions[idx];
        const long double golden = mexce::benchmark_data::kGoldenResults[idx];

        a = 1.1; b = 2.2; c = 3.3; x = 4.4; y = 5.5; z = 6.6; w = 7.7;

        const double golden_d = static_cast<double>(golden);

        record_t rec;
        rec.expr = expr;
        rec.expected = golden;
        rec.compiled = false;
        rec.eval_ok = false;
        rec.native_available = idx < mexce::benchmark_data::kNativeExpressionsCount;
        rec.native_eval_ok = false;
        rec.mexce_result = std::numeric_limits<double>::quiet_NaN();
        rec.native_result = std::numeric_limits<double>::quiet_NaN();
        rec.ulp_mexce_vs_compiler = UINT64_MAX;
        rec.ulp_mexce_vs_reference = UINT64_MAX;
        rec.ulp_compiler_vs_reference = UINT64_MAX;
        rec.compile_ns = 0;
        rec.avg_ns = 0;
        rec.dur_ns = 0;
        rec.native_avg_ns = 0;
        rec.native_dur_ns = 0;
        rec.error.clear();

        if (rec.native_available) {
            native_ctx.a = a;
            native_ctx.b = b;
            native_ctx.c = c;
            native_ctx.x = x;
            native_ctx.y = y;
            native_ctx.z = z;
            native_ctx.w = w;

            rec.native_result = mexce::benchmark_data::kNativeExpressions[idx](native_ctx);
            rec.native_eval_ok = true;

            const bool native_zero = std::fabs(rec.native_result) <= (double)k_zero_abs_tol;
            const bool golden_zero = std::abs(golden) <= k_zero_abs_tol;
            if (native_zero && golden_zero) {
                rec.ulp_compiler_vs_reference = 0;
            }
            else {
                rec.ulp_compiler_vs_reference = ulp_distance(rec.native_result, golden_d);
            }
            ulp_sum_comp_ref.add(rec.ulp_compiler_vs_reference);
            update_bin_counts(rec.ulp_compiler_vs_reference, exact_zero_count_comp_ref, bin_counts_comp_ref, comparisons_comp_ref);
        }

        mexce::stopwatch compile_timer;
        try {
            eval.set_expression(expr);
            rec.optimized_expr = eval.get_optimized_expression();
            rec.bytes_expr = eval.get_byte_representation();
            rec.compile_ns = (uint64_t)compile_timer.elapsed_nanoseconds();
            rec.compiled = true;
            ++compiled_count;
            total_compile_duration_ns += (long long)rec.compile_ns;
            sum_compile_ns += (long double)rec.compile_ns;
            compile_min_ns = std::min(compile_min_ns, rec.compile_ns);
            compile_max_ns = std::max(compile_max_ns, rec.compile_ns);
            compile_times.push_back(rec.compile_ns);
        }
        catch (const std::exception& e) {
            rec.compile_ns = (uint64_t)compile_timer.elapsed_nanoseconds();
            ++compile_fail_count;
            rec.error = std::string("compile: ") + e.what();
            records.push_back(rec);
            continue;
        }

        try {
            rec.mexce_result = eval.evaluate();
            rec.eval_ok = true;
        }
        catch (const std::exception& e) {
            ++eval_fail_count;
            rec.error = std::string("evaluate: ") + e.what();
            records.push_back(rec);
            continue;
        }

        const bool mexce_zero = std::fabs(rec.mexce_result) <= (double)k_zero_abs_tol;
        const bool golden_zero = std::abs(golden) <= k_zero_abs_tol;
        if (mexce_zero && golden_zero) {
            rec.ulp_mexce_vs_reference = 0;
        }
        else {
            rec.ulp_mexce_vs_reference = ulp_distance(rec.mexce_result, golden_d);
        }
        ulp_sum_mexce_ref.add(rec.ulp_mexce_vs_reference);
        update_bin_counts(rec.ulp_mexce_vs_reference, exact_zero_count_mexce_ref, bin_counts_mexce_ref, comparisons_mexce_ref);

        if (rec.native_eval_ok) {
            const bool native_zero = std::fabs(rec.native_result) <= (double)k_zero_abs_tol;
            if (mexce_zero && native_zero) {
                rec.ulp_mexce_vs_compiler = 0;
            }
            else {
                rec.ulp_mexce_vs_compiler = ulp_distance(rec.mexce_result, rec.native_result);
            }
            ulp_sum_mexce_comp.add(rec.ulp_mexce_vs_compiler);
            update_bin_counts(rec.ulp_mexce_vs_compiler, exact_zero_count_mexce_comp, bin_counts_mexce_comp, comparisons_mexce_comp);
        }

        timing_samples.clear();
        for (int trial = 0; trial < timing_trials; ++trial) {
            mexce::stopwatch timer;
            for (std::size_t i = 0; i < iterations_u; ++i) {
                (void)eval.evaluate();
            }
            timing_samples.push_back(timer.elapsed_nanoseconds());
        }
        std::nth_element(timing_samples.begin(),
                         timing_samples.begin() + timing_samples.size() / 2,
                         timing_samples.end());
        rec.dur_ns = timing_samples[timing_samples.size() / 2];
        rec.avg_ns = (uint64_t)((long double)rec.dur_ns / (long double)iterations_u + 0.5L);

        total_duration_ns += rec.dur_ns;
        ++benchmarked_functions;

        if (rec.native_eval_ok) {
            native_timing_samples.clear();
            for (int trial = 0; trial < timing_trials; ++trial) {
                mexce::stopwatch timer;
                for (std::size_t i = 0; i < iterations_u; ++i) {
                    (void)mexce::benchmark_data::kNativeExpressions[idx](native_ctx);
                }
                native_timing_samples.push_back(timer.elapsed_nanoseconds());
            }
            std::nth_element(native_timing_samples.begin(),
                             native_timing_samples.begin() + native_timing_samples.size() / 2,
                             native_timing_samples.end());
            rec.native_dur_ns = native_timing_samples[native_timing_samples.size() / 2];
            rec.native_avg_ns = (uint64_t)((long double)rec.native_dur_ns / (long double)iterations_u + 0.5L);
            total_native_duration_ns += rec.native_dur_ns;
            ++benchmarked_native_functions;
        }

        records.push_back(rec);
    }

    const std::string line = std::string(65, '-');
    auto print_kv = [&out](const std::string& k, const std::string& v) {
        out << std::left << std::setw(40) << k << v << "\n";
    };

    out << line << "\n" << "TEST SUMMARY" << "\n" << line << "\n";
    print_kv("Compiled", std::to_string(compiled_count));
    print_kv("Failed to compile", std::to_string(compile_fail_count));
    if (eval_fail_count > 0) {
        print_kv("Evaluation failures", std::to_string(eval_fail_count));
    }

    const std::size_t total_expr_count = static_cast<std::size_t>(total_expressions);
    const std::size_t native_covered = std::min<std::size_t>(total_expr_count, mexce::benchmark_data::kNativeExpressionsCount);
    print_kv("Native expressions covered", std::to_string(native_covered));

    out << "\nAccuracy distribution (ULP):\n";

    struct Distribution_column {
        std::string title;
        size_t comparisons;
        size_t exact_zero_count;
        const std::vector<size_t>* bins;
    };

    const Distribution_column distribution_columns[] = {
        {"Mexce vs Reference",    comparisons_mexce_ref, exact_zero_count_mexce_ref,   &bin_counts_mexce_ref},
        {"Compiler vs Reference", comparisons_comp_ref,  exact_zero_count_comp_ref,    &bin_counts_comp_ref},
        {"Mexce vs Compiler",     comparisons_mexce_comp, exact_zero_count_mexce_comp, &bin_counts_mexce_comp}
    };

    std::vector<std::string> row_labels;
    row_labels.reserve(k_num_bins + 2);
    row_labels.emplace_back("0 (exact)");
    for (size_t bin_idx = 0; bin_idx < k_num_bins; ++bin_idx) {
        char buf[32];
        uint64_t lo = (bin_idx == 0 ? 1 : (k_bin_thresholds[bin_idx - 1] + 1));
        uint64_t hi = k_bin_thresholds[bin_idx];
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
            for (size_t bin_idx = 0; bin_idx < k_num_bins + 1; ++bin_idx) {
                values.emplace_back("-");
            }
        }
        else {
            values.emplace_back(std::to_string(column.exact_zero_count));
            for (size_t bin_idx = 0; bin_idx < k_num_bins; ++bin_idx) {
                values.emplace_back(std::to_string((*column.bins)[bin_idx]));
            }
            values.emplace_back(std::to_string((*column.bins)[k_num_bins]));
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

    out << "Accumulated ULP distance (sum):\n";
    print_kv("Mexce vs Reference", comparisons_mexce_ref == 0 ? "-" : to_decimal_u128(ulp_sum_mexce_ref.hi, ulp_sum_mexce_ref.lo));
    print_kv("Compiler vs Reference", comparisons_comp_ref == 0 ? "-" : to_decimal_u128(ulp_sum_comp_ref.hi, ulp_sum_comp_ref.lo));
    print_kv("Mexce vs Compiler", comparisons_mexce_comp == 0 ? "-" : to_decimal_u128(ulp_sum_mexce_comp.hi, ulp_sum_mexce_comp.lo));
    out << "\n";

    out << "Compilation time histogram:" << "\n";
    if (compile_times.empty()) {
        out << "No successfully compiled expressions." << "\n";
    }
    else {
        constexpr size_t kCompileBins = 10;
        std::vector<size_t> compile_hist(kCompileBins, 0);

        // Use logarithmic bins so that both the common fast compilations and
        // rare slow outliers are visible.
        uint64_t log_min_ns = compile_min_ns;
        uint64_t log_max_ns = compile_max_ns;

        // Guard against all-zero or degenerate ranges.
        if (log_min_ns == 0) {
            log_min_ns = std::numeric_limits<uint64_t>::max();
            for (uint64_t ns : compile_times) {
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

        for (uint64_t ns : compile_times) {
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
                // Degenerate case: everything in one bucket.
                start_ns = compile_min_ns;
                end_ns = compile_max_ns;
            } else {
                const long double start_ratio = (long double)bin_idx / (long double)kCompileBins;
                const long double end_ratio = (long double)(bin_idx + 1) / (long double)kCompileBins;

                const long double start_log = log_min + start_ratio * log_range;
                const long double end_log = log_min + end_ratio * log_range;

                start_ns = (uint64_t)std::floor(std::pow((long double)10.0, start_log));
                end_ns   = (uint64_t)std::floor(std::pow((long double)10.0, end_log));

                if (start_ns < compile_min_ns) start_ns = compile_min_ns;
                if (end_ns > compile_max_ns)   end_ns = compile_max_ns;
                if (end_ns < start_ns)         end_ns = start_ns;

                if (bin_idx == kCompileBins - 1) {
                    end_ns = compile_max_ns;
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
        mexce_column.values[0] = std::to_string(benchmarked_functions);
        if (compiled_count > 0) {
            const uint64_t avg_compile_ns = (uint64_t)(sum_compile_ns / (long double)compiled_count + 0.5L);
            mexce_column.values[1] = format_ns(avg_compile_ns);
            mexce_column.values[3] = format_ns((uint64_t)total_compile_duration_ns);
        }
        if (benchmarked_functions > 0) {
            // Compute average time per single function call from totals
            // avg = total_time / (num_functions * iterations_per_function)
            const uint64_t total_calls = (uint64_t)benchmarked_functions * (uint64_t)iterations;
            const uint64_t avg_per_func_ns = (total_calls > 0)
                ? (uint64_t)((double)total_duration_ns / (double)total_calls + 0.5)
                : 0;
            mexce_column.values[2] = format_ns(avg_per_func_ns);
            mexce_column.values[4] = format_ns((uint64_t)total_duration_ns);
        }
        summary_columns.push_back(std::move(mexce_column));
    }

    {
        Summary_column compiler_column;
        compiler_column.title = "Compiler";
        compiler_column.values.assign(summary_rows.size(), "-");
        compiler_column.values[0] = std::to_string(benchmarked_native_functions);
        if (benchmarked_native_functions > 0) {
            // Compute average time per single function call from totals
            const uint64_t total_native_calls = (uint64_t)benchmarked_native_functions * (uint64_t)iterations;
            const uint64_t avg_native_ns = (total_native_calls > 0)
                ? (uint64_t)((double)total_native_duration_ns / (double)total_native_calls + 0.5)
                : 0;
            compiler_column.values[2] = format_ns(avg_native_ns);
            compiler_column.values[4] = format_ns((uint64_t)total_native_duration_ns);
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

    for (const auto& r : records) {
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

    // --- Create bins for sorting ---
    std::vector<const record_t*> compile_failures;
    std::vector<const record_t*> eval_failures;
    std::vector<const record_t*> passed;

    for (const auto& rec : records) {
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

    // --- Sort bins ---
    auto sort_by_expression = [](const record_t* a, const record_t* b) {
        return a->expr < b->expr;
    };
    std::sort(compile_failures.begin(), compile_failures.end(), sort_by_expression);
    std::sort(eval_failures.begin(), eval_failures.end(), sort_by_expression);

    // Sort "Passed" by Speedup (Native / Mexce) ASCENDING.
    // Speedup < 1.0: Native is slower (Mexce faster).
    // Speedup > 1.0: Native is faster (Mexce regression).
    // Wait, logic check:
    // Speedup = Native_Time / Mexce_Time
    // If Native takes 100ns and Mexce takes 10ns (Mexce fast), Speedup = 10.0.
    // If Native takes 10ns and Mexce takes 100ns (Mexce slow), Speedup = 0.1.
    //
    // User wants "ascending order, according to how much faster the compiler's version is".
    // If we sort by (Native_Time / Mexce_Time) Ascending:
    // 0.1 (Mexce slow/Problematic) comes first.
    // 10.0 (Mexce fast) comes last.
    // This highlights problematic regressions first.
    std::sort(passed.begin(), passed.end(), [](const record_t* a, const record_t* b) {
        // Push entries without native comparison to the end of the sort order?
        // Or sort them as 0 speedup? Let's handle native_eval_ok.
        if (!a->native_eval_ok && b->native_eval_ok) return false; // Put 'no native' after 'has native'
        if (a->native_eval_ok && !b->native_eval_ok) return true;
        if (!a->native_eval_ok && !b->native_eval_ok) return a->expr < b->expr;

        double speedup_a = (double)a->native_avg_ns / (std::max)(1.0, (double)a->avg_ns);
        double speedup_b = (double)b->native_avg_ns / (std::max)(1.0, (double)b->avg_ns);
        
        if (std::abs(speedup_a - speedup_b) > 1e-6) {
            return speedup_a < speedup_b; 
        }
        return a->expr < b->expr;
    });

    // --- Print sorted bins ---

    // 1) Compile failures first
    if (!compile_failures.empty()) {
        out << "Compile Failures (sorted alphabetically):" << "\n";
        print_row_header();
        for (const auto* r_ptr : compile_failures) {
            const record_t& r = *r_ptr;
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

    // 2) Evaluation failures next
    if (!eval_failures.empty()) {
        out << "Evaluation Failures (sorted alphabetically):" << "\n";
        print_row_header();
        for (const auto* r_ptr : eval_failures) {
            const record_t& r = *r_ptr;
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

    // 3) Passed cases
    if (!passed.empty()) {
        out << "Passed (sorted by Speedup [Native/Mexce] ASC - Problematic regressions first):" << "\n";
        print_row_header();
        auto ulp_exceeds = [](uint64_t value) { return value != UINT64_MAX && value > 8192; };
        for (const auto* r_ptr : passed) {
            const record_t& r = *r_ptr;

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

    return (compile_fail_count == 0 && eval_fail_count == 0) ? 0 : 1;
}

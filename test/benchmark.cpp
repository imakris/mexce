#include "benchmark_expressions.h"
#include "benchmark_results.h"

#ifdef _MSC_VER
#  pragma warning(push)
#  pragma warning(disable:4100 4189 4456)
#endif
#include "mexce.h"
#ifdef _MSC_VER
#  pragma warning(pop)
#endif

#include <omp.h>

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

// ------------------------------ Main ---------------------------------

int main(int argc, char* argv[])
{
    if (argc > 3) {
        std::cerr << "Usage: " << argv[0] << " [iterations] [output_file]\n";
        std::cerr << "You may also pass an output file as the first argument." << std::endl;
        return 1;
    }

    int iterations = 10000;
    std::string output_path = "benchmark_results.txt";
    bool iterations_set = false;

    if (argc >= 2) {
        int parsed = 0;
        Iteration_parse_result pr = parse_iterations(argv[1], &parsed);
        if (pr == success) {
            iterations = parsed;
            iterations_set = true;
        }
        else
        if (pr == not_numeric) {
            output_path = argv[1];
        }
        else {
            std::cerr << "Iteration count must be a positive integer." << std::endl;
            return 1;
        }
    }

    if (argc == 3) {
        if (iterations_set) {
            output_path = argv[2];
        }
        else {
            int parsed = 0;
            Iteration_parse_result pr = parse_iterations(argv[2], &parsed);
            if (pr == success) {
                iterations = parsed;
                iterations_set = true;
            }
            else
            if (pr == not_numeric) {
                std::cerr << "Invalid iteration count: " << argv[2] << std::endl;
                return 1;
            }
            else {
                std::cerr << "Iteration count must be a positive integer." << std::endl;
                return 1;
            }
        }
    }

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
    double a = 1.1, b = 2.2, c = 3.3, x = 4.4, y = 5.5, z = 6.6, w = 7.7;
    eval.bind(a, "a", b, "b", c, "c", x, "x", y, "y", z, "z", w, "w");

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

    const volatile std::size_t total_expressions = mexce::benchmark_data::kExpressionCount; // volatile, only for MSVC to shut up
    if (total_expressions == 0) {
        out << "No expressions available for benchmarking." << std::endl;
        return 0;
    }

    struct record_t {
        std::string expr;
        bool compiled;
        bool eval_ok;
        long double expected;
        double got;
        uint64_t ulp;
        uint64_t avg_ns;
        long long dur_ns;
        std::string error;
    };

    std::vector<record_t> records;
    records.reserve(total_expressions);

    constexpr uint64_t k_bin_thresholds[] = {
        16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536
    };
    constexpr size_t k_num_bins = sizeof(k_bin_thresholds) / sizeof(k_bin_thresholds[0]);

    size_t compiled_count = 0, compile_fail_count = 0, eval_fail_count = 0;
    size_t exact_zero_count = 0;
    std::vector<size_t> bin_counts(k_num_bins + 1, 0);

    long long total_duration_ns = 0;
    size_t benchmarked_functions = 0;
    long double sum_avg_ns = 0.0L;

    for (std::size_t idx = 0; idx < total_expressions; ++idx) {
        const std::string expr = mexce::benchmark_data::kExpressions[idx];
        const long double golden = mexce::benchmark_data::kGoldenResults[idx];

        a = 1.1; b = 2.2; c = 3.3; x = 4.4; y = 5.5; z = 6.6; w = 7.7;

        record_t rec;
        rec.expr = expr;
        rec.expected = golden;
        rec.compiled = false;
        rec.eval_ok = false;
        rec.got = std::numeric_limits<double>::quiet_NaN();
        rec.ulp = 0;
        rec.avg_ns = 0;
        rec.dur_ns = 0;
        rec.error.clear();

        try {
            eval.set_expression(expr);
            rec.compiled = true;
            ++compiled_count;
        }
        catch (const std::exception& e) {
            ++compile_fail_count;
            rec.error = std::string("compile: ") + e.what();
            records.push_back(rec);
            continue;
        }

        try {
            rec.got = eval.evaluate();
            rec.eval_ok = true;
        }
        catch (const std::exception& e) {
            ++eval_fail_count;
            rec.error = std::string("evaluate: ") + e.what();
            records.push_back(rec);
            continue;
        }

        constexpr long double k_zero_abs_tol = 1e-12L;
        if (std::abs(rec.got) <= (double)k_zero_abs_tol && std::abs(golden) <= k_zero_abs_tol) {
            rec.ulp = 0;
        }
        else {
            rec.ulp = ulp_distance(rec.got, (double)golden);
        }

        if (rec.ulp == 0) {
            ++exact_zero_count;
        }
        else {
            size_t bin_idx = k_num_bins;
            for (size_t bin_i = 0; bin_i < k_num_bins; ++bin_i) {
                if (rec.ulp <= k_bin_thresholds[bin_i]) {
                    bin_idx = bin_i;
                    break;
                }
            }
            ++bin_counts[bin_idx];
        }

        const double t0 = omp_get_wtime();
        std::size_t executed = 0;
        for (; executed < (std::size_t)iterations; ++executed) {
            (void)eval.evaluate();
        }
        const double t1 = omp_get_wtime();

        rec.dur_ns = (long long)((t1 - t0) * 1e9);
        rec.avg_ns = (uint64_t)((long double)rec.dur_ns / (long double)executed + 0.5L);

        total_duration_ns += rec.dur_ns;
        sum_avg_ns += (long double)rec.avg_ns;
        ++benchmarked_functions;

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

    out << "\nAccuracy distribution (ULP):\n";
    print_kv("  0 (exact)", std::to_string(exact_zero_count));

    for (size_t bin_idx = 0; bin_idx < k_num_bins; ++bin_idx) {
        char buf[64];
        uint64_t lo = (bin_idx == 0 ? 1 : (k_bin_thresholds[bin_idx - 1] + 1));
        uint64_t hi = k_bin_thresholds[bin_idx];
        std::snprintf(buf, sizeof(buf), "  %llu-%llu", (unsigned long long)lo, (unsigned long long)hi);
        print_kv(buf, std::to_string(bin_counts[bin_idx]));
    }
    print_kv("  >65536", std::to_string(bin_counts[k_num_bins]));

    out << "\n" << line << "\n" << "BENCHMARK SUMMARY" << "\n" << line << "\n";
    print_kv("Functions benchmarked", std::to_string(benchmarked_functions));
    if (benchmarked_functions > 0) {
        uint64_t avg_per_func_ns = (uint64_t)(sum_avg_ns / (long double)benchmarked_functions + 0.5L);
        print_kv("Average runtime per function", format_ns(avg_per_func_ns));
        print_kv("Total function execution time", format_ns((uint64_t)total_duration_ns));
    }

    out << "\n" << line << "\n" << "DETAILED REPORT" << "\n" << line << "\n";

    size_t max_ulp_len = 3;
    for (size_t i = 0; i < records.size(); ++i) {
        const record_t& r = records[i];
        size_t len = r.eval_ok ? std::to_string(r.ulp).size() : 1;
        if (len > max_ulp_len) {
            max_ulp_len = len;
        }
    }

    auto print_row_header = [&] {
        out << std::left << std::setw(10) << "Status"
            << "  " << std::setw((int)max_ulp_len) << "ULP"
            << "  " << std::setw(16) << "Avg/Call"
            << "  " << "Expression" << "\n";
        out << std::string(10, '-')
            << "  " << std::string((int)max_ulp_len, '-')
            << "  " << std::string(16, '-')
            << "  " << std::string(40, '-') << "\n";
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

    std::sort(passed.begin(), passed.end(), [](const record_t* a, const record_t* b) {
        if (a->ulp != b->ulp) {
            return a->ulp > b->ulp; // Primary key: ULP descending
        }
        return a->expr < b->expr;   // Secondary key: Alphabetical ascending
    });

    // --- Print sorted bins ---

    // 1) Compile failures first
    if (!compile_failures.empty()) {
        out << "Compile Failures (sorted alphabetically):" << "\n";
        print_row_header();
        for (const auto* r_ptr : compile_failures) {
            const record_t& r = *r_ptr;
            out << std::left << std::setw(10) << "compile"
                << "  " << std::setw((int)max_ulp_len) << "-"
                << "  " << std::setw(16) << "-"
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
                << "  " << std::setw((int)max_ulp_len) << "-"
                << "  " << std::setw(16) << "-"
                << "  " << r.expr << "\n";
            if (!r.error.empty()) {
                out << "    note: " << r.error << "\n";
            }
        }
        out << "\n";
    }

    // 3) Passed cases
    if (!passed.empty()) {
        out << "Passed (sorted by ULP desc, then alphabetically):" << "\n";
        print_row_header();
        for (const auto* r_ptr : passed) {
            const record_t& r = *r_ptr;

            if (r.ulp > 8192) {
                out << "\n";
            }

            out << std::left << std::setw(10) << "ok"
                << "  " << std::setw((int)max_ulp_len) << std::to_string(r.ulp)
                << "  " << std::setw(16) << format_ns(r.avg_ns)
                << "  " << r.expr << "\n";

            if (r.ulp > 8192) {
                std::ios_base::fmtflags original_flags = out.flags();
                auto original_precision = out.precision();

                out << std::fixed << std::setprecision(17);
                out << "    Got:      " << r.got << "\n";
                out << "    Expected: " << static_cast<double>(r.expected) << "\n";

                out.flags(original_flags);
                out.precision(original_precision);
            }
        }
    }

    return (compile_fail_count == 0 && eval_fail_count == 0) ? 0 : 1;
}

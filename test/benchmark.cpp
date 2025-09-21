#include <algorithm>
#include <omp.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <cstdint>
#include <vector>

// Human-readable nanoseconds formatter.
std::string format_ns(uint64_t ns)
{
    struct Unit { const char* name; uint64_t factor; };
    // Powers of 10 only, so decimals are exact (no rounding artifacts).
    static constexpr Unit units[] = {
        {"sec", 1000000000ULL},
        {"ms",    1000000ULL},
        {"us",       1000ULL},
        {"ns",          1ULL}
    };

    // Special-case zero.
    if (ns == 0) {
        return "0.0 ns";
    }

    const Unit* chosen = &units[3]; // default to ns
    for (const Unit& u : units) {
        uint64_t integer = ns / u.factor;
        if (integer >= 1 && integer <= 999) {
            chosen = &u;
            break;
        }
    }
    // If none matched (e.g., >= 1000 seconds), we keep "sec" anyway.
    if (ns / units[0].factor >= 1000) {
        chosen = &units[0];
    }

    uint64_t integer = ns / chosen->factor;
    uint64_t frac    = ns % chosen->factor;

    // Determine how many decimal digits this unit can have.
    unsigned width = 0;
    for (uint64_t f = chosen->factor; f > 1; f /= 10) {
        ++width;
    }

    std::string out;
    out.reserve(32);
    out += std::to_string(integer);

    if (width > 0) {
        // Zero-pad the fractional part to full width, then trim trailing zeros.
        std::string frac_str(width, '0');
        for (int i = int(width) - 1; i >= 0 && frac > 0; --i) {
            frac_str[std::size_t(i)] = char('0' + (frac % 10));
            frac /= 10;
        }
        // Trim trailing zeros.
        while (!frac_str.empty() && frac_str.back() == '0') {
            frac_str.pop_back();
        }
        out += '.';
        out += frac_str.empty() ? "0" : frac_str;
    }
    else {
        // For "ns" we still show one decimal to satisfy "…and the rest decimals".
        out += ".0";
    }

    out += ' ';
    out += chosen->name;
    return out;
}

#ifdef _WIN32
  #include <direct.h>   // _getcwd
  #include <stdlib.h>   // _fullpath, _MAX_PATH
#else
  #include <limits.h>   // PATH_MAX
  #include <unistd.h>   // getcwd, realpath
#endif

#ifdef _MSC_VER
#  pragma warning(push)
#  pragma warning(disable:4100) // silence unreferenced parameter warnings inside mexce.h
#endif
#include "mexce.h"
#ifdef _MSC_VER
#  pragma warning(pop)
#endif

#include "benchmark_expressions.h"

namespace {

enum class IterationParseResult {
    kSuccess,
    kNotNumeric,
    kInvalidRange,
};

IterationParseResult parse_iterations(const char* text, int* value)
{
    try {
        const int parsed = std::stoi(text);
        if (parsed <= 0) {
            return IterationParseResult::kInvalidRange;
        }
        *value = parsed;
        return IterationParseResult::kSuccess;
    }
    catch (const std::invalid_argument&) {
        return IterationParseResult::kNotNumeric;
    }
    catch (const std::out_of_range&) {
        return IterationParseResult::kInvalidRange;
    }
}

// Portable absolute-path resolver for display purposes.
static std::string resolve_full_path(const std::string& path)
{
#ifdef _WIN32
    char absbuf[_MAX_PATH];
    if (_fullpath(absbuf, path.c_str(), _MAX_PATH)) {
        return std::string(absbuf);
    }
    // Fallback: CWD + '\' + path
    char cwdbuf[_MAX_PATH];
    if (_getcwd(cwdbuf, _MAX_PATH)) {
        std::string s = cwdbuf;
        if (!s.empty() && s.back() != '\\') s += '\\';
        s += path;
        return s;
    }
    return path;
#else
    // Try realpath (may fail if file doesn't exist yet)
    if (char* rp = realpath(path.c_str(), nullptr)) {
        std::string s(rp);
        free(rp);
        return s;
    }
    // Fallback: CWD + '/' + path
    char cwdbuf[PATH_MAX];
    if (getcwd(cwdbuf, sizeof(cwdbuf))) {
        std::string s = cwdbuf;
        if (!s.empty() && s.back() != '/') s += '/';
        s += path;
        return s;
    }
    return path;
#endif
}

}  // namespace

int main(int argc, char* argv[])
{
    if (argc > 3) {
        std::cerr << "Usage: " << argv[0] << " [iterations] [output_file]" << std::endl;
        std::cerr << "You may also pass an output file as the first argument." << std::endl;
        return 1;
    }

    int iterations = 10000;
    std::string output_path = "benchmark_results.txt";
    bool iterations_set = false;

    if (argc >= 2) {
        int parsed_iterations = 0;
        switch (parse_iterations(argv[1], &parsed_iterations)) {
        case IterationParseResult::kSuccess:
            iterations = parsed_iterations;
            iterations_set = true;
            break;
        case IterationParseResult::kNotNumeric:
            output_path = argv[1];
            break;
        case IterationParseResult::kInvalidRange:
            std::cerr << "Iteration count must be a positive integer." << std::endl;
            return 1;
        }
    }

    if (argc == 3) {
        if (iterations_set) {
            output_path = argv[2];
        }
        else {
            int parsed_iterations = 0;
            switch (parse_iterations(argv[2], &parsed_iterations)) {
            case IterationParseResult::kSuccess:
                iterations = parsed_iterations;
                iterations_set = true;
                break;
            case IterationParseResult::kNotNumeric:
                std::cerr << "Invalid iteration count: " << argv[2] << std::endl;
                return 1;
            case IterationParseResult::kInvalidRange:
                std::cerr << "Iteration count must be a positive integer." << std::endl;
                return 1;
            }
        }
    }

    const std::string resolved_output = output_path.empty()
        ? std::string()
        : resolve_full_path(output_path);

    // Announce what we are going to do, as requested:
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
    double a = 1.1;
    double b = 2.2;
    double c = 3.3;
    double x = 4.4;
    double y = 5.5;
    double z = 6.6;
    double w = 7.7;
    eval.bind(a, "a", b, "b", c, "c", x, "x", y, "y", z, "z", w, "w");

    std::ofstream file_output;
    std::ostream* output_stream = &std::cout;
    if (!output_path.empty()) {
        file_output.open(output_path);
        if (!file_output) {
            std::cerr << "Failed to open output file: " << output_path << std::endl;
            return 1;
        }
        output_stream = &file_output;
    }

    std::ostream& out = *output_stream;
    out << std::fixed << std::setprecision(3);

    const std::size_t total_expressions = mexce::benchmark_data::kExpressionCount;
    if (total_expressions == 0) {
        out << "No expressions available for benchmarking." << std::endl;
        return 0;
    }

    std::vector<std::string> failed_expressions;
    failed_expressions.reserve(total_expressions);

    long long total_duration_ns = 0;
    std::size_t successful_expressions = 0;

    for (std::size_t idx = 0; idx < total_expressions; ++idx) {
        const std::string expr = mexce::benchmark_data::kExpressions[idx];

        a = 1.1;
        b = 2.2;
        c = 3.3;
        x = 4.4;
        y = 5.5;
        z = 6.6;
        w = 7.7;

        try {
            eval.set_expression(expr);
        }
        catch (const mexce::mexce_parsing_exception& err) {
            std::cerr << "Failed to compile expression \"" << expr << "\": " << err.what()
                      << std::endl;
            failed_expressions.push_back(expr);
            continue;
        }
        catch (const std::exception& err) {
            std::cerr << "Unexpected error compiling expression \"" << expr << "\": " << err.what()
                      << std::endl;
            failed_expressions.push_back(expr);
            continue;
        }

        const double start = omp_get_wtime();

        std::size_t executed_iterations = 0;
        bool success = true;

        for (; executed_iterations < static_cast<std::size_t>(iterations); ++executed_iterations) {
            try {
                volatile double result = eval.evaluate();
                (void)result;
            }
            catch (const std::exception& err) {
                std::cerr << "Error evaluating expression \"" << expr << "\": " << err.what()
                          << std::endl;
                success = false;
                break;
            }

            std::swap(a, b);
            std::swap(x, y);
        }

        const double end = omp_get_wtime();
        const long long duration_ns = static_cast<long long>((end - start) * 1e9);

        if (!success || executed_iterations == 0) {
            failed_expressions.push_back(expr);
            continue;
        }

        total_duration_ns += duration_ns;
        ++successful_expressions;
        const uint64_t avg_duration_ns = static_cast<uint64_t>((static_cast<long double>(duration_ns) / static_cast<long double>(executed_iterations)) + 0.5L);
        out << "Expression: \"" << expr << "\", Average time: " << format_ns(avg_duration_ns) << std::endl;
    }

    out << "-----------------------------------------------------" << std::endl;
    out << "Total expressions: " << total_expressions << std::endl;
    out << "Successful expressions: " << successful_expressions << std::endl;
    out << "Failed expressions: " << failed_expressions.size() << std::endl;

    if (successful_expressions > 0) {
        const uint64_t avg_total_duration_ns = static_cast<uint64_t>((static_cast<long double>(total_duration_ns) / static_cast<long double>(successful_expressions)) + 0.5L);
        out << "Total time for " << iterations
            << " iterations per expression: " << format_ns(total_duration_ns) << std::endl;
        out << "Average time per expression (over " << iterations
            << " iterations): " << format_ns(avg_total_duration_ns) << std::endl;
    }
    else {
        out << "No expressions were successfully evaluated." << std::endl;
    }

    if (!failed_expressions.empty()) {
        out << "-----------------------------------------------------" << std::endl;
        out << "Failed expressions:" << std::endl;
        for (const auto& failed : failed_expressions) {
            out << "  " << failed << std::endl;
        }
    }

    return 0;
}

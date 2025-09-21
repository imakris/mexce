#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "mexce.h"
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
    } catch (const std::invalid_argument&) {
        return IterationParseResult::kNotNumeric;
    } catch (const std::out_of_range&) {
        return IterationParseResult::kInvalidRange;
    }
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
        } else {
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

    if (argc == 1) {
        std::cout << "No arguments provided. Running 10000 iterations and writing results to "
                  << output_path << " in the current working directory." << std::endl;
    } else if (!iterations_set) {
        std::cout << "Using default iteration count of 10000." << std::endl;
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
        if (argc != 1) {
            std::cout << "Writing benchmark results to " << output_path << std::endl;
        }
    } else {
        std::cout << "Writing benchmark results to standard output." << std::endl;
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

    std::chrono::nanoseconds total_duration{0};
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
        } catch (const mexce::mexce_parsing_exception& err) {
            std::cerr << "Failed to compile expression \"" << expr << "\": " << err.what()
                      << std::endl;
            failed_expressions.push_back(expr);
            continue;
        } catch (const std::exception& err) {
            std::cerr << "Unexpected error compiling expression \"" << expr << "\": " << err.what()
                      << std::endl;
            failed_expressions.push_back(expr);
            continue;
        }

        const auto start = std::chrono::steady_clock::now();

        std::size_t executed_iterations = 0;
        bool success = true;

        for (; executed_iterations < static_cast<std::size_t>(iterations); ++executed_iterations) {
            try {
                volatile double result = eval.evaluate();
                (void)result;
            } catch (const std::exception& err) {
                std::cerr << "Error evaluating expression \"" << expr << "\": " << err.what()
                          << std::endl;
                success = false;
                break;
            }

            std::swap(a, b);
            std::swap(x, y);
        }

        const auto end = std::chrono::steady_clock::now();
        const auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);

        if (!success || executed_iterations == 0) {
            failed_expressions.push_back(expr);
            continue;
        }

        total_duration += duration;
        ++successful_expressions;

        const double avg_duration = static_cast<double>(duration.count()) /
                                    static_cast<double>(executed_iterations);
        out << "Expression: \"" << expr << "\", Average time: " << avg_duration << " ns" << std::endl;
    }

    out << "-----------------------------------------------------" << std::endl;
    out << "Total expressions: " << total_expressions << std::endl;
    out << "Successful expressions: " << successful_expressions << std::endl;
    out << "Failed expressions: " << failed_expressions.size() << std::endl;

    if (successful_expressions > 0) {
        const double avg_total_duration = static_cast<double>(total_duration.count()) /
                                          static_cast<double>(successful_expressions);
        out << "Total time for " << iterations
            << " iterations per expression: " << total_duration.count() << " ns" << std::endl;
        out << "Average time per expression (over " << iterations
            << " iterations): " << avg_total_duration << " ns" << std::endl;
    } else {
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

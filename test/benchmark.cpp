#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "mexce.h"

namespace {

std::string trim(const std::string& input)
{
    const auto first = input.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return std::string();
    }
    const auto last = input.find_last_not_of(" \t\r\n");
    return input.substr(first, last - first + 1);
}

std::vector<std::string> load_expressions(const std::string& path)
{
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("Failed to open expressions file: " + path);
    }

    std::vector<std::string> expressions;
    std::string line;
    while (std::getline(file, line)) {
        const auto cleaned = trim(line);
        if (cleaned.empty()) {
            continue;
        }
        if (cleaned[0] == '#') {
            continue;
        }
        expressions.push_back(cleaned);
    }

    return expressions;
}

}  // namespace

int main(int argc, char* argv[])
{
    if (argc < 2 || argc > 4) {
        std::cerr << "Usage: " << argv[0]
                  << " <expressions_file> [iterations] [output_file]" << std::endl;
        return 1;
    }

    int iterations = 1000;
    if (argc >= 3) {
        try {
            iterations = std::stoi(argv[2]);
        } catch (const std::exception&) {
            std::cerr << "Invalid iteration count: " << argv[2] << std::endl;
            return 1;
        }
        if (iterations <= 0) {
            std::cerr << "Iteration count must be positive" << std::endl;
            return 1;
        }
    }

    std::string output_path;
    if (argc == 4) {
        output_path = argv[3];
    }

    std::vector<std::string> expressions;
    try {
        expressions = load_expressions(argv[1]);
    } catch (const std::exception& err) {
        std::cerr << err.what() << std::endl;
        return 1;
    }

    if (expressions.empty()) {
        std::cout << "No expressions found in file." << std::endl;
        return 0;
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

    std::chrono::nanoseconds total_duration{0};
    std::size_t successful_expressions = 0;

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

    if (output_stream != &std::cout) {
        std::cout << "Writing benchmark results to " << output_path << std::endl;
    }

    std::ostream& out = *output_stream;
    out << std::fixed << std::setprecision(3);

    for (const auto& expr : expressions) {
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
            std::cerr << "Failed to compile expression \"" << expr << "\": "
                      << err.what() << std::endl;
            continue;
        } catch (const std::exception& err) {
            std::cerr << "Unexpected error compiling expression \"" << expr
                      << "\": " << err.what() << std::endl;
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
                std::cerr << "Error evaluating expression \"" << expr << "\": "
                          << err.what() << std::endl;
                success = false;
                break;
            }

            std::swap(a, b);
            std::swap(x, y);
        }

        const auto end = std::chrono::steady_clock::now();
        const auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);

        if (!success || executed_iterations == 0) {
            continue;
        }

        total_duration += duration;
        ++successful_expressions;

        const double avg_duration = static_cast<double>(duration.count()) /
                                    static_cast<double>(executed_iterations);
        out << "Expression: \"" << expr << "\", Average time: "
            << avg_duration << " ns" << std::endl;
    }

    if (successful_expressions == 0) {
        out << "No expressions were successfully evaluated." << std::endl;
        return 0;
    }

    const double avg_total_duration = static_cast<double>(total_duration.count()) /
                                      static_cast<double>(successful_expressions);

    out << "-----------------------------------------------------" << std::endl;
    out << "Total expressions: " << expressions.size() << std::endl;
    out << "Successful expressions: " << successful_expressions << std::endl;
    out << "Total time for " << iterations
        << " iterations per expression: " << total_duration.count() << " ns" << std::endl;
    out << "Average time per expression (over " << iterations
        << " iterations): " << avg_total_duration << " ns" << std::endl;

    return 0;
}

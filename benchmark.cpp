#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <chrono>
#include "mexce.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <expressions_file>" << std::endl;
        return 1;
    }

    std::ifstream infile(argv[1]);
    if (!infile) {
        std::cerr << "Error opening file: " << argv[1] << std::endl;
        return 1;
    }

    std::vector<std::string> expressions;
    std::string line;
    while (std::getline(infile, line)) {
        if (!line.empty() && line[0] != '#') {
            expressions.push_back(line);
        }
    }

    if (expressions.empty()) {
        std::cout << "No expressions found in file." << std::endl;
        return 0;
    }

    mexce::evaluator eval;
    double a = 1.1, b = 2.2, c = 3.3, x = 4.4, y = 5.5, z = 6.6, w = 7.7;
    eval.bind(a, "a", b, "b", c, "c", x, "x", y, "y", z, "z", w, "w");

    long long total_duration = 0;
    const int iterations = 1000;

    for (const auto& expr : expressions) {
        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < iterations; ++i) {
            try {
                volatile double result = eval.evaluate(expr);
                (void)result; // Prevent unused variable warning
            } catch (const std::exception& e) {
                std::cerr << "Error evaluating '" << expr << "': " << e.what() << std::endl;
                break;
            }
        }
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        total_duration += duration;
        double avg_duration_ns = static_cast<double>(duration) / iterations;
        std::cout << "Expression: \"" << expr << "\", Average time: " << avg_duration_ns << " ns" << std::endl;
    }

    double avg_total_duration_ns = static_cast<double>(total_duration) / expressions.size();
    std::cout << "-----------------------------------------------------" << std::endl;
    std::cout << "Total expressions: " << expressions.size() << std::endl;
    std::cout << "Total time for " << iterations << " iterations per expression: " << total_duration << " ns" << std::endl;
    std::cout << "Average time per expression (over " << iterations << " iterations): " << avg_total_duration_ns << " ns" << std::endl;

    return 0;
}

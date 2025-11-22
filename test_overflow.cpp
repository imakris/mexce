#include <iostream>
#include <string>
#include <cmath>
#include <limits>
#include "mexce.h"

int main() {
    mexce::evaluator eval;

    double val = 10.0;
    eval.bind(val, "val");

    // The x87 FPU has a strict stack limit of 8 registers.
    // We construct an expression that pushes 9 variables before reducing them.
    // Structure: max(val, max(val, max(val, ... )))
    // This forces the JIT to emit 9 consecutive 'fld' instructions.

    // 1. Start with one variable
    std::string expr = "val";

    // 2. Nest it 8 times -> Total 9 operands on stack
    for(int i = 0; i < 8; ++i) {
        expr = "max(val, " + expr + ")";
    }

    std::cout << "Compiling deep expression (Depth 9)..." << std::endl;

    try {
        eval.set_expression(expr);
    } catch (const std::exception& e) {
        // If the compiler has checks, it would throw here.
        std::cout << "Compiler threw: " << e.what() << std::endl;
        std::cout << "\n[PASS] FPU stack overflow was detected at compile time.\n";
        return 0;
    }

    std::cout << "Evaluating..." << std::endl;
    double result = eval.evaluate();

    std::cout << "Result: " << result << std::endl;
    std::cout << "Expected: " << val << std::endl;

    // Check for corruption (NaN or wrong value)
    if (std::isnan(result) || std::abs(result - val) > 0.001) {
        std::cout << "\n[FAIL] The result is incorrect! FPU stack overflow occurred.\n";
        return 1; // Non-zero return code indicates failure
    } else {
        std::cout << "\n[PASS] The result is correct (Unexpectedly).\n";
        return 0;
    }
}

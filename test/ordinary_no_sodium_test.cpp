#include "mexce.h"

#include <cmath>
#include <iostream>


int main()
{
    double value = 3.25;
    mexce::evaluator evaluator;
    evaluator.bind(value, "value");
    evaluator.set_expression("value*2");
    if (std::abs(evaluator.evaluate() - 6.5) > 1e-12) {
        std::cerr << "FAIL: ordinary evaluator result mismatch\n";
        return 1;
    }
    std::cout << "Ordinary no-sodium test passed\n";
    return 0;
}

#include <mexce.h>

#include <cmath>
#include <iostream>


int main()
{
    mexce::evaluator eval;
    double x = 0.5;
    eval.bind(x, "x");
    eval.set_expression("sin(x) + 1");
    const double result   = eval.evaluate();
    const double expected = std::sin(x) + 1.0;
    std::cout << "Result: " << result << '\n';
    return std::isfinite(result) && std::abs(result - expected) < 1e-12 ? 0 : 1;
}

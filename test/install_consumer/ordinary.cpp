#include <mexce.h>

#include <cmath>


int main()
{
    double value = 0.5;
    mexce::evaluator evaluator;
    evaluator.bind(value, "value");
    evaluator.set_expression("sin(value)+1");
    const double result   = evaluator.evaluate();
    const double expected = std::sin(value) + 1.0;
    return std::isfinite(result) && std::abs(result - expected) < 1e-12 ? 0 : 1;
}

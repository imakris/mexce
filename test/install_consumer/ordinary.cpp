#include <mexce.h>

#include <cmath>


int main()
{
    double value = 0.5;
    mexce::evaluator evaluator;
    evaluator.bind(value, "value");
    evaluator.set_expression("sin(value)+1");
    return std::isfinite(evaluator.evaluate()) ? 0 : 1;
}

#include <mexce_protected_encoder.h>

#include <sodium.h>

#include <cmath>
#include <utility>


int main()
{
    if (sodium_init() < 0) {
        return 1;
    }

    double value = 2.0;
    mexce::evaluator evaluator;
    evaluator.bind_protected(value, 0);
    auto bundle = mexce::encode_protected_expression(
        "value+1",
        {{"value", 0}},
        mexce::Protected_math_mode::STRICT);
    evaluator.set_protected_expression(
        bundle.program.data(), bundle.program.size(), std::move(bundle.key));
    return std::abs(evaluator.evaluate() - 3.0) < 1e-12 ? 0 : 1;
}

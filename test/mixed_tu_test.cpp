#include "mexce.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>


mexce::evaluator* make_ordinary_evaluator();
double evaluate_on_ordinary_side(mexce::evaluator* evaluator);
void destroy_on_ordinary_side(mexce::evaluator* evaluator);
size_t ordinary_evaluator_size();

void load_on_protected_side(
    mexce::evaluator* evaluator,
    const uint8_t* program,
    size_t program_size,
    const uint8_t* key_bytes,
    size_t key_size);
size_t protected_evaluator_size();


int main()
{
    std::ifstream input(MEXCE_PROTECTED_FIXTURE_PATH, std::ios::binary);
    const std::vector<uint8_t> program(
        (std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
    if (program.empty()) {
        std::cerr << "FAIL: protected fixture could not be read\n";
        return 1;
    }

    std::array<uint8_t, 32> key;
    for (size_t i = 0; i < key.size(); ++i) {
        key[i] = static_cast<uint8_t>(i);
    }

    mexce::evaluator* evaluator = make_ordinary_evaluator();
    double x = 4.0;
    double y = 1.0;
    evaluator->bind_protected(x, 0);
    evaluator->bind_protected(y, 1);
    load_on_protected_side(
        evaluator,
        program.data(),
        program.size(),
        key.data(),
        key.size());

    const bool valid = ordinary_evaluator_size() == protected_evaluator_size() &&
        std::abs(evaluate_on_ordinary_side(evaluator) - 11.0) < 1e-12;
    destroy_on_ordinary_side(evaluator);
    if (!valid) {
        std::cerr << "FAIL: mixed translation-unit evaluator mismatch\n";
        return 1;
    }
    std::cout << "Mixed translation-unit evaluator test passed\n";
    return 0;
}

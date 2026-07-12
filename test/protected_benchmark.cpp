#include "mexce_protected_encoder.h"

#include <sodium.h>

#ifdef _WIN32
#include <Psapi.h>
#else
#include <sys/resource.h>
#endif

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>


namespace {


uint64_t peak_working_set_bytes()
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS counters = {};
    counters.cb = sizeof(counters);
    if (!GetProcessMemoryInfo(
            GetCurrentProcess(), &counters, sizeof(counters)))
    {
        return 0;
    }
    return static_cast<uint64_t>(counters.PeakWorkingSetSize);
#else
    struct rusage usage = {};
    if (getrusage(RUSAGE_SELF, &usage) != 0) {
        return 0;
    }
    return static_cast<uint64_t>(usage.ru_maxrss) * 1024;
#endif
}


template<typename Function>
uint64_t elapsed_nanoseconds(Function function)
{
    const auto start = std::chrono::steady_clock::now();
    function();
    const auto end = std::chrono::steady_clock::now();
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
}


int matched_diagnostic()
{
    constexpr int k_compile_iterations = 25;
    constexpr int k_evaluate_iterations = 10000;
    const std::string expression = "sin(x)+x*y+pow(y,3)-log(x+2)";
    const std::vector<mexce::Protected_binding> bindings = {{"x", 0}, {"y", 1}};
    double x = 0.75;
    double y = 1.25;

    mexce::evaluator clear;
    clear.bind(x, "x", y, "y");
    const uint64_t clear_compile_ns = elapsed_nanoseconds([&] {
        for (int i = 0; i < k_compile_iterations; ++i) {
            clear.set_expression(expression);
        }
    });

    std::vector<mexce::Protected_expression_bundle> bundles;
    bundles.reserve(k_compile_iterations);
    for (int i = 0; i < k_compile_iterations; ++i) {
        bundles.push_back(mexce::encode_protected_expression(
            expression, bindings, mexce::Protected_math_mode::STRICT));
    }

    mexce::evaluator protected_evaluator;
    protected_evaluator.bind_protected(x, 0);
    protected_evaluator.bind_protected(y, 1);
    const uint64_t protected_compile_ns = elapsed_nanoseconds([&] {
        for (auto& bundle : bundles) {
            protected_evaluator.set_protected_expression(
                bundle.program.data(),
                bundle.program.size(),
                std::move(bundle.key));
        }
    });

    volatile double clear_result = 0.0;
    const uint64_t clear_evaluate_ns = elapsed_nanoseconds([&] {
        for (int i = 0; i < k_evaluate_iterations; ++i) {
            clear_result = clear.evaluate();
        }
    });
    volatile double protected_result = 0.0;
    const uint64_t protected_evaluate_ns = elapsed_nanoseconds([&] {
        for (int i = 0; i < k_evaluate_iterations; ++i) {
            protected_result = protected_evaluator.evaluate();
        }
    });

    std::cout
        << "{\"mode\":\"matched\",\"iterations\":" << k_compile_iterations
        << ",\"clear_compile_ns\":" << clear_compile_ns
        << ",\"protected_compile_ns\":" << protected_compile_ns
        << ",\"clear_evaluate_ns\":" << clear_evaluate_ns
        << ",\"protected_evaluate_ns\":" << protected_evaluate_ns
        << ",\"backend\":"
        << static_cast<int>(protected_evaluator.get_backend())
        << ",\"peak_working_set_bytes\":" << peak_working_set_bytes()
        << ",\"correct\":" << (clear_result == protected_result ? "true" : "false")
        << "}\n";
    return clear_result == protected_result ? 0 : 1;
}


std::string maximum_expression()
{
    constexpr size_t k_literal_count = 8191;
    std::string expression;
    expression.reserve(k_literal_count * 2);
    for (size_t i = 0; i < k_literal_count; ++i) {
        if (i != 0) {
            expression += '+';
        }
        expression += '1';
    }
    return expression;
}


int resource_diagnostic(bool late_invalid)
{
    auto bundle = mexce::encode_protected_expression(
        maximum_expression(), {}, mexce::Protected_math_mode::STRICT);
    if (late_invalid) {
        bundle.program.back() ^= 0x01;
    }

    mexce::evaluator evaluator;
    bool accepted = false;
    const uint64_t elapsed_ns = elapsed_nanoseconds([&] {
        try {
            evaluator.set_protected_expression(
                bundle.program.data(), bundle.program.size(), std::move(bundle.key));
            accepted = true;
        }
        catch (const mexce::Protected_expression_error&) {
        }
    });

    const bool correct = accepted != late_invalid;
    std::cout
        << "{\"mode\":\"" << (late_invalid ? "late_invalid" : "maximum_valid")
        << "\",\"artifact_bytes\":" << bundle.program.size()
        << ",\"elapsed_ns\":" << elapsed_ns
        << ",\"peak_working_set_bytes\":" << peak_working_set_bytes()
        << ",\"accepted\":" << (accepted ? "true" : "false")
        << ",\"correct\":" << (correct ? "true" : "false")
        << "}\n";
    return correct ? 0 : 1;
}


} // namespace


int main(int argc, char** argv)
{
    if (sodium_init() < 0) {
        std::cerr << "protected benchmark requires libsodium 1.0.22\n";
        return 1;
    }
    if (argc == 1) {
        return matched_diagnostic();
    }
    const std::string mode = argv[1];
    if (mode == "maximum-valid") {
        return resource_diagnostic(false);
    }
    if (mode == "late-invalid") {
        return resource_diagnostic(true);
    }
    std::cerr << "usage: protected_benchmark [maximum-valid|late-invalid]\n";
    return 1;
}

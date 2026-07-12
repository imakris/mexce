#include "mexce_protected_encoder.h"

#include <sodium.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifdef max
#undef max
#endif


namespace {


const char* g_fixture_path = MEXCE_PROTECTED_FIXTURE_PATH;


struct Test_suite
{
    std::vector<std::string> m_failures;

    void expect(bool value, const std::string& name)
    {
        if (!value) {
            m_failures.push_back(name);
        }
    }

    void expect_near(
        double actual,
        double expected,
        const std::string& name)
    {
        const double scale = std::max(1.0, std::abs(expected));
        expect(std::abs(actual - expected) <= 1e-10 * scale, name);
    }

    template<typename Function>
    void expect_error(
        const std::string& name,
        mexce::Protected_expression_error_category category,
        Function function)
    {
        try {
            function();
            m_failures.push_back(name + ": expected exception");
        }
        catch (const mexce::Protected_expression_error& error) {
            if (error.category() != category) {
                m_failures.push_back(name + ": wrong category");
            }
        }
        catch (const std::exception& error) {
            m_failures.push_back(name + ": unexpected exception: " + error.what());
        }
    }

    template<typename Function>
    void expect_logic_error(
        const std::string& name,
        const char* message,
        Function function)
    {
        try {
            function();
            m_failures.push_back(name + ": expected exception");
        }
        catch (const std::logic_error& error) {
            if (std::string(error.what()) != message) {
                m_failures.push_back(name + ": wrong message");
            }
        }
        catch (const std::exception& error) {
            m_failures.push_back(name + ": unexpected exception: " + error.what());
        }
    }
};


std::vector<uint8_t> read_fixture()
{
    std::ifstream input(g_fixture_path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("protected fixture could not be opened");
    }
    return std::vector<uint8_t>(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}


mexce::Protected_expression_key fixture_key()
{
    std::array<uint8_t, 32> bytes;
    for (size_t i = 0; i < bytes.size(); ++i) {
        bytes[i] = static_cast<uint8_t>(i);
    }
    return mexce::Protected_expression_key::from_bytes(bytes.data(), bytes.size());
}


void load(
    mexce::evaluator& evaluator,
    mexce::Protected_expression_bundle bundle)
{
    evaluator.set_protected_expression(
        bundle.program.data(), bundle.program.size(), std::move(bundle.key));
}


void test_independent_fixture(Test_suite& suite)
{
    double x = 4.0;
    double y = 1.0;
    mexce::evaluator evaluator;
    evaluator.bind_protected(x, 0);
    evaluator.bind_protected(y, 1);
    const auto program = read_fixture();
    evaluator.set_protected_expression(
        program.data(), program.size(), fixture_key());

    suite.expect_near(evaluator.evaluate(), 11.0, "independent_fixture_value");
    suite.expect(
        evaluator.get_backend() != mexce::backend_type::none,
        "independent_fixture_backend");
}


void test_operation_parity(Test_suite& suite)
{
    struct operation_case_t
    {
        const char* expression;
        bool        uses_y;
    };

    const operation_case_t cases[] = {
        {"add(x,y)", true},   {"sub(x,y)", true},   {"mul(x,y)", true},
        {"div(x,y)", true},   {"neg(x)", false},    {"pow(x,y)", true},
        {"sin(x)", false},    {"cos(x)", false},    {"tan(x)", false},
        {"abs(x)", false},    {"sign(x)", false},   {"signp(x)", false},
        {"expn(x)", false},   {"sfc(x)", false},    {"sqrt(x)", false},
        {"exp(x)", false},    {"lt(x,y)", true},    {"gt(x,y)", true},
        {"le(x,y)", true},    {"ge(x,y)", true},    {"eq(x,y)", true},
        {"ne(x,y)", true},    {"log(x)", false},    {"log2(x)", false},
        {"log10(x)", false},  {"logb(x,y)", true},  {"ylog2(x,y)", true},
        {"max(x,y)", true},   {"min(x,y)", true},   {"floor(x)", false},
        {"ceil(x)", false},   {"round(x)", false},  {"int(x)", false},
        {"trunc(x)", false},  {"mod(x,y)", true},   {"bnd(x,y)", true},
        {"bias(x,y)", true},  {"gain(x,y)", true},
    };

    double x = 0.25;
    double y = 0.75;
    for (const auto& test_case : cases) {
        mexce::evaluator clear;
        clear.bind(x, "x", y, "y");
        clear.set_expression(test_case.expression);

        std::vector<mexce::Protected_binding> bindings;
        bindings.push_back({"x", 0});
        if (test_case.uses_y) {
            bindings.push_back({"y", 1});
        }

        mexce::evaluator protected_evaluator;
        protected_evaluator.bind_protected(x, 0);
        protected_evaluator.bind_protected(y, 1);
        load(
            protected_evaluator,
            mexce::encode_protected_expression(
                test_case.expression,
                bindings,
                mexce::Protected_math_mode::STRICT));
        suite.expect_near(
            protected_evaluator.evaluate(),
            clear.evaluate(),
            std::string("operation_parity_") + test_case.expression);
    }
}


void test_binding_lifecycle(Test_suite& suite)
{
    double first = 3.0;
    double replacement = 8.0;
    double extra = 5.0;
    mexce::evaluator evaluator;
    evaluator.bind(first, "x");
    evaluator.bind_protected(first, 0);
    evaluator.bind_protected(extra, 7);
    evaluator.bind_protected(replacement, 7);
    load(
        evaluator,
        mexce::encode_protected_expression(
            "x+1", {{"x", 0}}, mexce::Protected_math_mode::STRICT));

    suite.expect_near(evaluator.evaluate(), 4.0, "binding_initial_value");
    suite.expect_error(
        "referenced_rebind_rejected",
        mexce::Protected_expression_error_category::INVALID_ARGUMENT,
        [&] { evaluator.bind_protected(replacement, 0); });
    suite.expect_near(evaluator.evaluate(), 4.0, "rejected_rebind_unchanged");

    evaluator.unbind_protected(0);
    suite.expect_logic_error(
        "referenced_unbind_empties_evaluator",
        "No expression has been compiled.",
        [&] { (void)evaluator.evaluate(); });
    suite.expect(
        evaluator.get_backend() == mexce::backend_type::none,
        "referenced_unbind_backend_empty");
    evaluator.set_expression("x+2");
    suite.expect_near(evaluator.evaluate(), 5.0, "clear_binding_map_is_independent");
    suite.expect_error(
        "unknown_unbind_rejected",
        mexce::Protected_expression_error_category::INVALID_ARGUMENT,
        [&] { evaluator.unbind_protected(0); });

    evaluator.bind_protected(first, 0);
    load(
        evaluator,
        mexce::encode_protected_expression(
            "x+1", {{"x", 0}}, mexce::Protected_math_mode::STRICT));
    evaluator.unbind_all_protected();
    suite.expect(
        evaluator.get_backend() == mexce::backend_type::none,
        "unbind_all_referenced_empties");
}


void test_binding_types_and_missing_binding(Test_suite& suite)
{
    double d = 1.5;
    float f = 2.5f;
    int16_t i16 = 3;
    int32_t i32 = 4;
    int64_t i64 = 5;
    mexce::evaluator evaluator;
    evaluator.bind_protected(d, 0);
    evaluator.bind_protected(f, 1);
    evaluator.bind_protected(i16, 2);
    evaluator.bind_protected(i32, 3);
    evaluator.bind_protected(i64, 4);
    load(
        evaluator,
        mexce::encode_protected_expression(
            "d+f+i16+i32+i64",
            {{"d", 0}, {"f", 1}, {"i16", 2}, {"i32", 3}, {"i64", 4}},
            mexce::Protected_math_mode::STRICT));
    suite.expect_near(evaluator.evaluate(), 16.0, "all_binding_types");

    mexce::evaluator missing;
    missing.bind_protected(f, 1);
    const auto program = read_fixture();
    try {
        missing.set_protected_expression(
            program.data(), program.size(), fixture_key());
        suite.m_failures.push_back("missing_binding: expected exception");
    }
    catch (const mexce::Protected_expression_error& error) {
        suite.expect(
            error.category() == mexce::Protected_expression_error_category::MISSING_BINDING &&
            error.has_record_index() && error.record_index() == 1,
            "missing_binding_category_and_index");
    }
    suite.expect(
        missing.get_backend() == mexce::backend_type::none,
        "missing_binding_leaves_empty");
}


void test_options_and_introspection(Test_suite& suite)
{
    double infinity = std::numeric_limits<double>::infinity();

    mexce::evaluator strict;
    strict.enable_fast_math();
    strict.bind_protected(infinity, 0);
    load(
        strict,
        mexce::encode_protected_expression(
            "x-x", {{"x", 0}}, mexce::Protected_math_mode::STRICT));
    suite.expect(std::isnan(strict.evaluate()), "authenticated_strict_precedence");
    suite.expect(strict.get_options().fast_math, "strict_does_not_mutate_stored_options");

    double finite = 7.0;
    mexce::evaluator fast;
    fast.bind_protected(finite, 0);
    load(
        fast,
        mexce::encode_protected_expression(
            "x-x", {{"x", 0}}, mexce::Protected_math_mode::FAST));
    suite.expect_near(fast.evaluate(), 0.0, "authenticated_fast_precedence");
    suite.expect(!fast.get_options().fast_math, "fast_does_not_mutate_stored_options");

    suite.expect_error(
        "optimized_introspection_disabled",
        mexce::Protected_expression_error_category::INTROSPECTION_DISABLED,
        [&] { (void)fast.get_optimized_expression(); });
    suite.expect_error(
        "byte_introspection_disabled",
        mexce::Protected_expression_error_category::INTROSPECTION_DISABLED,
        [&] { (void)fast.get_byte_representation(); });

    fast.bind(finite, "x");
    fast.set_expression("x+1");
    suite.expect(!fast.get_optimized_expression().empty(), "clear_restores_introspection");

    mexce::evaluator x87;
    x87.use_x87_backend();
    x87.bind_protected(finite, 0);
    load(
        x87,
        mexce::encode_protected_expression(
            "x+1", {{"x", 0}}, mexce::Protected_math_mode::STRICT));
    suite.expect(x87.get_backend() == mexce::backend_type::x87, "prefer_x87_preserved");
    suite.expect(x87.get_options().prefer_x87, "prefer_x87_option_unchanged");
    suite.expect_near(x87.evaluate(), 8.0, "prefer_x87_result");
}


void test_runtime_libm_selection(Test_suite& suite)
{
    struct libm_case_t
    {
        const char* expression;
        bool mexce::options::* option;
        bool uses_y;
        bool libm_uses_sse2;
    };

    const libm_case_t cases[] = {
        {"sin(x)", &mexce::options::use_libm_sin, false, true},
        {"cos(x)", &mexce::options::use_libm_cos, false, true},
        {"tan(x)", &mexce::options::use_libm_tan, false, true},
        {"exp(x)", &mexce::options::use_libm_exp, false, true},
        {"log(x)", &mexce::options::use_libm_log, false, true},
        {"log10(x)", &mexce::options::use_libm_log10, false, true},
        {"log2(x)", &mexce::options::use_libm_log2, false, true},
        {"logb(x,y)", &mexce::options::use_libm_logb, true, true},
        {"ylog2(x,y)", &mexce::options::use_libm_ylog2, true, false},
        {"pow(x,y)", &mexce::options::use_libm_generic_pow, true, false},
    };

    double x = 1.25;
    double y = 0.75;
    for (const auto& test_case : cases) {
        mexce::options libm_options;
        libm_options.set_use_libm(true);
        mexce::evaluator libm;
        libm.set_options(libm_options);
        libm.bind(x, "x", y, "y");
        libm.set_expression(test_case.expression);
        const double expected = libm.evaluate();
        const std::string libm_code = libm.get_byte_representation();
        suite.expect(std::isfinite(expected),
            std::string("libm_finite_") + test_case.expression);
#ifdef MEXCE_64
        suite.expect(
            libm.get_backend() == (test_case.libm_uses_sse2
                ? mexce::backend_type::sse2
                : mexce::backend_type::x87),
            std::string("libm_backend_") + test_case.expression);
#endif

        mexce::options inline_options = libm_options;
        inline_options.*(test_case.option) = false;
        mexce::evaluator clear_inline;
        clear_inline.set_options(inline_options);
        clear_inline.bind(x, "x", y, "y");
        clear_inline.set_expression(test_case.expression);
        suite.expect(clear_inline.get_backend() == mexce::backend_type::x87,
            std::string("inline_backend_") + test_case.expression);
        suite.expect_near(clear_inline.evaluate(), expected,
            std::string("inline_result_") + test_case.expression);
        suite.expect(clear_inline.get_byte_representation() != libm_code,
            std::string("runtime_selection_") + test_case.expression);

        std::vector<mexce::Protected_binding> bindings = {{"x", 0}};
        if (test_case.uses_y) {
            bindings.push_back({"y", 1});
        }
        mexce::evaluator protected_libm;
        protected_libm.set_options(libm_options);
        protected_libm.bind_protected(x, 0);
        protected_libm.bind_protected(y, 1);
        load(
            protected_libm,
            mexce::encode_protected_expression(
                test_case.expression,
                bindings,
                mexce::Protected_math_mode::STRICT));
        suite.expect_near(protected_libm.evaluate(), expected,
            std::string("protected_libm_result_") + test_case.expression);
#ifdef MEXCE_64
        suite.expect(
            protected_libm.get_backend() == (test_case.libm_uses_sse2
                ? mexce::backend_type::sse2
                : mexce::backend_type::x87),
            std::string("protected_libm_backend_") + test_case.expression);
#endif
        suite.expect(
            protected_libm.get_options().*(test_case.option),
            std::string("protected_libm_option_") + test_case.expression);

        mexce::evaluator protected_inline;
        protected_inline.set_options(inline_options);
        protected_inline.bind_protected(x, 0);
        protected_inline.bind_protected(y, 1);
        load(
            protected_inline,
            mexce::encode_protected_expression(
                test_case.expression,
                bindings,
                mexce::Protected_math_mode::STRICT));
        suite.expect(protected_inline.get_backend() == mexce::backend_type::x87,
            std::string("protected_inline_backend_") + test_case.expression);
        suite.expect_near(protected_inline.evaluate(), expected,
            std::string("protected_inline_result_") + test_case.expression);
        suite.expect(
            !(protected_inline.get_options().*(test_case.option)),
            std::string("protected_inline_option_") + test_case.expression);
    }

    double negative_zero = -0.0;
    for (const bool use_libm : {false, true}) {
        mexce::options selected;
        selected.set_use_libm(use_libm);
        mexce::evaluator clear;
        clear.set_options(selected);
        clear.bind(negative_zero, "x");
        clear.set_expression("sin(x)");
        suite.expect(clear.evaluate() == 0.0 && std::signbit(clear.evaluate()),
            use_libm ? "libm_signed_zero" : "inline_signed_zero");

        mexce::evaluator protected_evaluator;
        protected_evaluator.set_options(selected);
        protected_evaluator.bind_protected(negative_zero, 0);
        load(
            protected_evaluator,
            mexce::encode_protected_expression(
                "sin(x)", {{"x", 0}}, mexce::Protected_math_mode::STRICT));
        const double protected_result = protected_evaluator.evaluate();
        suite.expect(protected_result == 0.0 && std::signbit(protected_result),
            use_libm ? "protected_libm_signed_zero" : "protected_inline_signed_zero");
    }
}


void test_failure_and_key_consumption(Test_suite& suite)
{
    double value = 2.0;
    mexce::evaluator evaluator;
    evaluator.bind_protected(value, 0);
    evaluator.set_expression("19.25");

    auto bundle = mexce::encode_protected_expression(
        "x+1", {{"x", 0}}, mexce::Protected_math_mode::STRICT);
    bundle.program.back() ^= 0x01;
    suite.expect_error(
        "late_authentication_failure",
        mexce::Protected_expression_error_category::AUTHENTICATION_FAILED,
        [&] {
            evaluator.set_protected_expression(
                bundle.program.data(), bundle.program.size(), std::move(bundle.key));
        });
    suite.expect(evaluator.get_backend() == mexce::backend_type::none, "failure_backend_empty");
    try {
        (void)evaluator.evaluate();
        suite.m_failures.push_back("failure_evaluate_empty: expected exception");
    }
    catch (const std::logic_error& error) {
        suite.expect(
            std::string(error.what()) == "No expression has been compiled.",
            "failure_evaluate_empty_message");
    }
    suite.expect_error(
        "transferred_key_is_empty",
        mexce::Protected_expression_error_category::INVALID_ARGUMENT,
        [&] { bundle.key.consume_bytes([](const uint8_t*, size_t) {}); });

    evaluator.bind(value, "value");
    evaluator.set_expression("value+1");
    auto native_failure = mexce::encode_protected_expression(
        "x+1", {{"x", 0}}, mexce::Protected_math_mode::STRICT);
    native_failure.program.back() ^= 0x01;
    suite.expect_error(
        "late_failure_replaces_native_state",
        mexce::Protected_expression_error_category::AUTHENTICATION_FAILED,
        [&] {
            evaluator.set_protected_expression(
                native_failure.program.data(),
                native_failure.program.size(),
                std::move(native_failure.key));
        });
    suite.expect(
        evaluator.get_backend() == mexce::backend_type::none,
        "late_failure_clears_native_backend");

    evaluator.enable_cse();
    auto cse_bundle = mexce::encode_protected_expression(
        "x+1", {{"x", 0}}, mexce::Protected_math_mode::STRICT);
    suite.expect_error(
        "protected_cse_rejected",
        mexce::Protected_expression_error_category::INVALID_ARGUMENT,
        [&] {
            evaluator.set_protected_expression(
                cse_bundle.program.data(),
                cse_bundle.program.size(),
                std::move(cse_bundle.key));
        });
    suite.expect(evaluator.get_options().enable_cse, "cse_rejection_preserves_option");
    suite.expect_error(
        "cse_rejection_consumes_key",
        mexce::Protected_expression_error_category::INVALID_ARGUMENT,
        [&] { cse_bundle.key.consume_bytes([](const uint8_t*, size_t) {}); });
}


void test_compilation_resource_failures(Test_suite& suite)
{
    double value = 2.0;
    mexce::evaluator evaluator;
    evaluator.bind_protected(value, 0);
    auto& faults = mexce::impl::protected_test_faults();

    faults.fail_executable_allocation = true;
    auto allocation_bundle = mexce::encode_protected_expression(
        "x+1", {{"x", 0}}, mexce::Protected_math_mode::STRICT);
    suite.expect_error(
        "executable_allocation_failure",
        mexce::Protected_expression_error_category::RESOURCE_FAILURE,
        [&] {
            evaluator.set_protected_expression(
                allocation_bundle.program.data(),
                allocation_bundle.program.size(),
                std::move(allocation_bundle.key));
        });
    faults.fail_executable_allocation = false;
    suite.expect(
        evaluator.get_backend() == mexce::backend_type::none,
        "allocation_failure_leaves_empty");

    faults.fail_executable_finalization = true;
    auto finalization_bundle = mexce::encode_protected_expression(
        "x+1", {{"x", 0}}, mexce::Protected_math_mode::STRICT);
    suite.expect_error(
        "executable_finalization_failure",
        mexce::Protected_expression_error_category::RESOURCE_FAILURE,
        [&] {
            evaluator.set_protected_expression(
                finalization_bundle.program.data(),
                finalization_bundle.program.size(),
                std::move(finalization_bundle.key));
        });
    faults.fail_executable_finalization = false;
    suite.expect(
        evaluator.get_backend() == mexce::backend_type::none,
        "finalization_failure_leaves_empty");
}


void test_fallback_and_wipes(Test_suite& suite)
{
    auto& observer = mexce::impl::protected_wipe_observer();
    double value = 1.25;
    const std::string polynomial =
        "((((((((((2.20*x+1.1)*x+9.9)*x+8.8)*x+7.7)*x+6.6)*x+5.5)*x+4.4)*x+3.3)*x+2.2)*x+1.1)";

    mexce::evaluator clear;
    clear.bind(value, "x");
    clear.set_expression(polynomial);
    const double expected = clear.evaluate();

    mexce::evaluator evaluator;
    evaluator.bind_protected(value, 0);
    load(
        evaluator,
        mexce::encode_protected_expression(
            polynomial, {{"x", 0}}, mexce::Protected_math_mode::STRICT));
    suite.expect(evaluator.get_backend() == mexce::backend_type::x87, "protected_fallback_backend");
    const double result = evaluator.evaluate();
    suite.expect(std::isfinite(result) && result == expected, "protected_fallback_exact_value");
    suite.expect(!evaluator.get_options().prefer_x87, "fallback_preserves_stored_preference");

    observer.reset();
    evaluator.set_expression("3.5");
    suite.expect_near(evaluator.evaluate(), 3.5, "protected_replacement_value");
    suite.expect(
        observer.observed_before(
            mexce::impl::Protected_wipe_context::EXECUTABLE,
            mexce::impl::Protected_wipe_context::FALLBACK_STORAGE),
        "fallback_executable_wiped_before_owner");
    suite.expect(
        observer.count(mexce::impl::Protected_wipe_context::CONSTANT_STORAGE) == 0,
        "fallback_wipe_not_satisfied_by_primary_storage");
    suite.expect(observer.all_observed_regions_are_zero(), "fallback_replacement_wipes_zero");

    observer.reset();
    mexce::evaluator primary;
    primary.bind_protected(value, 0);
    load(
        primary,
        mexce::encode_protected_expression(
            "x+1", {{"x", 0}}, mexce::Protected_math_mode::STRICT));
    suite.expect(
        observer.count(mexce::impl::Protected_wipe_context::FALLBACK_STORAGE) > 0,
        "unused_fallback_owner_wiped_during_load");
    suite.expect(
        observer.count(mexce::impl::Protected_wipe_context::EXECUTABLE) == 0,
        "unused_fallback_not_satisfied_by_executable_wipe");

    observer.reset();
    primary.set_expression("4.5");
    suite.expect(
        observer.observed_before(
            mexce::impl::Protected_wipe_context::EXECUTABLE,
            mexce::impl::Protected_wipe_context::CONSTANT_STORAGE),
        "primary_executable_wiped_before_constants");
    suite.expect(
        observer.observed_before(
            mexce::impl::Protected_wipe_context::EXECUTABLE,
            mexce::impl::Protected_wipe_context::OPTIMIZER_STATE),
        "primary_executable_wiped_before_optimizer");
    suite.expect(
        observer.count(mexce::impl::Protected_wipe_context::OPTIONS_STORAGE) > 0,
        "primary_options_wiped_independently");
    suite.expect(observer.all_observed_regions_are_zero(), "primary_replacement_wipes_zero");

    observer.reset();
    {
        mexce::evaluator constant;
        constant.bind_protected(value, 9);
        load(
            constant,
            mexce::encode_protected_expression(
                "3.5", {}, mexce::Protected_math_mode::STRICT));
        constant.unbind_all_protected();
        suite.expect_near(constant.evaluate(), 3.5, "protected_constant_value");
        observer.reset();
    }
    suite.expect(
        observer.count(mexce::impl::Protected_wipe_context::CONSTANT_STORAGE) > 0,
        "constant_destruction_wipe_observed");
    suite.expect(
        observer.count(mexce::impl::Protected_wipe_context::EXECUTABLE) == 0,
        "constant_destruction_has_no_executable_wipe");
    suite.expect(observer.all_observed_regions_are_zero(), "constant_destruction_wipes_zero");

    observer.reset();
    {
        mexce::evaluator destruction;
        destruction.bind_protected(value, 0);
        load(
            destruction,
            mexce::encode_protected_expression(
                "x+2", {{"x", 0}}, mexce::Protected_math_mode::STRICT));
        observer.reset();
    }
    suite.expect(
        observer.observed_before(
            mexce::impl::Protected_wipe_context::EXECUTABLE,
            mexce::impl::Protected_wipe_context::CONSTANT_STORAGE),
        "destruction_executable_wiped_before_constants");
    suite.expect(observer.all_observed_regions_are_zero(), "destruction_wipes_zero");
}


} // namespace


int main(int argc, char** argv)
{
    if (argc > 2) {
        std::cerr << "usage: protected_runtime_tests [fixture]\n";
        return 1;
    }
    if (argc == 2) {
        g_fixture_path = argv[1];
    }
    if (sodium_init() < 0) {
        std::cerr << "protected runtime tests require libsodium 1.0.22\n";
        return 1;
    }

    Test_suite suite;
    test_independent_fixture(suite);
    test_operation_parity(suite);
    test_binding_lifecycle(suite);
    test_binding_types_and_missing_binding(suite);
    test_options_and_introspection(suite);
    test_runtime_libm_selection(suite);
    test_failure_and_key_consumption(suite);
    test_compilation_resource_failures(suite);
    test_fallback_and_wipes(suite);

    for (const auto& failure : suite.m_failures) {
        std::cerr << "FAIL: " << failure << '\n';
    }
    if (!suite.m_failures.empty()) {
        std::cerr << suite.m_failures.size() << " protected runtime test(s) failed\n";
        return 1;
    }

    std::cout << "All protected runtime tests passed\n";
    return 0;
}

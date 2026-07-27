#ifdef __linux__
#include <cerrno>
#include <sys/mman.h>

namespace mexce_test_overrides {

inline bool& force_mmap_failure_flag()
{
    static bool flag = false;
    return flag;
}

inline bool& force_mprotect_failure_flag()
{
    static bool flag = false;
    return flag;
}

} // namespace mexce_test_overrides

extern "C" void* mexce_test_override_mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    if (mexce_test_overrides::force_mmap_failure_flag()) {
        errno = ENOMEM;
        return MAP_FAILED;
    }
#undef mmap
    void* result = ::mmap(addr, length, prot, flags, fd, offset);
#define mmap mexce_test_override_mmap
    return result;
}

extern "C" int mexce_test_override_mprotect(void* addr, size_t len, int prot)
{
    if (mexce_test_overrides::force_mprotect_failure_flag()) {
        errno = EACCES;
        return -1;
    }
#undef mprotect
    int rc = ::mprotect(addr, len, prot);
#define mprotect mexce_test_override_mprotect
    return rc;
}

#define mmap mexce_test_override_mmap
#define mprotect mexce_test_override_mprotect
#endif

// Qt defines emit as an empty macro; the public header must remain includable.
#define emit
#include "mexce.h"
#undef emit

#ifdef __linux__
#undef mmap
#undef mprotect
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// Define M_PI if it's not available (e.g., on MSVC)
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {

struct ScopedTestFlag {
    bool& flag;
    bool previous;

    ScopedTestFlag(bool& flag_ref, bool value)
        : flag(flag_ref), previous(flag_ref)
    {
        flag = value;
    }

    ~ScopedTestFlag()
    {
        flag = previous;
    }
};

struct TestSuite {
    std::vector<std::string> failures;

    void expect_near(const std::string& name, double actual, double expected, double tol = 1e-9) {
        double scale = (std::max)(1.0, (std::max)(std::abs(actual), std::abs(expected)));
        if (std::isnan(actual) || std::abs(actual - expected) > tol * scale) {
            std::ostringstream oss;
            oss << name << " failed: expected " << std::setprecision(17) << expected
                << ", got " << actual;
            failures.push_back(oss.str());
        }
    }

    void expect_equal(const std::string& name, double actual, double expected) {
        if (actual != expected) {
            std::ostringstream oss;
            oss << name << " failed: expected " << expected << ", got " << actual;
            failures.push_back(oss.str());
        }
    }

    void expect_true(const std::string& name, bool value) {
        if (!value) {
            failures.push_back(name + " failed");
        }
    }

    template <typename Exception, typename Func>
    void expect_throw(const std::string& name, Func&& func, const std::string& expected_message = std::string()) {
        try {
            func();
            failures.push_back(name + " failed: expected exception");
        }
        catch (const Exception& ex) {
            if (!expected_message.empty() && expected_message != ex.what()) {
                std::ostringstream oss;
                oss << name << " failed: expected message \"" << expected_message
                    << "\", got \"" << ex.what() << "\"";
                failures.push_back(oss.str());
            }
        }
        catch (const std::exception& ex) {
            std::ostringstream oss;
            oss << name << " failed: unexpected exception type (" << ex.what() << ")";
            failures.push_back(oss.str());
        }
        catch (...) {
            failures.push_back(name + " failed: unexpected non-standard exception");
        }
    }
};

void test_bias_and_gain(TestSuite& suite) {
    mexce::evaluator eval;
    double x = 0.25;
    double a = 0.7;
    eval.bind(x, "x", a, "a");

    eval.set_expression("bias(x, a)");
    auto bias_expected = [&](double val) {
        return val / (((1.0 / a) - 2.0) * (1.0 - val) + 1.0);
    };
    suite.expect_near("bias(x<0.5)", eval.evaluate(), bias_expected(x));

    x = 0.75;
    suite.expect_near("bias(x>=0.5)", eval.evaluate(), bias_expected(x));
    
    eval.set_expression("gain(x, a)");
    auto gain_expected = [&](double value, double gain_a) {
        if (value < 0.5) {
            return value / (((1.0 / gain_a) - 2.0) * (1.0 - 2.0 * value) + 1.0);
        } else {
            return (((1.0 / gain_a) - 2.0) * (1.0 - 2.0 * value) - value) /
                   (((1.0 / gain_a) - 2.0) * (1.0 - 2.0 * value) - 1.0);
        }
    };

    x = 0.25;
    suite.expect_near("gain(x<0.5)", eval.evaluate(), gain_expected(x, a));

    x = 0.75;
    suite.expect_near("gain(x>=0.5)", eval.evaluate(), gain_expected(x, a));
}

void test_periodic_and_mod(TestSuite& suite) {
    mexce::evaluator eval;
    double x = 7.5;
    double period = 2.0;
    eval.bind(x, "x", period, "p");

    eval.set_expression("bnd(x, p)");
    auto wrap = [&](double value) {
        double r = std::fmod(value, period);
        if (r < 0.0) {
            r += period;
        }
        return r;
    };
    suite.expect_near("bnd(positive)", eval.evaluate(), wrap(x));

    x = -1.3;
    suite.expect_near("bnd(negative)", eval.evaluate(), wrap(x));

    eval.set_expression("mod(x, p)");
    x = 5.5;
    suite.expect_near("mod(positive)", eval.evaluate(), std::fmod(x, period));

    x = -3.5;
    suite.expect_near("mod(negative)", eval.evaluate(), std::fmod(x, period));
}

void test_sign_and_comparisons(TestSuite& suite) {
    mexce::evaluator eval;
    double v = -3.0;
    eval.bind(v, "v");

    eval.set_expression("sign(v)");
    suite.expect_near("sign(negative)", eval.evaluate(), -1.0);
    v = 0.0;
    suite.expect_near("sign(zero)", eval.evaluate(), 0.0);
    v = 3.0;
    suite.expect_near("sign(positive)", eval.evaluate(), 1.0);

    v = 3.0;
    suite.expect_equal("lt(3, 2) is false", eval.evaluate("lt(v, 2)"), 0.0);
    suite.expect_equal("lt(3, 4) is true", eval.evaluate("lt(v, 4)"), 1.0);
    
    suite.expect_equal("le(3, 3) is true", eval.evaluate("le(v, 3)"), 1.0);
    suite.expect_equal("le(3, 4) is true", eval.evaluate("le(v, 4)"), 1.0);

    suite.expect_equal("gt(3, 2) is true", eval.evaluate("gt(v, 2)"), 1.0);
    suite.expect_equal("gt(3, 4) is false", eval.evaluate("gt(v, 4)"), 0.0);
    suite.expect_equal("infix gt(3 > 2) is true", eval.evaluate("v > 2"), 1.0);
    suite.expect_equal("infix gt(3 > 4) is false", eval.evaluate("v > 4"), 0.0);

    suite.expect_equal("ge(3, 3) is true", eval.evaluate("ge(v, 3)"), 1.0);
    suite.expect_equal("ge(3, 2) is true", eval.evaluate("ge(v, 2)"), 1.0);

    suite.expect_equal("eq(3, 3) is true", eval.evaluate("eq(v, 3)"), 1.0);
    suite.expect_equal("eq(3, 2) is false", eval.evaluate("eq(v, 2)"), 0.0);

    suite.expect_equal("ne(3, 2) is true", eval.evaluate("ne(v, 2)"), 1.0);
    suite.expect_equal("ne(3, 3) is false", eval.evaluate("ne(v, 3)"), 0.0);
}

void test_exponent_and_significand(TestSuite& suite) {
    mexce::evaluator eval;
    double value = 5.75;
    eval.bind(value, "v");
    
    eval.set_expression("expn(v)");
    int exponent;
    std::frexp(value, &exponent);
    suite.expect_equal("expn", eval.evaluate(), static_cast<double>(exponent - 1));

    eval.set_expression("sfc(v)");
    suite.expect_near("sfc", eval.evaluate(), value / std::pow(2.0, static_cast<double>(exponent - 1)));
}

void test_logs_and_powers(TestSuite& suite) {
    mexce::evaluator eval;
    double base = 3.0;
    double value = 9.0;
    eval.bind(base, "b", value, "v");

    eval.set_expression("logb(b, v)");
    suite.expect_near("log_base", eval.evaluate(), std::log(value) / std::log(base));

    eval.set_expression("ylog2(v, b)");
    suite.expect_near("ylog2", eval.evaluate(), value * std::log2(base));

    eval.set_expression("pow(b, 3)");
    suite.expect_near("pow_function", eval.evaluate(), std::pow(base, 3.0));

    eval.set_expression("b^2");
    suite.expect_near("pow_operator", eval.evaluate(), std::pow(base, 2.0));

    eval.set_expression("b**2");
    suite.expect_near("pow_operator_double_star", eval.evaluate(), std::pow(base, 2.0));

    eval.set_expression("-b**2");
    suite.expect_near("pow_operator_double_star_unary_base", eval.evaluate(), -std::pow(base, 2.0));

    eval.set_expression("b**-2");
    suite.expect_near("pow_operator_double_star_unary_exp", eval.evaluate(), std::pow(base, -2.0));

    eval.set_expression("(-b)**2");
    suite.expect_near("pow_operator_double_star_paren_neg_base", eval.evaluate(), std::pow(-base, 2.0));
}

void test_rounding_functions(TestSuite& suite) {
    mexce::evaluator eval;
    double value = 3.75;
    eval.bind(value, "v");

    eval.set_expression("floor(v)");
    suite.expect_near("floor", eval.evaluate(), std::floor(value));

    eval.set_expression("ceil(v)");
    suite.expect_near("ceil", eval.evaluate(), std::ceil(value));

    eval.set_expression("round(v)");
    suite.expect_near("round", eval.evaluate(), std::round(value));

    eval.set_expression("int(v)");
    suite.expect_near("int", eval.evaluate(), std::nearbyint(value));
}

void test_ieee_self_cancellation_default_mode(TestSuite& suite) {
    // Default mode should preserve IEEE-visible self-cancellation effects.
    // fast_math remains the opt-in path for x-x -> 0 and x/x -> 1.
    double x = std::numeric_limits<double>::quiet_NaN();

    mexce::evaluator strict_eval;
    strict_eval.bind(x, "x");
    strict_eval.set_expression("x - x");
    suite.expect_true("default x-x with NaN stays NaN", std::isnan(strict_eval.evaluate()));

    x = 0.0;
    strict_eval.set_expression("x / x");
    suite.expect_true("default x/x with zero stays NaN", std::isnan(strict_eval.evaluate()));

    mexce::evaluator fast_eval;
    fast_eval.enable_fast_math();
    x = std::numeric_limits<double>::quiet_NaN();
    fast_eval.bind(x, "x");
    fast_eval.set_expression("x - x");
    suite.expect_equal("fast_math x-x folds to zero", fast_eval.evaluate(), 0.0);

    x = 0.0;
    fast_eval.set_expression("x / x");
    suite.expect_equal("fast_math x/x folds to one", fast_eval.evaluate(), 1.0);
}

void test_min_max_and_arithmetic(TestSuite& suite) {
    mexce::evaluator eval;
    double x = -1.5;
    double y = 4.0;
    eval.bind(x, "x", y, "y");

    eval.set_expression("min(x, y)");
    suite.expect_near("min", eval.evaluate(), (std::min)(x, y));

    eval.set_expression("max(x, y)");
    suite.expect_near("max", eval.evaluate(), (std::max)(x, y));

    eval.set_expression("add(x, y) - sub(y, x) + mul(x, y) / div(y, x)");
    double expected = (x + y) - (y - x) + (x * y) / (y / x);
    suite.expect_near("arithmetic_functions", eval.evaluate(), expected);

    eval.set_expression("neg(x)");
    suite.expect_near("neg", eval.evaluate(), -x);

    eval.set_expression("abs(x)");
    suite.expect_near("abs", eval.evaluate(), std::abs(x));
}

void test_constants_and_single_shot(TestSuite& suite) {
    mexce::evaluator eval;

    suite.expect_near("single_shot_eval", mexce::evaluator().evaluate("sin(pi/6)+cos(pi/3)"), 1.0);

    double v = 2.0;
    eval.bind(v, "v");

    eval.set_expression("exp(v)");
    suite.expect_near("exp", eval.evaluate(), std::exp(v));

    eval.set_expression("ln(v)");
    suite.expect_near("ln", eval.evaluate(), std::log(v));
    suite.expect_true(
        "ln_introspection_spelling",
        eval.get_optimized_expression().find("ln") != std::string::npos);

    eval.set_expression("log(v)");
    suite.expect_near("log", eval.evaluate(), std::log(v));
    suite.expect_true(
        "log_introspection_spelling",
        eval.get_optimized_expression().find("log") != std::string::npos);
}

void test_signed_zero_semantics(TestSuite& suite)
{
    mexce::evaluator eval;

    eval.set_expression("-0.0");
    suite.expect_true("source_unary_minus_signed_zero", !std::signbit(eval.evaluate()));

    eval.set_expression("neg(0.0)");
    suite.expect_true("explicit_neg_signed_zero", std::signbit(eval.evaluate()));
}

void test_pow_optimizer_special_cases(TestSuite& suite) {
    mexce::evaluator eval;
    double x = 9.0;
    eval.bind(x, "x");

    eval.set_expression("pow(x, 0.5)");
    suite.expect_near("pow_optimizer_sqrt", eval.evaluate(), std::sqrt(x));

    x = 4.0;
    eval.set_expression("pow(x, -0.5)");
    suite.expect_near("pow_optimizer_inverse_sqrt", eval.evaluate(), 1.0 / std::sqrt(x));

    x = 16.0;
    eval.set_expression("pow(x, 0)");
    suite.expect_near("pow_optimizer_zero_exponent", eval.evaluate(), 1.0);

    x = 5.0;
    eval.set_expression("pow(x, 1)");
    suite.expect_near("pow_optimizer_exponent_one", eval.evaluate(), x);

    x = 2.0;
    eval.set_expression("pow(x, 3.3)");
    suite.expect_near("pow_optimizer_generic_path", eval.evaluate(), std::pow(x, 3.3));

    x = 3.0;
    eval.set_expression("pow(x, 7)");
    suite.expect_near("pow_optimizer_diff_low_branch", eval.evaluate(), std::pow(x, 7.0));
}

void test_nested_pow_folding_sympy_semantics(TestSuite& suite) {
    mexce::evaluator eval;

    suite.expect_near("nested_pow_negative_sqrt_squared_constant",
        mexce::evaluator().evaluate("((-2)^0.5)^2"),
        -2.0);

    double x = -2.0;
    eval.bind(x, "x");
    eval.set_expression("((x^0.5)^2)");
    suite.expect_near("nested_pow_negative_sqrt_squared_variable", eval.evaluate(), x);
}

void test_helper_functions_and_element(TestSuite& suite) {
    using namespace mexce::impl;

    suite.expect_true("function_name_to_infix_operator_add", function_name_to_infix_operator("add") == "+");
    suite.expect_true("function_name_to_infix_operator_unknown", function_name_to_infix_operator("noop").empty());
    suite.expect_true("function_name_to_unary_operator_neg", function_name_to_unary_operator("neg") == "-");
    suite.expect_true("function_name_to_unary_operator_unknown", function_name_to_unary_operator("noop").empty());
    suite.expect_true("double_to_pretty_string", double_to_pretty_string(3.25) == "3.25");

    Element default_element;
    suite.expect_true("element_default_type", default_element.type == Element_type::CCONST);
    suite.expect_true("element_default_id", default_element.id == 0);

    double value = 4.0;
    auto variable = std::make_shared<Variable>(1, &value, "value", M32FP);
    auto constant = std::make_shared<Constant>(2, 3.0);
    auto add_function = std::make_shared<Function>(function_map().find("add")->second);

    elist_t elist;
    elist.push_back(Element(variable));
    elist.push_back(Element(constant));
    elist.push_back(Element(add_function));

    std::string elist_string = elist_to_string(elist);
    suite.expect_true("elist_to_string", elist_string == "(value+3)");

    suite.expect_true("get_ndt_float", get_ndt<float>() == M32FP);
    suite.expect_true("get_ndt_int16", get_ndt<int16_t>() == M16INT);
    suite.expect_true("get_ndt_int32", get_ndt<int32_t>() == M32INT);
    suite.expect_true("get_ndt_int64", get_ndt<int64_t>() == M64INT);
}

void test_binding_and_unbinding(TestSuite& suite) {
    mexce::evaluator eval;

    double x = 1.0;
    float f = 2.0f;
    int16_t i16 = 3;
    int32_t i32 = 4;
    int64_t i64 = 5;

    eval.bind(x, "x", f, "f", i16, "i16", i32, "i32", i64, "i64");

    eval.set_expression("x + f + i16 + i32 + i64");
    double expected = x + f + i16 + i32 + static_cast<double>(i64);
    suite.expect_near("multi_type_binding", eval.evaluate(), expected);

    eval.set_expression("i16");
    suite.expect_near("direct_int16_binding", eval.evaluate(), static_cast<double>(i16));

    eval.set_expression("i32");
    suite.expect_near("direct_int32_binding", eval.evaluate(), static_cast<double>(i32));

    eval.unbind_all();

    suite.expect_throw<mexce::mexce_parsing_exception>("unbind_all_removes_variables", [&] {
        eval.set_expression("x");
    }, "x is not a known constant, variable or function name");
    
    mexce::evaluator unbind_eval;
    double y = 2.5;
    unbind_eval.bind(y, "y");
    unbind_eval.unbind("y");
    suite.expect_throw<mexce::mexce_parsing_exception>("unbind_single_variable", [&] {
        unbind_eval.set_expression("y");
    }, "y is not a known constant, variable or function name");

    suite.expect_throw<std::logic_error>("unbind_empty_name", [&]() {
        unbind_eval.unbind("");
    }, "Variable name was an empty string");

    suite.expect_throw<std::logic_error>("unbind_unknown_variable", [&]() {
        unbind_eval.unbind("unknown");
    }, "Attempted to unbind an unknown variable");
}

void test_binding_name_conflicts(TestSuite& suite) {
    mexce::evaluator eval_function_name;
    double value = 1.0;

    suite.expect_throw<std::logic_error>(
        "bind_existing_function_name",
        [&]() { eval_function_name.bind(value, "sin"); },
        "Attempted to bind a variable, named as an existing function"
    );

    mexce::evaluator eval_constant_name;
    suite.expect_throw<std::logic_error>(
        "bind_existing_constant_name",
        [&]() { eval_constant_name.bind(value, "pi"); },
        "Attempted to bind a variable, named as an existing constant"
    );
}

void test_mexce_parsing_exception_class(TestSuite& suite) {
    mexce::mexce_parsing_exception ex("custom message", 3);
    suite.expect_true("mexce_parsing_exception_message", std::string(ex.what()) == "custom message");
}

void test_asmd_optimizer_branches(TestSuite& suite) {
    mexce::evaluator eval;
    float a = 2.0f;
    float b = 3.0f;
    eval.bind(a, "a", b, "b");

    eval.set_expression("a + b + 5");
    suite.expect_near("asmd_optimizer_add_constants", eval.evaluate(), a + b + 5.0);

    eval.set_expression("a * b * 2");
    suite.expect_near("asmd_optimizer_mul_constants", eval.evaluate(), a * b * 2.0);
}

void test_parsing_errors(TestSuite& suite) {
    suite.expect_throw<std::logic_error>("empty_expression", [] {
        mexce::evaluator().set_expression("");
    }, "Expected an expression");

    suite.expect_throw<mexce::mexce_parsing_exception>("leading_closing_parenthesis", [] {
        mexce::evaluator().set_expression(")");
    }, "Expected a \")\"");

    suite.expect_throw<mexce::mexce_parsing_exception>("unexpected_symbol_at_start", [] {
        mexce::evaluator().set_expression("@");
    }, "\"@\" not expected");

    {
        mexce::evaluator eval;
        double x = 1.0;
        eval.bind(x, "x");
        suite.expect_throw<mexce::mexce_parsing_exception>("unexpected_symbol_after_identifier", [&] {
            eval.set_expression("x@");
        }, "\"@\" not expected");
    }

    {
        mexce::evaluator eval;
        eval.set_expression(".5");
        suite.expect_near("leading_decimal_literal", eval.evaluate(), 0.5);
    }

    suite.expect_throw<mexce::mexce_parsing_exception>("invalid_numeric_suffix", [] {
        mexce::evaluator().set_expression("1a");
    }, "\"a\" not expected");

    suite.expect_throw<mexce::mexce_parsing_exception>("unknown_identifier_before_closing_paren", [] {
        mexce::evaluator().set_expression("(foo)");
    }, "foo is not a known constant or variable name");

    suite.expect_throw<mexce::mexce_parsing_exception>("unknown_identifier_before_operator", [] {
        mexce::evaluator().set_expression("bar+1");
    }, "bar is not a known constant or variable name");

    suite.expect_throw<mexce::mexce_parsing_exception>("unknown_identifier_before_comma", [] {
        mexce::evaluator().set_expression("min(foo,1)");
    }, "foo is not a known constant or variable name");

    suite.expect_throw<mexce::mexce_parsing_exception>("unknown_function_name", [] {
        mexce::evaluator().set_expression("baz(1)");
    }, "baz is not a known function name");

    suite.expect_throw<mexce::mexce_parsing_exception>("closing_paren_after_number", [] {
        mexce::evaluator().set_expression("1)");
    }, "\")\" not expected");

    suite.expect_throw<mexce::mexce_parsing_exception>("missing_function_argument", [] {
        mexce::evaluator().set_expression("min(1)");
    }, "Expected more arguments");

    suite.expect_throw<mexce::mexce_parsing_exception>("missing_function_argument_decimal_literal", [] {
        mexce::evaluator().set_expression("min(3.0)");
    }, "Expected more arguments");

    {
        mexce::evaluator eval;
        double x = 1.0;
        eval.bind(x, "x");
        suite.expect_throw<mexce::mexce_parsing_exception>(
            "missing_function_argument_variable_with_space",
            [&] { eval.set_expression("min(x )"); },
            "Expected more arguments"
        );
    }

    suite.expect_throw<mexce::mexce_parsing_exception>("missing_function_argument_space_after_literal", [] {
        mexce::evaluator().set_expression("min(1 )");
    }, "Expected more arguments");

    suite.expect_throw<mexce::mexce_parsing_exception>("closing_paren_after_decimal_literal", [] {
        mexce::evaluator().set_expression("3.0)");
    }, "\")\" not expected");

    suite.expect_throw<mexce::mexce_parsing_exception>("closing_paren_after_literal_with_space", [] {
        mexce::evaluator().set_expression("1 )");
    }, "\")\" not expected");

    suite.expect_throw<mexce::mexce_parsing_exception>("comma_requires_closing_paren_numeric", [] {
        mexce::evaluator().set_expression("(1,2)");
    }, "Expected a \")\"");

    suite.expect_throw<mexce::mexce_parsing_exception>("comma_requires_closing_paren_numeric_with_space", [] {
        mexce::evaluator().set_expression("(1 ,2)");
    }, "Expected a \")\"");

    suite.expect_throw<mexce::mexce_parsing_exception>("too_many_function_arguments", [] {
        mexce::evaluator().set_expression("min(1,2,3)");
    }, "Don't expect any arguments here");

    suite.expect_throw<mexce::mexce_parsing_exception>("too_many_function_arguments_space_before_comma", [] {
        mexce::evaluator().set_expression("min(1,2 ,3)");
    }, "Don't expect any arguments here");

    suite.expect_throw<mexce::mexce_parsing_exception>("numeric_exponent_missing_sign", [] {
        mexce::evaluator().set_expression("1e)");
    }, "expecting '+'/'-' followed by the exponent of the numeric literal");

    suite.expect_throw<mexce::mexce_parsing_exception>("numeric_exponent_missing_digits", [] {
        mexce::evaluator().set_expression("1e+");
    }, "expecting the exponent of the numeric literal");

    suite.expect_throw<mexce::mexce_parsing_exception>("numeric_exponent_bad_digit", [] {
        mexce::evaluator().set_expression("1e+1a");
    }, "\"a\" not expected");

    suite.expect_throw<mexce::mexce_parsing_exception>("unexpected_character_after_space", [] {
        mexce::evaluator().set_expression("1 ?");
    }, "\"?\" not expected");

    suite.expect_throw<mexce::mexce_parsing_exception>("missing_opening_parenthesis", [] {
        mexce::evaluator().set_expression("sin 1)");
    }, "Expected a \"(\"");

    suite.expect_throw<mexce::mexce_parsing_exception>("unclosed_parenthesis", [] {
        mexce::evaluator().set_expression("(");
    }, "Expected a \")\"");

    suite.expect_throw<mexce::mexce_parsing_exception>("unexpected_end_of_expression", [] {
        mexce::evaluator().set_expression("1+");
    }, "Unexpected end of expression");
}

void test_empty_state_after_failed_replacement(TestSuite& suite)
{
    mexce::evaluator eval;

    eval.set_expression("19.25");
    suite.expect_near("constant_before_failed_replacement", eval.evaluate(), 19.25);
    suite.expect_throw<mexce::mexce_parsing_exception>(
        "constant_failed_replacement",
        [&] { eval.set_expression("unknown_name"); },
        "unknown_name is not a known constant, variable or function name"
    );
    suite.expect_throw<std::logic_error>(
        "constant_empty_evaluate_after_failed_replacement",
        [&] { (void)eval.evaluate(); },
        "No expression has been compiled."
    );
    suite.expect_true(
        "constant_empty_backend_after_failed_replacement",
        eval.get_backend() == mexce::backend_type::none);
    suite.expect_true(
        "constant_empty_optimized_expression_after_failed_replacement",
        eval.get_optimized_expression().empty());
    suite.expect_true(
        "constant_empty_byte_representation_after_failed_replacement",
        eval.get_byte_representation().empty());

    double value = 3.0;
    eval.bind(value, "value");
    eval.set_expression("value + 2");
    suite.expect_near("native_before_failed_replacement", eval.evaluate(), 5.0);
    suite.expect_throw<mexce::mexce_parsing_exception>(
        "native_failed_replacement",
        [&] { eval.set_expression("unknown_name"); },
        "unknown_name is not a known constant, variable or function name"
    );
    suite.expect_throw<std::logic_error>(
        "native_empty_evaluate_after_failed_replacement",
        [&] { (void)eval.evaluate(); },
        "No expression has been compiled."
    );
    suite.expect_true(
        "native_empty_backend_after_failed_replacement",
        eval.get_backend() == mexce::backend_type::none);
    suite.expect_true(
        "native_empty_optimized_expression_after_failed_replacement",
        eval.get_optimized_expression().empty());
    suite.expect_true(
        "native_empty_byte_representation_after_failed_replacement",
        eval.get_byte_representation().empty());
    eval.unbind("value");
    suite.expect_throw<std::logic_error>(
        "unbind_after_failed_replacement_keeps_empty_state",
        [&] { (void)eval.evaluate(); },
        "No expression has been compiled."
    );

    double replacement_value = 4.0;
    eval.bind(replacement_value, "replacement_value");
    eval.set_expression("replacement_value * 3");
    suite.expect_near("successful_replacement_after_failure", eval.evaluate(), 12.0);
}

void test_late_compilation_failure_reference_state(TestSuite& suite)
{
    mexce::evaluator eval;
    double a = 1.25;
    eval.bind(a, "a");
    eval.use_x87_backend();

    suite.expect_throw<std::overflow_error>(
        "late_compilation_failure",
        [&] {
            eval.set_expression(
                "mod(a,mod(a,mod(a,mod(a,mod(a,mod(a,mod(a,mod(a,mod(a,a)))))))))");
        },
        "Expression too complex for x87 FPU (stack overflow)"
    );
    suite.expect_throw<std::logic_error>(
        "late_compilation_failure_empty_evaluate",
        [&] { (void)eval.evaluate(); },
        "No expression has been compiled."
    );

    eval.unbind("a");
    suite.expect_throw<std::logic_error>(
        "late_compilation_failure_unbind_preserves_empty_state",
        [&] { (void)eval.evaluate(); },
        "No expression has been compiled."
    );
}

void test_unbind_referenced_and_variadic(TestSuite& suite) {
    mexce::evaluator eval;

    double x = 10.0;
    double y = 2.0;
    eval.bind(x, "x", y, "y");

    eval.set_expression("x + 5");
    suite.expect_near("unbind_ref_pre", eval.evaluate(), 15.0);

    eval.unbind("x", "y");

    suite.expect_near("unbind_ref_sets_zero", eval.evaluate(), 0.0);

    suite.expect_throw<mexce::mexce_parsing_exception>(
        "unbind_ref_removed_name",
        [&] { eval.set_expression("x + 1"); },
        "x is not a known constant, variable or function name"
    );
}

void test_lock_executable_buffer_failure(TestSuite& suite) {
    const size_t sz = 4096;
    uint8_t* buf = mexce::impl::get_executable_buffer(sz);
    suite.expect_true("get_executable_buffer_ok", buf != nullptr);

    suite.expect_throw<std::runtime_error>(
        "lock_executable_buffer_zero_size_throws",
        [&]{ (void)mexce::impl::lock_executable_buffer(buf, 0); }
    );

    mexce::impl::free_executable_buffer(reinterpret_cast<double(*)()>(buf), sz);
}

void test_elist_to_string_unary_and_multi(TestSuite& suite) {
    using namespace mexce::impl;

    double value = 4.0;
    auto variable = std::make_shared<Variable>(1, &value, "value", M32FP);

    auto neg_function = std::make_shared<Function>(function_map().find("neg")->second);
    elist_t eu;
    eu.push_back(Element(variable));
    eu.push_back(Element(neg_function));
    suite.expect_true("elist_to_string_unary_neg", elist_to_string(eu) == "(-value)");

    auto constant3 = std::make_shared<Constant>(3, 3.0);
    auto min_function = std::make_shared<Function>(function_map().find("min")->second);
    elist_t em;
    em.push_back(Element(variable));
    em.push_back(Element(constant3));
    em.push_back(Element(min_function));
    suite.expect_true("elist_to_string_min_two", elist_to_string(em) == "min(value, 3)");
}

void test_parser_additional_coverage(TestSuite& suite) {
    suite.expect_throw<mexce::mexce_parsing_exception>("parser_empty_function_call", [] {
        mexce::evaluator().set_expression("min()");
    }, "Expected an expression");

    suite.expect_near("parser_numeric_trailing_dot", mexce::evaluator().evaluate("123."), 123.0);

    suite.expect_near("parser_space_after_function_name", mexce::evaluator().evaluate("sin (pi/2)"), 1.0);

    suite.expect_near("parser_mul_div_left_assoc", mexce::evaluator().evaluate("8 * 4 / 2"), 16.0);
}

#ifdef __linux__
void test_executable_buffer_failure_paths(TestSuite& suite)
{
    mexce::evaluator eval;
    double x = 1.0;
    eval.bind(x, "x");

    {
        ScopedTestFlag force_mmap_failure(mexce_test_overrides::force_mmap_failure_flag(), true);
        suite.expect_throw<std::bad_alloc>("mmap_failure_triggers_bad_alloc", [&] {
            eval.set_expression("x + 1");
        });
    }

    {
        ScopedTestFlag force_mprotect_failure(mexce_test_overrides::force_mprotect_failure_flag(), true);
        suite.expect_throw<std::runtime_error>(
            "mprotect_failure_triggers_runtime_error",
            [&] { eval.set_expression("x + 1"); },
            "mprotect(PROT_READ|PROT_EXEC) failed"
        );
    }
}
#endif

void test_fpu_stack_overflow(TestSuite& suite) {
    // The x87 FPU has a strict stack limit of 8 registers.
    // Construct an expression that requires 9 values on the stack simultaneously.
    // Structure: max(val, max(val, max(val, ... ))) nested 8 times = 9 operands
    mexce::evaluator eval;
    double val = 10.0;
    eval.bind(val, "val");

    std::string expr = "val";
    for (int i = 0; i < 8; ++i) {
        expr = "max(val, " + expr + ")";
    }

    suite.expect_throw<std::overflow_error>(
        "fpu_stack_overflow_depth_9",
        [&] { eval.set_expression(expr); },
        "Expression too complex for x87 FPU (stack overflow)"
    );
}

// Test SSE4.1 rounding functions with SSE2 backend explicitly enabled
void test_sse2_rounding_functions(TestSuite& suite) {
#ifdef MEXCE_64
    mexce::evaluator eval;
    eval.use_sse2_backend();  // Force SSE2 backend

    // Test with negative value
    double value = -2.7;
    eval.bind(value, "v");

    eval.set_expression("floor(v)");
    suite.expect_near("sse2_floor_neg", eval.evaluate(), std::floor(value));

    eval.set_expression("ceil(v)");
    suite.expect_near("sse2_ceil_neg", eval.evaluate(), std::ceil(value));

    eval.set_expression("trunc(v)");
    suite.expect_near("sse2_trunc_neg", eval.evaluate(), std::trunc(value));

    // Test rounding in expressions
    double x = 5.5, y = 2.3;
    eval.bind(x, "x", y, "y");
    eval.set_expression("floor(x) + ceil(y)");
    suite.expect_near("sse2_floor_ceil_expr", eval.evaluate(), std::floor(x) + std::ceil(y));
#else
    (void)suite;
#endif
}

// Test SSE2 expression simplification
void test_sse2_expression_simplification(TestSuite& suite) {
#ifdef MEXCE_64
    mexce::evaluator eval;
    eval.use_sse2_backend();     // Force SSE2 backend
    eval.enable_fast_math();     // Enable simplifications

    double x = -1.5, y = 4.0, z = 2.0;
    eval.bind(x, "x", y, "y", z, "z");

    // Test z/z cancellation: y/z*z should simplify to y
    eval.set_expression("y/z*z");
    suite.expect_near("sse2_simplify_cancel_z", eval.evaluate(), y);

    // Check the optimized expression shows simplification
    std::string opt = eval.get_optimized_expression();
    suite.expect_true("sse2_simplify_cancel_z_opt", opt.find('z') == std::string::npos);

    // Test constant folding in multiplicative chain
    eval.set_expression("y*2.0*3.0");
    suite.expect_near("sse2_simplify_const_fold", eval.evaluate(), y * 6.0);

    // Test mixed additive/multiplicative simplification
    // 3.123 - y/z*9.123*3.123/5.123*z = 3.123 - y*5.56141...
    eval.set_expression("3.123 - y/z*9.123*3.123/5.123*z");
    double expected = 3.123 - y * (9.123 * 3.123 / 5.123);
    suite.expect_near("sse2_simplify_mixed", eval.evaluate(), expected);

    // Test x+x simplification
    eval.set_expression("x+x");
    suite.expect_near("sse2_simplify_x_plus_x", eval.evaluate(), 2.0 * x);

    // Test x*x simplification
    eval.set_expression("x*x");
    suite.expect_near("sse2_simplify_x_times_x", eval.evaluate(), x * x);
#else
    (void)suite;
#endif
}

// Test trunc function (missing from original rounding tests)
void test_trunc_function(TestSuite& suite) {
    mexce::evaluator eval;

    double pos_val = 3.75;
    double neg_val = -3.75;
    eval.bind(pos_val, "pos_val", neg_val, "neg_val");

    eval.set_expression("trunc(pos_val)");
    suite.expect_near("trunc_positive", eval.evaluate(), std::trunc(pos_val));

    eval.set_expression("trunc(neg_val)");
    suite.expect_near("trunc_negative", eval.evaluate(), std::trunc(neg_val));

    // Test trunc at boundaries
    double half = 0.5;
    eval.bind(half, "half");
    eval.set_expression("trunc(half)");
    suite.expect_near("trunc_half", eval.evaluate(), 0.0);

    half = -0.5;
    eval.set_expression("trunc(half)");
    suite.expect_near("trunc_neg_half", eval.evaluate(), 0.0);
}

// Test options API coverage (get_options, set_options, set_use_libm)
void test_options_api(TestSuite& suite) {
    mexce::evaluator eval;

    // Test get_options returns current state
    const mexce::options& opts_ref = eval.get_options();
    suite.expect_true("get_options_fast_math_default", opts_ref.fast_math == false);
    suite.expect_true("get_options_prefer_x87_default", opts_ref.prefer_x87 == false);

    // Test set_options
    mexce::options new_opts;
    new_opts.fast_math = true;
    new_opts.prefer_x87 = true;
    new_opts.enable_cse = true;
    eval.set_options(new_opts);

    const mexce::options& updated = eval.get_options();
    suite.expect_true("set_options_fast_math", updated.fast_math == true);
    suite.expect_true("set_options_prefer_x87", updated.prefer_x87 == true);
    suite.expect_true("set_options_enable_cse", updated.enable_cse == true);

    // Test set_use_libm convenience function
    mexce::options libm_opts;
    libm_opts.set_use_libm(false);
    suite.expect_true("set_use_libm_sin", libm_opts.use_libm_sin == false);
    suite.expect_true("set_use_libm_cos", libm_opts.use_libm_cos == false);
    suite.expect_true("set_use_libm_tan", libm_opts.use_libm_tan == false);
    suite.expect_true("set_use_libm_exp", libm_opts.use_libm_exp == false);
    suite.expect_true("set_use_libm_log", libm_opts.use_libm_log == false);
    suite.expect_true("set_use_libm_log10", libm_opts.use_libm_log10 == false);
    suite.expect_true("set_use_libm_log2", libm_opts.use_libm_log2 == false);
    suite.expect_true("set_use_libm_logb", libm_opts.use_libm_logb == false);
    suite.expect_true("set_use_libm_ylog2", libm_opts.use_libm_ylog2 == false);
    suite.expect_true("set_use_libm_generic_pow", libm_opts.use_libm_generic_pow == false);

    // Test set_use_libm(true) sets all back
    libm_opts.set_use_libm(true);
    suite.expect_true("set_use_libm_true_sin", libm_opts.use_libm_sin == true);
    suite.expect_true("set_use_libm_true_cos", libm_opts.use_libm_cos == true);
    suite.expect_true("set_use_libm_true_tan", libm_opts.use_libm_tan == true);

    // Verify options affect compilation by testing prefer_x87
    mexce::evaluator eval2;
    double x = 2.0;
    eval2.bind(x, "x");
    eval2.use_x87_backend();
    eval2.set_expression("x + 1");
    suite.expect_near("options_affect_compilation", eval2.evaluate(), 3.0);
}

// Test Common Subexpression Elimination (CSE)
// Note: CSE uses x87 store instructions, so must use x87 backend
void test_cse_coverage(TestSuite& suite) {
    mexce::evaluator eval;
    eval.enable_cse();
    eval.use_x87_backend();  // CSE requires x87 backend

    double a = 2.0, b = 3.0, c = 4.0;
    eval.bind(a, "a", b, "b", c, "c");

    // CSE with transcendental functions (won't be absorbed by ASMD)
    eval.set_expression("sin(a) + sin(a) * 2");
    double expected = std::sin(a) + std::sin(a) * 2;
    suite.expect_near("cse_sin", eval.evaluate(), expected);

    // More complex CSE case
    eval.set_expression("cos(b) + cos(b) + cos(b)");
    expected = std::cos(b) * 3;
    suite.expect_near("cse_cos", eval.evaluate(), expected);

    // CSE with nested expression
    eval.set_expression("exp(a+b) * exp(a+b)");
    expected = std::exp(a + b) * std::exp(a + b);
    suite.expect_near("cse_exp", eval.evaluate(), expected);
}

void test_source_free_fallback_ownership(TestSuite& suite)
{
    mexce::evaluator eval;
    double a = 1.25;
    eval.bind(a, "a");
    eval.use_sse2_backend();

    const std::string polynomial =
        "((((((((((2.20*a+1.1)*a+9.9)*a+8.8)*a+7.7)*a+6.6)*a+5.5)*a+4.4)*a+3.3)*a+2.2)*a+1.1)";
    eval.set_expression(polynomial);

    double expected = 2.20;
    const double coefficients[] = {1.1, 9.9, 8.8, 7.7, 6.6, 5.5, 4.4, 3.3, 2.2, 1.1};
    for (double coefficient : coefficients) {
        expected = expected * a + coefficient;
    }

    suite.expect_true("post_optimizer_fallback_backend", eval.get_backend() == mexce::backend_type::x87);
    suite.expect_near("post_optimizer_fallback_value", eval.evaluate(), expected);
    suite.expect_true("fallback_does_not_mutate_options", !eval.get_options().prefer_x87);

    eval.enable_cse();
    eval.set_expression("(" + polynomial + ")+sin(a)+sin(a)");
    suite.expect_true("cse_fallback_backend", eval.get_backend() == mexce::backend_type::x87);
    suite.expect_near("cse_fallback_value", eval.evaluate(), expected + 2.0 * std::sin(a));
}

// Test SSE2 neg and abs operations (covers xorpd/andpd emit functions)
void test_sse2_neg_abs(TestSuite& suite) {
#ifdef MEXCE_64
    mexce::evaluator eval;
    eval.use_sse2_backend();  // Force SSE2

    double x = 5.0;
    eval.bind(x, "x");

    // Test negation
    eval.set_expression("neg(x)");
    suite.expect_near("sse2_neg_positive", eval.evaluate(), -5.0);

    x = -3.0;
    eval.set_expression("neg(x)");
    suite.expect_near("sse2_neg_negative", eval.evaluate(), 3.0);

    // Test absolute value
    eval.set_expression("abs(x)");
    suite.expect_near("sse2_abs_negative", eval.evaluate(), 3.0);

    x = 7.0;
    eval.set_expression("abs(x)");
    suite.expect_near("sse2_abs_positive", eval.evaluate(), 7.0);

    // Combined neg and abs
    x = -4.0;
    eval.set_expression("abs(neg(x))");
    suite.expect_near("sse2_abs_neg", eval.evaluate(), 4.0);
#else
    (void)suite;
#endif
}

// Test SSE2 log10 and log2 functions
void test_sse2_log_functions(TestSuite& suite) {
#ifdef MEXCE_64
    mexce::evaluator eval;
    eval.use_sse2_backend();  // Force SSE2

    double x = 100.0;
    eval.bind(x, "x");

    eval.set_expression("log10(x)");
    suite.expect_near("sse2_log10", eval.evaluate(), std::log10(x));

    x = 8.0;
    eval.set_expression("log2(x)");
    suite.expect_near("sse2_log2", eval.evaluate(), std::log2(x));

    // Test with different values
    x = 1000.0;
    eval.set_expression("log10(x)");
    suite.expect_near("sse2_log10_1000", eval.evaluate(), 3.0);

    x = 16.0;
    eval.set_expression("log2(x)");
    suite.expect_near("sse2_log2_16", eval.evaluate(), 4.0);

    // Regression for emit_sse2_libm_binary_call when arg1 is already in XMM1.
    // Before the fix, moving arg2 into XMM1 first clobbered arg1 for expressions
    // such as: a, b, 2, x, logb, add, logb.
    double a = 2.0;
    double b = 4.0;
    x = 8.0;
    eval.bind(a, "a", b, "b");
    eval.set_expression("logb(a, b + logb(2, x))");
    const double expected = std::log(b + std::log(x) / std::log(2.0)) / std::log(a);
    suite.expect_near("sse2_logb_depth3_arg_order", eval.evaluate(), expected);
#else
    (void)suite;
#endif
}

// Test different numeric data types for x87 load instructions
void test_numeric_data_types(TestSuite& suite) {
    mexce::evaluator eval;
    eval.use_x87_backend();  // Force x87 to test type-specific loads

    // Test all supported numeric types
    double d_val = 3.14159;
    float f_val = 2.71828f;
    int16_t i16_val = 100;
    int32_t i32_val = 1000;
    int64_t i64_val = 10000;

    eval.bind(d_val, "d", f_val, "f", i16_val, "i16", i32_val, "i32", i64_val, "i64");

    // Test double (M64FP)
    eval.set_expression("d");
    suite.expect_near("x87_load_m64fp", eval.evaluate(), d_val);

    // Test float (M32FP)
    eval.set_expression("f");
    suite.expect_near("x87_load_m32fp", eval.evaluate(), static_cast<double>(f_val));

    // Test int16_t (M16INT)
    eval.set_expression("i16");
    suite.expect_near("x87_load_m16int", eval.evaluate(), static_cast<double>(i16_val));

    // Test int32_t (M32INT)
    eval.set_expression("i32");
    suite.expect_near("x87_load_m32int", eval.evaluate(), static_cast<double>(i32_val));

    // Test int64_t (M64INT)
    eval.set_expression("i64");
    suite.expect_near("x87_load_m64int", eval.evaluate(), static_cast<double>(i64_val));

    // Test arithmetic with mixed types
    eval.set_expression("d + f + i16 + i32 + i64");
    double expected = d_val + static_cast<double>(f_val) + i16_val + i32_val + i64_val;
    suite.expect_near("x87_mixed_types", eval.evaluate(), expected);
}

} // namespace

int main() {
    TestSuite suite;
    test_bias_and_gain(suite);
    test_periodic_and_mod(suite);
    test_sign_and_comparisons(suite);
    test_exponent_and_significand(suite);
    test_logs_and_powers(suite);
    test_rounding_functions(suite);
    test_ieee_self_cancellation_default_mode(suite);
    test_min_max_and_arithmetic(suite);
    test_constants_and_single_shot(suite);
    test_signed_zero_semantics(suite);
    test_pow_optimizer_special_cases(suite);
    test_nested_pow_folding_sympy_semantics(suite);
    test_helper_functions_and_element(suite);
    test_binding_and_unbinding(suite);
    test_binding_name_conflicts(suite);
    test_mexce_parsing_exception_class(suite);
    test_asmd_optimizer_branches(suite);
    test_parsing_errors(suite);
    test_empty_state_after_failed_replacement(suite);
    test_late_compilation_failure_reference_state(suite);
    test_elist_to_string_unary_and_multi(suite);
    test_parser_additional_coverage(suite);
    test_unbind_referenced_and_variadic(suite);
    test_lock_executable_buffer_failure(suite);
#ifdef __linux__
    test_executable_buffer_failure_paths(suite);
#endif
    test_fpu_stack_overflow(suite);
    test_sse2_rounding_functions(suite);
    test_sse2_expression_simplification(suite);
    test_trunc_function(suite);
    test_options_api(suite);
    test_cse_coverage(suite);
    test_source_free_fallback_ownership(suite);
    test_sse2_neg_abs(suite);
    test_sse2_log_functions(suite);
    test_numeric_data_types(suite);

    if (!suite.failures.empty()) {
        std::cerr << "mexce unit tests failed (" << suite.failures.size() << ")" << std::endl;
        for (const auto& failure : suite.failures) {
            std::cerr << "  - " << failure << std::endl;
        }
        return 1;
    }

    std::cout << "All mexce unit tests passed" << std::endl;
    return 0;
}

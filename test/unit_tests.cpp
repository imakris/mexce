#include "mexce.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <iostream>
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
}

void test_pow_optimizer_special_cases(TestSuite& suite) {
    mexce::evaluator eval;
    double x = 9.0;
    eval.bind(x, "x");

    eval.set_expression("pow(x, 0.5)");
    suite.expect_near("pow_optimizer_sqrt", eval.evaluate(), std::sqrt(x));

    x = 16.0;
    eval.set_expression("pow(x, 0)");
    suite.expect_near("pow_optimizer_zero_exponent", eval.evaluate(), 1.0);

    x = 5.0;
    eval.set_expression("pow(x, 1)");
    suite.expect_near("pow_optimizer_exponent_one", eval.evaluate(), x);

    x = 2.0;
    eval.set_expression("pow(x, 3.3)");
    suite.expect_near("pow_optimizer_generic_path", eval.evaluate(), std::pow(x, 3.3));
}

void test_helper_functions_and_element(TestSuite& suite) {
    using namespace mexce::impl;
    mexce::evaluator eval;

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
    auto add_function = make_function(&eval, "add");

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

void test_mexce_parsing_exception_class(TestSuite& suite) {
    mexce::mexce_parsing_exception ex("custom message", 3);
    suite.expect_true("mexce_parsing_exception_message", std::string(ex.what()) == "custom message");
}

void test_memory_management(TestSuite& suite) {
    size_t size = 4096;
    uint8_t* buffer = mexce::impl::get_executable_buffer(size);
    suite.expect_true("get_executable_buffer", buffer != nullptr);
    mexce::impl::free_executable_buffer(reinterpret_cast<double(*)()>(buffer), size);
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

    suite.expect_throw<mexce::mexce_parsing_exception>("closing_paren_after_number", [] {
        mexce::evaluator().set_expression("1)");
    }, "\")\" not expected");

    suite.expect_throw<mexce::mexce_parsing_exception>("missing_function_argument", [] {
        mexce::evaluator().set_expression("min(1)");
    }, "Expected more arguments");

    suite.expect_throw<mexce::mexce_parsing_exception>("comma_requires_closing_paren_numeric", [] {
        mexce::evaluator().set_expression("(1,2)");
    }, "Expected a \")\"");

    suite.expect_throw<mexce::mexce_parsing_exception>("too_many_function_arguments", [] {
        mexce::evaluator().set_expression("min(1,2,3)");
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
    mexce::evaluator eval;

    double value = 4.0;
    auto variable = std::make_shared<Variable>(1, &value, "value", M32FP);

    auto neg_function = make_function(&eval, "neg");
    elist_t eu;
    eu.push_back(Element(variable));
    eu.push_back(Element(neg_function));
    suite.expect_true("elist_to_string_unary_neg", elist_to_string(eu) == "(-value)");

    auto constant3 = std::make_shared<Constant>(3, 3.0);
    auto min_function = make_function(&eval, "min");
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

} // namespace

int main() {
    TestSuite suite;
    test_bias_and_gain(suite);
    test_periodic_and_mod(suite);
    test_sign_and_comparisons(suite);
    test_exponent_and_significand(suite);
    test_logs_and_powers(suite);
    test_rounding_functions(suite);
    test_min_max_and_arithmetic(suite);
    test_constants_and_single_shot(suite);
    test_pow_optimizer_special_cases(suite);
    test_helper_functions_and_element(suite);
    test_binding_and_unbinding(suite);
    test_mexce_parsing_exception_class(suite);
    test_memory_management(suite);
    test_asmd_optimizer_branches(suite);
    test_parsing_errors(suite);
    test_elist_to_string_unary_and_multi(suite);
    test_parser_additional_coverage(suite);
    test_unbind_referenced_and_variadic(suite);
    test_lock_executable_buffer_failure(suite);

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

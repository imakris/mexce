#include "mexce.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

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
    void expect_throw(const std::string& name, Func func, const std::string& expected_message = std::string()) {
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
    auto bias_expected = [&](double value) {
        double denom = (1.0 / a - 2.0) * (1.0 - value) + 1.0;
        return value / denom;
    };
    suite.expect_near("bias(x<0.5)", eval.evaluate(), bias_expected(x));

    x = 0.9;
    suite.expect_near("bias(x>=0.5)", eval.evaluate(), bias_expected(x));

    eval.set_expression("gain(x, a)");
    auto gain_expected = [&](double value) {
        double base = (1.0 / a - 2.0) * (1.0 - 2.0 * value);
        if (value < 0.5) {
            return value / (base + 1.0);
        }
        double numerator = base - value;
        double denominator = base - 1.0;
        return numerator / denominator;
    };

    x = 0.25;
    suite.expect_near("gain(x<0.5)", eval.evaluate(), gain_expected(x));

    x = 0.75;
    suite.expect_near("gain(x>=0.5)", eval.evaluate(), gain_expected(x));
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
    suite.expect_near("mod(negative)", eval.evaluate(), std::fmod(x, period));

    x = 5.75;
    suite.expect_near("mod(positive)", eval.evaluate(), std::fmod(x, period));
}

void test_sign_and_comparisons(TestSuite& suite) {
    mexce::evaluator eval;
    double a = -4.5;
    eval.bind(a, "a");

    eval.set_expression("sign(a)");
    suite.expect_equal("sign(negative)", eval.evaluate(), -1.0);

    a = 0.0;
    suite.expect_equal("sign(zero)", eval.evaluate(), -1.0);

    eval.set_expression("signp(a)");
    suite.expect_equal("signp(zero)", eval.evaluate(), 0.0);

    a = 3.2;
    suite.expect_equal("signp(positive)", eval.evaluate(), 1.0);

    double x = 2.0;
    double y = 3.0;
    mexce::evaluator compare_eval;
    compare_eval.bind(x, "x", y, "y");

    compare_eval.set_expression("less_than(x, y)");
    suite.expect_equal("less_than_true", compare_eval.evaluate(), 1.0);

    x = 5.0;
    suite.expect_equal("less_than_false", compare_eval.evaluate(), 0.0);

    compare_eval.set_expression("x < y");
    suite.expect_equal("operator<(false)", compare_eval.evaluate(), 0.0);

    y = 8.0;
    suite.expect_equal("operator<(true)", compare_eval.evaluate(), 1.0);
}

void test_exponent_and_significand(TestSuite& suite) {
    mexce::evaluator eval;
    double value = 12.8;
    eval.bind(value, "v");

    eval.set_expression("expn(v)");
    int exponent = 0;
    double mantissa = std::frexp(value, &exponent);
    double expected_exponent = static_cast<double>(exponent - 1);
    suite.expect_near("expn", eval.evaluate(), expected_exponent);

    eval.set_expression("sfc(v)");
    double expected_significand = std::ldexp(mantissa, 1);
    suite.expect_near("sfc", eval.evaluate(), expected_significand);
}

void test_logs_and_powers(TestSuite& suite) {
    mexce::evaluator eval;
    double base = 3.2;
    double value = 9.6;
    eval.bind(base, "b", value, "v");

    eval.set_expression("logb(b, v)");
    suite.expect_near("logb", eval.evaluate(), std::log(value) / std::log(base));

    eval.set_expression("ylog2(v, b)");
    suite.expect_near("ylog2", eval.evaluate(), value * std::log2(base));

    eval.set_expression("pow(b, 3)");
    suite.expect_near("pow_function", eval.evaluate(), std::pow(base, 3.0));

    eval.set_expression("b^2");
    suite.expect_near("pow_operator", eval.evaluate(), std::pow(base, 2.0));
}

void test_rounding_functions(TestSuite& suite) {
    mexce::evaluator eval;
    double value = -2.75;
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
}

void test_constants_and_single_shot(TestSuite& suite) {
    mexce::evaluator eval;
    const double pi = std::acos(-1.0);
    suite.expect_near("constant_pi", eval.evaluate("pi"), pi);
    suite.expect_near("constant_e", eval.evaluate("e"), std::exp(1.0));

    double x = 0.25;
    double y = 0.5;
    eval.bind(x, "x", y, "y");
    eval.set_expression("sin(x) + cos(y)");
    suite.expect_near("trig", eval.evaluate(), std::sin(x) + std::cos(y));
}

void test_parsing_failures(TestSuite& suite) {
    mexce::evaluator eval;
    auto quoted = [](char c) { return std::string("\"") + c + "\""; };

    suite.expect_throw<std::logic_error>("empty_expression", [&]() {
        eval.set_expression("");
    }, "Expected an expression");

    suite.expect_throw<mexce::mexce_parsing_exception>("unexpected_closing_paren", [&]() {
        eval.set_expression("1)");
    }, quoted(')') + " not expected");

    suite.expect_throw<mexce::mexce_parsing_exception>("missing_arguments", [&]() {
        eval.set_expression("min(1)");
    }, "Expected more arguments");

    suite.expect_throw<mexce::mexce_parsing_exception>("missing_closing_paren_before_comma", [&]() {
        eval.set_expression("min((1,2), 3)");
    }, std::string("Expected a ") + quoted(')'));

    suite.expect_throw<mexce::mexce_parsing_exception>("top_level_comma", [&]() {
        eval.set_expression("1,2");
    }, "Don't expect any arguments here");

    suite.expect_throw<mexce::mexce_parsing_exception>("unknown_token", [&]() {
        eval.set_expression("$");
    }, quoted('$') + " not expected");
}

void test_binding_failures(TestSuite& suite) {
    mexce::evaluator eval;
    double value = 0.0;

    suite.expect_throw<std::logic_error>("bind_existing_function", [&]() {
        eval.bind(value, "sin");
    }, "Attempted to bind a variable, named as an existing function");

    suite.expect_throw<std::logic_error>("bind_existing_constant", [&]() {
        eval.bind(value, "pi");
    }, "Attempted to bind a variable, named as an existing constant");

    suite.expect_throw<std::logic_error>("unbind_empty_name", [&]() {
        eval.unbind("");
    }, "Variable name was an empty string");

    suite.expect_throw<std::logic_error>("unbind_unknown_variable", [&]() {
        eval.unbind("unknown");
    }, "Attempted to unbind an unknown variable");
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
    test_parsing_failures(suite);
    test_binding_failures(suite);

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


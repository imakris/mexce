#include "mexce.h"

#include <clocale>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <iostream>


int main()
{
    bool selected = false;
    for (const char* name : {"de_DE.UTF-8", "de_DE.utf8", "German_Germany.1252", "de_DE"}) {
        if (std::setlocale(LC_NUMERIC, name) &&
            std::strcmp(std::localeconv()->decimal_point, ",") == 0)
        {
            selected = true;
            break;
        }
    }
    if (!selected) {
        std::cerr << "A German numeric locale is required for this test.\n";
        return 1;
    }

    // Construct the first evaluator only after the host selects a comma locale.
    mexce::evaluator eval;
    int failures = 0;
    const auto check = [&](const char* expression, double expected) {
        const double actual = eval.evaluate(expression);
        if (!(std::abs(actual - expected) < 1e-14)) {
            std::cerr << "FAIL: " << expression << " returned " << actual << '\n';
            ++failures;
        }
    };
    check("1.5", 1.5);
    check("1.25e-2", 0.0125);
    check("min(1.5,2.5)", 1.5);
    check("pi", std::acos(-1.0));
    check("e", std::exp(1.0));
    if (std::strcmp(std::localeconv()->decimal_point, ",") != 0) {
        std::cerr << "FAIL: evaluation changed the host numeric locale\n";
        ++failures;
    }

    std::setlocale(LC_NUMERIC, "C");
    check("1.5", 1.5);
    check("pi", std::acos(-1.0));
    check("e", std::exp(1.0));
    return failures ? 1 : 0;
}

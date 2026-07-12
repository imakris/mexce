#include "mexce_protected.h"

#include <cstdint>
#include <exception>
#include <iostream>


int main()
{
    mexce::impl::protected_test_faults().fail_sodium_init = true;
    const uint8_t bytes[32] = {};
    try {
        (void)mexce::Protected_expression_key::from_bytes(bytes, sizeof(bytes));
    }
    catch (const mexce::Protected_expression_error& error) {
        return error.category() ==
            mexce::Protected_expression_error_category::CRYPTOGRAPHY_UNAVAILABLE ? 0 : 1;
    }
    catch (const std::exception& error) {
        std::cerr << "unexpected initialization exception: " << error.what() << '\n';
        return 1;
    }
    std::cerr << "sodium initialization failure was not reported\n";
    return 1;
}

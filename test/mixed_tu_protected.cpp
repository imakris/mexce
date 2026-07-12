#include "mexce_protected.h"

#include <cstddef>
#include <cstdint>


void load_on_protected_side(
    mexce::evaluator* evaluator,
    const uint8_t* program,
    size_t program_size,
    const uint8_t* key_bytes,
    size_t key_size)
{
    evaluator->set_protected_expression(
        program,
        program_size,
        mexce::Protected_expression_key::from_bytes(key_bytes, key_size));
}


size_t protected_evaluator_size()
{
    return sizeof(mexce::evaluator);
}

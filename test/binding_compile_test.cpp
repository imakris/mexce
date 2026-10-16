#include "../mexce.h"

#include <cstdint>


void instantiate_bindings()
{
    mexce::evaluator eval;
#ifdef MEXCE_TEST_UNSUPPORTED_BINDING
    bool value = true;
    eval.bind(value, "value");
#else
    double  real64 = 1.0;
    float   real32 = 1.0f;
    int16_t int16  = 1;
    int32_t int32  = 1;
    int64_t int64  = 1;
    eval.bind(real64, "real64", real32, "real32", int16, "int16", int32, "int32", int64, "int64");
#endif
}

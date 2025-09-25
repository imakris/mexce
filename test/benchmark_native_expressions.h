// This header provides native implementations for the first 100 benchmark expressions.
// Automatically generated using SymPy. Do not edit manually.
#pragma once

#include <cstddef>
#include <cmath>

namespace mexce {
namespace benchmark_data {

struct NativeContext {
    double a;
    double b;
    double c;
    double x;
    double y;
    double z;
    double w;
};

namespace detail {
constexpr double kPi = 3.14159265358979323846264338327950288419716939937510;
constexpr double kE  = 2.71828182845904523536028747135266249775724709369995;
template <typename T> inline void unused(const T&) noexcept {}
}  // namespace detail

using native_eval_fn = double(*)(const NativeContext&);
static const native_eval_fn kNativeExpressions[] = {
    // Expression: 1
    [](const NativeContext& ctx) -> double { detail::unused(ctx); return 1; },
    // Expression: a
    [](const NativeContext& ctx) -> double { return ctx.a; },
    // Expression: +1
    [](const NativeContext& ctx) -> double { detail::unused(ctx); return 1; },
    // Expression: 2*a
    [](const NativeContext& ctx) -> double { return 2*ctx.a; },
    // Expression: 3*a
    [](const NativeContext& ctx) -> double { return 3*ctx.a; },
    // Expression: 4*a
    [](const NativeContext& ctx) -> double { return 4*ctx.a; },
    // Expression: 5*a
    [](const NativeContext& ctx) -> double { return 5*ctx.a; },
    // Expression: a*2
    [](const NativeContext& ctx) -> double { return 2*ctx.a; },
    // Expression: a+1
    [](const NativeContext& ctx) -> double { return ctx.a + 1; },
    // Expression: a+a
    [](const NativeContext& ctx) -> double { return 2*ctx.a; },
    // Expression: a+b
    [](const NativeContext& ctx) -> double { return ctx.a + ctx.b; },
    // Expression: a^b
    [](const NativeContext& ctx) -> double { return std::pow(ctx.a, ctx.b); },
    // Expression: +2+1
    [](const NativeContext& ctx) -> double { detail::unused(ctx); return 3; },
    // Expression: +2-1
    [](const NativeContext& ctx) -> double { detail::unused(ctx); return 1; },
    // Expression: +a*b
    [](const NativeContext& ctx) -> double { return ctx.a*ctx.b; },
    // Expression: +a+b
    [](const NativeContext& ctx) -> double { return ctx.a + ctx.b; },
    // Expression: -1^0
    [](const NativeContext& ctx) -> double { detail::unused(ctx); return -1; },
    // Expression: -2-1
    [](const NativeContext& ctx) -> double { detail::unused(ctx); return -3; },
    // Expression: -a*b
    [](const NativeContext& ctx) -> double { return -ctx.a*ctx.b; },
    // Expression: -a-b
    [](const NativeContext& ctx) -> double { return -ctx.a - ctx.b; },
    // Expression: -a/b
    [](const NativeContext& ctx) -> double { return -ctx.a/ctx.b; },
    // Expression: 2+-1
    [](const NativeContext& ctx) -> double { detail::unused(ctx); return 1; },
    // Expression: 2-+1
    [](const NativeContext& ctx) -> double { detail::unused(ctx); return 1; },
    // Expression: 2--1
    [](const NativeContext& ctx) -> double { detail::unused(ctx); return 3; },
    // Expression: a*+b
    [](const NativeContext& ctx) -> double { return ctx.a*ctx.b; },
    // Expression: a*-b
    [](const NativeContext& ctx) -> double { return -ctx.a*ctx.b; },
    // Expression: a+-b
    [](const NativeContext& ctx) -> double { return ctx.a - ctx.b; },
    // Expression: a/-b
    [](const NativeContext& ctx) -> double { return -ctx.a/ctx.b; },
    // Expression: a^-b
    [](const NativeContext& ctx) -> double { return std::pow(ctx.a, -ctx.b); },
    // Expression: (a*e)
    [](const NativeContext& ctx) -> double { return detail::kE*ctx.a; },
    // Expression: (a+e)
    [](const NativeContext& ctx) -> double { return ctx.a + detail::kE; },
    // Expression: (a-a)
    [](const NativeContext& ctx) -> double { detail::unused(ctx); return 0; },
    // Expression: (b*e)
    [](const NativeContext& ctx) -> double { return detail::kE*ctx.b; },
    // Expression: (b+a)
    [](const NativeContext& ctx) -> double { return ctx.a + ctx.b; },
    // Expression: (b-b)
    [](const NativeContext& ctx) -> double { detail::unused(ctx); return 0; },
    // Expression: (e*e)
    [](const NativeContext& ctx) -> double { detail::unused(ctx); return std::exp(2); },
    // Expression: +2+-1
    [](const NativeContext& ctx) -> double { detail::unused(ctx); return 1; },
    // Expression: +2-+1
    [](const NativeContext& ctx) -> double { detail::unused(ctx); return 1; },
    // Expression: +2--1
    [](const NativeContext& ctx) -> double { detail::unused(ctx); return 3; },
    // Expression: +a*+b
    [](const NativeContext& ctx) -> double { return ctx.a*ctx.b; },
    // Expression: +a+-b
    [](const NativeContext& ctx) -> double { return ctx.a - ctx.b; },
    // Expression: +a-+b
    [](const NativeContext& ctx) -> double { return ctx.a - ctx.b; },
    // Expression: +a--b
    [](const NativeContext& ctx) -> double { return ctx.a + ctx.b; },
    // Expression: +a/+b
    [](const NativeContext& ctx) -> double { return ctx.a/ctx.b; },
    // Expression: +a^+b
    [](const NativeContext& ctx) -> double { return std::pow(ctx.a, ctx.b); },
    // Expression: +a^-b
    [](const NativeContext& ctx) -> double { return std::pow(ctx.a, -ctx.b); },
    // Expression: -a*-b
    [](const NativeContext& ctx) -> double { return ctx.a*ctx.b; },
    // Expression: -a+-b
    [](const NativeContext& ctx) -> double { return -ctx.a - ctx.b; },
    // Expression: -a--b
    [](const NativeContext& ctx) -> double { return -ctx.a + ctx.b; },
    // Expression: -a/-b
    [](const NativeContext& ctx) -> double { return ctx.a/ctx.b; },
    // Expression: -a^+b
    [](const NativeContext& ctx) -> double { return -std::pow(ctx.a, ctx.b); },
    // Expression: -a^-b
    [](const NativeContext& ctx) -> double { return -std::pow(ctx.a, -ctx.b); },
    // Expression: 1+a^2
    [](const NativeContext& ctx) -> double { return std::pow(ctx.a, 2) + 1; },
    // Expression: 2*a+1
    [](const NativeContext& ctx) -> double { return 2*ctx.a + 1; },
    // Expression: a*2.2
    [](const NativeContext& ctx) -> double { return 2.2000000000000002*ctx.a; },
    // Expression: a*b*c
    [](const NativeContext& ctx) -> double { return ctx.a*ctx.b*ctx.c; },
    // Expression: a*b+c
    [](const NativeContext& ctx) -> double { return ctx.a*ctx.b + ctx.c; },
    // Expression: a+1.1
    [](const NativeContext& ctx) -> double { return ctx.a + 1.1000000000000001; },
    // Expression: a+a+a
    [](const NativeContext& ctx) -> double { return 3*ctx.a; },
    // Expression: a+b*c
    [](const NativeContext& ctx) -> double { return ctx.a + ctx.b*ctx.c; },
    // Expression: a+b+c
    [](const NativeContext& ctx) -> double { return ctx.a + ctx.b + ctx.c; },
    // Expression: a+b-c
    [](const NativeContext& ctx) -> double { return ctx.a + ctx.b - ctx.c; },
    // Expression: a-b-c
    [](const NativeContext& ctx) -> double { return ctx.a - ctx.b - ctx.c; },
    // Expression: a/b/c
    [](const NativeContext& ctx) -> double { return ctx.a/(ctx.b*ctx.c); },
    // Expression: a^2+1
    [](const NativeContext& ctx) -> double { return std::pow(ctx.a, 2) + 1; },
    // Expression: a^2^3
    [](const NativeContext& ctx) -> double { return std::pow(ctx.a, 8); },
    // Expression: b^2*2
    [](const NativeContext& ctx) -> double { return 2*std::pow(ctx.b, 2); },
    // Expression: x*y*z
    [](const NativeContext& ctx) -> double { return ctx.x*ctx.y*ctx.z; },
    // Expression: x*y+z
    [](const NativeContext& ctx) -> double { return ctx.x*ctx.y + ctx.z; },
    // Expression: x*y-z
    [](const NativeContext& ctx) -> double { return ctx.x*ctx.y - ctx.z; },
    // Expression: x*y/z
    [](const NativeContext& ctx) -> double { return ctx.x*ctx.y/ctx.z; },
    // Expression: x+y*z
    [](const NativeContext& ctx) -> double { return ctx.x + ctx.y*ctx.z; },
    // Expression: x+y+z
    [](const NativeContext& ctx) -> double { return ctx.x + ctx.y + ctx.z; },
    // Expression: x+y-z
    [](const NativeContext& ctx) -> double { return ctx.x + ctx.y - ctx.z; },
    // Expression: x+y/z
    [](const NativeContext& ctx) -> double { return ctx.x + ctx.y/ctx.z; },
    // Expression: x-y*z
    [](const NativeContext& ctx) -> double { return ctx.x - ctx.y*ctx.z; },
    // Expression: x-y/z
    [](const NativeContext& ctx) -> double { return ctx.x - ctx.y/ctx.z; },
    // Expression: x/y*z
    [](const NativeContext& ctx) -> double { return ctx.x*ctx.z/ctx.y; },
    // Expression: x/y+z
    [](const NativeContext& ctx) -> double { return ctx.x/ctx.y + ctx.z; },
    // Expression: x/y-z
    [](const NativeContext& ctx) -> double { return ctx.x/ctx.y - ctx.z; },
    // Expression: x/y/z
    [](const NativeContext& ctx) -> double { return ctx.x/(ctx.y*ctx.z); },
    // Expression: (-1)^0
    [](const NativeContext& ctx) -> double { detail::unused(ctx); return 1; },
    // Expression: (pi*b)
    [](const NativeContext& ctx) -> double { return detail::kPi*ctx.b; },
    // Expression: (pi+b)
    [](const NativeContext& ctx) -> double { return ctx.b + detail::kPi; },
    // Expression: +1+2^3
    [](const NativeContext& ctx) -> double { detail::unused(ctx); return 9; },
    // Expression: -1^1-1
    [](const NativeContext& ctx) -> double { detail::unused(ctx); return -2; },
    // Expression: abs(a)
    [](const NativeContext& ctx) -> double { return std::fabs(ctx.a); },
    // Expression: cos(a)
    [](const NativeContext& ctx) -> double { return std::cos(ctx.a); },
    // Expression: sin(a)
    [](const NativeContext& ctx) -> double { return std::sin(ctx.a); },
    // Expression: tan(a)
    [](const NativeContext& ctx) -> double { return std::tan(ctx.a); },
    // Expression: (2*a)*2
    [](const NativeContext& ctx) -> double { return 4*ctx.a; },
    // Expression: (a)+(b)
    [](const NativeContext& ctx) -> double { return ctx.a + ctx.b; },
    // Expression: (a)-(b)
    [](const NativeContext& ctx) -> double { return ctx.a - ctx.b; },
    // Expression: (a+b)*2
    [](const NativeContext& ctx) -> double { return 2*ctx.a + 2*ctx.b; },
    // Expression: (a+b)*3
    [](const NativeContext& ctx) -> double { return 3*ctx.a + 3*ctx.b; },
    // Expression: (pi+pi)
    [](const NativeContext& ctx) -> double { detail::unused(ctx); return 2*detail::kPi; },
    // Expression: +1+2^+3
    [](const NativeContext& ctx) -> double { detail::unused(ctx); return 9; },
    // Expression: -a^(-b)
    [](const NativeContext& ctx) -> double { return -std::pow(ctx.a, -ctx.b); },
    // Expression: 1+a-2/3
    [](const NativeContext& ctx) -> double { return ctx.a + 1.0/3.0; },
    // Expression: 1.1*a^2
    [](const NativeContext& ctx) -> double { return 1.1000000000000001*std::pow(ctx.a, 2); },
};

constexpr std::size_t kNativeExpressionCount = sizeof(kNativeExpressions) / sizeof(kNativeExpressions[0]);

}  // namespace benchmark_data
}  // namespace mexce

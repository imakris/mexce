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

// Expression: 1
inline double native_expression_0(const NativeContext& ctx) {
    detail::unused(ctx);
    return 1;
}

// Expression: a
inline double native_expression_1(const NativeContext& ctx) {
    return ctx.a;
}

// Expression: +1
inline double native_expression_2(const NativeContext& ctx) {
    detail::unused(ctx);
    return 1;
}

// Expression: 2*a
inline double native_expression_3(const NativeContext& ctx) {
    return 2*ctx.a;
}

// Expression: 3*a
inline double native_expression_4(const NativeContext& ctx) {
    return 3*ctx.a;
}

// Expression: 4*a
inline double native_expression_5(const NativeContext& ctx) {
    return 4*ctx.a;
}

// Expression: 5*a
inline double native_expression_6(const NativeContext& ctx) {
    return 5*ctx.a;
}

// Expression: a*2
inline double native_expression_7(const NativeContext& ctx) {
    return 2*ctx.a;
}

// Expression: a+1
inline double native_expression_8(const NativeContext& ctx) {
    return ctx.a + 1;
}

// Expression: a+a
inline double native_expression_9(const NativeContext& ctx) {
    return 2*ctx.a;
}

// Expression: a+b
inline double native_expression_10(const NativeContext& ctx) {
    return ctx.a + ctx.b;
}

// Expression: a^b
inline double native_expression_11(const NativeContext& ctx) {
    return std::pow(ctx.a, ctx.b);
}

// Expression: +2+1
inline double native_expression_12(const NativeContext& ctx) {
    detail::unused(ctx);
    return 3;
}

// Expression: +2-1
inline double native_expression_13(const NativeContext& ctx) {
    detail::unused(ctx);
    return 1;
}

// Expression: +a*b
inline double native_expression_14(const NativeContext& ctx) {
    return ctx.a*ctx.b;
}

// Expression: +a+b
inline double native_expression_15(const NativeContext& ctx) {
    return ctx.a + ctx.b;
}

// Expression: -1^0
inline double native_expression_16(const NativeContext& ctx) {
    detail::unused(ctx);
    return -1;
}

// Expression: -2-1
inline double native_expression_17(const NativeContext& ctx) {
    detail::unused(ctx);
    return -3;
}

// Expression: -a*b
inline double native_expression_18(const NativeContext& ctx) {
    return -ctx.a*ctx.b;
}

// Expression: -a-b
inline double native_expression_19(const NativeContext& ctx) {
    return -ctx.a - ctx.b;
}

// Expression: -a/b
inline double native_expression_20(const NativeContext& ctx) {
    return -ctx.a/ctx.b;
}

// Expression: 2+-1
inline double native_expression_21(const NativeContext& ctx) {
    detail::unused(ctx);
    return 1;
}

// Expression: 2-+1
inline double native_expression_22(const NativeContext& ctx) {
    detail::unused(ctx);
    return 1;
}

// Expression: 2--1
inline double native_expression_23(const NativeContext& ctx) {
    detail::unused(ctx);
    return 3;
}

// Expression: a*+b
inline double native_expression_24(const NativeContext& ctx) {
    return ctx.a*ctx.b;
}

// Expression: a*-b
inline double native_expression_25(const NativeContext& ctx) {
    return -ctx.a*ctx.b;
}

// Expression: a+-b
inline double native_expression_26(const NativeContext& ctx) {
    return ctx.a - ctx.b;
}

// Expression: a/-b
inline double native_expression_27(const NativeContext& ctx) {
    return -ctx.a/ctx.b;
}

// Expression: a^-b
inline double native_expression_28(const NativeContext& ctx) {
    return std::pow(ctx.a, -ctx.b);
}

// Expression: (a*e)
inline double native_expression_29(const NativeContext& ctx) {
    return detail::kE*ctx.a;
}

// Expression: (a+e)
inline double native_expression_30(const NativeContext& ctx) {
    return ctx.a + detail::kE;
}

// Expression: (a-a)
inline double native_expression_31(const NativeContext& ctx) {
    detail::unused(ctx);
    return 0;
}

// Expression: (b*e)
inline double native_expression_32(const NativeContext& ctx) {
    return detail::kE*ctx.b;
}

// Expression: (b+a)
inline double native_expression_33(const NativeContext& ctx) {
    return ctx.a + ctx.b;
}

// Expression: (b-b)
inline double native_expression_34(const NativeContext& ctx) {
    detail::unused(ctx);
    return 0;
}

// Expression: (e*e)
inline double native_expression_35(const NativeContext& ctx) {
    detail::unused(ctx);
    return std::exp(2);
}

// Expression: +2+-1
inline double native_expression_36(const NativeContext& ctx) {
    detail::unused(ctx);
    return 1;
}

// Expression: +2-+1
inline double native_expression_37(const NativeContext& ctx) {
    detail::unused(ctx);
    return 1;
}

// Expression: +2--1
inline double native_expression_38(const NativeContext& ctx) {
    detail::unused(ctx);
    return 3;
}

// Expression: +a*+b
inline double native_expression_39(const NativeContext& ctx) {
    return ctx.a*ctx.b;
}

// Expression: +a+-b
inline double native_expression_40(const NativeContext& ctx) {
    return ctx.a - ctx.b;
}

// Expression: +a-+b
inline double native_expression_41(const NativeContext& ctx) {
    return ctx.a - ctx.b;
}

// Expression: +a--b
inline double native_expression_42(const NativeContext& ctx) {
    return ctx.a + ctx.b;
}

// Expression: +a/+b
inline double native_expression_43(const NativeContext& ctx) {
    return ctx.a/ctx.b;
}

// Expression: +a^+b
inline double native_expression_44(const NativeContext& ctx) {
    return std::pow(ctx.a, ctx.b);
}

// Expression: +a^-b
inline double native_expression_45(const NativeContext& ctx) {
    return std::pow(ctx.a, -ctx.b);
}

// Expression: -a*-b
inline double native_expression_46(const NativeContext& ctx) {
    return ctx.a*ctx.b;
}

// Expression: -a+-b
inline double native_expression_47(const NativeContext& ctx) {
    return -ctx.a - ctx.b;
}

// Expression: -a--b
inline double native_expression_48(const NativeContext& ctx) {
    return -ctx.a + ctx.b;
}

// Expression: -a/-b
inline double native_expression_49(const NativeContext& ctx) {
    return ctx.a/ctx.b;
}

// Expression: -a^+b
inline double native_expression_50(const NativeContext& ctx) {
    return -std::pow(ctx.a, ctx.b);
}

// Expression: -a^-b
inline double native_expression_51(const NativeContext& ctx) {
    return -std::pow(ctx.a, -ctx.b);
}

// Expression: 1+a^2
inline double native_expression_52(const NativeContext& ctx) {
    return std::pow(ctx.a, 2) + 1;
}

// Expression: 2*a+1
inline double native_expression_53(const NativeContext& ctx) {
    return 2*ctx.a + 1;
}

// Expression: a*2.2
inline double native_expression_54(const NativeContext& ctx) {
    return 2.2000000000000002*ctx.a;
}

// Expression: a*b*c
inline double native_expression_55(const NativeContext& ctx) {
    return ctx.a*ctx.b*ctx.c;
}

// Expression: a*b+c
inline double native_expression_56(const NativeContext& ctx) {
    return ctx.a*ctx.b + ctx.c;
}

// Expression: a+1.1
inline double native_expression_57(const NativeContext& ctx) {
    return ctx.a + 1.1000000000000001;
}

// Expression: a+a+a
inline double native_expression_58(const NativeContext& ctx) {
    return 3*ctx.a;
}

// Expression: a+b*c
inline double native_expression_59(const NativeContext& ctx) {
    return ctx.a + ctx.b*ctx.c;
}

// Expression: a+b+c
inline double native_expression_60(const NativeContext& ctx) {
    return ctx.a + ctx.b + ctx.c;
}

// Expression: a+b-c
inline double native_expression_61(const NativeContext& ctx) {
    return ctx.a + ctx.b - ctx.c;
}

// Expression: a-b-c
inline double native_expression_62(const NativeContext& ctx) {
    return ctx.a - ctx.b - ctx.c;
}

// Expression: a/b/c
inline double native_expression_63(const NativeContext& ctx) {
    return ctx.a/(ctx.b*ctx.c);
}

// Expression: a^2+1
inline double native_expression_64(const NativeContext& ctx) {
    return std::pow(ctx.a, 2) + 1;
}

// Expression: a^2^3
inline double native_expression_65(const NativeContext& ctx) {
    return std::pow(ctx.a, 8);
}

// Expression: b^2*2
inline double native_expression_66(const NativeContext& ctx) {
    return 2*std::pow(ctx.b, 2);
}

// Expression: x*y*z
inline double native_expression_67(const NativeContext& ctx) {
    return ctx.x*ctx.y*ctx.z;
}

// Expression: x*y+z
inline double native_expression_68(const NativeContext& ctx) {
    return ctx.x*ctx.y + ctx.z;
}

// Expression: x*y-z
inline double native_expression_69(const NativeContext& ctx) {
    return ctx.x*ctx.y - ctx.z;
}

// Expression: x*y/z
inline double native_expression_70(const NativeContext& ctx) {
    return ctx.x*ctx.y/ctx.z;
}

// Expression: x+y*z
inline double native_expression_71(const NativeContext& ctx) {
    return ctx.x + ctx.y*ctx.z;
}

// Expression: x+y+z
inline double native_expression_72(const NativeContext& ctx) {
    return ctx.x + ctx.y + ctx.z;
}

// Expression: x+y-z
inline double native_expression_73(const NativeContext& ctx) {
    return ctx.x + ctx.y - ctx.z;
}

// Expression: x+y/z
inline double native_expression_74(const NativeContext& ctx) {
    return ctx.x + ctx.y/ctx.z;
}

// Expression: x-y*z
inline double native_expression_75(const NativeContext& ctx) {
    return ctx.x - ctx.y*ctx.z;
}

// Expression: x-y/z
inline double native_expression_76(const NativeContext& ctx) {
    return ctx.x - ctx.y/ctx.z;
}

// Expression: x/y*z
inline double native_expression_77(const NativeContext& ctx) {
    return ctx.x*ctx.z/ctx.y;
}

// Expression: x/y+z
inline double native_expression_78(const NativeContext& ctx) {
    return ctx.x/ctx.y + ctx.z;
}

// Expression: x/y-z
inline double native_expression_79(const NativeContext& ctx) {
    return ctx.x/ctx.y - ctx.z;
}

// Expression: x/y/z
inline double native_expression_80(const NativeContext& ctx) {
    return ctx.x/(ctx.y*ctx.z);
}

// Expression: (-1)^0
inline double native_expression_81(const NativeContext& ctx) {
    detail::unused(ctx);
    return 1;
}

// Expression: (pi*b)
inline double native_expression_82(const NativeContext& ctx) {
    return detail::kPi*ctx.b;
}

// Expression: (pi+b)
inline double native_expression_83(const NativeContext& ctx) {
    return ctx.b + detail::kPi;
}

// Expression: +1+2^3
inline double native_expression_84(const NativeContext& ctx) {
    detail::unused(ctx);
    return 9;
}

// Expression: -1^1-1
inline double native_expression_85(const NativeContext& ctx) {
    detail::unused(ctx);
    return -2;
}

// Expression: abs(a)
inline double native_expression_86(const NativeContext& ctx) {
    return std::fabs(ctx.a);
}

// Expression: cos(a)
inline double native_expression_87(const NativeContext& ctx) {
    return std::cos(ctx.a);
}

// Expression: sin(a)
inline double native_expression_88(const NativeContext& ctx) {
    return std::sin(ctx.a);
}

// Expression: tan(a)
inline double native_expression_89(const NativeContext& ctx) {
    return std::tan(ctx.a);
}

// Expression: (2*a)*2
inline double native_expression_90(const NativeContext& ctx) {
    return 4*ctx.a;
}

// Expression: (a)+(b)
inline double native_expression_91(const NativeContext& ctx) {
    return ctx.a + ctx.b;
}

// Expression: (a)-(b)
inline double native_expression_92(const NativeContext& ctx) {
    return ctx.a - ctx.b;
}

// Expression: (a+b)*2
inline double native_expression_93(const NativeContext& ctx) {
    return 2*ctx.a + 2*ctx.b;
}

// Expression: (a+b)*3
inline double native_expression_94(const NativeContext& ctx) {
    return 3*ctx.a + 3*ctx.b;
}

// Expression: (pi+pi)
inline double native_expression_95(const NativeContext& ctx) {
    detail::unused(ctx);
    return 2*detail::kPi;
}

// Expression: +1+2^+3
inline double native_expression_96(const NativeContext& ctx) {
    detail::unused(ctx);
    return 9;
}

// Expression: -a^(-b)
inline double native_expression_97(const NativeContext& ctx) {
    return -std::pow(ctx.a, -ctx.b);
}

// Expression: 1+a-2/3
inline double native_expression_98(const NativeContext& ctx) {
    return ctx.a + 1.0/3.0;
}

// Expression: 1.1*a^2
inline double native_expression_99(const NativeContext& ctx) {
    return 1.1000000000000001*std::pow(ctx.a, 2);
}

static constexpr native_eval_fn kNativeExpressions[] = {
    native_expression_0,
    native_expression_1,
    native_expression_2,
    native_expression_3,
    native_expression_4,
    native_expression_5,
    native_expression_6,
    native_expression_7,
    native_expression_8,
    native_expression_9,
    native_expression_10,
    native_expression_11,
    native_expression_12,
    native_expression_13,
    native_expression_14,
    native_expression_15,
    native_expression_16,
    native_expression_17,
    native_expression_18,
    native_expression_19,
    native_expression_20,
    native_expression_21,
    native_expression_22,
    native_expression_23,
    native_expression_24,
    native_expression_25,
    native_expression_26,
    native_expression_27,
    native_expression_28,
    native_expression_29,
    native_expression_30,
    native_expression_31,
    native_expression_32,
    native_expression_33,
    native_expression_34,
    native_expression_35,
    native_expression_36,
    native_expression_37,
    native_expression_38,
    native_expression_39,
    native_expression_40,
    native_expression_41,
    native_expression_42,
    native_expression_43,
    native_expression_44,
    native_expression_45,
    native_expression_46,
    native_expression_47,
    native_expression_48,
    native_expression_49,
    native_expression_50,
    native_expression_51,
    native_expression_52,
    native_expression_53,
    native_expression_54,
    native_expression_55,
    native_expression_56,
    native_expression_57,
    native_expression_58,
    native_expression_59,
    native_expression_60,
    native_expression_61,
    native_expression_62,
    native_expression_63,
    native_expression_64,
    native_expression_65,
    native_expression_66,
    native_expression_67,
    native_expression_68,
    native_expression_69,
    native_expression_70,
    native_expression_71,
    native_expression_72,
    native_expression_73,
    native_expression_74,
    native_expression_75,
    native_expression_76,
    native_expression_77,
    native_expression_78,
    native_expression_79,
    native_expression_80,
    native_expression_81,
    native_expression_82,
    native_expression_83,
    native_expression_84,
    native_expression_85,
    native_expression_86,
    native_expression_87,
    native_expression_88,
    native_expression_89,
    native_expression_90,
    native_expression_91,
    native_expression_92,
    native_expression_93,
    native_expression_94,
    native_expression_95,
    native_expression_96,
    native_expression_97,
    native_expression_98,
    native_expression_99,
};

constexpr std::size_t kNativeExpressionCount = sizeof(kNativeExpressions) / sizeof(kNativeExpressions[0]);

}  // namespace benchmark_data
}  // namespace mexce

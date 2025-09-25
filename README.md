# mexce

A single-header, dependency-free JIT compiler for mathematical expressions.

## Overview

`mexce` is a runtime compiler for scalar mathematical expressions written in C++. It parses standard C-like expressions and compiles them directly into x86/x86-64 machine code that utilizes the x87 FPU.

Once an expression is compiled, subsequent evaluations are direct function calls, which avoids parsing and interpretation overhead. This makes `mexce` well-suited for applications that repeatedly evaluate the same formula with different inputs, such as numerical simulations, data processing kernels, or graphics.

The library is contained in a single header file (`mexce.h`) with no external dependencies.

### Requirements
*   **Platforms:** Windows, Linux
*   **Architectures:** x86, x86-64 (other architectures are not supported)
*   **Compiler:** Requires a C++11 compliant compiler.

## Installation

Copy `mexce.h` into your project's include path and `#include "mexce.h"`. No other steps are needed.

## Quick Start

The following example shows how to bind variables and evaluate an expression in a loop. A `mexce::evaluator` instance initializes to the constant expression `"0"`.

```cpp
#include <iostream>
#include "mexce.h"

int main() {
    float   x  = 0.0f;
    double  y  = 0.1;
    mexce::evaluator eval;

    // Associate runtime variables with aliases in the expression.
    eval.bind(x, "x", y, "y");

    eval.set_expression("sin(x) + y");

    // The evaluator can also be used for single-shot evaluations
    // without changing the main expression.
    double result = eval.evaluate("cos(pi / 4)");
    std::cout << "Single-shot evaluation: " << result << std::endl;

    // Loop with the main expression
    std::cout << "\nLoop evaluation results:" << std::endl;
    for (int i = 0; i < 5; ++i, x += 0.1f) {
        std::cout << "  " << eval.evaluate() << std::endl;
    }

    return 0;
}
```

## API Reference

#### `bind()`
Associates a C++ variable with a symbolic name.
*   **Signature:** `void bind(T& var, const std::string& name, ...);`
*   **Supported Types:** `double`, `float`, `int16_t`, `int32_t`, `int64_t`.
*   **Behavior:**
    *   Bound variables must outlive the `mexce::evaluator` instance.
    *   Throws `std::logic_error` if `name` collides with a built-in function or constant.

#### `unbind() / unbind_all()`
Removes one or all variable bindings.
*   **Signature:** `void unbind(const std::string& name, ...);`, `void unbind_all();`
*   **Behavior:**
    *   If a variable used by the currently compiled expression is unbound, the expression is safely reset to the constant `"0"`.
    *   Throws `std::logic_error` if `name` is unknown or empty.

#### `set_expression()`
Compiles an expression, making it the default for `evaluate()`.
*   **Signature:** `void set_expression(std::string expr);`
*   **Behavior:**
    *   Throws `mexce_parsing_exception` on syntax errors, providing the position of the error.
    *   Throws `std::logic_error` if the expression string is empty.

#### `evaluate()`
Executes the expression most recently compiled by `set_expression()`.
*   **Signature:** `double evaluate();`

#### `evaluate(const std::string&)`
Compiles and executes an expression for a single use without replacing the default expression.
*   **Signature:** `double evaluate(const std::string& expression);`

## Expression Syntax

`mexce` supports standard mathematical notation.

*   **Literals:** Numbers in decimal (`123.45`) or scientific (`1.2345e+02`) notation.
*   **Operators:** Infix operators with the following precedence:
    | Precedence | Operator | Function | Description |
    | :--- | :--- | :--- | :--- |
    | 1 (highest) | `^` | `pow` | Power / Exponentiation |
    | 2 | `*`, `/` | `mul`, `div` | Multiplication, Division |
    | 3 | `+`, `-` | `add`, `sub` | Addition, Subtraction |
    | 4 (lowest) | `<` | `less_than`| Less-than comparison |
*   **Unary Operators:** Unary `+` and `-` are supported.
*   **Comparison:** The `<` operator returns a `double` (`1.0` if true, `0.0` if false).

## Built-in Identifiers

### Constants
*   `pi`: The mathematical constant π.
*   `e`: Euler's number *e*.

### Functions
| Function | Description |
| :--- | :--- |
| `add(a,b)`, `sub(a,b)`, `mul(a,b)`, `div(a,b)` | Basic arithmetic. |
| `neg(x)` | Negation (unary minus). |
| `abs(x)` | Absolute value. |
| `mod(a,b)` | Modulo operator. |
| `min(a,b)`, `max(a,b)` | Minimum and maximum. |
| `sin(x)`, `cos(x)`, `tan(x)` | Trigonometric functions. |
| `pow(base, exp)` | General exponentiation. |
| `exp(x)` | Base-e exponent (`e^x`). |
| `sqrt(x)` | Square root. |
| `ln(x)` / `log(x)` | Natural logarithm. |
| `log2(x)`, `log10(x)` | Base-2 and Base-10 logarithms. |
| `logb(base, value)` | Logarithm with a custom base. |
| `ylog2(y, x)` | Computes `y * log2(x)`. |
| `ceil(x)`, `floor(x)`, `round(x)`, `int(x)` | Rounding functions. |
| `less_than(a, b)` | Returns `1.0` if `a < b`, else `0.0`. |
| `sign(x)` | Returns `-1.0` for negative `x`, `1.0` otherwise. |
| `signp(x)` | Returns `1.0` for positive `x`, `0.0` otherwise. |
| `bnd(x, period)` | Wraps `x` to the interval `[0, period)`. |
| `bias(x, a)`, `gain(x, a)` | Common tone-mapping curves (for inputs in `[0,1]`). |
| `expn(x)` | Returns the exponent part of `x`. |
| `sfc(x)` | Returns the significand (fractional part) of `x`. |

### Configuration
*   **`MEXCE_ACCURACY`:** Define this macro before including `mexce.h` to enable higher-precision polynomial refinements for `sin()` and `cos()`, trading a small runtime cost for improved accuracy.

## Performance Analysis

`mexce` is designed to produce code with performance comparable to a statically optimizing compiler. Its efficiency was measured using a benchmark suite of 44,229 expressions.

### Benchmark Methodology
*   **System:** AMD Ryzen 7 7840U CPU
*   **Compiler:** GNU GCC 13.1.0 with flags `-O3 -DNDEBUG -Wall -Wextra -Wpedantic`.
*   **Reference Standard:** A high-precision "golden reference" for each expression was generated using Python's **SymPy** library with arbitrary-precision rationals.
*   **Accuracy Metric (ULP):** **Units in the Last Place (ULP)** measures the distance between two floating-point numbers by counting how many representable values exist between them. A ULP of 0 means the numbers are identical. The ULP is computed via a monotonic mapping of floating-point values to integers and finding their absolute difference.

### Speed

The benchmark measures the average time per evaluation. For this scalar workload, `mexce`'s JIT-compiled code performed favorably against statically compiled C++ functions.

| Metric | Mexce | Native Compiler |
| :--- | :--- | :--- |
| **Functions Benchmarked** | 44,229 | 44,229 |
| **Average Runtime per Function** | **4.0 ns** | 5.0 ns |
| **Total Execution Time** | 19.50 sec | 21.34 sec |

The performance characteristics are attributed to the code generation strategy. For sequential scalar floating-point math, the x87 FPU's stack-based architecture can be more compact and efficient than the register-to-register operations of SSE/AVX instruction sets.

### Accuracy

`mexce`'s accuracy is comparable to that of the native compiler.

#### Accuracy Distribution (ULP)

| ULP Range      | Mexce vs Reference | Compiler vs Reference | Mexce vs Compiler |
|----------------|---------------------|-----------------------|-------------------|
| 0 (exact)      | 20,164             | 16,636               | 24,183           |
| 1–16           | 23,494             | 26,870               | 19,537           |
| 17–32          | 198                | 279                  | 181              |
| 33–64          | 136                | 152                  | 116              |
| 65–128         | 60                 | 97                   | 59               |
| 129–256        | 33                 | 50                   | 31               |
| 257–512        | 66                 | 31                   | 38               |
| 513–1024       | 13                 | 25                   | 17               |
| 1025–2048      | 13                 | 19                   | 11               |
| 2049–4096      | 21                 | 8                    | 12               |
| 4097–8192      | 6                  | 13                   | 12               |
| 8193–16,384    | 7                  | 8                    | 3                |
| 16,385–32,768  | 1                  | 2                    | 2                |
| 32,769–65,536  | 2                  | 1                    | 1                |
| >65,536        | 15                 | 31                   | 19               |  

#### Analysis of Large Deviations
The few cases with very large ULP deviations occur where the mathematically correct result is infinity. The symbolic reference engine correctly returns `inf`. However, finite-precision floating-point hardware correctly handles this by overflowing to a very large finite number. This is the expected behavior emulated by both `mexce` and the native compiler.

## Building the Benchmarks

The benchmark harness is included in the repository and can be run using CMake:

```bash
# Configure and build the project
cmake -S . -B build
cmake --build build

# Run quick validation tests
ctest --test-dir build

# Run the full performance benchmark
cmake --build build --target run_benchmarks
```
*   The benchmark harness requires **OpenMP** for its timer; this is not a dependency of the `mexce` library itself.

## License

The source code is licensed under the Simplified BSD License.

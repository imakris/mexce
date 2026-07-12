# mexce

A single-header, dependency-free JIT compiler for mathematical expressions.

[![Build & Benchmark](https://github.com/imakris/mexce/actions/workflows/main.yml/badge.svg)](https://github.com/imakris/mexce/actions/workflows/main.yml)

[![codecov](https://codecov.io/gh/imakris/mexce/branch/master/graph/badge.svg)](https://app.codecov.io/gh/imakris/mexce?branch=master)

## Overview

`mexce` is a runtime compiler for scalar mathematical expressions written in C++. It parses standard C-like expressions and compiles them directly into x86 or x86-64 machine code. On 64-bit systems, it uses **SSE2 instructions** for basic arithmetic and calls **C standard library** math functions for transcendentals. On 32-bit systems, or when higher internal precision is desired, the **x87 FPU** backend is used.

Once an expression is compiled, subsequent evaluations are direct function calls, which avoids parsing and interpretation overhead. This makes `mexce` well-suited for applications that repeatedly evaluate the same formula with different inputs, such as numerical simulations, data processing kernels, or graphics.

The ordinary library is contained in a single header file (`mexce.h`) with no
external dependencies. An opt-in protected-expression surface uses libsodium
1.0.22 to load authenticated, encrypted semantic programs.

### Requirements
*   **Platforms:** Windows, Linux
*   **Architectures:** x86, x86-64 (SSE2 backend requires x86-64; x87 backend works on both)
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
    double result = eval.evaluate("x + y * y");
    std::cout << "Single-shot evaluation with x=0: " << result << std::endl;

    // Loop with the main expression
    std::cout << "\nLoop evaluation results:" << std::endl;
    for (int i = 0; i < 5; ++i, x += 0.1f) {
        std::cout << "  " << eval.evaluate() << std::endl;
    }

    return 0;
}
```

This program prints:

```
Single-shot evaluation with x=0: 0.01

Loop evaluation results:
  0.1
  0.199833
  0.298669
  0.39552
  0.489418
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

#### `opts()`
Returns a mutable reference to the evaluator's options. Changes take effect on the next `set_expression()` call.
*   **Signature:** `options& opts();`
*   **Example:** `eval.opts().fast_math = true;`

#### `get_options() / set_options()`
Get or set the evaluator's options.
*   **Signatures:** `const options& get_options() const;`, `void set_options(const options& opts);`

## Runtime Options

Options can be configured before calling `set_expression()` to control code generation behavior:

```cpp
mexce::evaluator eval;
eval.enable_fast_math();           // Enable algebraic simplifications
eval.use_x87_backend();            // Use x87 FPU instead of SSE2
eval.enable_cse();                 // Enable common subexpression elimination
eval.set_expression("x + y");      // Options take effect here
```

| Method | Description |
| :--- | :--- |
| `enable_fast_math()` | Enables algebraic simplifications that may change results for special values (NaN, Inf). Examples: `x-x → 0`, `x/x → 1`, `0*x → 0`. |
| `use_x87_backend()` | Forces the x87 FPU backend instead of SSE2. The x87 backend uses 80-bit internal precision. On 32-bit x86, this backend is always used. |
| `enable_cse()` | Enables Common Subexpression Elimination. Repeated identical subexpressions are computed once and reused. Only works with the x87 backend. |

## Expression Syntax

`mexce` supports standard mathematical notation.

*   **Literals:** Numbers in decimal (`123.45`) or scientific (`1.2345e+02`) notation.
*   **Operators:** Infix operators with the following precedence:
    | Precedence | Operator | Function | Description |
    | :--- | :--- | :--- | :--- |
    | 1 (highest) | `^`, `**` | `pow` | Power / Exponentiation |
    | 2 | `*`, `/` | `mul`, `div` | Multiplication, Division |
    | 3 | `+`, `-` | `add`, `sub` | Addition, Subtraction |
    | 4 (lowest) | `<`, `>` | — | Less-than and greater-than comparison |
*   **Unary Operators:** Unary `+` and `-` are supported. Note that power operators (`^` and `**`) bind tighter than unary minus, so `-a**2` is evaluated as `-(a**2)`, matching Python semantics. Use parentheses to change the grouping: `(-a)**2`.
*   **Comparison:** The `<` and `>` operators return a `double` (`1.0` if true, `0.0` if false).

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
| `ceil(x)`, `floor(x)`, `round(x)`, `trunc(x)`, `int(x)` | Rounding functions. |
| `sign(x)` | Returns `-1.0` for negative `x`, `1.0` otherwise. |
| `signp(x)` | Returns `1.0` for positive `x`, `0.0` otherwise. |
| `bnd(x, period)` | Wraps `x` to the interval `[0, period)`. |
| `bias(x, a)`, `gain(x, a)` | Common tone-mapping curves (for inputs in `[0,1]`). |
| `expn(x)` | Returns the exponent part of `x`. |
| `sfc(x)` | Returns the significand (fractional part) of `x`. |

## Backends

`mexce` provides two code generation backends:

### SSE2 Backend (Default on x86-64)
*   Uses SSE2 scalar instructions (`addsd`, `mulsd`, etc.) for basic arithmetic
*   Uses SSE4.1 `roundsd` instruction for rounding functions (`floor`, `ceil`, `round`, `trunc`)
*   Calls C standard library math functions for transcendentals (`sin`, `cos`, `exp`, `log`, etc.)
*   Faster on modern CPUs due to better pipelining and avoiding x87 state transitions
*   Results in XMM0 register (standard x64 ABI return convention)

### x87 Backend (Default on 32-bit x86)
*   Uses x87 FPU instructions with 80-bit internal precision
*   All operations (including transcendentals) use native x87 instructions
*   Stack-based architecture can be more compact for certain expression patterns
*   Required for Common Subexpression Elimination (CSE) feature
*   On x86-64, enable with `eval.use_x87_backend();`

## Protected Expressions

Protected expressions let an application distribute a formula without shipping
its original expression text. The `mexce_protect` tool turns the formula into
an encrypted, authenticated `.mxp` program and a 32-byte key. The application
loads that pair, binds values by numeric slot, and evaluates the formula through
the usual `mexce::evaluator` interface.

This feature is optional, available on x64, and uses libsodium 1.0.22. Ordinary
users can continue to include only `mexce.h` with no external dependency.

### Build and install

Make libsodium 1.0.22 available through Conan, vcpkg, or pkg-config. For
example, with Conan:

```sh
conan install --requires=libsodium/1.0.22 --output-folder=deps \
  --generator=CMakeDeps --generator=CMakeToolchain \
  --settings=build_type=Release --build=missing
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE=deps/conan_toolchain.cmake \
  -DBUILD_TESTING=OFF \
  -DMEXCE_ENABLE_PROTECTED_EXPRESSIONS=ON \
  -DMEXCE_BUILD_ISSUER_TOOLS=ON
cmake --build build --config Release
cmake --install build --config Release --prefix install
```

Protected consumers link `mexce::protected`. The `MEXCE_BUILD_ISSUER_TOOLS`
option builds and installs the `mexce_protect` command used to create programs.

### Creating a protected program

Create an expression file, for example `expression.txt`:

```text
value+1
```

Then assign each variable a numeric runtime slot in `bindings.txt`:

```text
value=0
```

```sh
mexce_protect expression.txt bindings.txt expression.mxp expression.key
```

The command writes `expression.mxp` and `expression.key`. The two output paths
must be different and must not exist; remove an orphaned key before retrying an
interrupted run. Keep the key separate from the program and import it into the
application's key-delivery system. The slot schema must list every variable
exactly once, using consecutive slots from zero. The command uses strict math
semantics; applications that need fast-math semantics can use the encoder API
directly.

### Loading a protected program

At runtime, bind application values to the same slots and load the program:

```cpp
const std::vector<uint8_t> program = read_file("expression.mxp");
std::vector<uint8_t> raw_key = obtain_unwrapped_key_from_host();
Raw_key_wipe_guard raw_key_wipe(raw_key); // Defined in protected_example.cpp.
auto key = mexce::Protected_expression_key::from_bytes(
    raw_key.data(), raw_key.size());

double value = 2.0;
mexce::evaluator evaluator;
evaluator.bind_protected(value, 0);
evaluator.set_protected_expression(
    program.data(), program.size(), std::move(key));
const double result = evaluator.evaluate();
```

`set_protected_expression` verifies and compiles the program, then consumes the
move-only key. The `.mxp` file keeps the formula encrypted and detects
modification when it is loaded. A failed load leaves the evaluator without an
executable expression. The application process supplies the key and is the
trusted endpoint. The complete
[`protected_example.cpp`](protected_example.cpp) includes file loading and a
reusable key-wiping guard.

## Performance Analysis

`mexce` is designed to produce code with performance comparable to a statically optimizing compiler. Its efficiency was measured using a benchmark suite of 44,229 expressions on GitHub Actions CI (Ubuntu runner).

### Benchmark Results

| Configuration | Avg Compile | Avg Eval | Total Eval |
| :--- | ---: | ---: | ---: |
| **Native (baseline)** | — | 7.0 ns | 32.7 ms |
| **SSE2** | 174 μs | **6.0 ns** | 27.2 ms |
| **SSE2 + fast-math** | 177 μs | **6.0 ns** | 26.8 ms |
| **x87** | 135 μs | 8.0 ns | 34.8 ms |
| **x87 + fast-math** | 138 μs | 8.0 ns | 34.7 ms |

**Key observations:**
*   The SSE2 backend performs on par with or faster than native compiler-generated code
*   The SSE2 backend is faster than the x87 backend due to better pipelining on modern CPUs
*   The `fast_math` option provides modest improvement through algebraic simplification
*   Compilation time is in the microsecond range, negligible for most use cases

### Protected-expression overhead

Protected programs add work when they are created and loaded, but execute the
same generated code as clear expressions after compilation. Release benchmarks
measured six representative formulas on x64 with MSVC, GCC, and Clang. Each
phase ran in an isolated process, with seven retained runs per formula.

| Phase | Observed median range |
| :--- | ---: |
| Create protected program | 17.1-66.3 us per expression |
| Load and compile | 1.32-2.65x clear compilation |
| Repeated evaluation | 0.94-1.03x clear evaluation |

The cost is therefore concentrated at program creation and startup. No material
steady-state evaluation penalty appeared in the retained measurements. See the
[full methodology and per-compiler results](analysis/performance_acceptance.md).

### Accuracy

Both backends produce results comparable to the native compiler. The table below shows accuracy measured in **Units in the Last Place (ULP)** against a high-precision reference computed with SymPy.

| ULP Range | Native | SSE2 | x87 |
| :--- | ---: | ---: | ---: |
| 0 (exact) | 16,636 | 17,103 | 20,323 |
| 1–16 | 26,871 | 26,254 | 23,347 |
| 17–32 | 279 | 246 | 182 |
| 33–64 | 152 | 154 | 138 |
| 65–128 | 96 | 88 | 62 |
| >128 | 195 | 139 | 177 |

**Notes:**
*   The x87 backend produces more exact results due to 80-bit internal precision
*   Large ULP deviations occur in edge cases involving infinity or very large numbers
*   Both backends match the native compiler's handling of special values

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

Using the protected build above with `BUILD_TESTING=ON`, run the protected
benchmark with:

```bash
ctest --test-dir build -C Release -R mexce_protected_benchmark --verbose
python analysis/run_protected_benchmarks.py \
  build/protected_benchmark protected_benchmark_results.json \
  --repeats 3 --resource-repeats 1
```

## License

The source code is licensed under the Simplified BSD License.

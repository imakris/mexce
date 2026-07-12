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

### Protected-expression build

Protected expressions are x64-only and remain off by default. Make libsodium
1.0.22 available through Conan, vcpkg, or pkg-config, then configure the source
build with:

```sh
cmake -S . -B build \
  -DBUILD_TESTING=OFF \
  -DMEXCE_ENABLE_PROTECTED_EXPRESSIONS=ON \
  -DMEXCE_BUILD_ISSUER_TOOLS=ON
cmake --build build --config Release
cmake --install build --config Release --prefix install
```

Protected consumers link `mexce::protected`; ordinary consumers continue to
link `mexce::mexce` without a cryptographic dependency. The
`MEXCE_BUILD_ISSUER_TOOLS` option adds and installs `mexce_protect`, and requires
protected expressions to be enabled.

### Creating a protected program

`mexce_protect` is an issuer-side build tool, not a licence system. It accepts
an expression file, a binding-schema file, a program output, and a key output.
For `expression.txt`:

```text
value+1
```

For `bindings.txt`:

```text
value=0
```

```sh
mexce_protect expression.txt bindings.txt expression.mxp expression.key
```

Schema entries use `name=decimal_slot`, one per line. Names use
`[A-Za-z_][A-Za-z0-9_]*`, slots are unique and dense from zero, and the schema
must exactly describe the variables used by the expression. The tool removes
one terminal LF, plus its preceding CR when present, from the expression. It
does not otherwise normalize source whitespace.

The command-line issuer encodes `Protected_math_mode::STRICT`. Applications
that intentionally require `FAST` policy can use the issuer-side encoder API.

The program and key destinations must be distinct and absent. The tool rejects
symlink or reparse-point traversal, never overwrites a destination, restricts
the 32-byte raw key file to the current owner, and publishes the key before the
program. A failed publication can therefore leave a key without a program, but
not a program published by that invocation without its key. Remove an orphan
key explicitly before retrying; the tool will not guess or overwrite partial
state. Filesystem or machine power-loss atomicity is not claimed.

Immediately import the raw key into the host product's key-wrapping or secure
delivery system. After confirming that import, remove the caller-owned final
key file according to the host platform's data-retention policy. Do not ship
the raw key beside the protected program.

### Loading a protected program

The complete example is in `protected_example.cpp`. The runtime sequence is:

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

The key is move-only. `set_protected_expression` consumes it, authenticates and
compiles the program, and leaves the evaluator empty on failure. Runtime slots
are authenticated numbers, not source variable names or C++ types.
Construct the caller-owned raw-key wipe guard before `from_bytes`, as in the
example, so invalid keys and allocation or locking failures also wipe the vector.

### Protected-expression security boundary

Protected artifacts conceal and authenticate semantic operations, literal
bits, variable-slot use, and compiler policy against someone who can inspect or
modify stored artifacts but does not have the matching key and cannot modify
the trusted process. Variable names, comments, whitespace, parentheses, and
original spelling are not stored in the artifact.

This does not provide confidentiality against an attacker who controls the
licensed process, debugger, process memory, registers, libsodium calls, or
emitted native code. It is not virtual-black-box obfuscation, anti-debugging,
white-box cryptography, issuer authentication, freshness, rollback protection,
revocation, device binding, or a licence system. Key storage, transport,
wrapping, device and licence policy, and issuer security belong to the host
product. A valid replayed program and matching key are accepted.

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

## License

The source code is licensed under the Simplified BSD License.

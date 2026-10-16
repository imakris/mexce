# mexce

A single-header, dependency-free JIT compiler for mathematical expressions.

[![Windows CI](https://github.com/imakris/mexce/actions/workflows/ci-windows.yml/badge.svg)](https://github.com/imakris/mexce/actions/workflows/ci-windows.yml)
[![Linux CI](https://github.com/imakris/mexce/actions/workflows/ci-linux.yml/badge.svg)](https://github.com/imakris/mexce/actions/workflows/ci-linux.yml)
[![macOS CI](https://github.com/imakris/mexce/actions/workflows/ci-macos.yml/badge.svg)](https://github.com/imakris/mexce/actions/workflows/ci-macos.yml)

[![codecov](https://codecov.io/gh/imakris/mexce/branch/master/graph/badge.svg)](https://app.codecov.io/gh/imakris/mexce?branch=master)

## Overview

`mexce` is a runtime compiler for scalar mathematical expressions written in C++. It parses standard C-like expressions and compiles them directly into x86, x86-64, or Apple Silicon ARM64 machine code. The x86-64 and ARM64 backends use hardware double-precision arithmetic and **C standard library** math functions for transcendentals. On 32-bit x86, or when higher internal precision is desired on x86-64, the **x87 FPU** backend is used.

Once an expression is compiled, subsequent evaluations are direct function calls, which avoids parsing and interpretation overhead. This makes `mexce` well-suited for applications that repeatedly evaluate the same formula with different inputs, such as numerical simulations, data processing kernels, or graphics.

The ordinary library is contained in a single header file (`mexce.h`) with no
external dependencies. An opt-in protected-expression surface uses libsodium
1.0.22 or newer to load authenticated, encrypted semantic programs.

### Requirements
*   **Platforms:** Windows, Linux, macOS
*   **Architectures:** x86 and x86-64 on Windows and Linux; x86-64 and ARM64 on macOS
*   **Compiler:** Requires a C++11 compliant compiler.

## Installation

Copy `mexce.h` into your project's include path and `#include "mexce.h"`. No other steps are needed.

On hardened-runtime macOS hosts, generated code requires an architecture-specific
entitlement. Apple Silicon also requires a single shared `MAP_JIT` arena in the
process. Read [executable-memory requirements](docs/building.md#macos-executable-memory)
before embedding in a signed host.

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
Bound variables must remain alive while the evaluator can use them. Set options
before compiling; repeated `evaluate()` calls use the compiled default, while
`evaluate(expression)` preserves that default.

## Documentation

- [Evaluator reference](docs/api.md): bindings, options, expression syntax,
  built-in functions and backend behavior.
- [Protected expressions](docs/protected_expressions.md): encrypted programs,
  issuance, numeric slots and runtime key ownership. This optional component
  requires libsodium 1.0.22 or newer; ordinary header-only use does not.
- [Building and deployment](docs/building.md): tests, benchmarks, macOS
  entitlements and JIT memory requirements.
- [Release performance evidence](analysis/performance_acceptance.md): attributed
  measurements and methodology.

## License

The source code is licensed under the [Simplified BSD License](LICENSE.txt).

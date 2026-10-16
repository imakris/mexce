# Evaluator reference

## Binding and evaluation

### `bind()`

Associates a C++ variable with a symbolic name.

*   **Signature:** `void bind(T& var, const std::string& name, ...);`
*   **Supported Types:** `double`, `float`, `int16_t`, `int32_t`, `int64_t`.
*   **Behavior:**
    *   Bound variables must outlive the `mexce::evaluator` instance.
    *   Rebinding a name affects subsequent compilations. An existing compiled
        expression retains its captured binding; unbinding that name still
        resets the expression to `"0"` when it uses the name.
    *   Throws `std::logic_error` if `name` collides with a built-in function or constant.

### `unbind() / unbind_all()`

Removes one or all variable bindings.

*   **Signature:** `void unbind(const std::string& name, ...);`, `void unbind_all();`
*   **Behavior:**
    *   If a variable used by the currently compiled expression is unbound, the expression is safely reset to the constant `"0"`.
    *   Throws `std::logic_error` if `name` is unknown or empty.

### `set_expression()`

Compiles an expression, making it the default for `evaluate()`.

*   **Signature:** `void set_expression(std::string expr);`
*   **Behavior:**
    *   Throws `mexce_parsing_exception` on syntax errors, providing the position of the error.
    *   Throws `std::logic_error` if the expression string is empty.

### `evaluate()`

Executes the expression most recently compiled by `set_expression()`.

*   **Signature:** `double evaluate();`

### `evaluate(const std::string&)`

Compiles and executes an expression for a single use without replacing the default expression.
It uses the current named bindings and default evaluator options; instance option
changes apply to `set_expression()`. To evaluate with configured options, call
`set_expression()` followed by `evaluate()`.

*   **Signature:** `double evaluate(const std::string& expression);`

### `opts()`

Returns a mutable reference to the evaluator's options. Changes take effect on the next `set_expression()` call.

*   **Signature:** `options& opts();`
*   **Example:** `eval.opts().fast_math = true;`

### `get_options() / set_options()`

Get or set the evaluator's options.

*   **Signatures:** `const options& get_options() const;`, `void set_options(const options& opts);`

## Runtime Options

Options can be configured before calling `set_expression()` to control code generation behavior:

```cpp
mexce::evaluator eval;
eval.enable_fast_math();           // Allow additional special-value assumptions
eval.use_x87_backend();            // Use x87 FPU instead of SSE2
eval.enable_cse();                 // Enable common subexpression elimination
eval.set_expression("x + y");      // Options take effect here
```

| Method | Description |
| :--- | :--- |
| `enable_fast_math()` | Allows additional algebraic simplifications that may change results for special values (NaN, Inf). Examples: `x-x → 0`, `x/x → 1`, `0*x → 0`. |
| `use_x87_backend()` | Selects the x87 FPU backend on x86, with 80-bit internal precision. On Apple Silicon this preference is accepted and the ARM64 backend is used. |
| `enable_cse()` | Enables Common Subexpression Elimination. Repeated identical subexpressions are computed once and reused. Supported by the x87 and ARM64 backends. |

Algebraic simplification is enabled by default to reduce arithmetic work and
improve numerical accuracy. Addition, subtraction, multiplication, and division
can be regrouped and common terms combined; source grouping does not prescribe
every floating-point intermediate. `fast_math` additionally permits the
special-value assumptions listed above.

On Apple Silicon, x86 backend preferences in `options` are compatibility
no-ops; `get_backend()` reports `backend_type::arm64`. Fast-math and CSE options
retain their meaning. ARM64 evaluates in `double` precision; it does not emulate
x87 extended precision.

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

Source unary minus participates in algebraic simplification as subtraction
from zero. In default mode, `-0.0` therefore produces positive zero. The
explicit `neg(x)` function directly changes the sign, so `neg(0.0)` produces
negative zero.

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
| `sign(x)` | Returns `-1.0` for negative `x`, `0.0` for zero and `1.0` for positive `x`. |
| `signp(x)` | Returns `1.0` for positive `x`, `0.0` otherwise. |
| `bnd(x, period)` | Wraps finite `x` to `[0, period)` for a finite positive period. |
| `bias(x, a)`, `gain(x, a)` | Common tone-mapping curves (for inputs in `[0,1]`). |
| `expn(x)` | Returns the exponent part of `x`. |
| `sfc(x)` | Returns the significand (fractional part) of `x`. |

Constant power exponents `+0.5` and `-0.5` use `sqrt(x)` and `1/sqrt(x)`
respectively. These shortcuts preserve the sign of zero: an input of `-0.0`
produces `-0.0` and negative infinity respectively. A runtime-bound exponent
uses the general power operation and can differ from these shortcuts.

Nested powers deliberately follow an algebraic convention, including in strict
mode: `(x^b)^n` can become `x^(b*n)` when both exponents are constants, `n` is an
integer, `b` is `+0.5`, `-0.5` or an integer with magnitude at most 65,536, and
the combined exponent is finite. For `x = -4`, `(x^0.5)^2` therefore returns
`-4`, while `sqrt(x)^2` produces NaN. General fractional inner exponents such
as `1.5` do not use this rewrite.

## Backends

`mexce` selects a code generation backend for the target architecture:

Intermediate precision and platform math libraries can produce different
floating-point results. The library does not promise bit-for-bit equality
across architectures or backends.

### ARM64 Backend (Apple Silicon)
*   Emits native AArch64 scalar instructions for double-precision arithmetic
*   Calls C standard library math functions using the macOS ARM64 ABI
*   Supports common subexpression elimination and fast-math optimizations
*   Uses macOS JIT memory write protection and instruction-cache invalidation
*   Reports `backend_type::arm64` through `get_backend()`

Evaluation uses 16 bytes of stack per simultaneously live operand, plus the
function frame and math-library calls. Clear expressions have no
fixed operand-depth limit, so applications must provide enough thread stack for
the expressions they evaluate. Protected programs have a validated operand-depth
limit of 1,024; that limit does not account for the caller's existing stack use.

### SSE2 Backend (Default on x86-64)
*   Uses SSE2 scalar instructions (`addsd`, `mulsd`, etc.) for basic arithmetic
*   Uses SSE4.1 `roundsd` instruction for rounding functions (`floor`, `ceil`, `round`, `trunc`)
*   Calls C standard library math functions for transcendentals (`sin`, `cos`, `exp`, `log`, etc.)
*   Faster on modern CPUs due to better pipelining and avoiding x87 state transitions
*   Results in XMM0 register (standard x64 ABI return convention)

Rounding on this backend requires SSE4.1; there is no automatic CPU-feature
check. On CPUs without SSE4.1, select `use_x87_backend()` before
`set_expression()` and execute with `evaluate()`.

### x87 Backend (Default on 32-bit x86)
*   Uses x87 FPU instructions with 80-bit internal precision
*   Uses native x87 arithmetic, including periodic wrapping, and transcendental instructions
*   Stack-based architecture can be more compact for certain expression patterns
*   Supports Common Subexpression Elimination (CSE)
*   On x86-64, enable with `eval.use_x87_backend();`

On x86, compilation is limited by simultaneously live intermediate values.
SSE2 uses six operand registers on Windows and eight on Linux/macOS, falling
back to x87 when needed. x87 has eight stack registers; expressions that exceed
its capacity after algebraic simplification throw `std::overflow_error` during
compilation.

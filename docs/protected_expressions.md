# Protected expressions

Protected expressions let an application distribute a formula without shipping
its original expression text. The `mexce_protect` tool turns the formula into
an encrypted, authenticated `.mxp` program and a 32-byte key. The application
loads that pair, binds values by numeric slot, and evaluates the formula through
the usual `mexce::evaluator` interface.

This feature is optional, available on x86-64 and Apple Silicon ARM64, and uses
libsodium 1.0.22 or newer. Ordinary users can continue to include only `mexce.h`
with no external dependency.
The encrypted program format is shared across architectures; programs issued
on x86-64 can be loaded on Apple Silicon and vice versa. This program portability
does not promise identical evaluation bits: platform math libraries and
intermediate precision, including `long double`, can differ.

## Build integration

Enable `MEXCE_ENABLE_PROTECTED_EXPRESSIONS` and link `mexce::protected`.
`MEXCE_BUILD_ISSUER_TOOLS` also builds and installs `mexce_protect`.
See [building](building.md#protected-expressions) for dependencies and commands.

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

Every existing output-parent component must be a real directory; the issuer
rejects symbolic-link traversal. On macOS, `/tmp` and `/var` are symbolic-link
aliases. Resolve the existing parent directory with `realpath`, or use its
physical path such as `/private/tmp` or `/private/var`, then append the new
output filename. The final output itself must not exist.

On macOS, the issuer requires a filesystem with atomic file cloning, such as
APFS. It creates the key with owner-only access and preserves that restriction
when the destination directory has inheritable ACL permissions. Filesystems
without the required publication support produce an error.
The macOS issuer also uses Apple's exported private `pthread_fchdir_np` API
to create the key securely in a pinned directory without changing the caller's
working directory. This dependency belongs to the issuer tool.

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
[`protected_example.cpp`](../protected_example.cpp) includes file loading and a
reusable key-wiping guard.

# Protected expressions

Protected expressions are intended for applications that distribute formulas
separately from their main executable. The `mexce_protect` tool converts a clear
expression into an encrypted and authenticated program. A matching key lets the
receiving application verify, compile, and evaluate that program without
storing the original expression text in the distributed artifact.

Protected expressions are available on x64 and require libsodium 1.0.22.

## Build

Make libsodium available through Conan, vcpkg, or pkg-config. The following
example resolves it with Conan, then enables the protected library and the
`mexce_protect` command-line tool:

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

CMake consumers link `mexce::protected`. The ordinary `mexce::mexce` target
remains header-only and has no libsodium dependency.

## Create a program

`mexce_protect` takes four paths:

```text
mexce_protect EXPRESSION BINDINGS PROGRAM KEY
```

For an expression file containing:

```text
price * quantity + shipping
```

the binding schema could be:

```text
price=0
quantity=1
shipping=2
```

Create the protected program with:

```sh
mexce_protect expression.txt bindings.txt pricing.mxp pricing.key
```

Each schema line has the form `name=slot`. Names follow the same identifier
rules as clear expressions. Slots must be unique and consecutive from zero,
and the schema must list exactly the variables used by the expression.

The command removes a final line ending from the expression file and preserves
all other source whitespace.

### Output handling

The program and key paths must be different, and both must be new files. The
tool rejects linked output paths, restricts the key file to the current owner,
and publishes the key before the program. If publication is interrupted between
those writes, remove the orphaned key before retrying.

The command-line tool uses `Protected_math_mode::STRICT`. Applications that
require `Protected_math_mode::FAST` can call the encoder API in
`mexce_protected_encoder.h`.

## Load and evaluate

Bind runtime values to the same numeric slots used in the binding schema:

```cpp
const std::vector<uint8_t> program = read_file("pricing.mxp");
std::vector<uint8_t> raw_key = obtain_unwrapped_key_from_host();
Raw_key_wipe_guard raw_key_wipe(raw_key);
auto key = mexce::Protected_expression_key::from_bytes(
    raw_key.data(), raw_key.size());

double price = 12.50;
double quantity = 3.0;
double shipping = 4.99;

mexce::evaluator evaluator;
evaluator.bind_protected(price, 0);
evaluator.bind_protected(quantity, 1);
evaluator.bind_protected(shipping, 2);
evaluator.set_protected_expression(
    program.data(), program.size(), std::move(key));

const double total = evaluator.evaluate();
```

The key is move-only and is consumed by `set_protected_expression`. The method
authenticates the program before compiling it. If loading fails, the evaluator
is left without an executable expression.

The complete [`protected_example.cpp`](../protected_example.cpp) includes file
loading and a key-wiping guard suitable for adapting to an application's own
key provider.

## Key handling

Treat the generated `.key` file as a short-lived transfer format. Import it
into the application's key-wrapping or delivery system, confirm the import, and
then remove the raw file according to the application's retention policy. Ship
the `.mxp` program and deliver its key through separate channels.

Protection applies to the formula artifact while it is stored or distributed.
The runtime application is the trusted endpoint: it supplies the key, binds the
inputs, and executes the generated code.

## Performance

Creating a program and authenticating it during loading add one-time work.
After compilation, protected and clear expressions use the same generated-code
path. Across the MSVC, GCC, and Clang release measurements, protected loading
and compilation took 1.32-2.65 times the clear compilation time, while repeated
evaluation measured 0.94-1.03 times the clear path.

See [`analysis/performance_acceptance.md`](../analysis/performance_acceptance.md)
for the benchmark design, per-compiler measurements, resource limits, and
acceptance criteria.

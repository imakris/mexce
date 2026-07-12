# Keyed Semantic Expression Protection

## Objective

Add an optional protected-expression capability to MEXCE. The runtime receives:

- an opaque binary semantic program;
- a separate 256-bit artifact key; and
- variable bindings addressed by numeric slots.

The runtime never receives expression source text and never reconstructs source
text or a complete clear serialized program. It authenticates and decodes one
fixed-size semantic record at a time, validates the postfix program, constructs
the existing compiler representation, emits native code through the existing
backends, and erases transient key and record storage.

The issuer-side encoder accepts clear source in a trusted build or
licence-issuance environment. It resolves the source grammar before encryption.
Lexical spelling, comments, whitespace, parentheses, and character boundaries
do not appear in the protected artifact.

## Product invariants

1. The protected runtime API accepts only opaque program bytes and a separate
   key. There is no plaintext overload or automatic input detection.
2. Protection operates on canonical semantic records, not characters, source
   tokens, or a stable secret opcode substitution table.
3. The wire program is backend-neutral and portable between supported Windows
   and Linux builds.
4. Clear and protected compilation share semantic validation, compiler object
   construction, optimization, backend selection, and native-code emission.
   There is no protected-only compiler backend.
5. Authenticated encryption is provided by libsodium secretstream using
   XChaCha20-Poly1305. MEXCE does not implement a cipher, MAC, or custom
   composition.
6. The canonical encoder generates a fresh random 256-bit key and secretstream
   header for every artifact. Its API has no caller-supplied-key overload, so
   encoder key reuse is not representable.
7. Key storage, key transport, device binding, issuer identity, licence
   signing, rollback policy, and revocation belong to the host product.
8. No complete clear semantic-record sequence exists in runtime memory. At
   most one decrypted wire record is held at a time.
9. Invalid, truncated, reordered, substituted, oversized, or trailing input
   hard-fails and leaves the evaluator empty.
10. Existing set_expression remains the one unprotected source API. The
    protected capability has one encoder API and one runtime load API.
11. Existing clear-expression behavior remains unchanged except that a failed
    replacement has a defined empty-state contract instead of stale-result or
    null-call behavior.
12. The implementation remains C++11-compatible.
13. Ordinary MEXCE remains header-only and dependency-free. Only protected
    consumers include the protected headers and link libsodium.
14. The definition and layout of evaluator are identical in every translation
    unit. Protected support never changes class layout through a preprocessor
    definition.

## Scope exclusions

The capability does not provide:

- confidentiality against an attacker controlling the licensed process;
- protection of emitted native instructions from disassembly;
- virtual-black-box obfuscation;
- anti-debugging, white-box cryptography, or self-modifying code;
- TPM, licence, certificate, revocation, or device-enrollment behavior;
- issuer authentication or artifact freshness;
- record-count padding;
- source-language encryption or format-preserving encryption;
- protected CSE;
- protected x86 package support; or
- support for architectures outside MEXCE's declared targets.

The host may bind the artifact key to a licence, device, product version, or
monotonic policy. MEXCE treats a replayed valid artifact and matching key as a
valid input.

## Existing architecture constraints

MEXCE is a C++11 single-header JIT. evaluator owns variable bindings, compiler
state, and executable memory. Clear compilation currently performs lexical
parsing, postfix conversion, compiler-object construction, optimization,
fallback selection, and code emission in one set_expression path.

Both the x87 and SSE2 backends consume the same compiler representation.
Optimizers mutate that representation. Some emitted x87 instructions retain
addresses into constants and CSE storage owned outside the final visible graph.
The compilation lifetime owner must therefore include every object whose
address can be embedded in code, not only the final graph.

The safe shared boundary is a canonical semantic producer feeding one semantic
validator/compiler:

~~~
clear source -> lexical parser -> semantic records -> validator/compiler

protected frames ---------------------------> validator/compiler
~~~

The clear route passes semantic records directly without serializing them. The
protected route decrypts one wire record and immediately applies it. Both routes
converge before linking, optimization, backend selection, and code emission.

The cryptographic construction follows the maintained libsodium contracts:

- https://doc.libsodium.org/secret-key_cryptography/secretstream
- https://doc.libsodium.org/memory_management

The protected dependency is libsodium 1.0.22. Conan Center and the controlled
vcpkg baseline both provide that release.

## Security model

### Protected assets

- semantic operations and their order;
- numeric literal bits;
- variable-slot use and repetition;
- authenticated compiler policy;
- integrity of the program supplied to the JIT; and
- the artifact key while MEXCE owns it.

Variable names are absent rather than encrypted. Source comments, whitespace,
parentheses, and original spelling are removed before encoding.

### Trusted computing base

The executing MEXCE code, libsodium implementation, operating-system process
isolation, and host key-delivery path are trusted. Replacing or patching any of
them can bypass verification or capture clear state and is outside the static
artifact claim.

### Static artifact adversary

The adversary can read, replace, truncate, reorder, or modify opaque artifacts
and other untrusted stored product data. The adversary does not possess the
matching artifact key and cannot modify the trusted computing base.

The format provides authenticated confidentiality and integrity of semantic
records against this adversary. It does not authenticate a vendor identity;
authentication means consistency under the supplied symmetric key.

### Licensed local observer

The observer possesses a valid installation and can inspect ordinary files and
process behavior but does not control process memory or a debugger. Host-bound
key delivery and short MEXCE key ownership increase the effort needed to recover
the expression, but provide an obfuscation or delay benefit rather than a
cryptographic secrecy guarantee.

### Licensed process controller

The controller can inspect or modify process memory, registers, branches,
libsodium calls, or emitted code. Confidentiality is not claimed against this
attacker. The controller can recover transient records, compiler objects, or
equivalent native semantics.

### Issuer compromise

An attacker controlling the issuer or build environment can read source and
generated keys. Issuer hardening and key-handling policy are outside MEXCE.

### Required rejection behavior

The runtime rejects:

- random corruption and use of the wrong key;
- unsupported format versions or public flags;
- ciphertext, tag, header, or authenticated-data modification;
- frame insertion, removal, reordering, duplication, or truncation;
- missing, early, or repeated final tags;
- trailing bytes or records;
- unknown record kinds or operation IDs;
- non-zero reserved fields and unused operands;
- invalid manifest policy;
- stack underflow, excess semantic depth, or invalid terminal depth;
- missing, unused, out-of-range, or unbound variable slots;
- non-canonical record sequences;
- inputs above declared resource limits; and
- any failure that would otherwise leave partial compiled state reachable.

### Accepted leakage

The artifact reveals:

- that it is a MEXCE protected program;
- format version and public flags;
- a random program identifier;
- fixed record size;
- total semantic record count; and
- approximate formula complexity derived from that count.

Runtime observation can additionally reveal evaluation timing, native-code
size, bound addresses, outputs, and everything observable by a process
controller.

## End-to-end behavior

### Issuer flow

1. Validate the source and binding-schema resource limits.
2. Parse source with the existing grammar and an issuer symbol table.
3. Emit canonical postfix semantic records through the shared producer.
4. Validate record order, stack behavior, operation IDs, and slot use.
5. Generate a fresh Protected_expression_key and random program identifier.
6. Initialize one XChaCha20-Poly1305 secretstream.
7. Encrypt each 32-byte record as a separate message. The END record alone
   carries TAG_FINAL.
8. Return the opaque bytes, move-only key, and program identifier. No API writes
   them to disk implicitly.

### Runtime flow

1. Bind caller-owned variables to numeric slots.
2. Transfer an opaque byte range and a Protected_expression_key into
   set_protected_expression.
3. Invalidate the previous compiled expression and enter the private loading
   state.
4. Validate bounded public framing before allocating compiler state.
5. Initialize secretstream pull state. Erase the transferred key immediately
   after initialization, whether initialization succeeds or fails.
6. Authenticate and decrypt one frame into a fixed 32-byte buffer.
7. Validate and apply that record to unpublished compiler state.
8. Erase the record buffer before reading another frame.
9. Require an authenticated END record with TAG_FINAL, exact byte consumption,
   and semantic stack depth one.
10. Compile through the shared optimizer and selected backend using a
    per-compilation effective-options context.
11. Publish executable code and its complete lifetime owner only after
    authentication, semantic validation, optimization, and executable-memory
    finalization all succeed.
12. Erase stream state, unused compilation state, and every transient buffer.

The lexical parser is unreachable from the protected runtime entry point.

### Evaluator state

The externally observable state is either empty or compiled. Loading state is
private and never callable.

- Starting either clear or protected replacement releases the previous
  callable expression and resets backend, constant-result, referenced-binding,
  graph, optimizer, and executable state.
- A successful replacement publishes one complete clear or protected
  compilation.
- Any failure returns to empty.
- evaluate in empty state throws std::logic_error with the stable message
  No expression has been compiled.
- get_backend returns backend_type::none in empty state.
- Clear introspection keeps its existing behavior.
- Protected introspection throws Protected_expression_error with
  INTROSPECTION_DISABLED.
- A later successful clear replacement restores normal introspection behavior.

The evaluator and its bound values follow the existing caller-synchronization
model. Concurrent evaluation, mutation, binding, replacement, or destruction
of one evaluator or its bound values requires external synchronization.

## Wire format 1.0

All integers are unsigned little-endian. Floating literals are exact IEEE-754
binary64 bits. Protected release targets are little-endian x64, but decoding
still uses explicit byte loads rather than pointer casts.

### Version compatibility

The encoder emits format 1.0. The initial decoder accepts exactly format 1.0.

Within major version 1:

- existing record kinds, operation IDs, arities, field meanings, and semantic
  meanings are immutable;
- a newer decoder must continue accepting older supported minor versions;
- an older decoder rejects an unsupported newer minor version;
- a new operation or public feature requires a minor-version decision; and
- an incompatible field or semantic change requires a new major version or a
  new operation ID.

Unknown versions, flags, record kinds, and operation IDs hard-fail. There is no
best-effort downgrade.

### Public header

The header is exactly 64 bytes:

| Offset | Size | Field | Format 1.0 rule |
|---:|---:|---|---|
| 0 | 8 | magic | ASCII MEXCEPRG |
| 8 | 2 | format_major | 1 |
| 10 | 2 | format_minor | 0 |
| 12 | 2 | header_size | 64 |
| 14 | 2 | plaintext_record_size | 32 |
| 16 | 4 | record_count | 3 through 16384 |
| 20 | 4 | feature_flags | 0 |
| 24 | 16 | program_id | random and not all zero |
| 40 | 24 | secretstream_header | generated by libsodium |

Every field is authenticated as additional data for every record.

The exact blob size is:

~~~
64 + record_count * (32 + crypto_secretstream_xchacha20poly1305_ABYTES)
~~~

For format 1.0, ABYTES is 17 and the maximum blob size is 802880 bytes. The
decoder validates record_count before computing the exact size. The bounded
calculation cannot overflow a supported size_t and does not need an unreachable
overflow branch.

### Per-record additional data

Additional data is the exact 64-byte public header followed by the record index
as a 32-bit little-endian integer. Secretstream already authenticates ordering;
the explicit index binds application framing and supplies a direct format
oracle.

### Clear semantic record

Each authenticated plaintext record is exactly 32 bytes:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | record_kind |
| 1 | 1 | record_flags |
| 2 | 2 | reserved_16 |
| 4 | 4 | operand_32 |
| 8 | 8 | operand_64 |
| 16 | 16 | reserved_128 |

record_flags and all reserved bytes are zero. Each kind defines its meaningful
operands; unused operands are also zero. The in-memory semantic representation
is not this wire struct and is never serialized with memcpy.

### Record sequence

The only valid sequence is:

1. one MANIFEST record;
2. one or more semantic PUSH or CALL records; and
3. one END record.

MANIFEST is frame zero. END is the final frame and the only frame carrying
TAG_FINAL. All preceding frames carry TAG_MESSAGE. TAG_PUSH, TAG_REKEY, early
TAG_FINAL, non-final END, and data after END reject.

### Record kinds

Wire values are explicit and independent of C++ enum order:

| Value | Name | operand_32 | operand_64 | Stack effect |
|---:|---|---|---|---|
| 1 | MANIFEST | binding_count | semantic policy | none |
| 2 | PUSH_LITERAL_F64 | zero | exact double bits | +1 |
| 3 | PUSH_VARIABLE | slot | zero | +1 |
| 4 | CALL | operation ID | zero | 1 - arity |
| 5 | END | zero | zero | requires depth 1 |

MANIFEST semantic-policy bit 0 selects fast_math. Every other policy bit is
zero. binding_count is 0 through 4096. Encoder slots are dense from zero to
binding_count minus one. Every declared slot occurs at least once and every
PUSH_VARIABLE slot is below binding_count.

Semantic stack depth starts at zero and may not exceed 1024. PUSH at depth 1024
rejects. CALL requires at least its fixed arity and changes depth to
depth - arity + 1. END requires depth one.

The manifest fast_math bit is the effective protected fast-math value and does
not mutate stored evaluator options. prefer_x87 and existing libm-selection
options come from a snapshot of evaluator options. enable_cse must be false;
protected load rejects when it is true.

Literal records must contain finite values. Runtime operations may still
produce non-finite results according to existing MEXCE semantics.

### Stable operation IDs

| ID | Operation | Arity | Public meaning |
|---:|---|---:|---|
| 1 | ADD | 2 | a + b |
| 2 | SUB | 2 | a - b |
| 3 | MUL | 2 | a * b |
| 4 | DIV | 2 | a / b |
| 5 | NEG | 1 | -a |
| 6 | POW | 2 | pow(a, b) |
| 7 | SIN | 1 | sin(a) |
| 8 | COS | 1 | cos(a) |
| 9 | TAN | 1 | tan(a) |
| 10 | ABS | 1 | abs(a) |
| 11 | SIGN | 1 | -1, 0, or 1 according to the sign of a |
| 12 | SIGNP | 1 | 1 when a is positive, otherwise 0 |
| 13 | EXPN | 1 | binary exponent returned by MEXCE expn |
| 14 | SFC | 1 | binary significand returned by MEXCE sfc |
| 15 | SQRT | 1 | sqrt(a) |
| 16 | EXP | 1 | exp(a) |
| 17 | LT | 2 | a < b |
| 18 | GT | 2 | a > b |
| 19 | LE | 2 | a <= b |
| 20 | GE | 2 | a >= b |
| 21 | EQ | 2 | a == b |
| 22 | NE | 2 | a != b |
| 23 | LOG | 1 | natural logarithm |
| 24 | LOG2 | 1 | base-two logarithm |
| 25 | LOG10 | 1 | base-ten logarithm |
| 26 | LOGB | 2 | logarithm of b in base a |
| 27 | YLOG2 | 2 | a * log2(b) |
| 28 | MAX | 2 | max(a, b) |
| 29 | MIN | 2 | min(a, b) |
| 30 | FLOOR | 1 | MEXCE floor(a) |
| 31 | CEIL | 1 | MEXCE ceil(a) |
| 32 | ROUND | 1 | MEXCE round(a) |
| 33 | INT | 1 | MEXCE int(a) |
| 34 | TRUNC | 1 | MEXCE trunc(a) |
| 35 | MOD | 2 | fmod(a, b) |
| 36 | BND | 2 | a modulo b, adjusted into [0, b) for positive b |
| 37 | BIAS | 2 | a / (((1 / b) - 2) * (1 - a) + 1) |
| 38 | GAIN | 2 | MEXCE gain(a, b) tone-mapping curve |

For finite nonzero a, EXPN is the exponent from frexp(a) minus one and SFC is
a divided by 2 raised to EXPN. GAIN is:

~~~
a / (((1 / b) - 2) * (1 - 2*a) + 1)                    when a < 0.5

(((1 / b) - 2) * (1 - 2*a) - a) /
(((1 / b) - 2) * (1 - 2*a) - 1)                        otherwise
~~~

Exceptional floating-point behavior, rounding details, and libm selection are
the existing public MEXCE semantics for the selected backend. Clear and
protected paths use the same operation prototypes and parity tests. A change
that alters a public operation contract cannot silently reinterpret an existing
wire ID.

Source aliases map as follows:

- infix +, -, *, / map to ADD, SUB, MUL, DIV;
- ^ and ** map to POW;
- infix < and > map to LT and GT; LE, GE, EQ, and NE retain their function
  spellings;
- log and ln both map to LOG;
- built-in pi and e become PUSH_LITERAL_F64 records; and
- function spellings otherwise map to the same-named operation.

The clear semantic representation retains clear-only spelling metadata where
needed for get_optimized_expression. That metadata is not a wire field and is
absent from protected compiler state.

### Canonical production

The issuer producer:

- resolves aliases before encoding;
- emits exact literal bits rather than decimal spelling;
- preserves current postfix operand order;
- emits source unary minus as positive-zero, operand, and SUB records, preserving
  current precedence and signed-zero behavior; unary plus emits no operation;
- maps explicit neg(a) to NEG;
- assigns slots from the explicit schema, never container iteration;
- emits no optimizer-created node;
- emits exactly one END; and
- writes every reserved field as zero.

Backend optimization and commutative reordering remain runtime compiler work.

## Public API

### Protected_expression_key

Protected_expression_key is a move-only owner of exactly
crypto_secretstream_xchacha20poly1305_KEYBYTES bytes.

The public construction surface is:

~~~cpp
static Protected_expression_key from_bytes(
    const uint8_t* bytes,
    size_t size);
~~~

The factory rejects a null pointer and every size other than 32 bytes. It copies
the bytes into a guarded allocation; the caller remains responsible for wiping
its input buffer. The type has no public default constructor.

Key storage uses sodium_malloc followed by an explicit checked sodium_mlock.
sodium_malloc alone is insufficient because libsodium may return an unlocked
allocation when its internal lock attempt fails. Allocation or lock failure
wipes and frees any allocation and throws RESOURCE_FAILURE. Memory locking is
defense in depth and does not protect registers, stack spills, or hostile
process inspection.

The type:

- is movable and not copyable;
- leaves the source empty after a move;
- wipes and frees owned bytes on destruction;
- exposes no string, hex, comparison, logging, or general serialization API;
- rejects use after move or consumption; and
- grants only the encoder and runtime loader narrow internal byte access.

Issuer code can transfer the key to host storage with:

~~~cpp
template<typename Consumer>
void consume_bytes(Consumer&& consumer);
~~~

The callback receives const uint8_t* and size_t for the dynamic extent of the
call. Ownership moves to a private local guard before user code runs. Normal
return and exception unwinding both wipe and empty the key; a callback exception
is rethrown after wiping. Recursive consumption, moving the original object
inside the callback, and a second consumption observe an empty key.

### Protected_expression_bundle

Protected_expression_bundle contains:

~~~cpp
std::vector<uint8_t>       program;
Protected_expression_key   key;
std::array<uint8_t, 16>    program_id;
~~~

Because it owns a key, the bundle is movable and not copyable. It performs no
implicit file output.

### Protected_binding

Protected_binding is issuer-side schema data containing a source variable name
and uint32_t slot.

Names use the exact ASCII grammar:

~~~
[A-Za-z_][A-Za-z0-9_]*
~~~

Names are at most 255 bytes and cannot equal a built-in constant, function, or
source alias. Names and slots are unique. Slots are dense from zero. The schema
contains exactly the variables referenced by the source.

The encoder limits are:

- source: at most 1 MiB;
- bindings: at most 4096;
- one name: at most 255 bytes;
- cumulative binding-name bytes: at most 256 KiB; and
- semantic records including MANIFEST and END: at most 16384.

Limits are checked before parsing, random generation, or storage proportional
to untrusted sizes.

### Encoder

The issuer API is:

~~~cpp
enum class Protected_math_mode
{
    STRICT = 0,
    FAST = 1,
};

Protected_expression_bundle encode_protected_expression(
    const std::string& expression,
    const std::vector<Protected_binding>& bindings,
    Protected_math_mode math_mode);
~~~

STRICT clears manifest policy bit 0. FAST sets it. Invalid enum values reject
before parsing or random generation.

Syntax errors use mexce_parsing_exception and may include trusted issuer source
content or identifiers. Schema, limit, initialization, allocation, and encoding
failures use Protected_expression_error. The function never returns a partial
bundle.

### Runtime binding

The runtime binding surface is:

~~~cpp
template<typename T>
void bind_protected(T& referenced_variable, uint32_t slot);

void unbind_protected(uint32_t slot);
void unbind_all_protected();
~~~

Supported T is double, float, int16_t, int32_t, or int64_t. A slot above 4095
rejects at bind time. The artifact authenticates slot numbers, not the C++ type
or address of a runtime binding.

Caller-owned bound objects must not move and must outlive their binding and
every evaluation that uses it.

Binding behavior is:

- binding an unused existing slot replaces that slot;
- rebinding a slot referenced by the compiled protected expression rejects
  without changing state;
- unbinding an unknown slot rejects;
- unbinding a referenced slot first invalidates the compiled expression, then
  removes the binding;
- unbind_all_protected invalidates a protected expression that references any
  slot, then removes all protected bindings;
- extra runtime bindings not referenced by an artifact are permitted and
  ignored; and
- clear name bindings and protected slot bindings occupy independent maps.

### Runtime load

The runtime API is:

~~~cpp
void set_protected_expression(
    const uint8_t* program,
    size_t program_size,
    Protected_expression_key key);
~~~

Passing the key by value makes ownership transfer mandatory because copying is
deleted. Every attempted call consumes the transferred key on success or
failure. The key is erased immediately after init_pull derives stream state.
The stream state then owns the decryption capability and is wiped on every exit.

program may be null only when program_size is zero; that input still rejects as
empty. Program bytes remain caller-owned and are not retained.

On success:

- evaluate executes the protected expression;
- get_backend reports the selected existing backend;
- get_optimized_expression and get_byte_representation throw
  INTROSPECTION_DISABLED;
- no key, stream state, wire record, or program pointer remains in evaluator;
  and
- every code-addressed compiler object remains owned until code is no longer
  callable.

On failure, evaluator is empty and all transferred key, stream, record,
compiler, optimizer, executable, constant-result, and referenced-binding state
has been reset or wiped as applicable.

### Protected errors

Protected_expression_error provides:

~~~cpp
Protected_expression_error_category category() const noexcept;
bool has_record_index() const noexcept;
uint32_t record_index() const;
~~~

record_index throws std::logic_error when no index is available.

Categories are:

- INVALID_ARGUMENT;
- UNSUPPORTED_FORMAT;
- SIZE_LIMIT;
- AUTHENTICATION_FAILED;
- MALFORMED_PROGRAM;
- MISSING_BINDING;
- INTROSPECTION_DISABLED;
- CRYPTOGRAPHY_UNAVAILABLE;
- COMPILATION_FAILED; and
- RESOURCE_FAILURE.

Runtime decode and authentication messages contain no key bytes, literal bits,
slot values, operation names, or decrypted content. Wrong key and tampering are
not distinguished. Issuer parsing diagnostics are not subject to the runtime
content restriction.

| Failure | Category | Record index |
|---|---|---|
| invalid runtime argument or protected CSE | INVALID_ARGUMENT | none |
| bad magic; unsupported version; header-size or record-size field; non-zero public flags | UNSUPPORTED_FORMAT | none |
| declared resource limit exceeded | SIZE_LIMIT | none before frames; current index after authentication |
| init_pull rejects a syntactically sized secretstream header | AUTHENTICATION_FAILED | none |
| pull authentication failure | AUTHENTICATION_FAILED | current index |
| record count below three; exact-size mismatch; zero program ID | MALFORMED_PROGRAM | none |
| authenticated semantic or framing violation | MALFORMED_PROGRAM | current index |
| a referenced declared slot has no runtime binding | MISSING_BINDING | first PUSH_VARIABLE index for that slot |
| protected introspection | INTROSPECTION_DISABLED | none |
| sodium_init failure | CRYPTOGRAPHY_UNAVAILABLE | none |
| valid semantic state rejected by optimizer or selected backend | COMPILATION_FAILED | none |
| allocation, memory protection, or OS resource failure | RESOURCE_FAILURE | none |

With libsodium 1.0.22, same-size secretstream-header mutations normally pass
init_pull and fail pull at frame zero. The documented init_pull failure path is
still supported and tested through the cryptographic-call boundary.

## Compiler invariants

### Shared semantic boundary

Clear parsing and issuer parsing share one semantic producer. Clear and
protected compilation share one semantic consumer. The consumer validates stack
effects, constructs compiler objects, links arguments, optimizes, selects a
backend, and finalizes executable memory.

The protected route never calls the lexical producer. The clear route never
calls cryptography. There is no serialized clear semantic vector between the
clear producer and consumer.

### Effective options

Each compilation captures one effective-options context used by parsing,
normalization, CSE, fast-math passes, constant folding, compatibility checks,
fallback, and emission.

- Clear compilation copies all stored evaluator options.
- Protected compilation takes fast_math from the authenticated manifest,
  prefer_x87 and libm choices from stored options, and rejects enable_cse.
- No compilation mutates stored evaluator options, including fallback and
  exception paths.

### Primary and fallback ownership

Optimizers may mutate their input. A fallback compilation is created from
source-free semantic state before destructive mutation can make reconstruction
unsafe. Primary and fallback compilations share no mutable compiler objects or
wipeable literal storage.

One per-compilation lifetime owner contains:

- the semantic/compiler graph;
- every intermediate constant whose address may enter native code;
- CSE value storage where applicable;
- optimizer-owned values such as power terms;
- every other address-emitted object; and
- the executable buffer metadata.

The selected owner outlives its executable code. Replacement and destruction
make code uncallable before wiping or releasing any code-addressed storage.
Unused owners are wiped and released immediately.

The fallback route runs the same complete x87 pipeline required by the clear
semantics, including CSE when enabled for clear input. It does not reparse
source and does not copy a graph whose nodes share mutable objects.

### Protected data handling

Protected compiler objects do not retain source names or reconstructable debug
strings. Backend compatibility uses semantic state, never the presence of debug
text. Clear-only spelling and debug descriptions remain available only to clear
introspection.

Every protected literal or derived scalar held in:

- the selected or unused graph;
- intermediate-constant storage;
- optimizer containers;
- constant-expression result storage;
- power-term storage; or
- temporary fallback state

is explicitly wiped before its storage is released. Protected literal bits are
not used as immutable container keys in storage that cannot be wiped.

Native code is never writable and executable simultaneously. Where the platform
permits a safe writable transition during teardown, code bytes are wiped before
release. Failure to make an executable page writable during noexcept teardown
does not terminate or throw; native-code wiping is best effort. Key, stream,
record, and protected scalar wiping is mandatory.

## Cryptographic lifecycle

### Initialization

Protected public operations use a process-wide C++11 thread-safe sodium_init
gate. A return value below zero is cached as failure and every protected call
hard-fails. Normal MEXCE paths do not initialize sodium.

### Randomness and key separation

Artifact keys use crypto_secretstream_xchacha20poly1305_keygen. Program IDs use
randombytes_buf and retry the all-zero value. Secretstream generates its own
header. No standard-library PRNG, timestamp, or deterministic seed participates.

The canonical encoder always generates the key internally and performs one
encode operation. There is no deterministic or existing-key encoder.

### Authentication before use

Before the first successful pull, only bounded public framing fields may be
interpreted. A record is applied to temporary compiler state only after pull
authenticates it. Earlier authenticated records may populate unpublished state
before a later failure, but no native program is published until final
authentication and compilation complete.

### Erasure

sodium_memzero or an equivalently non-optimizable wrapper wipes:

- all key copies;
- secretstream push and pull states;
- each clear wire record;
- partially built issuer records;
- temporary authenticated-data buffers containing derived framing;
- protected compiler and optimizer scalar storage; and
- failure-path storage containing decoded values.

sodium_free wipes guarded key allocations. Erasure does not claim to remove
copies from registers, compiler spills, crash dumps, swap, or a hostile
debugger. Core-dump, swap, and hibernation policy belong to the host.

## Build, packaging, and platform contract

### Supported release matrix

Protected format 1.0 release gates are:

- Windows x64 with MSVC;
- Linux x64 with GCC; and
- Linux x64 with Clang.

Both SSE2 and x87 paths are exercised on x64. Ordinary MEXCE x86 support remains
unchanged, but protected x86 headers/packages are not advertised.

Cross-platform portability means the same artifact, key, bindings, and policy
are accepted on Windows and Linux. Results follow existing backend and libm
semantics; bit-identical results across different C libraries or x87/SSE2
precision are not promised.

### Header and ODR boundary

mexce.h contains the unconditional sodium-free declarations and evaluator state
needed by both normal and protected translation units. The evaluator definition,
layout, constructor, destructor, and ordinary inline methods never depend on
MEXCE_ENABLE_PROTECTED_EXPRESSIONS or on which protected header was included.

mexce_protected.h includes mexce.h and supplies protected key, error, codec, and
inline runtime definitions that call libsodium. mexce_protected_encoder.h adds
issuer encoding. No sodium header or symbol is reachable merely by including
mexce.h.

Protected compilation state must be destructible through the normal evaluator
lifecycle without requiring a sodium reference from a translation unit that
uses only mexce.h. Private type erasure is permitted, but no particular private
mechanism is required.

### CMake targets

The final source-build surface is:

- mexce::mexce: header-only ordinary MEXCE with no cryptographic dependency;
- mexce::protected: header-only protected surface, transitively linking
  libsodium;
- protected unit, benchmark, and fuzz targets when protected support is
  enabled; and
- mexce_protect when issuer tools are enabled.

MEXCE_ENABLE_PROTECTED_EXPRESSIONS is the only protected source-build option.
It defaults OFF. Enabling it without a supported libsodium target is a configure
error. It controls build targets and installation content, never C++ class
layout.

CMake normalizes supported dependency providers behind one private adapter:

- Conan target libsodium::libsodium;
- vcpkg target unofficial-sodium::sodium; or
- a validated pkg-config imported target on Linux.

The installed mexce::protected configuration recreates the adapter and fails
clearly when libsodium is absent. mexce::mexce has no transitive sodium include,
compile definition, symbol, or link item.

### Conan

Conan adds with_protected, default false. The protected package requires
libsodium/1.0.22 and exports mexce::protected. Package identity retains the
option and dependency; protected and ordinary packages cannot collapse to the
same ID.

The repository recipe packages exported candidate headers and build metadata,
not a previously released archive. Its test package builds ordinary and
protected consumers from that candidate package.

### vcpkg

The port adds a protected feature depending on libsodium and installs the
protected headers and CMake target. Default installation remains
dependency-free.

Candidate testing uses a transient overlay whose source path is the candidate
tree rather than the tagged archive. The installed header hashes and target
files are compared with the candidate. Release packaging separately validates
the final archive reference and checksum.

## Issuer utility

mexce_protect is an issuer tool, not a licence system.

Inputs are:

- expression file;
- binding-schema file;
- output program path; and
- output key path.

The expression file is at most 1 MiB. The tool removes one terminal LF and an
optional preceding CR; it performs no other whitespace normalization. NUL bytes
and a UTF-8 BOM reject. Non-ASCII source is handled by the existing parser and
does not extend the source grammar.

The schema is at most 256 KiB and contains one name=decimal_slot entry per line.
Blank lines, comments, NUL bytes, invalid UTF-8, duplicate names, duplicate
slots, sparse slots, oversized names, and trailing characters reject.

Output paths are compared after absolute platform-appropriate normalization.
They must identify distinct paths. Existing destinations, symlinks,
reparse-point traversal, or indeterminate path identity hard-fail.

The tool publishes:

- opaque program bytes; and
- exactly 32 raw key bytes.

Publication invariants are:

- temporary files are exclusive-created siblings of final paths on the same
  filesystem or volume;
- both temporaries are fully written and flushed before publication;
- final paths are never overwritten;
- the key file receives owner-only permissions before publication;
- the key is published first and the program last;
- ordinary failure before publication removes temporaries;
- failure after key publication can leave an orphan key but never a program
  published by that invocation without its key;
- a later invocation detects and reports partial final state and never guesses
  or overwrites; and
- machine or filesystem power-loss atomicity is not claimed.

The implementation uses the platform's atomic no-replace primitive but does not
expose that primitive in the public utility contract. The tool consumes
Protected_expression_key through consume_bytes and never prints source or key
content.

Documentation instructs issuers to import the raw key immediately into the host
wrapping system and remove the caller-owned final key file.

## Verification contract

### Authorities

Correctness derives from:

- the byte-exact wire contract;
- libsodium's documented secretstream behavior;
- existing adopted clear-expression semantics;
- direct mathematical definitions for semantic operations; and
- independently authored semantic fixtures.

Encoder/decoder round trips are necessary but not sufficient because both sides
could share the same wrong ID, arity, operand order, or framing rule.

### Clear semantic refactor

Verify:

- all existing unit tests and expression-corpus results on Windows and Linux;
- exact backend selection for existing SSE2 and x87 cases;
- signed-zero behavior for source unary minus and explicit neg;
- source-free SSE2-to-x87 fallback after an optimizer makes the primary graph
  incompatible;
- CSE-enabled fallback using an independent pre-mutation owner;
- log and ln clear introspection spelling;
- operation, optimized-expression, and numeric parity with the pre-refactor
  clear evaluator;
- failed replacement after a constant expression;
- failed replacement after a native expression; and
- empty-state evaluate, backend, binding-reference, executable, constant, and
  optimizer invariants.

Machine-code byte snapshots are not an oracle because addresses and platform
code generation are unstable.

### Independent format fixtures

A test-only reference producer:

- defines wire constants independently from production headers;
- accepts hand-authored semantic records;
- uses libsodium directly;
- emits fixed-key binary fixtures and a human-readable manifest; and
- is not linked into production.

Production tests consume checked-in fixtures. Regeneration is explicit.
Duplicated constants are confined to this independent boundary.

### Cryptographic and format checks

Verify:

- known fixture and matching key succeed;
- wrong key rejects;
- public structural-field mutations map to their exact format category;
- secretstream-header mutation with real libsodium fails authentication;
- injected init_pull failure reports no index;
- every ciphertext and tag byte mutation rejects;
- frame swap, substitution, and reordering reject at pull;
- insertion, removal, truncation, and trailing bytes reject exact framing;
- compensated framing changes that reach pull fail authentication;
- final-tag and END mismatches reject;
- every reserved or unused byte is enforced; and
- the transferred runtime key is empty after successful and failing calls.

### Semantic checks

Cover:

- every operation ID, arity, alias, and operand order;
- finite literal boundary values and signed zero;
- every supported bound type;
- repeated, missing, extra, and multiple slots;
- strict and fast-math policy;
- protected CSE rejection;
- prefer_x87 and libm option preservation;
- constant-only programs;
- SSE2 and x87 results;
- source-free post-optimization fallback;
- protected compilation with caller buffers destroyed afterward; and
- Windows-produced artifacts on Linux and Linux-produced artifacts on Windows.

Hand-authored records exercise semantic failures without using the production
encoder.

### Failure and resource checks

Table-drive every error-mapping row, including:

- empty and short input;
- record, source, schema, name, slot, and semantic-depth limits;
- malformed public fields and zero program ID;
- misplaced MANIFEST or END;
- unknown records and operations;
- non-zero reserved and unused fields;
- invalid policy and non-finite literal;
- stack underflow and invalid terminal depth;
- missing runtime bindings;
- allocation and executable-finalization failures;
- failed replacement from constant and native states;
- guarded allocation and explicit mlock failure; and
- absent record_index behavior.

sodium_init injected failure runs in an isolated process because initialization
state is process-wide and cached.

### Lifetime and erasure checks

A narrow test-only wipe observer verifies wipe requests for key, stream, record,
protected scalar, constant-only result, optimizer, fallback, replacement, and
destruction paths.

Tests also verify:

- one-shot key consumption, move ownership, callback exception unwinding,
  recursive consumption rejection, and moved-from rejection;
- code-addressed constants and CSE storage remain alive while code is callable;
- code becomes uncallable before protected storage is wiped;
- protected introspection rejects without constructing protected debug text;
- runtime errors contain no decoded content;
- the protected path never calls the lexical parser; and
- writable-executable memory never exists.

### Fuzzing

One bounded decoder fuzz target uses arbitrary bytes and a fixed non-secret key.
A structured mutator independently re-encrypts altered semantic records so
semantic validation is reachable instead of stopping every case at
authentication.

The invariant is no crash, hang, out-of-bounds access, undefined behavior, or
acceptance without a complete valid authenticated stream. Fuzzing runs under
Clang sanitizers in a dedicated job.

### ODR and dependency isolation

Build:

- one translation unit including only mexce.h;
- one translation unit including mexce_protected.h;
- an evaluator created on one side and destroyed or used on the other; and
- an ordinary installed consumer in an environment where sodium is not linked.

Inspect the ordinary consumer's link interface and binary symbols to confirm no
sodium dependency. Build a protected installed consumer and verify the
normalized transitive dependency.

### Performance and resource behavior

Release baseline and candidate builds use the existing corpus, identical
compiler flags, discarded warm-ups, alternating run order, and enough repeated
runs to distinguish persistent movement from system noise. Reports retain the
per-run values, medians, dispersion, backend selection, and correctness counts.
During functional implementation these measurements are diagnostic: batches
1 through 5 record changes and suspected causes but do not use fixed percentage
limits as closure gates.

Protected benchmarks run clear and protected compilation in isolated processes
with matched formulas, bindings, and options. They report per-expression
compile ratio, evaluate timing, and peak working set as structured output.
Maximum-size valid and late-invalid artifacts also report elapsed time and peak
working set from an isolated process. Resource failures remain subject to the
specified bounded-input and cleanup behavior even while timing and memory
measurements are diagnostic.

No native code-size measurement is required unless a harness that measures it
is explicitly added.

## Implementation batches

### Batch 1: shared semantic compilation and lifecycle

Outcome:

- one canonical semantic producer/consumer boundary;
- clear compilation routed through that boundary;
- one complete per-compilation lifetime owner;
- source-free primary/fallback ownership;
- per-compilation effective options; and
- defined empty-state behavior after failed replacement.

Primary write scope:

- mexce.h
- test/unit_tests.cpp

Deletion points:

- direct Token-to-Element construction inside set_expression;
- m_last_expression and recursive source reparse;
- any plaintext set_protected_expression surface or scaffolding present in the
  implementation base;
- compiler decisions that depend on debug-string presence; and
- newly orphaned source-name conversion or fallback helpers.

Gates:

- clear semantic-refactor checks;
- failed-replacement and empty-state checks;
- primary/fallback independence and complete lifetime ownership checks;
- Windows and Linux unit/corpus runs; and
- clear performance diagnostics with backend and correctness counts.

Owned risks:

- CSE mutation leaking between primary and fallback owners;
- code-addressed constants or CSE storage outliving the wrong owner;
- clear optimized-expression spelling drift; and
- backend selection drift after removing debug-string signals.

### Batch 2: protected key and wire codec

Dependency: shared semantic compilation and lifecycle.

Outcome:

- protected key ownership and secure allocation;
- byte-exact format 1.0 encoder and bounded decoder;
- protected errors and complete category/index mapping;
- final protected CMake build option for codec tests; and
- independent format fixtures.

Affected surfaces:

- protected public headers;
- protected CMake test targets;
- key, codec, and fixture tests; and
- independent reference fixtures.

The decoder produces validated semantic records for an internal sink but does
not publish executable evaluator state.

Gates:

- independent fixture checks;
- cryptographic and format mutation matrix;
- key move, consume, mlock, initialization, and erasure checks;
- encoder resource and schema limits;
- error category/index matrix; and
- Windows MSVC plus Linux GCC/Clang codec builds against libsodium 1.0.22.

Owned risks:

- secretstream init versus pull error classification;
- unauthenticated allocation or semantic interpretation;
- accidental key reuse or retained key copies;
- immutable storage containing protected literal bits; and
- production and reference codecs sharing constants or helpers.

### Batch 3: protected evaluator integration

Dependency: protected key and wire codec.

Outcome:

- canonical runtime binding and load APIs;
- source-free streaming compilation;
- authenticated option precedence;
- protected introspection behavior;
- complete protected lifecycle and wipe behavior; and
- cross-platform artifact portability.

Affected surfaces:

- mexce.h sodium-free protected declarations/state;
- mexce_protected.h runtime definitions;
- protected runtime tests;
- protected benchmark; and
- decoder fuzz target.

Gates:

- semantic, failure, lifetime, erasure, and introspection checks;
- mixed-translation-unit ODR test;
- ordinary no-sodium link and symbol check;
- ASan/UBSan protected tests on Linux;
- focused Windows heap diagnostics;
- structured decoder fuzz smoke;
- Windows/Linux cross-produced artifacts; and
- protected compile, evaluate, and isolated-resource diagnostics.

Owned risks:

- macro-conditioned evaluator layout;
- sodium symbols leaking into ordinary consumers;
- incomplete compilation-state ownership;
- protected debug-string construction;
- stored options being mutated during protected compilation; and
- failure publishing partial code or leaving stale callable state.

### Batch 4: install and package integration

Dependency: protected evaluator integration.

Outcome:

- installed mexce::mexce and mexce::protected targets;
- final libsodium dependency normalization;
- Conan ordinary and protected packages with distinct identities; and
- vcpkg default and protected feature packages.

Affected surfaces:

- CMake install/export configuration;
- Conan recipe and test package;
- vcpkg port metadata and feature definition; and
- installed-consumer tests.

Gates:

- dependency-free default configure, build, install, and consumer;
- protected configure, build, install, and consumer on every release compiler;
- Conan candidate-source ordinary and protected consumers;
- vcpkg candidate-source ordinary and protected consumers;
- installed file/hash provenance from candidate source;
- missing-libsodium hard failure for protected consumers; and
- licence and third-party notice checks.

Owned risks:

- candidate package tests resolving an older release archive;
- protected and ordinary Conan package IDs collapsing;
- source-tree include fallback hiding broken install exports; and
- provider-specific sodium target names leaking into the public MEXCE target.

### Batch 5: issuer utility, documentation, and continuous gates

Dependency: install and package integration.

Outcome:

- mexce_protect with safe no-overwrite publication;
- public protected usage and threat-model documentation;
- protected example;
- release-platform CI; and
- scheduled sanitizer/fuzz coverage.

Affected surfaces:

- issuer utility and utility tests;
- README, example, changelog, and notices;
- Windows/Linux workflows; and
- protected fuzz workflow.

Gates:

- issuer input and publication invariants on Windows and Linux;
- output permissions and partial-publication recovery;
- end-to-end utility output consumed through the installed protected target;
- documented commands executed in a clean consumer;
- default and protected release matrices; and
- scheduled extended fuzz run; and
- complete-system clear and protected performance diagnostics on release
  configurations.

Owned risks:

- overwriting or following attacker-controlled paths;
- publishing a program without its key;
- leaving raw key files with broad permissions;
- documentation overstating local-process secrecy; and
- CI exercising source-tree headers instead of installed packages.

### Batch 6: performance stabilization and release acceptance

Dependency: issuer utility, documentation, and continuous gates.

Outcome:

- release performance characterized from the complete implementation;
- causal regressions identified with profiles rather than timing deltas alone;
- targeted optimizations applied without weakening semantic, cryptographic,
  lifecycle, packaging, or portability guarantees; and
- release acceptance based on finished-system workload evidence.

Affected surfaces:

- clear and protected benchmark harnesses and structured reports;
- implementation paths demonstrated by profiles to cause material regressions;
  and
- release performance documentation and continuous performance jobs.

Gates:

- matched baseline and candidate measurements across supported release
  configurations, with warm-up, alternating order, per-run results, medians,
  and dispersion retained;
- clear compile and evaluate profiles, protected encode/load profiles, and
  maximum-size valid and late-invalid resource profiles;
- causal explanation for every optimization and before/after evidence on the
  workload that exposed it;
- all functional, security, lifecycle, portability, and packaging gates affected
  by an optimization rerun after the change; and
- release acceptance limits selected from complete-system distributions,
  workload requirements, platform noise, and resource-safety evidence rather
  than preassigned percentages.

Owned risks:

- optimizing an intermediate architecture that later batches replace;
- treating measurement noise as a product regression;
- improving aggregate timing while worsening tail or maximum-size behavior;
- duplicating clear and protected compiler paths for speed; and
- trading away cleanup, authentication, ownership, or portability guarantees.

## Completion criteria

The protected capability is complete when:

- format 1.0 bytes, semantic IDs, APIs, ownership, errors, limits, and security
  claims are implemented as specified;
- normal MEXCE remains dependency-free and ODR-safe;
- clear compilation retains adopted behavior;
- protected compilation passes format, semantic, mutation, lifetime, erasure,
  fuzz, portability, and resource gates;
- installed CMake, Conan, and vcpkg consumers use candidate artifacts;
- the issuer utility satisfies publication and permission invariants; and
- the complete implementation satisfies Batch 6 release performance and
  resource acceptance; and
- no plaintext protected overload, duplicate compiler path, temporary build
  switch, source-reparse fallback, or process-only artifact remains.

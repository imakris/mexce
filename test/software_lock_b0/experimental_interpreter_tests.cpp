#include "experimental_interpreter.h"

#include <emmintrin.h>

#include <algorithm>
#include <array>
#include <cfenv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <thread>
#include <vector>


namespace {


using Interpreter = mexce::software_lock_b0::Experimental_interpreter;
using Status      = Interpreter::Status;

constexpr uint8_t k_artifact_magic[8]      = {'M', 'X', 'B', '0', 'R', 'E', 'C', 'S'};
constexpr uint8_t k_load_magic[8]          = {'M', 'X', 'B', '0', 'L', 'O', 'A', 'D'};
constexpr uint8_t k_load_response_magic[8] = {'M', 'X', 'B', '0', 'L', 'R', 'S', 'P'};
constexpr uint8_t k_evaluate_magic[8]      = {'M', 'X', 'B', '0', 'E', 'V', 'A', 'L'};
constexpr uint8_t k_unload_magic[8]        = {'M', 'X', 'B', '0', 'U', 'N', 'L', 'D'};

constexpr uint32_t k_manifest = 1;
constexpr uint32_t k_literal  = 2;
constexpr uint32_t k_variable = 3;
constexpr uint32_t k_call     = 4;
constexpr uint32_t k_end      = 5;

constexpr uint32_t k_add = 1;
constexpr uint32_t k_sub = 2;
constexpr uint32_t k_mul = 3;
constexpr uint32_t k_div = 4;
constexpr uint32_t k_sin = 7;
constexpr uint32_t k_cos = 8;

constexpr uint32_t k_row_major  = 0;
constexpr uint32_t k_slot_major = 1;


class Test_suite
{
public:
    void expect(bool condition, const std::string& name)
    {
        if (!condition) {
            m_failures.push_back(name);
        }
    }

    const std::vector<std::string>& failures() const noexcept { return m_failures; }

private:
    std::vector<std::string> m_failures;
};


struct loaded_program_t
{
    uint64_t handle;
    uint32_t slot_count;
};


struct record_t
{
    uint8_t  kind;
    uint32_t operand;
    uint64_t value;
};


void store_u16(uint8_t* bytes, uint16_t value)
{
    bytes[0] = static_cast<uint8_t>(value);
    bytes[1] = static_cast<uint8_t>(value >> 8);
}


void store_u32(uint8_t* bytes, uint32_t value)
{
    for (size_t i = 0; i < 4; ++i) {
        bytes[i] = static_cast<uint8_t>(value >> (i * 8));
    }
}


void store_u64(uint8_t* bytes, uint64_t value)
{
    for (size_t i = 0; i < 8; ++i) {
        bytes[i] = static_cast<uint8_t>(value >> (i * 8));
    }
}


uint32_t load_u32(const uint8_t* bytes)
{
    return static_cast<uint32_t>(bytes[0])      |
        static_cast<uint32_t>(bytes[1]) << 8  |
        static_cast<uint32_t>(bytes[2]) << 16 |
        static_cast<uint32_t>(bytes[3]) << 24;
}


uint64_t load_u64(const uint8_t* bytes)
{
    uint64_t value = 0;
    for (size_t i = 0; i < 8; ++i) {
        value |= static_cast<uint64_t>(bytes[i]) << (i * 8);
    }
    return value;
}


uint64_t bits_of(double value)
{
    uint64_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}


double from_bits(uint64_t bits)
{
    double value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}


record_t manifest(uint32_t slots, bool fast = false)
{
    return {k_manifest, slots, fast ? 1ULL : 0ULL};
}
record_t literal_bits(uint64_t bits) { return {k_literal, 0, bits}; }
record_t literal(double value)       { return literal_bits(bits_of(value)); }
record_t variable(uint32_t slot)     { return {k_variable, slot, 0}; }
record_t call(uint32_t operation)    { return {k_call, operation, 0}; }
record_t end()                       { return {k_end, 0, 0}; }


std::vector<uint8_t> artifact(const std::vector<record_t>& records)
{
    std::vector<uint8_t> bytes(32 + records.size() * 32, 0);
    std::memcpy(bytes.data(), k_artifact_magic, sizeof(k_artifact_magic));
    store_u16(bytes.data() + 8, 1);
    store_u16(bytes.data() + 10, 0);
    store_u16(bytes.data() + 12, 32);
    store_u16(bytes.data() + 14, 32);
    store_u32(bytes.data() + 16, static_cast<uint32_t>(records.size()));
    for (size_t index = 0; index < records.size(); ++index) {
        uint8_t* destination = bytes.data() + 32 + index * 32;
        destination[0] = records[index].kind;
        store_u32(destination + 4, records[index].operand);
        store_u64(destination + 8, records[index].value);
    }
    return bytes;
}


std::vector<uint8_t> load_request(const std::vector<uint8_t>& program)
{
    std::vector<uint8_t> request(24 + program.size(), 0);
    std::memcpy(request.data(), k_load_magic, sizeof(k_load_magic));
    store_u16(request.data() + 8, 1);
    store_u16(request.data() + 10, 0);
    store_u32(request.data() + 12, 24);
    store_u64(request.data() + 16, program.size());
    std::copy(program.begin(), program.end(), request.begin() + 24);
    return request;
}


Status load_program(
    Interpreter& interpreter,
    const std::vector<record_t>& records,
    loaded_program_t& loaded)
{
    const auto program = artifact(records);
    auto request       = load_request(program);
    std::array<uint8_t, 32> response;
    response.fill(0xa5);
    const Status status = interpreter.load(
        request.data(), request.size(), response.data(), response.size());
    if (status == Status::SUCCESS) {
        if (std::memcmp(response.data(), k_load_response_magic, 8) != 0 ||
            load_u32(response.data() + 12) != response.size() ||
            load_u32(response.data() + 28) != 1)
        {
            return Status::INTERNAL_FAILURE;
        }
        loaded.handle     = load_u64(response.data() + 16);
        loaded.slot_count = load_u32(response.data() + 24);
    }
    return status;
}


std::vector<uint8_t> evaluate_request(
    const loaded_program_t& loaded,
    uint32_t layout,
    uint32_t rows,
    const std::vector<double>& values)
{
    std::vector<uint8_t> request(40 + values.size() * sizeof(double), 0);
    std::memcpy(request.data(), k_evaluate_magic, sizeof(k_evaluate_magic));
    store_u16(request.data() + 8, 1);
    store_u16(request.data() + 10, 0);
    store_u32(request.data() + 12, 40);
    store_u64(request.data() + 16, loaded.handle);
    store_u32(request.data() + 24, layout);
    store_u32(request.data() + 28, rows);
    store_u32(request.data() + 32, loaded.slot_count);
    for (size_t index = 0; index < values.size(); ++index) {
        store_u64(request.data() + 40 + index * sizeof(double), bits_of(values[index]));
    }
    return request;
}


Status evaluate(
    Interpreter& interpreter,
    const loaded_program_t& loaded,
    uint32_t layout,
    uint32_t rows,
    const std::vector<double>& values,
    std::vector<uint8_t>& results)
{
    auto request = evaluate_request(loaded, layout, rows, values);
    return interpreter.evaluate_batch(
        request.data(), request.size(), results.data(), results.size());
}


std::vector<uint8_t> unload_request(uint64_t handle)
{
    std::vector<uint8_t> request(24, 0);
    std::memcpy(request.data(), k_unload_magic, sizeof(k_unload_magic));
    store_u16(request.data() + 8, 1);
    store_u16(request.data() + 10, 0);
    store_u32(request.data() + 12, 24);
    store_u64(request.data() + 16, handle);
    return request;
}


Status unload(Interpreter& interpreter, uint64_t handle)
{
    auto request = unload_request(handle);
    return interpreter.unload(request.data(), request.size());
}


uint64_t result_bits(const std::vector<uint8_t>& results, uint32_t row)
{
    return load_u64(results.data() + static_cast<size_t>(row) * sizeof(double));
}


bool within_ulps(uint64_t actual, uint64_t expected, uint64_t limit)
{
    if ((actual >> 63) != (expected >> 63)) {
        return actual == expected;
    }
    const uint64_t distance = actual > expected ? actual - expected : expected - actual;
    return distance <= limit;
}


std::vector<record_t> binary_program(uint32_t operation)
{
    return {manifest(2), variable(0), variable(1), call(operation), end()};
}


std::vector<record_t> unary_program(uint32_t operation)
{
    return {manifest(1), variable(0), call(operation), end()};
}


void test_load_contract(Test_suite& suite)
{
    Interpreter interpreter;
    loaded_program_t strict{};
    suite.expect(
        load_program(interpreter, binary_program(k_add), strict) == Status::SUCCESS,
        "strict program loads");
    suite.expect(strict.handle != 0 && strict.slot_count == 2, "load response shape");

    loaded_program_t literal_program{};
    suite.expect(
        load_program(
            interpreter,
            {manifest(0), literal_bits(0x8000000000000000ULL), end()},
            literal_program) == Status::SUCCESS,
        "literal-only program loads");
    std::vector<uint8_t> literal_result(8, 0xa5);
    suite.expect(
        evaluate(
            interpreter,
            literal_program,
            k_row_major,
            1,
            {},
            literal_result) == Status::SUCCESS &&
        result_bits(literal_result, 0) == 0x8000000000000000ULL,
        "literal binary64 bits are preserved");
    suite.expect(unload(interpreter, literal_program.handle) == Status::SUCCESS,
        "literal-only handle unloads");

    loaded_program_t unused{};
    suite.expect(
        load_program(
            interpreter,
            {manifest(1, true), variable(0), end()},
            unused) == Status::POLICY_REJECTED,
        "fast policy is recognized and rejected");

    auto separated_program = artifact(binary_program(k_add));
    std::memcpy(separated_program.data(), "MEXCEPRG", 8);
    auto separated_request = load_request(separated_program);
    std::array<uint8_t, 32> sentinel;
    sentinel.fill(0xa5);
    const auto original_sentinel = sentinel;
    suite.expect(
        interpreter.load(
            separated_request.data(), separated_request.size(),
            sentinel.data(), sentinel.size()) == Status::ARTIFACT_INVALID,
        "public protected-format magic is domain separated");
    suite.expect(sentinel == original_sentinel, "failed load publishes no response");

    auto unknown_operation = binary_program(k_add);
    unknown_operation[3].operand = 38;
    suite.expect(
        load_program(interpreter, unknown_operation, unused) == Status::ARTIFACT_INVALID,
        "operation outside bounded profile rejects");
    suite.expect(
        load_program(
            interpreter,
            {manifest(1), variable(0), call(k_add), end()},
            unused) == Status::ARTIFACT_INVALID,
        "stack underflow rejects");
    suite.expect(
        load_program(
            interpreter,
            {literal(1.0), manifest(0), end()},
            unused) == Status::ARTIFACT_INVALID,
        "manifest record order is enforced");
    suite.expect(
        load_program(
            interpreter,
            {manifest(2), variable(0), variable(0), call(k_add), end()},
            unused) == Status::ARTIFACT_INVALID,
        "unused declared slot rejects");
    suite.expect(
        load_program(
            interpreter,
            {manifest(0), literal_bits(0x7ff0000000000000ULL), end()},
            unused) == Status::ARTIFACT_INVALID,
        "nonfinite literal rejects");

    auto malformed_artifact = artifact(binary_program(k_add));
    malformed_artifact[33] = 1;
    auto malformed_request = load_request(malformed_artifact);
    suite.expect(
        interpreter.load(
            malformed_request.data(), malformed_request.size(),
            sentinel.data(), sentinel.size()) == Status::ARTIFACT_INVALID,
        "reserved record bytes reject");

    const auto valid_request = load_request(artifact(binary_program(k_add)));
    auto wrong_version = valid_request;
    wrong_version[8] = 2;
    suite.expect(
        interpreter.load(
            wrong_version.data(), wrong_version.size(),
            sentinel.data(), sentinel.size()) == Status::INPUT_INVALID,
        "load ABI version rejects");
    suite.expect(
        interpreter.load(
            valid_request.data(), valid_request.size() - 1,
            sentinel.data(), sentinel.size()) == Status::INPUT_INVALID,
        "truncated load request rejects");
    suite.expect(
        interpreter.load(
            valid_request.data(), valid_request.size(),
            sentinel.data(), sentinel.size() - 1) == Status::INPUT_INVALID,
        "load response width is exact");

    auto overflowing_size = valid_request;
    store_u64(overflowing_size.data() + 16, (std::numeric_limits<uint64_t>::max)());
    suite.expect(
        interpreter.load(
            overflowing_size.data(), overflowing_size.size(),
            sentinel.data(), sentinel.size()) == Status::INPUT_INVALID,
        "overflowing artifact size rejects");

    auto owned_request = load_request(artifact({manifest(0), literal(2.0), end()}));
    std::array<uint8_t, 32> owned_response = {};
    suite.expect(
        interpreter.load(
            owned_request.data(), owned_request.size(),
            owned_response.data(), owned_response.size()) == Status::SUCCESS,
        "copied artifact loads");
    const loaded_program_t owned_program = {
        load_u64(owned_response.data() + 16),
        load_u32(owned_response.data() + 24),
    };
    std::fill(owned_request.begin(), owned_request.end(), 0);
    std::vector<uint8_t> owned_result(8, 0xa5);
    suite.expect(
        evaluate(interpreter, owned_program, k_row_major, 1, {}, owned_result) ==
            Status::SUCCESS &&
        result_bits(owned_result, 0) == bits_of(2.0),
        "caller mutation after load cannot change owned artifact");
    suite.expect(unload(interpreter, owned_program.handle) == Status::SUCCESS,
        "copied artifact handle unloads");

    suite.expect(unload(interpreter, strict.handle) == Status::SUCCESS, "strict handle unloads");
}


void test_arithmetic_oracles(Test_suite& suite)
{
    // These cases are exact consequences of IEEE 754-2019 binary64 and
    // roundTiesToEven rules; no result was captured from the implementation
    // under test.
    struct binary_case_t
    {
        uint32_t operation;
        uint64_t left;
        uint64_t right;
        uint64_t expected;
        const char* name;
    };
    const binary_case_t cases[] = {
        {k_add, 0x3ff8000000000000ULL, 0x4002000000000000ULL,
            0x400e000000000000ULL, "exact add"},
        {k_add, 0x3ff0000000000000ULL, 0x3ca0000000000000ULL,
            0x3ff0000000000000ULL, "nearest-even tie add"},
        {k_sub, 0xbff8000000000000ULL, 0x4002000000000000ULL,
            0xc00e000000000000ULL, "exact subtract"},
        {k_mul, 0x0000000000000001ULL, 0x3ff0000000000000ULL,
            0x0000000000000001ULL, "subnormal multiply"},
        {k_mul, 0x8000000000000000ULL, 0x4008000000000000ULL,
            0x8000000000000000ULL, "negative-zero multiply"},
        {k_div, 0x3ff0000000000000ULL, 0x4000000000000000ULL,
            0x3fe0000000000000ULL, "exact divide"},
    };

    Interpreter interpreter;
    for (const binary_case_t& test_case : cases) {
        loaded_program_t loaded{};
        suite.expect(
            load_program(interpreter, binary_program(test_case.operation), loaded) == Status::SUCCESS,
            std::string(test_case.name) + " load");
        const std::vector<double> values = {
            from_bits(test_case.left),
            from_bits(test_case.right),
        };
        std::vector<uint8_t> results(8, 0xa5);
        suite.expect(
            evaluate(interpreter, loaded, k_row_major, 1, values, results) == Status::SUCCESS,
            std::string(test_case.name) + " status");
        suite.expect(result_bits(results, 0) == test_case.expected, test_case.name);
        suite.expect(unload(interpreter, loaded.handle) == Status::SUCCESS,
            std::string(test_case.name) + " unload");
    }
}


void test_transcendental_oracles(Test_suite& suite)
{
    // Frozen independently with mpmath 1.3.0 pure-Python libmp at 2048-bit
    // working precision. Each input was reconstructed as its exact binary64
    // rational, then sin/cos was rounded to binary64 by CPython 3.13.7's
    // nearest-even conversion. The four-ULP limit is Gate 0's provisional
    // continuation threshold, not a production numeric contract or a UCRT
    // exact-bit oracle.
    struct trig_case_t
    {
        uint64_t input;
        uint64_t sin_expected;
        uint64_t cos_expected;
        const char* name;
    };
    const trig_case_t cases[] = {
        {0x0000000000000001ULL, 0x0000000000000001ULL, 0x3ff0000000000000ULL,
            "minimum subnormal"},
        {0x3fb999999999999aULL, 0x3fb98eaecb8bcb2cULL, 0x3fefd712f9a817c1ULL,
            "one tenth"},
        {0x3fe0c152382d7365ULL, 0x3fdfffffffffffffULL, 0x3febb67ae8584cabULL,
            "binary64 pi over six"},
        {0xbff0c152382d7365ULL, 0xbfebb67ae8584caaULL, 0x3fe0000000000001ULL,
            "negative binary64 pi over three"},
        {0x400921fb54442d18ULL, 0x3ca1a62633145c07ULL, 0xbff0000000000000ULL,
            "binary64 pi"},
        {0x4415af1d78b58c40ULL, 0xbfe4a5e605fd6450ULL, 0x3fe872720fc60d3dULL,
            "large argument"},
        {0x7e37e43c8800759cULL, 0xbfea2c16b010e385ULL, 0xbfe2699022adc4c1ULL,
            "very large finite argument"},
    };

    Interpreter interpreter;
    loaded_program_t sin_program{};
    loaded_program_t cos_program{};
    suite.expect(
        load_program(interpreter, unary_program(k_sin), sin_program) == Status::SUCCESS,
        "sin program loads");
    suite.expect(
        load_program(interpreter, unary_program(k_cos), cos_program) == Status::SUCCESS,
        "cos program loads");

    for (const trig_case_t& test_case : cases) {
        const std::vector<double> input = {from_bits(test_case.input)};
        std::vector<uint8_t> sin_result(8, 0xa5);
        std::vector<uint8_t> cos_result(8, 0xa5);
        suite.expect(
            evaluate(interpreter, sin_program, k_row_major, 1, input, sin_result) == Status::SUCCESS,
            std::string(test_case.name) + " sin status");
        suite.expect(
            evaluate(interpreter, cos_program, k_row_major, 1, input, cos_result) == Status::SUCCESS,
            std::string(test_case.name) + " cos status");
        suite.expect(
            within_ulps(result_bits(sin_result, 0), test_case.sin_expected, 4),
            std::string(test_case.name) + " sin oracle");
        suite.expect(
            within_ulps(result_bits(cos_result, 0), test_case.cos_expected, 4),
            std::string(test_case.name) + " cos oracle");
    }

    // ISO/IEC 9899:2018 7.12.4.6 and 7.12.4.5 respectively define signed
    // sin(+-0) and cos(+-0)=1.
    for (const uint64_t zero : {0x0000000000000000ULL, 0x8000000000000000ULL}) {
        const std::vector<double> input = {from_bits(zero)};
        std::vector<uint8_t> sin_result(8, 0xa5);
        std::vector<uint8_t> cos_result(8, 0xa5);
        suite.expect(
            evaluate(interpreter, sin_program, k_row_major, 1, input, sin_result) == Status::SUCCESS &&
            result_bits(sin_result, 0) == zero,
            zero == 0 ? "sin positive zero" : "sin negative zero");
        suite.expect(
            evaluate(interpreter, cos_program, k_row_major, 1, input, cos_result) == Status::SUCCESS &&
            result_bits(cos_result, 0) == 0x3ff0000000000000ULL,
            zero == 0 ? "cos positive zero" : "cos negative zero");
    }

    suite.expect(unload(interpreter, sin_program.handle) == Status::SUCCESS, "sin unloads");
    suite.expect(unload(interpreter, cos_program.handle) == Status::SUCCESS, "cos unloads");
}


void test_validation_and_atomic_publication(Test_suite& suite)
{
    Interpreter interpreter;
    loaded_program_t divide{};
    suite.expect(
        load_program(interpreter, binary_program(k_div), divide) == Status::SUCCESS,
        "divide program loads");

    const std::vector<double> rows = {4.0, 2.0, 3.0, -0.0, 8.0, 4.0};
    std::vector<uint8_t> sentinel(3 * sizeof(double), 0xa5);
    const auto original = sentinel;
    suite.expect(
        evaluate(interpreter, divide, k_row_major, 3, rows, sentinel) == Status::EVALUATION_FAILED,
        "signed-zero divisor fails whole batch");
    suite.expect(sentinel == original, "failed row publishes no result bytes");

    sentinel.assign(8, 0xa5);
    const auto positive_zero_sentinel = sentinel;
    suite.expect(
        evaluate(interpreter, divide, k_row_major, 1, {3.0, 0.0}, sentinel) ==
            Status::EVALUATION_FAILED,
        "positive-zero divisor fails");
    suite.expect(sentinel == positive_zero_sentinel,
        "positive-zero failure publishes nothing");

    const std::vector<double> good_rows = {4.0, 2.0, 9.0, 3.0, 8.0, 4.0};
    sentinel.assign(3 * sizeof(double), 0xa5);
    suite.expect(
        evaluate(interpreter, divide, k_row_major, 3, good_rows, sentinel) == Status::SUCCESS,
        "context remains usable after evaluation failure");
    suite.expect(
        result_bits(sentinel, 0) == bits_of(2.0) &&
        result_bits(sentinel, 1) == bits_of(3.0) &&
        result_bits(sentinel, 2) == bits_of(2.0),
        "success publishes one ordered result per row");

    std::vector<double> nonfinite = {
        8.0,
        2.0,
        1.0,
        std::numeric_limits<double>::infinity(),
    };
    sentinel.assign(2 * sizeof(double), 0xa5);
    const auto nonfinite_sentinel = sentinel;
    suite.expect(
        evaluate(interpreter, divide, k_row_major, 2, nonfinite, sentinel) == Status::INPUT_INVALID,
        "every input cell is finite-validated before row zero");
    suite.expect(sentinel == nonfinite_sentinel, "input rejection leaves sentinel unchanged");

    sentinel.assign(8, 0xa5);
    auto wrong_shape = evaluate_request(divide, k_row_major, 1, {1.0, 2.0});
    store_u32(wrong_shape.data() + 32, 3);
    suite.expect(
        interpreter.evaluate_batch(
            wrong_shape.data(), wrong_shape.size(), sentinel.data(), sentinel.size()) ==
            Status::INPUT_INVALID,
        "slot shape mismatch rejects");
    wrong_shape = evaluate_request(divide, k_row_major, 1, {1.0, 2.0});
    wrong_shape[8] = 2;
    suite.expect(
        interpreter.evaluate_batch(
            wrong_shape.data(), wrong_shape.size(), sentinel.data(), sentinel.size()) ==
            Status::INPUT_INVALID,
        "evaluate ABI version rejects");
    const auto valid_evaluate = evaluate_request(divide, k_row_major, 1, {1.0, 2.0});
    suite.expect(
        interpreter.evaluate_batch(
            valid_evaluate.data(), valid_evaluate.size(), sentinel.data(), 7) ==
            Status::INPUT_INVALID,
        "result ABI width is exact");
    suite.expect(
        interpreter.evaluate_batch(
            valid_evaluate.data(), valid_evaluate.size(), nullptr, 8) ==
            Status::INPUT_INVALID,
        "null result buffer rejects");
    auto invalid_layout = valid_evaluate;
    store_u32(invalid_layout.data() + 24, 2);
    suite.expect(
        interpreter.evaluate_batch(
            invalid_layout.data(), invalid_layout.size(), sentinel.data(), 8) ==
            Status::INPUT_INVALID,
        "unknown matrix layout rejects");

    loaded_program_t multiply{};
    suite.expect(
        load_program(interpreter, binary_program(k_mul), multiply) == Status::SUCCESS,
        "multiply program loads");
    const std::vector<double> overflow = {
        std::numeric_limits<double>::max(),
        2.0,
    };
    sentinel.assign(8, 0xa5);
    suite.expect(
        evaluate(interpreter, multiply, k_row_major, 1, overflow, sentinel) ==
            Status::EVALUATION_FAILED,
        "nonfinite intermediate rejects");

    interpreter.fail_next_for_testing(Interpreter::Test_fault::EVALUATE_ALLOCATION);
    sentinel.assign(8, 0xa5);
    const auto allocation_sentinel = sentinel;
    suite.expect(
        evaluate(interpreter, divide, k_row_major, 1, {8.0, 2.0}, sentinel) ==
            Status::INTERNAL_FAILURE,
        "evaluate allocation failure surfaces");
    suite.expect(sentinel == allocation_sentinel, "allocation failure publishes nothing");

    suite.expect(unload(interpreter, divide.handle) == Status::SUCCESS, "divide unloads");
    suite.expect(unload(interpreter, multiply.handle) == Status::SUCCESS, "multiply unloads");
}


std::vector<record_t> motor_crystal_rotation()
{
    return {
        manifest(3), variable(0), variable(1), call(k_sub),
        literal_bits(0x400921fb54442d18ULL), call(k_div), literal(180.0), call(k_mul),
        variable(2), call(k_add), end(),
    };
}


std::vector<record_t> motor_detector_rotation()
{
    return {
        manifest(3), literal(2.0), variable(0), variable(1), call(k_sub), call(k_mul),
        literal_bits(0x400921fb54442d18ULL), call(k_div), literal(180.0), call(k_mul),
        variable(2), call(k_add), end(),
    };
}


void append_r_over_sin(std::vector<record_t>& records, uint32_t angle_slot)
{
    records.push_back(variable(0));
    records.push_back(variable(angle_slot));
    records.push_back(call(k_sin));
    records.push_back(call(k_div));
}


void append_twice_angle(
    std::vector<record_t>& records,
    uint32_t angle_slot,
    uint32_t operation)
{
    records.push_back(literal(2.0));
    records.push_back(variable(angle_slot));
    records.push_back(call(k_mul));
    records.push_back(call(operation));
}


std::vector<record_t> motor_crystal_linear()
{
    std::vector<record_t> records = {manifest(4)};
    append_r_over_sin(records, 1);
    append_r_over_sin(records, 2);
    records.push_back(call(k_sub));
    records.push_back(variable(3));
    records.push_back(call(k_add));
    records.push_back(end());
    return records;
}


void append_detector_linear_term(std::vector<record_t>& records, uint32_t angle_slot)
{
    records.push_back(variable(0));
    records.push_back(literal(1.0));
    append_twice_angle(records, angle_slot, k_cos);
    records.push_back(call(k_add));
    records.push_back(call(k_mul));
    records.push_back(variable(angle_slot));
    records.push_back(call(k_sin));
    records.push_back(call(k_div));
}


std::vector<record_t> motor_detector_linear()
{
    std::vector<record_t> records = {manifest(6)};
    append_detector_linear_term(records, 1);
    append_detector_linear_term(records, 2);
    records.push_back(call(k_sub));
    records.push_back(variable(3));
    records.push_back(variable(4));
    records.push_back(call(k_mul));
    append_twice_angle(records, 1, k_sin);
    records.push_back(call(k_mul));
    records.push_back(call(k_add));
    records.push_back(variable(5));
    records.push_back(call(k_add));
    records.push_back(end());
    return records;
}


void append_detector_transverse_term(
    std::vector<record_t>& records,
    uint32_t angle_slot)
{
    records.push_back(variable(1));
    append_twice_angle(records, angle_slot, k_sin);
    records.push_back(call(k_mul));
    records.push_back(variable(angle_slot));
    records.push_back(call(k_sin));
    records.push_back(call(k_div));
}


std::vector<record_t> motor_detector_transverse()
{
    std::vector<record_t> records = {manifest(7), variable(0)};
    append_detector_transverse_term(records, 2);
    append_detector_transverse_term(records, 3);
    records.push_back(call(k_sub));
    records.push_back(call(k_mul));
    records.push_back(variable(4));
    records.push_back(variable(5));
    records.push_back(call(k_mul));
    append_twice_angle(records, 2, k_cos);
    records.push_back(call(k_mul));
    records.push_back(call(k_add));
    records.push_back(variable(6));
    records.push_back(call(k_add));
    records.push_back(end());
    return records;
}


std::vector<record_t> port_crystal_rotation()
{
    return {manifest(2), variable(0), variable(1), call(k_add), end()};
}


std::vector<record_t> port_detector_transverse()
{
    return {
        manifest(3), literal(0.0), variable(0), call(k_sub), variable(1), call(k_sin), call(k_div),
        literal(2.0), variable(1), call(k_mul), call(k_sin), call(k_mul),
        variable(2), call(k_add), end(),
    };
}


std::vector<record_t> port_detector_rotation()
{
    return {manifest(2), literal(2.0), variable(0), call(k_mul), variable(1), call(k_add), end()};
}


std::vector<record_t> port_detector_linear()
{
    std::vector<record_t> records = {manifest(3)};
    append_r_over_sin(records, 1);
    records.push_back(literal(1.0));
    append_twice_angle(records, 1, k_cos);
    records.push_back(call(k_add));
    records.push_back(call(k_mul));
    records.push_back(variable(2));
    records.push_back(call(k_add));
    records.push_back(end());
    return records;
}


std::vector<record_t> port_crystal_linear()
{
    std::vector<record_t> records = {manifest(3)};
    append_r_over_sin(records, 1);
    records.push_back(variable(2));
    records.push_back(call(k_add));
    records.push_back(end());
    return records;
}


std::vector<double> transpose_to_slot_major(
    const std::vector<double>& row_major,
    uint32_t rows,
    uint32_t slots)
{
    std::vector<double> slot_major(row_major.size());
    for (uint32_t row = 0; row < rows; ++row) {
        for (uint32_t slot = 0; slot < slots; ++slot) {
            slot_major[static_cast<size_t>(slot) * rows + row] =
                row_major[static_cast<size_t>(row) * slots + slot];
        }
    }
    return slot_major;
}


void test_hixas_representative_workflows(Test_suite& suite)
{
    // These source-derived cases prove only that the provisional B0 record and
    // ABI profile can carry all ten owned hiXAS expressions. They are not an
    // oracle for physical motor position, crystal geometry, or alignment truth.
    const std::vector<std::vector<record_t>> programs = {
        motor_crystal_rotation(),
        motor_detector_rotation(),
        motor_crystal_linear(),
        motor_detector_linear(),
        motor_detector_transverse(),
        port_crystal_rotation(),
        port_detector_transverse(),
        port_detector_rotation(),
        port_detector_linear(),
        port_crystal_linear(),
    };
    const std::vector<std::vector<double>> inputs = {
        {0.35, 0.34, 0.1},
        {0.35, 0.34, -0.2},
        {1000.0, 0.35, 0.34, 1.25},
        {1000.0, 0.35, 0.34, 12.5, 0.075, -2.5},
        {-1.0, 1000.0, 0.35, 0.34, 12.5, 0.075, 3.75},
        {0.35, 0.25},
        {1000.0, 0.35, 0.25},
        {0.35, 0.25},
        {1000.0, 0.35, 0.25},
        {1000.0, 0.35, 0.25},
    };

    Interpreter interpreter;
    std::vector<loaded_program_t> handles(programs.size());
    for (size_t index = 0; index < programs.size(); ++index) {
        suite.expect(
            load_program(interpreter, programs[index], handles[index]) == Status::SUCCESS,
            "hiXAS expression " + std::to_string(index) + " loads");
        std::vector<uint8_t> result(8, 0xa5);
        suite.expect(
            evaluate(interpreter, handles[index], k_row_major, 1, inputs[index], result) ==
                Status::SUCCESS &&
            std::isfinite(from_bits(result_bits(result, 0))),
            "hiXAS expression " + std::to_string(index) + " evaluates");
    }

    const loaded_program_t longest = handles[4];
    for (const uint32_t rows : {1U, 8U, 64U, 256U, 1024U}) {
        std::vector<double> row_major;
        row_major.reserve(static_cast<size_t>(rows) * longest.slot_count);
        for (uint32_t row = 0; row < rows; ++row) {
            const double delta = static_cast<double>(row) * 1.0e-7;
            row_major.insert(row_major.end(), {
                -1.0, 1000.0, 0.35 + delta, 0.34, 12.5, 0.075, 3.75,
            });
        }
        const auto slot_major = transpose_to_slot_major(
            row_major, rows, longest.slot_count);
        std::vector<uint8_t> row_results(static_cast<size_t>(rows) * sizeof(double), 0xa5);
        std::vector<uint8_t> slot_results(static_cast<size_t>(rows) * sizeof(double), 0x5a);
        suite.expect(
            evaluate(interpreter, longest, k_row_major, rows, row_major, row_results) ==
                Status::SUCCESS,
            "longest hiXAS row-major " + std::to_string(rows));
        suite.expect(
            evaluate(interpreter, longest, k_slot_major, rows, slot_major, slot_results) ==
                Status::SUCCESS,
            "longest hiXAS slot-major " + std::to_string(rows));
        suite.expect(
            row_results == slot_results,
            "hiXAS layout parity " + std::to_string(rows));
    }

    for (const loaded_program_t& handle : handles) {
        suite.expect(unload(interpreter, handle.handle) == Status::SUCCESS,
            "hiXAS handle unloads");
    }
}


void test_limits_lifecycle_and_concurrency(Test_suite& suite)
{
    Interpreter interpreter;
    loaded_program_t loaded{};

    std::vector<record_t> maximum_stack = {manifest(0)};
    maximum_stack.insert(maximum_stack.end(), 1024, literal(1.0));
    maximum_stack.insert(maximum_stack.end(), 1023, call(k_add));
    maximum_stack.push_back(end());
    suite.expect(
        load_program(interpreter, maximum_stack, loaded) == Status::SUCCESS,
        "stack depth 1024 accepted");
    suite.expect(unload(interpreter, loaded.handle) == Status::SUCCESS,
        "maximum-stack handle unloads");

    std::vector<record_t> excessive_stack = {manifest(0)};
    excessive_stack.insert(excessive_stack.end(), 1025, literal(1.0));
    excessive_stack.insert(excessive_stack.end(), 1024, call(k_add));
    excessive_stack.push_back(end());
    suite.expect(
        load_program(interpreter, excessive_stack, loaded) == Status::ARTIFACT_INVALID,
        "stack depth above 1024 rejects");

    std::vector<record_t> maximum_records = {manifest(0), literal(1.0)};
    for (uint32_t index = 0; index < 8190; ++index) {
        maximum_records.push_back(literal(1.0));
        maximum_records.push_back(call(k_add));
    }
    maximum_records.push_back(call(k_sin));
    maximum_records.push_back(end());
    suite.expect(maximum_records.size() == 16384, "maximum-record fixture shape");
    suite.expect(
        load_program(interpreter, maximum_records, loaded) == Status::SUCCESS,
        "record count 16384 accepted");
    suite.expect(unload(interpreter, loaded.handle) == Status::SUCCESS,
        "maximum-record handle unloads");

    std::vector<loaded_program_t> maximum_contexts(10);
    for (size_t index = 0; index < maximum_contexts.size(); ++index) {
        suite.expect(
            load_program(interpreter, maximum_records, maximum_contexts[index]) == Status::SUCCESS,
            "ten contexts fit the adopted conservative accounting boundary");
    }
    loaded_program_t excessive_context{};
    suite.expect(
        load_program(interpreter, maximum_records, excessive_context) == Status::INTERNAL_FAILURE,
        "explicit conservative context boundary rejects eleven");

    std::vector<uint8_t> held_result(8, 0xa5);
    Status held_status   = Status::INTERNAL_FAILURE;
    Status unload_status = Status::INTERNAL_FAILURE;
    interpreter.hold_next_evaluation_for_testing();
    std::thread held_evaluation([&] {
        held_status = evaluate(
            interpreter,
            maximum_contexts[0],
            k_row_major,
            1,
            {},
            held_result);
    });
    interpreter.wait_until_evaluation_held_for_testing();
    std::thread unloading([&] {
        unload_status = unload(interpreter, maximum_contexts[0].handle);
    });
    interpreter.wait_until_unloading_for_testing(maximum_contexts[0].handle);

    const auto unloading_request = evaluate_request(
        maximum_contexts[0], k_row_major, 1, {});
    std::vector<uint8_t> unloading_sentinel(8, 0xa5);
    const auto original_unloading_sentinel = unloading_sentinel;
    suite.expect(
        interpreter.evaluate_batch(
            unloading_request.data(),
            unloading_request.size(),
            unloading_sentinel.data(),
            unloading_sentinel.size()) == Status::INPUT_INVALID,
        "unload start closes later evaluation admission");
    suite.expect(
        unloading_sentinel == original_unloading_sentinel,
        "post-unload-start rejection publishes nothing");
    suite.expect(
        load_program(interpreter, maximum_records, excessive_context) == Status::INTERNAL_FAILURE,
        "unloading context retains its conservative charge until last use");

    interpreter.release_evaluation_for_testing();
    held_evaluation.join();
    unloading.join();
    suite.expect(
        held_status == Status::SUCCESS && unload_status == Status::SUCCESS,
        "unload waits for the one admitted evaluation");
    suite.expect(
        load_program(interpreter, maximum_records, excessive_context) == Status::SUCCESS,
        "context charge is released after last use and wipe");
    suite.expect(unload(interpreter, excessive_context.handle) == Status::SUCCESS,
        "replacement maximum-record context unloads");
    for (size_t index = 1; index < maximum_contexts.size(); ++index) {
        suite.expect(unload(interpreter, maximum_contexts[index].handle) == Status::SUCCESS,
            "maximum-record context unloads");
    }

    maximum_records.insert(maximum_records.end() - 1, call(k_cos));
    suite.expect(
        load_program(interpreter, maximum_records, loaded) == Status::ARTIFACT_INVALID,
        "record count above 16384 rejects");

    std::vector<record_t> maximum_slots = {
        manifest(4096), variable(0), variable(1), call(k_add),
    };
    for (uint32_t slot = 2; slot < 4096; ++slot) {
        maximum_slots.push_back(variable(slot));
        maximum_slots.push_back(call(k_add));
    }
    maximum_slots.push_back(end());
    suite.expect(
        load_program(interpreter, maximum_slots, loaded) == Status::SUCCESS,
        "slot count 4096 accepted");
    std::vector<double> maximum_slot_values(4096, 1.0);
    std::vector<uint8_t> maximum_slot_result(8, 0xa5);
    suite.expect(
        evaluate(
            interpreter,
            loaded,
            k_row_major,
            1,
            maximum_slot_values,
            maximum_slot_result) == Status::SUCCESS &&
        result_bits(maximum_slot_result, 0) == bits_of(4096.0),
        "maximum indexed-slot request evaluates");

    loaded_program_t second_maximum_slots{};
    suite.expect(
        load_program(interpreter, maximum_slots, second_maximum_slots) == Status::SUCCESS,
        "second maximum-slot context loads");
    constexpr uint32_t k_large_admission_rows = 255;
    std::vector<double> large_values(
        static_cast<size_t>(k_large_admission_rows) * 4096,
        1.0);
    std::vector<uint8_t> large_result(
        k_large_admission_rows * sizeof(double),
        0xa5);
    Status large_status = Status::INTERNAL_FAILURE;
    interpreter.hold_next_evaluation_for_testing();
    std::thread large_evaluation([&] {
        large_status = evaluate(
            interpreter,
            loaded,
            k_row_major,
            k_large_admission_rows,
            large_values,
            large_result);
    });
    interpreter.wait_until_evaluation_held_for_testing();

    interpreter.fail_next_for_testing(Interpreter::Test_fault::EVALUATE_ALLOCATION);
    std::vector<uint8_t> rejected_result(
        k_large_admission_rows * sizeof(double),
        0xa5);
    const auto original_rejected_result = rejected_result;
    suite.expect(
        evaluate(
            interpreter,
            second_maximum_slots,
            k_row_major,
            k_large_admission_rows,
            large_values,
            rejected_result) == Status::INTERNAL_FAILURE,
        "combined call-local reservation rejects before a second large copy");
    suite.expect(
        rejected_result == original_rejected_result,
        "call-local admission rejection publishes nothing");
    interpreter.release_evaluation_for_testing();
    large_evaluation.join();
    suite.expect(
        large_status == Status::SUCCESS &&
        result_bits(large_result, 0) == bits_of(4096.0) &&
        result_bits(large_result, k_large_admission_rows - 1) == bits_of(4096.0),
        "admitted large evaluation completes after gate release");

    std::vector<uint8_t> fault_result(8, 0xa5);
    const auto original_fault_result = fault_result;
    suite.expect(
        evaluate(
            interpreter,
            second_maximum_slots,
            k_row_major,
            1,
            maximum_slot_values,
            fault_result) == Status::INTERNAL_FAILURE,
        "rejected large call did not consume the allocation hook");
    suite.expect(fault_result == original_fault_result,
        "deferred allocation failure publishes nothing");
    suite.expect(
        evaluate(
            interpreter,
            second_maximum_slots,
            k_row_major,
            1,
            maximum_slot_values,
            fault_result) == Status::SUCCESS,
        "context remains usable after bounded admission and allocation failures");

    suite.expect(unload(interpreter, loaded.handle) == Status::SUCCESS,
        "maximum-slot handle unloads");
    suite.expect(unload(interpreter, second_maximum_slots.handle) == Status::SUCCESS,
        "second maximum-slot handle unloads");
    suite.expect(
        load_program(
            interpreter,
            {manifest(4097), variable(0), end()},
            loaded) == Status::ARTIFACT_INVALID,
        "slot count above 4096 rejects");

    suite.expect(
        load_program(interpreter, binary_program(k_add), loaded) == Status::SUCCESS,
        "lifecycle program loads");
    std::vector<uint8_t> oversize(8 * 1024 * 1024 + 1, 0);
    std::memcpy(oversize.data(), k_evaluate_magic, sizeof(k_evaluate_magic));
    suite.expect(
        interpreter.evaluate_batch(
            oversize.data(), oversize.size(), oversize.data(), 8) == Status::INPUT_INVALID,
        "copied input above 8 MiB rejects");

    auto excessive_rows = evaluate_request(loaded, k_row_major, 1025, {});
    std::vector<uint8_t> output(1025 * sizeof(double), 0xa5);
    suite.expect(
        interpreter.evaluate_batch(
            excessive_rows.data(), excessive_rows.size(), output.data(), output.size()) ==
            Status::INPUT_INVALID,
        "row count above 1024 rejects");

    constexpr uint32_t k_concurrent_rows = 1024;
    std::vector<double> first_values;
    std::vector<double> second_values;
    first_values.reserve(k_concurrent_rows * 2);
    second_values.reserve(k_concurrent_rows * 2);
    for (uint32_t row = 0; row < k_concurrent_rows; ++row) {
        first_values.insert(first_values.end(), {1.25, 2.5});
        second_values.insert(second_values.end(), {100.0, -7.0});
    }
    std::vector<uint8_t> first_result(k_concurrent_rows * sizeof(double), 0xa5);
    std::vector<uint8_t> second_result(k_concurrent_rows * sizeof(double), 0xa5);
    const auto original_second_result = second_result;
    Status first_status = Status::INTERNAL_FAILURE;
    interpreter.hold_next_evaluation_for_testing();
    std::thread first([&] {
        first_status = evaluate(
            interpreter, loaded, k_row_major, k_concurrent_rows, first_values, first_result);
    });
    interpreter.wait_until_evaluation_held_for_testing();
    suite.expect(
        evaluate(
            interpreter,
            loaded,
            k_row_major,
            k_concurrent_rows,
            second_values,
            second_result) == Status::INTERNAL_FAILURE,
        "one context rejects a second concurrent admission");
    suite.expect(second_result == original_second_result,
        "concurrent admission rejection publishes nothing");
    interpreter.release_evaluation_for_testing();
    first.join();
    suite.expect(
        first_status == Status::SUCCESS &&
        result_bits(first_result, 0) == bits_of(3.75) &&
        result_bits(first_result, k_concurrent_rows - 1) == bits_of(3.75),
        "one admitted call completes under concurrent pressure");
    suite.expect(
        evaluate(
            interpreter,
            loaded,
            k_row_major,
            k_concurrent_rows,
            second_values,
            second_result) == Status::SUCCESS &&
        result_bits(second_result, 0) == bits_of(93.0) &&
        result_bits(second_result, k_concurrent_rows - 1) == bits_of(93.0),
        "context accepts the next serialized call after release");

    auto malformed_unload = unload_request(loaded.handle);
    malformed_unload[8] = 2;
    suite.expect(
        interpreter.unload(malformed_unload.data(), malformed_unload.size()) ==
            Status::INPUT_INVALID,
        "malformed unload request rejects without changing context");

    const std::vector<double> values = {1.25, 2.5};
    const auto stale_request = evaluate_request(loaded, k_row_major, 1, values);
    std::vector<uint8_t> stale_sentinel(8, 0xa5);
    const auto original_stale_sentinel = stale_sentinel;
    suite.expect(unload(interpreter, loaded.handle) == Status::SUCCESS, "lifecycle unload succeeds");
    suite.expect(
        interpreter.evaluate_batch(
            stale_request.data(),
            stale_request.size(),
            stale_sentinel.data(),
            stale_sentinel.size()) ==
            Status::INPUT_INVALID,
        "stale handle rejects");
    suite.expect(stale_sentinel == original_stale_sentinel,
        "stale handle rejection publishes nothing");
    suite.expect(unload(interpreter, loaded.handle) == Status::INPUT_INVALID,
        "repeated unload rejects stale handle");

    interpreter.fail_next_for_testing(Interpreter::Test_fault::LOAD_ALLOCATION);
    std::array<uint8_t, 32> load_sentinel;
    load_sentinel.fill(0xa5);
    const auto original_sentinel = load_sentinel;
    auto request = load_request(artifact(binary_program(k_add)));
    suite.expect(
        interpreter.load(
            request.data(), request.size(), load_sentinel.data(), load_sentinel.size()) ==
            Status::INTERNAL_FAILURE,
        "load allocation failure surfaces");
    suite.expect(load_sentinel == original_sentinel, "failed allocation publishes no handle");
}


void test_floating_point_environment(Test_suite& suite)
{
    Interpreter interpreter;
    loaded_program_t multiply{};
    suite.expect(
        load_program(interpreter, binary_program(k_mul), multiply) == Status::SUCCESS,
        "floating-point profile program loads");

    const int original_rounding    = std::fegetround();
    const uint32_t original_mxcsr  = _mm_getcsr();
    std::fesetround(FE_UPWARD);
    constexpr uint32_t k_mxcsr_round_up = 0x00004000U;
    constexpr uint32_t k_mxcsr_ftz_daz  = 0x00008040U;
    _mm_setcsr((_mm_getcsr() & ~0x00006000U) | k_mxcsr_round_up | k_mxcsr_ftz_daz);
    const int hostile_rounding   = std::fegetround();
    const uint32_t hostile_mxcsr = _mm_getcsr();

    std::vector<uint8_t> result(8, 0xa5);
    suite.expect(
        evaluate(
            interpreter,
            multiply,
            k_row_major,
            1,
            {from_bits(0x0000000000000001ULL), 1.0},
            result) == Status::SUCCESS,
        "hostile caller environment evaluates");
    suite.expect(
        result_bits(result, 0) == 0x0000000000000001ULL,
        "FTZ and DAZ are disabled during evaluation");
    suite.expect(
        std::fegetround() == hostile_rounding && _mm_getcsr() == hostile_mxcsr,
        "caller floating-point environment is restored");

    _mm_setcsr(original_mxcsr);
    std::fesetround(original_rounding);
    suite.expect(unload(interpreter, multiply.handle) == Status::SUCCESS,
        "floating-point profile handle unloads");
}


} // namespace


int main()
{
    Test_suite suite;
    test_load_contract(suite);
    test_arithmetic_oracles(suite);
    test_transcendental_oracles(suite);
    test_validation_and_atomic_publication(suite);
    test_hixas_representative_workflows(suite);
    test_limits_lifecycle_and_concurrency(suite);
    test_floating_point_environment(suite);

    for (const std::string& failure : suite.failures()) {
        std::cerr << "FAIL: " << failure << '\n';
    }
    if (!suite.failures().empty()) {
        std::cerr << suite.failures().size() << " experimental B0 test(s) failed\n";
        return 1;
    }

    std::cout << "All experimental software-lock B0 tests passed\n";
    return 0;
}

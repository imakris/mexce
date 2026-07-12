#include "mexce_protected_encoder.h"

#include <sodium.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>


namespace {


constexpr size_t k_ref_header_size = 64;
constexpr size_t k_ref_record_size = 32;
constexpr size_t k_ref_frame_size  = 49;
const char* g_fixture_path = MEXCE_PROTECTED_FIXTURE_PATH;
const char* g_fixture_manifest_path = MEXCE_PROTECTED_FIXTURE_MANIFEST_PATH;

static_assert(!std::is_default_constructible<mexce::Protected_expression_key>::value,
    "Protected_expression_key must not be default constructible");
static_assert(!std::is_copy_constructible<mexce::Protected_expression_key>::value,
    "Protected_expression_key must not be copy constructible");
static_assert(std::is_move_constructible<mexce::Protected_expression_key>::value,
    "Protected_expression_key must be move constructible");
static_assert(std::is_move_assignable<mexce::Protected_expression_key>::value,
    "Protected_expression_key must be move assignable");
static_assert(!std::is_copy_constructible<mexce::Protected_expression_bundle>::value,
    "Protected_expression_bundle must not be copy constructible");
static_assert(std::is_move_constructible<mexce::Protected_expression_bundle>::value,
    "Protected_expression_bundle must be move constructible");


struct Test_suite
{
    std::vector<std::string> m_failures;

    void expect(bool value, const std::string& name)
    {
        if (!value) {
            m_failures.push_back(name);
        }
    }

    template<typename Function>
    void expect_error(
        const std::string& name,
        mexce::Protected_expression_error_category category,
        bool has_index,
        Function function)
    {
        try {
            function();
            m_failures.push_back(name + ": expected exception");
        }
        catch (const mexce::Protected_expression_error& error) {
            if (error.category() != category || error.has_record_index() != has_index) {
                m_failures.push_back(name + ": wrong category or index state");
            }
        }
        catch (const std::exception& error) {
            m_failures.push_back(name + ": unexpected exception: " + error.what());
        }
    }

    template<typename Function>
    void expect_error_at(
        const std::string& name,
        mexce::Protected_expression_error_category category,
        uint32_t record_index,
        Function function)
    {
        try {
            function();
            m_failures.push_back(name + ": expected exception");
        }
        catch (const mexce::Protected_expression_error& error) {
            if (error.category() != category || !error.has_record_index() ||
                error.record_index() != record_index)
            {
                m_failures.push_back(name + ": wrong category or record index");
            }
        }
        catch (const std::exception& error) {
            m_failures.push_back(name + ": unexpected exception: " + error.what());
        }
    }
};


struct Collecting_sink
{
    std::vector<std::string> m_records;
    std::vector<uint64_t>    m_literal_bits;

    void manifest(uint32_t bindings, bool fast_math, uint32_t)
    {
        m_records.push_back(
            std::string("manifest:") + std::to_string(bindings) +
            (fast_math ? ":fast" : ":strict"));
    }

    void literal(double value, uint32_t)
    {
        uint64_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        m_literal_bits.push_back(bits);
        std::ostringstream stream;
        stream.precision(17);
        stream << "literal:" << value;
        m_records.push_back(stream.str());
    }

    void variable(uint32_t slot, uint32_t)
    {
        m_records.push_back("variable:" + std::to_string(slot));
    }

    void call(mexce::impl::Protected_operation operation, uint32_t)
    {
        m_records.push_back("call:" + std::to_string(static_cast<uint32_t>(operation)));
    }

    void end(uint32_t) { m_records.push_back("end"); }
};


struct Const_key_consumer
{
    bool& m_called;

    void operator()(const uint8_t*, size_t) { m_called = true; }
    void operator()(uint8_t*, size_t) = delete;
};


std::array<uint8_t, 32> fixture_key_bytes()
{
    std::array<uint8_t, 32> key;
    for (size_t i = 0; i < key.size(); ++i) {
        key[i] = static_cast<uint8_t>(i);
    }
    return key;
}


mexce::Protected_expression_key fixture_key()
{
    const auto bytes = fixture_key_bytes();
    return mexce::Protected_expression_key::from_bytes(bytes.data(), bytes.size());
}


std::vector<uint8_t> read_fixture()
{
    std::ifstream input(g_fixture_path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("protected fixture could not be opened");
    }
    std::vector<uint8_t> program(
        (std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (program.size() != k_ref_header_size + 7 * k_ref_frame_size) {
        throw std::runtime_error("protected fixture has the wrong size");
    }
    return program;
}


std::map<std::string, std::string> read_fixture_manifest()
{
    std::ifstream input(g_fixture_manifest_path);
    if (!input) {
        throw std::runtime_error("protected fixture manifest could not be opened");
    }

    std::map<std::string, std::string> fields;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const size_t separator = line.find('=');
        if (separator == std::string::npos ||
            !fields.insert(std::make_pair(
                line.substr(0, separator), line.substr(separator + 1))).second)
        {
            throw std::runtime_error("protected fixture manifest is malformed");
        }
    }
    return fields;
}


void ref_store_u16(uint8_t* bytes, uint16_t value)
{
    bytes[0] = static_cast<uint8_t>(value);
    bytes[1] = static_cast<uint8_t>(value >> 8);
}


void ref_store_u32(uint8_t* bytes, uint32_t value)
{
    for (size_t i = 0; i < 4; ++i) {
        bytes[i] = static_cast<uint8_t>(value >> (i * 8));
    }
}


void ref_store_u64(uint8_t* bytes, uint64_t value)
{
    for (size_t i = 0; i < 8; ++i) {
        bytes[i] = static_cast<uint8_t>(value >> (i * 8));
    }
}


std::array<uint8_t, k_ref_record_size> ref_record(
    uint8_t kind,
    uint32_t operand_32 = 0,
    uint64_t operand_64 = 0)
{
    std::array<uint8_t, k_ref_record_size> result = {};
    result[0] = kind;
    ref_store_u32(result.data() + 4, operand_32);
    ref_store_u64(result.data() + 8, operand_64);
    return result;
}


std::vector<uint8_t> reference_encrypt_with_tags(
    const std::vector<std::array<uint8_t, k_ref_record_size>>& records,
    const std::vector<unsigned char>& tags)
{
    if (records.size() != tags.size()) {
        throw std::runtime_error("reference record/tag count mismatch");
    }
    const auto key = fixture_key_bytes();
    std::vector<uint8_t> program(k_ref_header_size + records.size() * k_ref_frame_size, 0);
    const uint8_t magic[8] = {'M', 'E', 'X', 'C', 'E', 'P', 'R', 'G'};
    std::memcpy(program.data(), magic, sizeof(magic));
    ref_store_u16(program.data() + 8, 1);
    ref_store_u16(program.data() + 10, 0);
    ref_store_u16(program.data() + 12, k_ref_header_size);
    ref_store_u16(program.data() + 14, k_ref_record_size);
    ref_store_u32(program.data() + 16, static_cast<uint32_t>(records.size()));
    program[24] = 1;

    crypto_secretstream_xchacha20poly1305_state state;
    if (crypto_secretstream_xchacha20poly1305_init_push(
            &state, program.data() + 40, key.data()) != 0)
    {
        throw std::runtime_error("reference init_push failed");
    }
    for (uint32_t i = 0; i < records.size(); ++i) {
        uint8_t additional_data[k_ref_header_size + 4];
        std::memcpy(additional_data, program.data(), k_ref_header_size);
        ref_store_u32(additional_data + k_ref_header_size, i);
        unsigned long long size = 0;
        if (crypto_secretstream_xchacha20poly1305_push(
                &state,
                program.data() + k_ref_header_size + static_cast<size_t>(i) * k_ref_frame_size,
                &size, records[i].data(), records[i].size(),
                additional_data, sizeof(additional_data), tags[i]) != 0 ||
            size != k_ref_frame_size)
        {
            throw std::runtime_error("reference push failed");
        }
    }
    sodium_memzero(&state, sizeof(state));
    return program;
}


std::vector<uint8_t> reference_encrypt(
    const std::vector<std::array<uint8_t, k_ref_record_size>>& records)
{
    std::vector<unsigned char> tags(
        records.size(), crypto_secretstream_xchacha20poly1305_TAG_MESSAGE);
    tags.back() = crypto_secretstream_xchacha20poly1305_TAG_FINAL;
    return reference_encrypt_with_tags(records, tags);
}


void decode(const std::vector<uint8_t>& program, Collecting_sink& sink)
{
    mexce::impl::decode_protected_expression(
        program.data(), program.size(), fixture_key(), sink);
}


void test_fixture_and_encoder(Test_suite& suite)
{
    const auto program = read_fixture();
    const auto manifest = read_fixture_manifest();
    const std::string expected_key_hex =
        "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";
    const std::string expected_program_id_hex =
        "a0a1a2a3a4a5a6a7a8a9aaabacadaeaf";
    const std::string expected_records =
        "MANIFEST(2,strict),VARIABLE(0),LITERAL_F64(2.5),"
        "CALL(MUL),VARIABLE(1),CALL(ADD),END";
    suite.expect(manifest.size() == 4, "fixture manifest field count");
    suite.expect(manifest.at("format") == "1.0", "fixture manifest format");
    suite.expect(manifest.at("key_hex") == expected_key_hex, "fixture manifest key");
    suite.expect(
        manifest.at("program_id_hex") == expected_program_id_hex,
        "fixture manifest program id");
    suite.expect(manifest.at("records") == expected_records, "fixture manifest records");
    suite.expect(program.size() == 407, "fixture size");
    suite.expect(std::equal(program.begin(), program.begin() + 8, "MEXCEPRG"), "fixture magic");
    suite.expect(program[8] == 1 && program[9] == 0 && program[10] == 0, "fixture version");
    suite.expect(program[12] == 64 && program[14] == 32 && program[16] == 7, "fixture framing");
    bool fixture_program_id_matches = true;
    for (size_t i = 0; i < 16; ++i) {
        fixture_program_id_matches &= program[24 + i] == static_cast<uint8_t>(0xa0 + i);
    }
    suite.expect(fixture_program_id_matches, "fixture binary program id matches manifest");

    Collecting_sink fixture_sink;
    auto& wipe_observer = mexce::impl::protected_wipe_observer();
    using Wipe_context = mexce::impl::Protected_wipe_context;
    wipe_observer.reset();
    decode(program, fixture_sink);
    const std::vector<std::string> expected = {
        "manifest:2:strict", "variable:0", "literal:2.5", "call:3",
        "variable:1", "call:1", "end",
    };
    suite.expect(fixture_sink.m_records == expected, "independent fixture records");
    suite.expect(
        wipe_observer.count(Wipe_context::KEY) > 0 &&
        wipe_observer.count(Wipe_context::PULL_STATE) > 0 &&
        wipe_observer.count(Wipe_context::CLEAR_RECORD) > 0 &&
        wipe_observer.count(Wipe_context::ADDITIONAL_DATA) > 0 &&
        wipe_observer.count(Wipe_context::DECODED_SCALAR) > 0 &&
        wipe_observer.count(Wipe_context::VALIDATOR_STATE) > 0 &&
        wipe_observer.all_observed_regions_are_zero(),
        "decoder wipes each transient context to zero");

    const std::vector<mexce::Protected_binding> bindings = {{"x", 0}, {"y", 1}};
    wipe_observer.reset();
    auto bundle = mexce::encode_protected_expression(
        "x * 2.5 + y", bindings, mexce::Protected_math_mode::STRICT);
    suite.expect(
        wipe_observer.count(Wipe_context::KEY) > 0 &&
        wipe_observer.count(Wipe_context::PUSH_STATE) > 0 &&
        wipe_observer.count(Wipe_context::CLEAR_RECORD) > 0 &&
        wipe_observer.count(Wipe_context::ADDITIONAL_DATA) > 0 &&
        wipe_observer.all_observed_regions_are_zero(),
        "encoder wipes each transient context to zero");
    suite.expect(bundle.program.size() == 407, "encoder blob size");
    suite.expect(
        std::equal(bundle.program.begin(), bundle.program.begin() + 8, "MEXCEPRG"),
        "encoder magic");
    suite.expect(
        bundle.program[8] == 1 && bundle.program[9] == 0 &&
        bundle.program[10] == 0 && bundle.program[11] == 0 &&
        bundle.program[12] == 64 && bundle.program[13] == 0 &&
        bundle.program[14] == 32 && bundle.program[15] == 0 &&
        bundle.program[16] == 7 && bundle.program[17] == 0 &&
        bundle.program[18] == 0 && bundle.program[19] == 0,
        "encoder version and framing fields");
    suite.expect(
        std::equal(
            bundle.program.begin() + 24, bundle.program.begin() + 40,
            bundle.program_id.begin()),
        "encoder program id field");
    Collecting_sink encoded_sink;
    mexce::impl::decode_protected_expression(
        bundle.program.data(), bundle.program.size(), std::move(bundle.key), encoded_sink);
    suite.expect(encoded_sink.m_records == expected, "encoder uses canonical semantic records");
    suite.expect_error(
        "successful decode consumes transferred key",
        mexce::Protected_expression_error_category::INVALID_ARGUMENT, false,
        [&] { bundle.key.consume_bytes([](const uint8_t*, size_t) {}); });

    auto second_bundle = mexce::encode_protected_expression(
        "x * 2.5 + y", bindings, mexce::Protected_math_mode::STRICT);
    suite.expect(bundle.program != second_bundle.program, "encoder uses fresh artifact randomness");
    suite.expect(!mexce::impl::all_zero(
        second_bundle.program_id.data(), second_bundle.program_id.size()),
        "encoder program id is nonzero");

    auto separation_bundle = mexce::encode_protected_expression(
        "x * 2.5 + y", bindings, mexce::Protected_math_mode::STRICT);
    std::array<uint8_t, 32> second_key = {};
    std::array<uint8_t, 32> separation_key = {};
    second_bundle.key.consume_bytes([&](const uint8_t* bytes, size_t size) {
        std::copy(bytes, bytes + size, second_key.begin());
    });
    separation_bundle.key.consume_bytes([&](const uint8_t* bytes, size_t size) {
        std::copy(bytes, bytes + size, separation_key.begin());
    });
    suite.expect(second_key != separation_key, "encoder generates a fresh key per artifact");

    auto fast_bundle = mexce::encode_protected_expression(
        "x+y", bindings, mexce::Protected_math_mode::FAST);
    Collecting_sink fast_sink;
    mexce::impl::decode_protected_expression(
        fast_bundle.program.data(), fast_bundle.program.size(),
        std::move(fast_bundle.key), fast_sink);
    suite.expect(fast_sink.m_records.front() == "manifest:2:fast", "fast policy round trip");

    auto unary_minus_bundle = mexce::encode_protected_expression(
        "-x", {{"x", 0}}, mexce::Protected_math_mode::STRICT);
    Collecting_sink unary_minus_sink;
    mexce::impl::decode_protected_expression(
        unary_minus_bundle.program.data(), unary_minus_bundle.program.size(),
        std::move(unary_minus_bundle.key), unary_minus_sink);
    const std::vector<std::string> unary_minus_expected = {
        "manifest:1:strict", "literal:0", "variable:0", "call:2", "end",
    };
    suite.expect(
        unary_minus_sink.m_records == unary_minus_expected,
        "source unary minus canonical records");

    auto unary_plus_bundle = mexce::encode_protected_expression(
        "+x", {{"x", 0}}, mexce::Protected_math_mode::STRICT);
    Collecting_sink unary_plus_sink;
    mexce::impl::decode_protected_expression(
        unary_plus_bundle.program.data(), unary_plus_bundle.program.size(),
        std::move(unary_plus_bundle.key), unary_plus_sink);
    const std::vector<std::string> unary_plus_expected = {
        "manifest:1:strict", "variable:0", "end",
    };
    suite.expect(
        unary_plus_sink.m_records == unary_plus_expected,
        "source unary plus emits no operation");

    auto constants_bundle = mexce::encode_protected_expression(
        "pi+e", {}, mexce::Protected_math_mode::STRICT);
    Collecting_sink constants_sink;
    mexce::impl::decode_protected_expression(
        constants_bundle.program.data(), constants_bundle.program.size(),
        std::move(constants_bundle.key), constants_sink);
    suite.expect(
        constants_sink.m_literal_bits.size() == 2 &&
        std::find(constants_sink.m_records.begin(), constants_sink.m_records.end(), "call:1") !=
            constants_sink.m_records.end(),
        "built-in constants become literals");
}


void test_operation_ids_and_aliases(Test_suite& suite)
{
    struct operation_case_t
    {
        const char* expression;
        uint32_t    operation;
        bool        binary;
    };
    const operation_case_t cases[] = {
        {"add(x,y)", 1, true},   {"sub(x,y)", 2, true},   {"mul(x,y)", 3, true},
        {"div(x,y)", 4, true},   {"neg(x)", 5, false},    {"pow(x,y)", 6, true},
        {"sin(x)", 7, false},    {"cos(x)", 8, false},    {"tan(x)", 9, false},
        {"abs(x)", 10, false},   {"sign(x)", 11, false},  {"signp(x)", 12, false},
        {"expn(x)", 13, false},  {"sfc(x)", 14, false},   {"sqrt(x)", 15, false},
        {"exp(x)", 16, false},   {"lt(x,y)", 17, true},   {"gt(x,y)", 18, true},
        {"le(x,y)", 19, true},   {"ge(x,y)", 20, true},   {"eq(x,y)", 21, true},
        {"ne(x,y)", 22, true},   {"log(x)", 23, false},   {"log2(x)", 24, false},
        {"log10(x)", 25, false}, {"logb(x,y)", 26, true}, {"ylog2(x,y)", 27, true},
        {"max(x,y)", 28, true},  {"min(x,y)", 29, true},  {"floor(x)", 30, false},
        {"ceil(x)", 31, false},  {"round(x)", 32, false}, {"int(x)", 33, false},
        {"trunc(x)", 34, false}, {"mod(x,y)", 35, true},  {"bnd(x,y)", 36, true},
        {"bias(x,y)", 37, true}, {"gain(x,y)", 38, true},
        {"x+y", 1, true},        {"x-y", 2, true},        {"x*y", 3, true},
        {"x/y", 4, true},        {"x^y", 6, true},        {"x**y", 6, true},
        {"x<y", 17, true},       {"x>y", 18, true},       {"ln(x)", 23, false},
    };

    for (const auto& operation_case : cases) {
        std::vector<mexce::Protected_binding> bindings = {{"x", 0}};
        if (operation_case.binary) {
            bindings.push_back({"y", 1});
        }
        auto bundle = mexce::encode_protected_expression(
            operation_case.expression, bindings, mexce::Protected_math_mode::STRICT);
        Collecting_sink sink;
        mexce::impl::decode_protected_expression(
            bundle.program.data(), bundle.program.size(), std::move(bundle.key), sink);
        std::vector<std::string> expected_records = {
            std::string("manifest:") + (operation_case.binary ? "2" : "1") + ":strict",
            "variable:0",
        };
        if (operation_case.binary) {
            expected_records.push_back("variable:1");
        }
        expected_records.push_back("call:" + std::to_string(operation_case.operation));
        expected_records.push_back("end");
        suite.expect(
            sink.m_records == expected_records,
            std::string("canonical operation records: ") + operation_case.expression);

        std::vector<std::array<uint8_t, k_ref_record_size>> reference_records;
        reference_records.push_back(ref_record(1, operation_case.binary ? 2 : 1));
        reference_records.push_back(ref_record(3, 0));
        if (operation_case.binary) {
            reference_records.push_back(ref_record(3, 1));
        }
        reference_records.push_back(ref_record(4, operation_case.operation));
        reference_records.push_back(ref_record(5));
        auto reference_program = reference_encrypt(reference_records);
        Collecting_sink reference_sink;
        decode(reference_program, reference_sink);
        suite.expect(
            reference_sink.m_records == expected_records,
            std::string("independent operation arity: ") + operation_case.expression);
    }

    const uint64_t finite_literal_bits[] = {
        0x0000000000000000ULL,
        0x8000000000000000ULL,
        0x0000000000000001ULL,
        0x7fefffffffffffffULL,
        0xffefffffffffffffULL,
    };
    for (const uint64_t bits : finite_literal_bits) {
        auto literal_program = reference_encrypt({
            ref_record(1), ref_record(2, 0, bits), ref_record(5)});
        Collecting_sink literal_sink;
        decode(literal_program, literal_sink);
        suite.expect(
            literal_sink.m_literal_bits.size() == 1 &&
            literal_sink.m_literal_bits.front() == bits,
            "finite literal bits: " + std::to_string(bits));
    }
}


void test_key_ownership(Test_suite& suite)
{
    auto bytes = fixture_key_bytes();
    auto& wipe_observer = mexce::impl::protected_wipe_observer();
    using Wipe_context = mexce::impl::Protected_wipe_context;
    suite.expect_error(
        "null key", mexce::Protected_expression_error_category::INVALID_ARGUMENT, false,
        [&] { (void)mexce::Protected_expression_key::from_bytes(nullptr, bytes.size()); });
    suite.expect_error(
        "short key", mexce::Protected_expression_error_category::INVALID_ARGUMENT, false,
        [&] { (void)mexce::Protected_expression_key::from_bytes(bytes.data(), bytes.size() - 1); });

    auto copied_key = mexce::Protected_expression_key::from_bytes(bytes.data(), bytes.size());
    std::fill(bytes.begin(), bytes.end(), uint8_t(0xff));
    bool copied_bytes_match = false;
    copied_key.consume_bytes([&](const uint8_t* owned, size_t size) {
        const auto expected = fixture_key_bytes();
        copied_bytes_match = size == expected.size() &&
            std::equal(owned, owned + size, expected.begin());
    });
    suite.expect(copied_bytes_match, "key factory copies caller bytes");

    bool const_consumer_called = false;
    auto const_consumer_key = fixture_key();
    Const_key_consumer const_consumer = {const_consumer_called};
    const_consumer_key.consume_bytes(const_consumer);
    suite.expect(const_consumer_called, "key consumer receives only const bytes");

    auto key = fixture_key();
    auto moved = std::move(key);
    suite.expect_error(
        "moved-from key", mexce::Protected_expression_error_category::INVALID_ARGUMENT, false,
        [&] { key.consume_bytes([](const uint8_t*, size_t) {}); });

    bool recursive_rejected = false;
    moved.consume_bytes([&](const uint8_t*, size_t) {
        try {
            moved.consume_bytes([](const uint8_t*, size_t) {});
        }
        catch (const mexce::Protected_expression_error&) {
            recursive_rejected = true;
        }
    });
    suite.expect(recursive_rejected, "recursive consumption rejected");
    suite.expect_error(
        "second consumption", mexce::Protected_expression_error_category::INVALID_ARGUMENT, false,
        [&] { moved.consume_bytes([](const uint8_t*, size_t) {}); });

    auto move_in_callback = fixture_key();
    bool callback_move_observed_empty = false;
    move_in_callback.consume_bytes([&](const uint8_t*, size_t) {
        auto moved_inside = std::move(move_in_callback);
        try {
            moved_inside.consume_bytes([](const uint8_t*, size_t) {});
        }
        catch (const mexce::Protected_expression_error&) {
            callback_move_observed_empty = true;
        }
    });
    suite.expect(callback_move_observed_empty, "callback move observes empty key");

    auto throwing = fixture_key();
    bool callback_exception_rethrown = false;
    wipe_observer.reset();
    try {
        throwing.consume_bytes([](const uint8_t*, size_t) {
            throw std::runtime_error("callback");
        });
    }
    catch (const std::runtime_error& error) {
        callback_exception_rethrown = std::string(error.what()) == "callback";
    }
    suite.expect(callback_exception_rethrown, "callback exception is rethrown unchanged");
    suite.expect(
        wipe_observer.count(Wipe_context::KEY) > 0 &&
        wipe_observer.all_observed_regions_are_zero(),
        "callback exception wipes key to zero");
    suite.expect_error(
        "callback exception consumes key",
        mexce::Protected_expression_error_category::INVALID_ARGUMENT, false,
        [&] { throwing.consume_bytes([](const uint8_t*, size_t) {}); });

    auto assigned = fixture_key();
    auto assignment_source = fixture_key();
    wipe_observer.reset();
    assigned = std::move(assignment_source);
    suite.expect(
        wipe_observer.count(Wipe_context::KEY) > 0 &&
        wipe_observer.all_observed_regions_are_zero(),
        "move assignment wipes replaced key to zero");
    assigned.consume_bytes([](const uint8_t*, size_t) {});
    suite.expect_error(
        "move-assignment source empty",
        mexce::Protected_expression_error_category::INVALID_ARGUMENT, false,
        [&] { assignment_source.consume_bytes([](const uint8_t*, size_t) {}); });

    auto& faults = mexce::impl::protected_test_faults();
    faults.fail_malloc = true;
    suite.expect_error(
        "guarded allocation failure",
        mexce::Protected_expression_error_category::RESOURCE_FAILURE, false,
        [&] { (void)mexce::Protected_expression_key::from_bytes(bytes.data(), bytes.size()); });
    faults.fail_malloc = false;

    wipe_observer.reset();
    faults.fail_mlock = true;
    suite.expect_error(
        "mlock failure", mexce::Protected_expression_error_category::RESOURCE_FAILURE, false,
        [&] { (void)mexce::Protected_expression_key::from_bytes(bytes.data(), bytes.size()); });
    faults.fail_mlock = false;
    suite.expect(
        wipe_observer.count(Wipe_context::KEY) > 0 &&
        wipe_observer.all_observed_regions_are_zero(),
        "mlock failure wipes allocation to zero");
}


void test_public_framing(Test_suite& suite)
{
    const auto fixture = read_fixture();
    Collecting_sink sink;
    suite.expect_error(
        "empty", mexce::Protected_expression_error_category::INVALID_ARGUMENT, false,
        [&] { mexce::impl::decode_protected_expression(nullptr, 0, fixture_key(), sink); });
    suite.expect_error(
        "short", mexce::Protected_expression_error_category::MALFORMED_PROGRAM, false,
        [&] {
            mexce::impl::decode_protected_expression(
                fixture.data(), 63, fixture_key(), sink);
        });

    struct mutation_t
    {
        size_t offset;
        mexce::Protected_expression_error_category category;
    };
    const mutation_t mutations[] = {
        {0, mexce::Protected_expression_error_category::UNSUPPORTED_FORMAT},
        {8, mexce::Protected_expression_error_category::UNSUPPORTED_FORMAT},
        {10, mexce::Protected_expression_error_category::UNSUPPORTED_FORMAT},
        {12, mexce::Protected_expression_error_category::UNSUPPORTED_FORMAT},
        {14, mexce::Protected_expression_error_category::UNSUPPORTED_FORMAT},
        {20, mexce::Protected_expression_error_category::UNSUPPORTED_FORMAT},
        {24, mexce::Protected_expression_error_category::MALFORMED_PROGRAM},
    };
    for (const auto& mutation : mutations) {
        auto changed = fixture;
        changed[mutation.offset] ^= 1;
        if (mutation.offset == 24) {
            std::fill(changed.begin() + 24, changed.begin() + 40, 0);
        }
        suite.expect_error(
            "public mutation " + std::to_string(mutation.offset),
            mutation.category, false,
            [&] { decode(changed, sink); });
    }

    auto oversized = fixture;
    ref_store_u32(oversized.data() + 16, 16385);
    suite.expect_error(
        "record limit", mexce::Protected_expression_error_category::SIZE_LIMIT, false,
        [&] { decode(oversized, sink); });
    auto too_few = fixture;
    ref_store_u32(too_few.data() + 16, 2);
    too_few.resize(k_ref_header_size + 2 * k_ref_frame_size);
    suite.expect_error(
        "too few records", mexce::Protected_expression_error_category::MALFORMED_PROGRAM, false,
        [&] { decode(too_few, sink); });

    auto truncated = fixture;
    truncated.pop_back();
    auto truncated_key = fixture_key();
    suite.expect_error(
        "truncated", mexce::Protected_expression_error_category::MALFORMED_PROGRAM, false,
        [&] {
            mexce::impl::decode_protected_expression(
                truncated.data(), truncated.size(), std::move(truncated_key), sink);
        });
    suite.expect_error(
        "failed decode consumes transferred key",
        mexce::Protected_expression_error_category::INVALID_ARGUMENT, false,
        [&] { truncated_key.consume_bytes([](const uint8_t*, size_t) {}); });
    auto trailing = fixture;
    trailing.push_back(0);
    suite.expect_error(
        "trailing", mexce::Protected_expression_error_category::MALFORMED_PROGRAM, false,
        [&] { decode(trailing, sink); });

    auto compensated_removal = fixture;
    ref_store_u32(compensated_removal.data() + 16, 6);
    compensated_removal.resize(k_ref_header_size + 6 * k_ref_frame_size);
    suite.expect_error_at(
        "compensated frame removal",
        mexce::Protected_expression_error_category::AUTHENTICATION_FAILED, 0,
        [&] { decode(compensated_removal, sink); });

    auto compensated_insertion = fixture;
    ref_store_u32(compensated_insertion.data() + 16, 8);
    compensated_insertion.resize(k_ref_header_size + 8 * k_ref_frame_size);
    suite.expect_error_at(
        "compensated frame insertion",
        mexce::Protected_expression_error_category::AUTHENTICATION_FAILED, 0,
        [&] { decode(compensated_insertion, sink); });
}


void test_authentication(Test_suite& suite)
{
    const auto fixture = read_fixture();
    Collecting_sink sink;

    auto wrong_bytes = fixture_key_bytes();
    wrong_bytes[0] ^= 1;
    auto& wipe_observer = mexce::impl::protected_wipe_observer();
    using Wipe_context = mexce::impl::Protected_wipe_context;
    wipe_observer.reset();
    suite.expect_error_at(
        "wrong key", mexce::Protected_expression_error_category::AUTHENTICATION_FAILED, 0,
        [&] {
            mexce::impl::decode_protected_expression(
                fixture.data(), fixture.size(),
                mexce::Protected_expression_key::from_bytes(
                    wrong_bytes.data(), wrong_bytes.size()),
                sink);
        });
    suite.expect(
        wipe_observer.count(Wipe_context::KEY) > 0 &&
        wipe_observer.count(Wipe_context::PULL_STATE) > 0 &&
        wipe_observer.count(Wipe_context::CLEAR_RECORD) > 0 &&
        wipe_observer.count(Wipe_context::ADDITIONAL_DATA) > 0 &&
        wipe_observer.count(Wipe_context::VALIDATOR_STATE) > 0 &&
        wipe_observer.all_observed_regions_are_zero(),
        "authentication failure wipes each transient context to zero");

    auto late_failure = fixture;
    late_failure.back() ^= 1;
    wipe_observer.reset();
    suite.expect_error_at(
        "late authentication failure",
        mexce::Protected_expression_error_category::AUTHENTICATION_FAILED, 6,
        [&] { decode(late_failure, sink); });
    suite.expect(
        wipe_observer.count(Wipe_context::KEY) > 0 &&
        wipe_observer.count(Wipe_context::PULL_STATE) > 0 &&
        wipe_observer.count(Wipe_context::CLEAR_RECORD) > 0 &&
        wipe_observer.count(Wipe_context::ADDITIONAL_DATA) > 0 &&
        wipe_observer.count(Wipe_context::DECODED_SCALAR) > 0 &&
        wipe_observer.count(Wipe_context::VALIDATOR_STATE) > 0 &&
        wipe_observer.all_observed_regions_are_zero(),
        "late authentication failure wipes populated transient state to zero");

    for (size_t offset = 40; offset < fixture.size(); ++offset) {
        auto changed = fixture;
        changed[offset] ^= 1;
        const uint32_t record_index = offset < k_ref_header_size
            ? 0
            : static_cast<uint32_t>((offset - k_ref_header_size) / k_ref_frame_size);
        suite.expect_error_at(
            "authenticated byte " + std::to_string(offset),
            mexce::Protected_expression_error_category::AUTHENTICATION_FAILED,
            record_index,
            [&] { decode(changed, sink); });
    }

    for (size_t offset = 24; offset < 40; ++offset) {
        auto changed = fixture;
        changed[offset] ^= 1;
        suite.expect_error_at(
            "program id byte " + std::to_string(offset),
            mexce::Protected_expression_error_category::AUTHENTICATION_FAILED, 0,
            [&] { decode(changed, sink); });
    }

    auto swapped = fixture;
    std::swap_ranges(
        swapped.begin() + k_ref_header_size,
        swapped.begin() + k_ref_header_size + k_ref_frame_size,
        swapped.begin() + k_ref_header_size + k_ref_frame_size);
    suite.expect_error_at(
        "frame swap", mexce::Protected_expression_error_category::AUTHENTICATION_FAILED, 0,
        [&] { decode(swapped, sink); });

    auto duplicated = fixture;
    std::copy(
        duplicated.begin() + k_ref_header_size + k_ref_frame_size,
        duplicated.begin() + k_ref_header_size + 2 * k_ref_frame_size,
        duplicated.begin() + k_ref_header_size + 2 * k_ref_frame_size);
    suite.expect_error_at(
        "frame duplication", mexce::Protected_expression_error_category::AUTHENTICATION_FAILED, 2,
        [&] { decode(duplicated, sink); });

    auto& faults = mexce::impl::protected_test_faults();
    wipe_observer.reset();
    faults.fail_init_pull = true;
    suite.expect_error(
        "init pull", mexce::Protected_expression_error_category::AUTHENTICATION_FAILED, false,
        [&] { decode(fixture, sink); });
    faults.fail_init_pull = false;
    suite.expect(
        wipe_observer.count(Wipe_context::KEY) > 0 &&
        wipe_observer.count(Wipe_context::PULL_STATE) > 0 &&
        wipe_observer.all_observed_regions_are_zero(),
        "init-pull failure wipes key and stream state to zero");
}


void expect_malformed_records(
    Test_suite& suite,
    const std::string& name,
    const std::vector<std::array<uint8_t, k_ref_record_size>>& records,
    uint32_t record_index)
{
    auto program = reference_encrypt(records);
    Collecting_sink sink;
    suite.expect_error_at(
        name, mexce::Protected_expression_error_category::MALFORMED_PROGRAM, record_index,
        [&] { decode(program, sink); });
}


void test_semantic_validation(Test_suite& suite)
{
    const auto manifest = ref_record(1, 0, 0);
    const auto literal  = ref_record(2, 0, 0x3ff0000000000000ULL);
    const auto end      = ref_record(5);

    std::vector<std::array<uint8_t, k_ref_record_size>> valid_records = {
        ref_record(1, 1),
        ref_record(3, 0),
        literal,
        ref_record(4, 1),
        end,
    };
    const size_t reserved_offsets[] = {
        1, 2, 3,
        16, 17, 18, 19, 20, 21, 22, 23,
        24, 25, 26, 27, 28, 29, 30, 31,
    };
    for (uint32_t index = 0; index < valid_records.size(); ++index) {
        for (const size_t offset : reserved_offsets) {
            auto changed = valid_records;
            changed[index][offset] = 1;
            expect_malformed_records(
                suite,
                "reserved byte " + std::to_string(index) + ":" + std::to_string(offset),
                changed,
                index);
        }
    }

    for (size_t offset = 4; offset < 8; ++offset) {
        auto changed = valid_records;
        changed[2][offset] = 1;
        expect_malformed_records(
            suite, "literal unused byte " + std::to_string(offset), changed, 2);
    }
    for (size_t offset = 8; offset < 16; ++offset) {
        auto variable_changed = valid_records;
        variable_changed[1][offset] = 1;
        expect_malformed_records(
            suite, "variable unused byte " + std::to_string(offset), variable_changed, 1);

        auto call_changed = valid_records;
        call_changed[3][offset] = 1;
        expect_malformed_records(
            suite, "call unused byte " + std::to_string(offset), call_changed, 3);
    }
    for (size_t offset = 4; offset < 16; ++offset) {
        auto changed = valid_records;
        changed[4][offset] = 1;
        expect_malformed_records(
            suite, "end unused byte " + std::to_string(offset), changed, 4);
    }

    expect_malformed_records(suite, "unknown kind", {manifest, ref_record(9), end}, 1);
    expect_malformed_records(suite, "unknown operation", {
        manifest, literal, literal, ref_record(4, 39), end}, 3);
    expect_malformed_records(suite, "stack underflow", {
        manifest, literal, ref_record(4, 1), end}, 2);
    expect_malformed_records(suite, "terminal depth", {
        manifest, literal, literal, end}, 3);
    expect_malformed_records(suite, "nonfinite", {
        manifest, ref_record(2, 0, 0x7ff0000000000000ULL), end}, 1);
    expect_malformed_records(suite, "slot out of range", {
        ref_record(1, 1), ref_record(3, 1), end}, 1);
    expect_malformed_records(suite, "unused slot", {
        ref_record(1, 1), literal, end}, 2);
    expect_malformed_records(suite, "manifest policy", {
        ref_record(1, 0, 2), literal, end}, 0);
    expect_malformed_records(suite, "early end", {manifest, end, end}, 1);

    auto excessive_bindings = reference_encrypt({ref_record(1, 4097), literal, end});
    Collecting_sink excessive_bindings_sink;
    suite.expect_error_at(
        "authenticated binding limit", mexce::Protected_expression_error_category::SIZE_LIMIT, 0,
        [&] { decode(excessive_bindings, excessive_bindings_sink); });

    std::vector<std::array<uint8_t, k_ref_record_size>> deep_records;
    deep_records.push_back(manifest);
    deep_records.insert(deep_records.end(), 1025, literal);
    deep_records.push_back(end);
    auto deep_program = reference_encrypt(deep_records);
    Collecting_sink deep_sink;
    suite.expect_error_at(
        "authenticated semantic depth limit",
        mexce::Protected_expression_error_category::SIZE_LIMIT, 1025,
        [&] { decode(deep_program, deep_sink); });

    const std::vector<std::array<uint8_t, k_ref_record_size>> tag_records = {
        manifest, literal, end};
    Collecting_sink sink;
    const unsigned char message = crypto_secretstream_xchacha20poly1305_TAG_MESSAGE;
    const unsigned char final   = crypto_secretstream_xchacha20poly1305_TAG_FINAL;
    const unsigned char push    = crypto_secretstream_xchacha20poly1305_TAG_PUSH;
    const unsigned char rekey   = crypto_secretstream_xchacha20poly1305_TAG_REKEY;

    auto missing_final = reference_encrypt_with_tags(
        tag_records, {message, message, message});
    suite.expect_error_at(
        "missing final tag", mexce::Protected_expression_error_category::MALFORMED_PROGRAM, 2,
        [&] { decode(missing_final, sink); });

    auto nonfinal_final = reference_encrypt_with_tags(
        tag_records, {message, final, final});
    suite.expect_error_at(
        "non-final final tag", mexce::Protected_expression_error_category::MALFORMED_PROGRAM, 1,
        [&] { decode(nonfinal_final, sink); });

    auto push_tag = reference_encrypt_with_tags(
        tag_records, {message, push, final});
    suite.expect_error_at(
        "push tag", mexce::Protected_expression_error_category::MALFORMED_PROGRAM, 1,
        [&] { decode(push_tag, sink); });

    auto rekey_tag = reference_encrypt_with_tags(
        tag_records, {message, rekey, final});
    suite.expect_error_at(
        "rekey tag", mexce::Protected_expression_error_category::MALFORMED_PROGRAM, 1,
        [&] { decode(rekey_tag, sink); });
}


void test_encoder_validation(Test_suite& suite)
{
    using Category = mexce::Protected_expression_error_category;
    const std::vector<mexce::Protected_binding> xy = {{"x", 0}, {"y", 1}};
    suite.expect_error(
        "invalid math mode", Category::INVALID_ARGUMENT, false,
        [&] {
            (void)mexce::encode_protected_expression(
                "x+y", xy, static_cast<mexce::Protected_math_mode>(2));
        });
    bool syntax_error_reported = false;
    try {
        (void)mexce::encode_protected_expression(
            "unknown_name", {}, mexce::Protected_math_mode::STRICT);
    }
    catch (const mexce::mexce_parsing_exception&) {
        syntax_error_reported = true;
    }
    suite.expect(syntax_error_reported, "issuer syntax errors retain parsing exception type");
    suite.expect_error(
        "sparse slots", Category::INVALID_ARGUMENT, false,
        [&] {
            (void)mexce::encode_protected_expression(
                "x", {{"x", 1}}, mexce::Protected_math_mode::STRICT);
        });
    suite.expect_error(
        "duplicate names", Category::INVALID_ARGUMENT, false,
        [&] {
            (void)mexce::encode_protected_expression(
                "x", {{"x", 0}, {"x", 1}}, mexce::Protected_math_mode::STRICT);
        });
    suite.expect_error(
        "invalid name", Category::INVALID_ARGUMENT, false,
        [&] {
            (void)mexce::encode_protected_expression(
                "x", {{"9x", 0}}, mexce::Protected_math_mode::STRICT);
        });
    suite.expect_error(
        "reserved name", Category::INVALID_ARGUMENT, false,
        [&] {
            (void)mexce::encode_protected_expression(
                "sin", {{"sin", 0}}, mexce::Protected_math_mode::STRICT);
        });
    suite.expect_error(
        "unused binding", Category::INVALID_ARGUMENT, false,
        [&] {
            (void)mexce::encode_protected_expression(
                "x", xy, mexce::Protected_math_mode::STRICT);
        });
    suite.expect_error(
        "source limit", Category::SIZE_LIMIT, false,
        [&] {
            (void)mexce::encode_protected_expression(
                std::string(1024 * 1024 + 1, 'x'), {},
                mexce::Protected_math_mode::STRICT);
        });

    std::vector<mexce::Protected_binding> too_many_bindings;
    too_many_bindings.reserve(4097);
    for (uint32_t slot = 0; slot < 4097; ++slot) {
        too_many_bindings.push_back({"v" + std::to_string(slot), slot});
    }
    suite.expect_error(
        "binding count limit", Category::SIZE_LIMIT, false,
        [&] {
            (void)mexce::encode_protected_expression(
                "1", too_many_bindings, mexce::Protected_math_mode::STRICT);
        });
    suite.expect_error(
        "binding name limit", Category::SIZE_LIMIT, false,
        [&] {
            (void)mexce::encode_protected_expression(
                "1", {{std::string(256, 'x'), 0}}, mexce::Protected_math_mode::STRICT);
        });

    std::vector<mexce::Protected_binding> cumulative_names;
    cumulative_names.reserve(1029);
    for (uint32_t slot = 0; slot < 1029; ++slot) {
        std::string name = "v" + std::to_string(slot);
        name.append(255 - name.size(), 'x');
        cumulative_names.push_back({name, slot});
    }
    suite.expect_error(
        "cumulative binding name limit", Category::SIZE_LIMIT, false,
        [&] {
            (void)mexce::encode_protected_expression(
                "1", cumulative_names, mexce::Protected_math_mode::STRICT);
        });

    std::string deep_expression;
    for (size_t i = 0; i < 1024; ++i) {
        deep_expression += "add(1,";
    }
    deep_expression += "1";
    deep_expression.append(1024, ')');
    suite.expect_error(
        "semantic depth limit", Category::SIZE_LIMIT, false,
        [&] {
            (void)mexce::encode_protected_expression(
                deep_expression, {}, mexce::Protected_math_mode::STRICT);
        });

    std::string record_limit_expression = "1";
    for (size_t i = 1; i < 8192; ++i) {
        record_limit_expression += "+1";
    }
    suite.expect_error(
        "semantic record limit", Category::SIZE_LIMIT, false,
        [&] {
            (void)mexce::encode_protected_expression(
                record_limit_expression, {}, mexce::Protected_math_mode::STRICT);
        });
}


void test_error_index_contract(Test_suite& suite)
{
    mexce::Protected_expression_error error(
        mexce::Protected_expression_error_category::INVALID_ARGUMENT, "test");
    bool threw = false;
    try {
        (void)error.record_index();
    }
    catch (const std::logic_error&) {
        threw = true;
    }
    suite.expect(threw, "absent record index throws");
}


} // namespace


int main(int argc, char** argv)
{
    if (argc > 3) {
        std::cerr << "usage: protected_codec_tests [fixture [manifest]]\n";
        return 2;
    }
    if (argc >= 2) {
        g_fixture_path = argv[1];
    }
    if (argc == 3) {
        g_fixture_manifest_path = argv[2];
    }
    if (sodium_init() < 0) {
        std::cerr << "libsodium initialization failed\n";
        return 1;
    }
    if (std::string(sodium_version_string()) != "1.0.22") {
        std::cerr << "protected codec tests require libsodium 1.0.22\n";
        return 1;
    }

    Test_suite suite;
    try {
        test_fixture_and_encoder(suite);
        test_operation_ids_and_aliases(suite);
        test_key_ownership(suite);
        test_public_framing(suite);
        test_authentication(suite);
        test_semantic_validation(suite);
        test_encoder_validation(suite);
        test_error_index_contract(suite);
    }
    catch (const std::exception& error) {
        suite.m_failures.push_back(std::string("test harness failure: ") + error.what());
    }

    for (const auto& failure : suite.m_failures) {
        std::cerr << "FAIL: " << failure << '\n';
    }
    if (!suite.m_failures.empty()) {
        std::cerr << suite.m_failures.size() << " protected codec test(s) failed\n";
        return 1;
    }
    std::cout << "All protected codec tests passed\n";
    return 0;
}

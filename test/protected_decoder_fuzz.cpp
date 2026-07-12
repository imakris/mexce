#include "mexce_protected.h"

#include <sodium.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <vector>


namespace {


std::array<uint8_t, 32> fixed_key_bytes()
{
    std::array<uint8_t, 32> key;
    for (size_t i = 0; i < key.size(); ++i) {
        key[i] = static_cast<uint8_t>(i);
    }
    return key;
}


void exercise(const uint8_t* data, size_t size)
{
    const auto key_bytes = fixed_key_bytes();
    double slot0 = 1.25;
    double slot1 = -2.5;
    mexce::evaluator evaluator;
    evaluator.bind_protected(slot0, 0);
    evaluator.bind_protected(slot1, 1);
    try {
        evaluator.set_protected_expression(
            data,
            size,
            mexce::Protected_expression_key::from_bytes(
                key_bytes.data(), key_bytes.size()));
        (void)evaluator.evaluate();
    }
    catch (const std::exception&) {
    }
}


bool rejects_as_malformed(const std::vector<uint8_t>& program)
{
    const auto key_bytes = fixed_key_bytes();
    double slot0 = 1.25;
    double slot1 = -2.5;
    mexce::evaluator evaluator;
    evaluator.bind_protected(slot0, 0);
    evaluator.bind_protected(slot1, 1);
    try {
        evaluator.set_protected_expression(
            program.data(),
            program.size(),
            mexce::Protected_expression_key::from_bytes(
                key_bytes.data(), key_bytes.size()));
    }
    catch (const mexce::Protected_expression_error& error) {
        return error.category() ==
            mexce::Protected_expression_error_category::MALFORMED_PROGRAM;
    }
    catch (const std::exception&) {
        return false;
    }
    return false;
}


constexpr size_t k_reference_header_size = 64;
constexpr size_t k_reference_record_size = 32;
constexpr size_t k_reference_frame_size  = 49;


void store_u32(uint8_t* bytes, uint32_t value)
{
    for (size_t i = 0; i < 4; ++i) {
        bytes[i] = static_cast<uint8_t>(value >> (i * 8));
    }
}


uint32_t load_u32(const uint8_t* bytes)
{
    return static_cast<uint32_t>(bytes[0])       |
        static_cast<uint32_t>(bytes[1]) << 8  |
        static_cast<uint32_t>(bytes[2]) << 16 |
        static_cast<uint32_t>(bytes[3]) << 24;
}


std::vector<std::array<uint8_t, k_reference_record_size>> decrypt_fixture(
    const std::vector<uint8_t>& program)
{
    if (program.size() < k_reference_header_size) {
        throw std::runtime_error("structured input is too short");
    }
    const uint32_t count = load_u32(program.data() + 16);
    if (count < 3 || count > 16384 ||
        program.size() != k_reference_header_size + count * k_reference_frame_size)
    {
        throw std::runtime_error("structured input has invalid framing");
    }

    const auto key = fixed_key_bytes();
    crypto_secretstream_xchacha20poly1305_state state;
    if (crypto_secretstream_xchacha20poly1305_init_pull(
            &state, program.data() + 40, key.data()) != 0)
    {
        throw std::runtime_error("fixture pull initialization failed");
    }

    std::vector<std::array<uint8_t, k_reference_record_size>> records(count);
    for (uint32_t index = 0; index < count; ++index) {
        std::array<uint8_t, k_reference_header_size + 4> additional_data = {};
        std::memcpy(additional_data.data(), program.data(), k_reference_header_size);
        store_u32(additional_data.data() + k_reference_header_size, index);
        unsigned long long clear_size = 0;
        unsigned char tag = 0;
        if (crypto_secretstream_xchacha20poly1305_pull(
                &state,
                records[index].data(),
                &clear_size,
                &tag,
                program.data() + k_reference_header_size + index * k_reference_frame_size,
                k_reference_frame_size,
                additional_data.data(),
                additional_data.size()) != 0 ||
            clear_size != k_reference_record_size)
        {
            throw std::runtime_error("fixture decryption failed");
        }
    }
    sodium_memzero(&state, sizeof(state));
    return records;
}


std::vector<uint8_t> encrypt_records(
    const std::vector<uint8_t>& fixture,
    const std::vector<std::array<uint8_t, k_reference_record_size>>& records)
{
    const auto key = fixed_key_bytes();
    std::vector<uint8_t> program = fixture;
    crypto_secretstream_xchacha20poly1305_state state;
    crypto_secretstream_xchacha20poly1305_init_push(
        &state, program.data() + 40, key.data());

    for (uint32_t index = 0; index < records.size(); ++index) {
        std::array<uint8_t, k_reference_header_size + 4> additional_data = {};
        std::memcpy(additional_data.data(), program.data(), k_reference_header_size);
        store_u32(additional_data.data() + k_reference_header_size, index);
        unsigned long long frame_size = 0;
        const unsigned char tag = index + 1 == records.size()
            ? crypto_secretstream_xchacha20poly1305_TAG_FINAL
            : crypto_secretstream_xchacha20poly1305_TAG_MESSAGE;
        crypto_secretstream_xchacha20poly1305_push(
            &state,
            program.data() + k_reference_header_size + index * k_reference_frame_size,
            &frame_size,
            records[index].data(),
            records[index].size(),
            additional_data.data(),
            additional_data.size(),
            tag);
    }
    sodium_memzero(&state, sizeof(state));
    return program;
}
} // namespace


extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    exercise(data, size);
    return 0;
}


#ifndef MEXCE_FUZZ_SMOKE_MAIN
extern "C" size_t LLVMFuzzerMutate(
    uint8_t* data,
    size_t size,
    size_t max_size);


extern "C" size_t LLVMFuzzerCustomMutator(
    uint8_t* data,
    size_t size,
    size_t max_size,
    unsigned int seed)
{
    try {
        const std::vector<uint8_t> program(data, data + size);
        auto records = decrypt_fixture(program);
        if (records.size() > 2 && size <= max_size) {
            const size_t record = 1 + seed % (records.size() - 2);
            const size_t byte   = (seed / records.size()) % records[record].size();
            records[record][byte] ^= static_cast<uint8_t>(1U << (seed % 8));
            const auto mutation = encrypt_records(program, records);
            std::memcpy(data, mutation.data(), mutation.size());
            return mutation.size();
        }
    }
    catch (const std::exception&) {
    }
    return LLVMFuzzerMutate(data, size, max_size);
}
#endif


#ifdef MEXCE_FUZZ_SMOKE_MAIN
int main()
{
    if (sodium_init() < 0) {
        std::cerr << "protected decoder fuzz smoke requires libsodium 1.0.22\n";
        return 1;
    }

    std::ifstream input(MEXCE_PROTECTED_FIXTURE_PATH, std::ios::binary);
    const std::vector<uint8_t> fixture(
        (std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
    if (fixture.empty()) {
        std::cerr << "FAIL: protected fixture could not be read\n";
        return 1;
    }

    exercise(nullptr, 0);
    exercise(fixture.data(), fixture.size());
    for (size_t i = 0; i < fixture.size(); ++i) {
        std::vector<uint8_t> mutation = fixture;
        mutation[i] ^= 0x01;
        exercise(mutation.data(), mutation.size());
    }

    auto records = decrypt_fixture(fixture);
    records[2][2] = 1;
    auto reserved_mutation = encrypt_records(fixture, records);
    if (!rejects_as_malformed(reserved_mutation)) {
        std::cerr << "FAIL: authenticated reserved field was not rejected as malformed\n";
        return 1;
    }

    records = decrypt_fixture(fixture);
    records[3][4] = 255;
    auto operation_mutation = encrypt_records(fixture, records);
    if (!rejects_as_malformed(operation_mutation)) {
        std::cerr << "FAIL: authenticated operation was not rejected as malformed\n";
        return 1;
    }

    std::cout << "Protected decoder fuzz smoke passed\n";
    return 0;
}
#endif

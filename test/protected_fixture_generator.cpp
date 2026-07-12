#include <sodium.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>


namespace {


constexpr size_t k_header_size = 64;
constexpr size_t k_record_size = 32;
constexpr size_t k_frame_size  = 49;


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


std::array<uint8_t, k_record_size> record(
    uint8_t kind,
    uint32_t operand_32,
    uint64_t operand_64)
{
    std::array<uint8_t, k_record_size> result = {};
    result[0] = kind;
    store_u32(result.data() + 4, operand_32);
    store_u64(result.data() + 8, operand_64);
    return result;
}


bool write_fixture(const std::string& binary_path, const std::string& manifest_path)
{
    if (sodium_init() < 0) {
        return false;
    }

    std::array<uint8_t, crypto_secretstream_xchacha20poly1305_KEYBYTES> key;
    for (size_t i = 0; i < key.size(); ++i) {
        key[i] = static_cast<uint8_t>(i);
    }

    uint64_t literal_bits = 0;
    const double literal = 2.5;
    std::memcpy(&literal_bits, &literal, sizeof(literal_bits));
    const std::array<std::array<uint8_t, k_record_size>, 7> records = {{
        record(1, 2, 0),
        record(3, 0, 0),
        record(2, 0, literal_bits),
        record(4, 3, 0),
        record(3, 1, 0),
        record(4, 1, 0),
        record(5, 0, 0),
    }};

    std::vector<uint8_t> program(k_header_size + records.size() * k_frame_size, 0);
    const uint8_t magic[8] = {'M', 'E', 'X', 'C', 'E', 'P', 'R', 'G'};
    std::memcpy(program.data(), magic, sizeof(magic));
    store_u16(program.data() + 8, 1);
    store_u16(program.data() + 10, 0);
    store_u16(program.data() + 12, k_header_size);
    store_u16(program.data() + 14, k_record_size);
    store_u32(program.data() + 16, static_cast<uint32_t>(records.size()));
    for (size_t i = 0; i < 16; ++i) {
        program[24 + i] = static_cast<uint8_t>(0xa0 + i);
    }

    crypto_secretstream_xchacha20poly1305_state state;
    if (crypto_secretstream_xchacha20poly1305_init_push(
            &state, program.data() + 40, key.data()) != 0)
    {
        return false;
    }

    for (uint32_t i = 0; i < records.size(); ++i) {
        uint8_t additional_data[k_header_size + 4];
        std::memcpy(additional_data, program.data(), k_header_size);
        store_u32(additional_data + k_header_size, i);
        unsigned long long frame_size = 0;
        const unsigned char tag = i + 1 == records.size()
            ? crypto_secretstream_xchacha20poly1305_TAG_FINAL
            : crypto_secretstream_xchacha20poly1305_TAG_MESSAGE;
        if (crypto_secretstream_xchacha20poly1305_push(
                &state,
                program.data() + k_header_size + static_cast<size_t>(i) * k_frame_size,
                &frame_size,
                records[i].data(), records[i].size(),
                additional_data, sizeof(additional_data), tag) != 0 ||
            frame_size != k_frame_size)
        {
            return false;
        }
    }

    sodium_memzero(&state, sizeof(state));
    std::ofstream binary(binary_path.c_str(), std::ios::binary | std::ios::trunc);
    binary.write(reinterpret_cast<const char*>(program.data()), program.size());
    std::ofstream manifest(manifest_path.c_str(), std::ios::trunc);
    manifest
        << "format=1.0\n"
        << "key_hex=000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f\n"
        << "program_id_hex=a0a1a2a3a4a5a6a7a8a9aaabacadaeaf\n"
        << "records=MANIFEST(2,strict),VARIABLE(0),LITERAL_F64(2.5),"
           "CALL(MUL),VARIABLE(1),CALL(ADD),END\n";
    sodium_memzero(key.data(), key.size());
    return binary.good() && manifest.good();
}


} // namespace


int main(int argc, char** argv)
{
    if (argc != 3) {
        std::cerr << "usage: protected_fixture_generator <binary> <manifest>\n";
        return 2;
    }
    return write_fixture(argv[1], argv[2]) ? 0 : 1;
}

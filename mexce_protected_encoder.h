#ifndef MEXCE_PROTECTED_ENCODER_H
#define MEXCE_PROTECTED_ENCODER_H

#include "mexce_protected.h"

#include <array>
#include <map>
#include <new>
#include <set>
#include <string>
#include <vector>


// Windows defines STRICT as a source token, which would make the contracted
// Protected_math_mode::STRICT enumerator impossible to name at call sites.
#ifdef STRICT
    #undef STRICT
#endif


namespace mexce {


enum class Protected_math_mode
{
    STRICT = 0,
    FAST = 1,
};


struct Protected_binding
{
    std::string name;
    uint32_t    slot;
};


struct Protected_expression_bundle
{
    std::vector<uint8_t>     program;
    Protected_expression_key key;
    std::array<uint8_t, 16>  program_id;
};


namespace impl {


inline void store_u16(uint8_t* bytes, uint16_t value)
{
    bytes[0] = static_cast<uint8_t>(value);
    bytes[1] = static_cast<uint8_t>(value >> 8);
}


inline void store_u64(uint8_t* bytes, uint64_t value)
{
    for (size_t i = 0; i < 8; ++i) {
        bytes[i] = static_cast<uint8_t>(value >> (i * 8));
    }
}


inline bool valid_protected_name(const std::string& name)
{
    if (name.empty() || name.size() > 255) {
        return false;
    }
    const auto first = static_cast<unsigned char>(name[0]);
    if (!(first == '_' || (first >= 'A' && first <= 'Z') ||
        (first >= 'a' && first <= 'z')))
    {
        return false;
    }
    for (size_t i = 1; i < name.size(); ++i) {
        const auto c = static_cast<unsigned char>(name[i]);
        if (!(c == '_' || (c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')))
        {
            return false;
        }
    }
    return true;
}


inline uint32_t protected_operation_id(const std::string& name)
{
    static const std::map<std::string, uint32_t> ids = {
        {"add", 1},   {"sub", 2},   {"mul", 3},   {"div", 4},
        {"neg", 5},   {"pow", 6},   {"sin", 7},   {"cos", 8},
        {"tan", 9},   {"abs", 10},  {"sign", 11}, {"signp", 12},
        {"expn", 13}, {"sfc", 14},  {"sqrt", 15}, {"exp", 16},
        {"lt", 17},   {"gt", 18},   {"le", 19},   {"ge", 20},
        {"eq", 21},   {"ne", 22},   {"log", 23},  {"ln", 23},
        {"log2", 24}, {"log10", 25},{"logb", 26}, {"ylog2", 27},
        {"max", 28},  {"min", 29},  {"floor", 30},{"ceil", 31},
        {"round", 32},{"int", 33},  {"trunc", 34},{"mod", 35},
        {"bnd", 36},  {"bias", 37}, {"gain", 38},
    };
    const auto found = ids.find(name);
    return found == ids.end() ? 0 : found->second;
}


inline void validate_protected_schema(
    const std::string& expression,
    const std::vector<Protected_binding>& bindings,
    Protected_math_mode math_mode)
{
    if (math_mode != Protected_math_mode::STRICT && math_mode != Protected_math_mode::FAST) {
        throw Protected_expression_error(
            Protected_expression_error_category::INVALID_ARGUMENT,
            "The protected-expression math mode is invalid.");
    }
    if (expression.size() > 1024 * 1024) {
        throw Protected_expression_error(
            Protected_expression_error_category::SIZE_LIMIT,
            "The protected-expression source exceeds a resource limit.");
    }
    if (bindings.size() > 4096) {
        throw Protected_expression_error(
            Protected_expression_error_category::SIZE_LIMIT,
            "The protected-expression schema exceeds a resource limit.");
    }

    size_t cumulative_size = 0;
    std::set<std::string> names;
    std::set<uint32_t> slots;
    for (const auto& binding : bindings) {
        cumulative_size += binding.name.size();
        if (binding.name.size() > 255 || cumulative_size > 256 * 1024) {
            throw Protected_expression_error(
                Protected_expression_error_category::SIZE_LIMIT,
                "The protected-expression schema exceeds a resource limit.");
        }
        if (!valid_protected_name(binding.name) ||
            function_map().find(binding.name) != function_map().end() ||
            built_in_constants_map().find(binding.name) != built_in_constants_map().end() ||
            !names.insert(binding.name).second || !slots.insert(binding.slot).second)
        {
            throw Protected_expression_error(
                Protected_expression_error_category::INVALID_ARGUMENT,
                "The protected-expression binding schema is invalid.");
        }
    }
    for (uint32_t slot = 0; slot < bindings.size(); ++slot) {
        if (slots.find(slot) == slots.end()) {
            throw Protected_expression_error(
                Protected_expression_error_category::INVALID_ARGUMENT,
                "The protected-expression binding schema is invalid.");
        }
    }
}


class Protected_push_state
{
public:
    Protected_push_state() { std::memset(&m_state, 0, sizeof(m_state)); }
    ~Protected_push_state()
    {
        protected_memzero(
            &m_state, sizeof(m_state), Protected_wipe_context::PUSH_STATE);
    }

    crypto_secretstream_xchacha20poly1305_state* get() { return &m_state; }

private:
    crypto_secretstream_xchacha20poly1305_state m_state;
};


inline void append_encrypted_record(
    std::vector<uint8_t>& program,
    Protected_push_state& push_state,
    const uint8_t* record,
    uint32_t index,
    unsigned char tag)
{
    constexpr size_t k_header_size = 64;
    constexpr size_t k_record_size = 32;
    constexpr size_t k_frame_size =
        k_record_size + crypto_secretstream_xchacha20poly1305_ABYTES;
    uint8_t additional_data[k_header_size + 4] = {};
    Protected_wipe_guard additional_data_wipe(
        additional_data,
        sizeof(additional_data),
        Protected_wipe_context::ADDITIONAL_DATA);
    std::memcpy(additional_data, program.data(), k_header_size);
    store_u32(additional_data + k_header_size, index);

    const size_t old_size = program.size();
    program.resize(old_size + k_frame_size);
    unsigned long long frame_size = 0;
    if (crypto_secretstream_xchacha20poly1305_push(
            push_state.get(), program.data() + old_size, &frame_size,
            record, k_record_size,
            additional_data, sizeof(additional_data), tag) != 0 ||
        frame_size != k_frame_size)
    {
        throw Protected_expression_error(
            Protected_expression_error_category::RESOURCE_FAILURE,
            "Protected-expression encoding failed.");
    }
}


} // namespace impl


namespace impl {


inline Protected_expression_bundle encode_protected_expression_impl(
    const std::string& expression,
    const std::vector<Protected_binding>& bindings,
    Protected_math_mode math_mode)
{
    validate_protected_schema(expression, bindings, math_mode);
    if (expression.empty()) {
        throw mexce_parsing_exception("Expected an expression", 0);
    }

    evaluator parser;
    std::vector<double> variables(bindings.size(), 0.0);
    std::map<std::string, uint32_t> slots;
    for (const auto& binding : bindings) {
        parser.bind(variables[binding.slot], binding.name);
        slots.insert(std::make_pair(binding.name, binding.slot));
    }

    const Semantic_program semantic_program =
        Clear_semantic_producer::produce(parser, expression);
    if (semantic_program.size() + 2 > 16384) {
        throw Protected_expression_error(
            Protected_expression_error_category::SIZE_LIMIT,
            "The protected expression exceeds a resource limit.");
    }

    std::vector<bool> used_slots(bindings.size(), false);
    size_t stack_depth = 0;
    for (const auto& record : semantic_program) {
        if (record.kind() == Semantic_record_kind::LITERAL) {
            if (!std::isfinite(record.literal())) {
                throw Protected_expression_error(
                    Protected_expression_error_category::INVALID_ARGUMENT,
                    "The protected expression contains a non-finite literal.");
            }
            ++stack_depth;
        }
        else
        if (record.kind() == Semantic_record_kind::VARIABLE) {
            used_slots[slots.find(record.variable()->name)->second] = true;
            ++stack_depth;
        }
        else
        if (record.kind() == Semantic_record_kind::CALL) {
            const uint32_t operation = protected_operation_id(record.function()->name);
            const uint32_t arity     = protected_operation_arity(operation);
            if (arity == 0) {
                throw Protected_expression_error(
                    Protected_expression_error_category::INVALID_ARGUMENT,
                    "The protected expression contains an unsupported operation.");
            }
            stack_depth = stack_depth - arity + 1;
        }
        if (stack_depth > 1024) {
            throw Protected_expression_error(
                Protected_expression_error_category::SIZE_LIMIT,
                "The protected expression exceeds a resource limit.");
        }
    }
    for (size_t slot = 0; slot < used_slots.size(); ++slot) {
        if (!used_slots[slot]) {
            throw Protected_expression_error(
                Protected_expression_error_category::INVALID_ARGUMENT,
                "The protected-expression schema contains an unused binding.");
        }
    }

    require_sodium();
    uint8_t key_bytes[crypto_secretstream_xchacha20poly1305_KEYBYTES] = {};
    Protected_wipe_guard key_bytes_wipe(
        key_bytes, sizeof(key_bytes), Protected_wipe_context::KEY);
    crypto_secretstream_xchacha20poly1305_keygen(key_bytes);
    Protected_expression_key key = Protected_expression_key::from_bytes(
        key_bytes, sizeof(key_bytes));

    std::array<uint8_t, 16> program_id;
    do {
        randombytes_buf(program_id.data(), program_id.size());
    }
    while (all_zero(program_id.data(), program_id.size()));

    constexpr size_t k_header_size = 64;
    constexpr size_t k_record_size = 32;
    constexpr size_t k_frame_size =
        k_record_size + crypto_secretstream_xchacha20poly1305_ABYTES;
    const uint32_t record_count = static_cast<uint32_t>(semantic_program.size() + 2);
    std::vector<uint8_t> program;
    program.reserve(k_header_size + static_cast<size_t>(record_count) * k_frame_size);
    program.resize(k_header_size, 0);
    const uint8_t magic[8] = {'M', 'E', 'X', 'C', 'E', 'P', 'R', 'G'};
    std::memcpy(program.data(), magic, sizeof(magic));
    store_u16(program.data() + 8, 1);
    store_u16(program.data() + 10, 0);
    store_u16(program.data() + 12, k_header_size);
    store_u16(program.data() + 14, k_record_size);
    store_u32(program.data() + 16, record_count);
    std::memcpy(program.data() + 24, program_id.data(), program_id.size());

    Protected_push_state push_state;
    if (crypto_secretstream_xchacha20poly1305_init_push(
            push_state.get(), program.data() + 40,
            Protected_key_access::bytes(key)) != 0)
    {
        throw Protected_expression_error(
            Protected_expression_error_category::RESOURCE_FAILURE,
            "Protected-expression encoding failed.");
    }

    uint8_t clear[k_record_size] = {};
    Protected_wipe_guard clear_wipe(
        clear, sizeof(clear), Protected_wipe_context::CLEAR_RECORD);
    clear[0] = 1;
    store_u32(clear + 4, static_cast<uint32_t>(bindings.size()));
    store_u64(clear + 8, math_mode == Protected_math_mode::FAST ? 1 : 0);
    append_encrypted_record(
        program, push_state, clear, 0,
        crypto_secretstream_xchacha20poly1305_TAG_MESSAGE);

    uint32_t index = 1;
    for (const auto& record : semantic_program) {
        protected_memzero(
            clear, sizeof(clear), Protected_wipe_context::CLEAR_RECORD);
        if (record.kind() == Semantic_record_kind::LITERAL) {
            clear[0] = 2;
            uint64_t bits;
            const double literal = record.literal();
            std::memcpy(&bits, &literal, sizeof(bits));
            store_u64(clear + 8, bits);
        }
        else
        if (record.kind() == Semantic_record_kind::VARIABLE) {
            clear[0] = 3;
            store_u32(clear + 4, slots.find(record.variable()->name)->second);
        }
        else {
            clear[0] = 4;
            store_u32(clear + 4, protected_operation_id(record.function()->name));
        }
        append_encrypted_record(
            program, push_state, clear, index++,
            crypto_secretstream_xchacha20poly1305_TAG_MESSAGE);
    }

    protected_memzero(
        clear, sizeof(clear), Protected_wipe_context::CLEAR_RECORD);
    clear[0] = 5;
    append_encrypted_record(
        program, push_state, clear, index,
        crypto_secretstream_xchacha20poly1305_TAG_FINAL);
    return Protected_expression_bundle{
        std::move(program), std::move(key), program_id};
}


} // namespace impl


inline Protected_expression_bundle encode_protected_expression(
    const std::string& expression,
    const std::vector<Protected_binding>& bindings,
    Protected_math_mode math_mode)
{
    try {
        return impl::encode_protected_expression_impl(expression, bindings, math_mode);
    }
    catch (const std::bad_alloc&) {
        throw Protected_expression_error(
            Protected_expression_error_category::RESOURCE_FAILURE,
            "Protected-expression storage allocation failed.");
    }
}


} // namespace mexce


#endif

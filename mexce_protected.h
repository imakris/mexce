#ifndef MEXCE_PROTECTED_H
#define MEXCE_PROTECTED_H

#include "mexce.h"

#include <sodium.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <utility>


namespace mexce {


static_assert(crypto_secretstream_xchacha20poly1305_KEYBYTES == 32,
    "Protected-expression format 1.0 requires 32-byte secretstream keys");
static_assert(crypto_secretstream_xchacha20poly1305_HEADERBYTES == 24,
    "Protected-expression format 1.0 requires 24-byte secretstream headers");
static_assert(crypto_secretstream_xchacha20poly1305_ABYTES == 17,
    "Protected-expression format 1.0 requires 17-byte secretstream overhead");


namespace impl {


#ifdef MEXCE_PROTECTED_TESTING
class Protected_wipe_observer
{
public:
    void observe(Protected_wipe_context context, const uint8_t* bytes, size_t size)
    {
        const size_t context_index = static_cast<size_t>(context);
        const size_t sequence = ++m_sequence;
        ++m_counts[context_index];
        if (m_first_sequence[context_index] == 0) {
            m_first_sequence[context_index] = sequence;
        }
        for (size_t i = 0; i < size; ++i) {
            m_saw_nonzero_after_wipe[context_index] |= bytes[i] != 0;
        }
    }

    void reset()
    {
        std::memset(m_counts, 0, sizeof(m_counts));
        std::memset(m_first_sequence, 0, sizeof(m_first_sequence));
        std::memset(
            m_saw_nonzero_after_wipe, 0, sizeof(m_saw_nonzero_after_wipe));
        m_sequence = 0;
    }

    size_t count(Protected_wipe_context context) const
    {
        return m_counts[static_cast<size_t>(context)];
    }

    size_t first_sequence(Protected_wipe_context context) const
    {
        return m_first_sequence[static_cast<size_t>(context)];
    }

    bool observed_before(
        Protected_wipe_context first,
        Protected_wipe_context second) const
    {
        const size_t first_value  = first_sequence(first);
        const size_t second_value = first_sequence(second);
        return first_value != 0 && second_value != 0 && first_value < second_value;
    }

    bool all_observed_regions_are_zero() const
    {
        for (size_t i = 0; i < static_cast<size_t>(Protected_wipe_context::COUNT); ++i) {
            if (m_saw_nonzero_after_wipe[i]) {
                return false;
            }
        }
        return true;
    }

private:
    size_t m_counts[static_cast<size_t>(Protected_wipe_context::COUNT)] = {};
    size_t m_first_sequence[static_cast<size_t>(Protected_wipe_context::COUNT)] = {};
    bool   m_saw_nonzero_after_wipe[
        static_cast<size_t>(Protected_wipe_context::COUNT)] = {};
    size_t m_sequence = 0;
};


struct protected_test_faults_t
{
    bool fail_sodium_init             = false;
    bool fail_malloc                  = false;
    bool fail_mlock                   = false;
    bool fail_init_pull               = false;
    bool fail_executable_allocation   = false;
    bool fail_executable_finalization = false;
};


inline protected_test_faults_t& protected_test_faults()
{
    static protected_test_faults_t faults;
    return faults;
}


inline Protected_wipe_observer& protected_wipe_observer()
{
    static Protected_wipe_observer observer;
    return observer;
}
#endif


inline void protected_memzero(
    void* bytes,
    size_t size,
    Protected_wipe_context context)
{
    sodium_memzero(bytes, size);
#ifdef MEXCE_PROTECTED_TESTING
    auto& observer = protected_wipe_observer();
    observer.observe(context, static_cast<const uint8_t*>(bytes), size);
#else
    (void)context;
#endif
}


inline void* protected_malloc(size_t size)
{
#ifdef MEXCE_PROTECTED_TESTING
    return protected_test_faults().fail_malloc ? nullptr : sodium_malloc(size);
#else
    return sodium_malloc(size);
#endif
}


inline int protected_mlock(void* bytes, size_t size)
{
#ifdef MEXCE_PROTECTED_TESTING
    return protected_test_faults().fail_mlock ? -1 : sodium_mlock(bytes, size);
#else
    return sodium_mlock(bytes, size);
#endif
}


inline int protected_init_pull(
    crypto_secretstream_xchacha20poly1305_state* state,
    const unsigned char* header,
    const unsigned char* key)
{
#ifdef MEXCE_PROTECTED_TESTING
    return protected_test_faults().fail_init_pull
        ? -1
        : crypto_secretstream_xchacha20poly1305_init_pull(state, header, key);
#else
    return crypto_secretstream_xchacha20poly1305_init_pull(state, header, key);
#endif
}


inline void require_sodium()
{
#ifdef MEXCE_PROTECTED_TESTING
    static const int result = protected_test_faults().fail_sodium_init ? -1 : sodium_init();
#else
    static const int result = sodium_init();
#endif
    if (result < 0) {
        throw Protected_expression_error(
            Protected_expression_error_category::CRYPTOGRAPHY_UNAVAILABLE,
            "Protected-expression cryptography is unavailable.");
    }
}


class Protected_key_access;


} // namespace impl


class Protected_expression_key
{
public:
    static Protected_expression_key from_bytes(const uint8_t* bytes, size_t size)
    {
        if (!bytes || size != crypto_secretstream_xchacha20poly1305_KEYBYTES) {
            throw Protected_expression_error(
                Protected_expression_error_category::INVALID_ARGUMENT,
                "A protected-expression key must contain exactly 32 bytes.");
        }

        impl::require_sodium();
        uint8_t* owned = static_cast<uint8_t*>(impl::protected_malloc(size));
        if (!owned) {
            throw Protected_expression_error(
                Protected_expression_error_category::RESOURCE_FAILURE,
                "Protected-expression key allocation failed.");
        }
        if (impl::protected_mlock(owned, size) != 0) {
            impl::protected_memzero(
                owned, size, impl::Protected_wipe_context::KEY);
            sodium_free(owned);
            throw Protected_expression_error(
                Protected_expression_error_category::RESOURCE_FAILURE,
                "Protected-expression key locking failed.");
        }
        std::memcpy(owned, bytes, size);
        return Protected_expression_key(owned);
    }

    Protected_expression_key(Protected_expression_key&& other) noexcept
    :
        m_bytes(other.m_bytes)
    {
        other.m_bytes = nullptr;
    }

    Protected_expression_key& operator=(Protected_expression_key&& other) noexcept
    {
        if (this != &other) {
            release();
            m_bytes       = other.m_bytes;
            other.m_bytes = nullptr;
        }
        return *this;
    }

    Protected_expression_key(const Protected_expression_key&) = delete;
    Protected_expression_key& operator=(const Protected_expression_key&) = delete;

    ~Protected_expression_key() { release(); }

    template<typename Consumer>
    void consume_bytes(Consumer&& consumer)
    {
        require_bytes();
        Protected_expression_key owned(std::move(*this));
        std::forward<Consumer>(consumer)(
            static_cast<const uint8_t*>(owned.m_bytes),
            crypto_secretstream_xchacha20poly1305_KEYBYTES);
    }

private:
    explicit Protected_expression_key(uint8_t* bytes)
    :
        m_bytes(bytes)
    {}

    void require_bytes() const
    {
        if (!m_bytes) {
            throw Protected_expression_error(
                Protected_expression_error_category::INVALID_ARGUMENT,
                "The protected-expression key is empty.");
        }
    }

    void release() noexcept
    {
        if (m_bytes) {
            impl::protected_memzero(
                m_bytes,
                crypto_secretstream_xchacha20poly1305_KEYBYTES,
                impl::Protected_wipe_context::KEY);
            sodium_munlock(m_bytes, crypto_secretstream_xchacha20poly1305_KEYBYTES);
            sodium_free(m_bytes);
            m_bytes = nullptr;
        }
    }

    uint8_t* m_bytes;

    friend class impl::Protected_key_access;
};


namespace impl {


class Protected_key_access
{
public:
    static const uint8_t* bytes(const Protected_expression_key& key)
    {
        key.require_bytes();
        return key.m_bytes;
    }
};


enum class Protected_operation
{
    ADD   = 1,
    SUB   = 2,
    MUL   = 3,
    DIV   = 4,
    NEG   = 5,
    POW   = 6,
    SIN   = 7,
    COS   = 8,
    TAN   = 9,
    ABS   = 10,
    SIGN  = 11,
    SIGNP = 12,
    EXPN  = 13,
    SFC   = 14,
    SQRT  = 15,
    EXP   = 16,
    LT    = 17,
    GT    = 18,
    LE    = 19,
    GE    = 20,
    EQ    = 21,
    NE    = 22,
    LOG   = 23,
    LOG2  = 24,
    LOG10 = 25,
    LOGB  = 26,
    YLOG2 = 27,
    MAX   = 28,
    MIN   = 29,
    FLOOR = 30,
    CEIL  = 31,
    ROUND = 32,
    INT   = 33,
    TRUNC = 34,
    MOD   = 35,
    BND   = 36,
    BIAS  = 37,
    GAIN  = 38,
};


inline uint16_t load_u16(const uint8_t* bytes)
{
    return static_cast<uint16_t>(bytes[0]) |
        static_cast<uint16_t>(bytes[1]) << 8;
}


inline uint32_t load_u32(const uint8_t* bytes)
{
    return static_cast<uint32_t>(bytes[0]) |
        static_cast<uint32_t>(bytes[1]) << 8  |
        static_cast<uint32_t>(bytes[2]) << 16 |
        static_cast<uint32_t>(bytes[3]) << 24;
}


inline uint64_t load_u64(const uint8_t* bytes)
{
    uint64_t value = 0;
    for (size_t i = 0; i < 8; ++i) {
        value |= static_cast<uint64_t>(bytes[i]) << (i * 8);
    }
    return value;
}


inline void store_u32(uint8_t* bytes, uint32_t value)
{
    for (size_t i = 0; i < 4; ++i) {
        bytes[i] = static_cast<uint8_t>(value >> (i * 8));
    }
}


inline bool all_zero(const uint8_t* bytes, size_t size)
{
    uint8_t any = 0;
    for (size_t i = 0; i < size; ++i) {
        any |= bytes[i];
    }
    return any == 0;
}


inline uint32_t protected_operation_arity(uint32_t operation)
{
    switch (static_cast<Protected_operation>(operation)) {
        case Protected_operation::NEG:
        case Protected_operation::SIN:
        case Protected_operation::COS:
        case Protected_operation::TAN:
        case Protected_operation::ABS:
        case Protected_operation::SIGN:
        case Protected_operation::SIGNP:
        case Protected_operation::EXPN:
        case Protected_operation::SFC:
        case Protected_operation::SQRT:
        case Protected_operation::EXP:
        case Protected_operation::LOG:
        case Protected_operation::LOG2:
        case Protected_operation::LOG10:
        case Protected_operation::FLOOR:
        case Protected_operation::CEIL:
        case Protected_operation::ROUND:
        case Protected_operation::INT:
        case Protected_operation::TRUNC: return 1;

        case Protected_operation::ADD:
        case Protected_operation::SUB:
        case Protected_operation::MUL:
        case Protected_operation::DIV:
        case Protected_operation::POW:
        case Protected_operation::LT:
        case Protected_operation::GT:
        case Protected_operation::LE:
        case Protected_operation::GE:
        case Protected_operation::EQ:
        case Protected_operation::NE:
        case Protected_operation::LOGB:
        case Protected_operation::YLOG2:
        case Protected_operation::MAX:
        case Protected_operation::MIN:
        case Protected_operation::MOD:
        case Protected_operation::BND:
        case Protected_operation::BIAS:
        case Protected_operation::GAIN: return 2;
        default:                        return 0;
    }
}


class Protected_wipe_guard
{
public:
    Protected_wipe_guard(
        void* bytes,
        size_t size,
        Protected_wipe_context context)
    :
        m_bytes(bytes),
        m_size(size),
        m_context(context)
    {}

    ~Protected_wipe_guard() { protected_memzero(m_bytes, m_size, m_context); }

private:
    void*                  m_bytes;
    size_t                 m_size;
    Protected_wipe_context m_context;
};


inline const char* protected_operation_name(Protected_operation operation)
{
    switch (operation) {
        case Protected_operation::ADD:   return "add";
        case Protected_operation::SUB:   return "sub";
        case Protected_operation::MUL:   return "mul";
        case Protected_operation::DIV:   return "div";
        case Protected_operation::NEG:   return "neg";
        case Protected_operation::POW:   return "pow";
        case Protected_operation::SIN:   return "sin";
        case Protected_operation::COS:   return "cos";
        case Protected_operation::TAN:   return "tan";
        case Protected_operation::ABS:   return "abs";
        case Protected_operation::SIGN:  return "sign";
        case Protected_operation::SIGNP: return "signp";
        case Protected_operation::EXPN:  return "expn";
        case Protected_operation::SFC:   return "sfc";
        case Protected_operation::SQRT:  return "sqrt";
        case Protected_operation::EXP:   return "exp";
        case Protected_operation::LT:    return "lt";
        case Protected_operation::GT:    return "gt";
        case Protected_operation::LE:    return "le";
        case Protected_operation::GE:    return "ge";
        case Protected_operation::EQ:    return "eq";
        case Protected_operation::NE:    return "ne";
        case Protected_operation::LOG:   return "log";
        case Protected_operation::LOG2:  return "log2";
        case Protected_operation::LOG10: return "log10";
        case Protected_operation::LOGB:  return "logb";
        case Protected_operation::YLOG2: return "ylog2";
        case Protected_operation::MAX:   return "max";
        case Protected_operation::MIN:   return "min";
        case Protected_operation::FLOOR: return "floor";
        case Protected_operation::CEIL:  return "ceil";
        case Protected_operation::ROUND: return "round";
        case Protected_operation::INT:   return "int";
        case Protected_operation::TRUNC: return "trunc";
        case Protected_operation::MOD:   return "mod";
        case Protected_operation::BND:   return "bnd";
        case Protected_operation::BIAS:  return "bias";
        case Protected_operation::GAIN:  return "gain";
        default:
            assert(false);
            return "";
    }
}


class Protected_semantic_sink
{
public:
    explicit Protected_semantic_sink(evaluator& owner)
    :
        m_owner(owner)
    {}

    void manifest(uint32_t, bool fast_math, uint32_t)
    {
        options effective_options = m_owner.m_options;
        Protected_wipe_guard effective_options_wipe(
            &effective_options,
            sizeof(effective_options),
            Protected_wipe_context::OPTIONS_STORAGE);
        effective_options.fast_math = fast_math;
        Compilation_test_fault test_fault = Compilation_test_fault::NONE;
#ifdef MEXCE_PROTECTED_TESTING
        const auto& faults = protected_test_faults();
        if (faults.fail_executable_allocation) {
            test_fault = Compilation_test_fault::EXECUTABLE_ALLOCATION;
        }
        else
        if (faults.fail_executable_finalization) {
            test_fault = Compilation_test_fault::EXECUTABLE_FINALIZATION;
        }
#endif
        m_compiler.reset(new Semantic_compiler(
            m_owner,
            effective_options,
            m_owner.m_next_variable_id,
            protected_memzero,
            test_fault));
    }

    void literal(double value, uint32_t)
    {
        m_compiler->consume_literal(value);
    }

    void variable(uint32_t slot, uint32_t record_index)
    {
        const auto binding = m_owner.m_protected_variables.find(slot);
        if (binding == m_owner.m_protected_variables.end()) {
            throw Protected_expression_error(
                Protected_expression_error_category::MISSING_BINDING,
                "A protected-expression binding is missing.",
                record_index);
        }
        m_compiler->consume_variable(binding->second);
    }

    void call(Protected_operation operation, uint32_t)
    {
        m_compiler->consume_call(protected_operation_name(operation));
    }

    void end(uint32_t)
    {
        m_result = m_compiler->finish();
    }

    Semantic_compilation_result take_result()
    {
        return std::move(m_result);
    }

private:
    evaluator&                         m_owner;
    std::unique_ptr<Semantic_compiler> m_compiler;
    Semantic_compilation_result        m_result;
};


class Protected_pull_state
{
public:
    Protected_pull_state() { std::memset(&m_state, 0, sizeof(m_state)); }
    ~Protected_pull_state()
    {
        protected_memzero(
            &m_state, sizeof(m_state), Protected_wipe_context::PULL_STATE);
    }

    crypto_secretstream_xchacha20poly1305_state* get() { return &m_state; }

private:
    crypto_secretstream_xchacha20poly1305_state m_state;
};


inline void throw_malformed(uint32_t record_index)
{
    throw Protected_expression_error(
        Protected_expression_error_category::MALFORMED_PROGRAM,
        "The protected expression is malformed.",
        record_index);
}


inline void throw_size_limit(uint32_t record_index)
{
    throw Protected_expression_error(
        Protected_expression_error_category::SIZE_LIMIT,
        "The protected expression exceeds a resource limit.",
        record_index);
}


template<typename Sink>
void decode_protected_expression(
    const uint8_t* program,
    size_t program_size,
    Protected_expression_key key,
    Sink& sink)
{
    constexpr size_t k_header_size = 64;
    constexpr size_t k_record_size = 32;
    constexpr size_t k_frame_size  =
        k_record_size + crypto_secretstream_xchacha20poly1305_ABYTES;

    if (!program && program_size != 0) {
        throw Protected_expression_error(
            Protected_expression_error_category::INVALID_ARGUMENT,
            "A protected-expression byte range is invalid.");
    }
    if (program_size == 0) {
        throw Protected_expression_error(
            Protected_expression_error_category::INVALID_ARGUMENT,
            "A protected expression cannot be empty.");
    }
    if (program_size < k_header_size) {
        throw Protected_expression_error(
            Protected_expression_error_category::MALFORMED_PROGRAM,
            "The protected expression is malformed.");
    }

    static const uint8_t magic[8] = {'M', 'E', 'X', 'C', 'E', 'P', 'R', 'G'};
    if (std::memcmp(program, magic, sizeof(magic)) != 0 ||
        load_u16(program + 8) != 1 ||
        load_u16(program + 10) != 0 ||
        load_u16(program + 12) != k_header_size ||
        load_u16(program + 14) != k_record_size ||
        load_u32(program + 20) != 0)
    {
        throw Protected_expression_error(
            Protected_expression_error_category::UNSUPPORTED_FORMAT,
            "The protected-expression format is unsupported.");
    }

    const uint32_t record_count = load_u32(program + 16);
    if (record_count > 16384) {
        throw Protected_expression_error(
            Protected_expression_error_category::SIZE_LIMIT,
            "The protected expression exceeds a resource limit.");
    }
    if (record_count < 3 || all_zero(program + 24, 16) ||
        program_size != k_header_size + static_cast<size_t>(record_count) * k_frame_size)
    {
        throw Protected_expression_error(
            Protected_expression_error_category::MALFORMED_PROGRAM,
            "The protected expression is malformed.");
    }

    require_sodium();
    Protected_pull_state pull_state;
    key.consume_bytes([&](const uint8_t* key_bytes, size_t) {
        if (protected_init_pull(pull_state.get(), program + 40, key_bytes) != 0) {
            throw Protected_expression_error(
                Protected_expression_error_category::AUTHENTICATION_FAILED,
                "Protected-expression authentication failed.");
        }
    });

    uint32_t binding_count = 0;
    size_t stack_depth = 0;
    uint8_t used_slots[4096] = {};
    Protected_wipe_guard binding_count_wipe(
        &binding_count, sizeof(binding_count), Protected_wipe_context::VALIDATOR_STATE);
    Protected_wipe_guard stack_depth_wipe(
        &stack_depth, sizeof(stack_depth), Protected_wipe_context::VALIDATOR_STATE);
    Protected_wipe_guard used_slots_wipe(
        used_slots, sizeof(used_slots), Protected_wipe_context::VALIDATOR_STATE);

    for (uint32_t index = 0; index < record_count; ++index) {
        uint8_t clear[k_record_size] = {};
        uint8_t additional_data[k_header_size + 4] = {};
        Protected_wipe_guard clear_wipe(
            clear, sizeof(clear), Protected_wipe_context::CLEAR_RECORD);
        Protected_wipe_guard additional_data_wipe(
            additional_data,
            sizeof(additional_data),
            Protected_wipe_context::ADDITIONAL_DATA);
        std::memcpy(additional_data, program, k_header_size);
        store_u32(additional_data + k_header_size, index);

        unsigned long long clear_size = 0;
        unsigned char tag = 0;
        const uint8_t* frame = program + k_header_size + static_cast<size_t>(index) * k_frame_size;
        if (crypto_secretstream_xchacha20poly1305_pull(
                pull_state.get(), clear, &clear_size, &tag,
                frame, k_frame_size,
                additional_data, sizeof(additional_data)) != 0)
        {
            throw Protected_expression_error(
                Protected_expression_error_category::AUTHENTICATION_FAILED,
                "Protected-expression authentication failed.",
                index);
        }

        const bool final_record = index + 1 == record_count;
        if (clear_size != k_record_size ||
            (!final_record && tag != crypto_secretstream_xchacha20poly1305_TAG_MESSAGE) ||
            (final_record && tag != crypto_secretstream_xchacha20poly1305_TAG_FINAL))
        {
            throw_malformed(index);
        }

        uint8_t kind     = clear[0];
        uint32_t operand = load_u32(clear + 4);
        uint64_t value   = load_u64(clear + 8);
        Protected_wipe_guard kind_wipe(
            &kind, sizeof(kind), Protected_wipe_context::DECODED_SCALAR);
        Protected_wipe_guard operand_wipe(
            &operand, sizeof(operand), Protected_wipe_context::DECODED_SCALAR);
        Protected_wipe_guard value_wipe(
            &value, sizeof(value), Protected_wipe_context::DECODED_SCALAR);
        if (clear[1] != 0 || load_u16(clear + 2) != 0 || !all_zero(clear + 16, 16)) {
            throw_malformed(index);
        }

        if (index == 0) {
            if (kind != 1 || (value & ~uint64_t(1)) != 0) {
                throw_malformed(index);
            }
            if (operand > 4096) {
                throw_size_limit(index);
            }
            binding_count = operand;
            sink.manifest(binding_count, (value & 1) != 0, index);
            continue;
        }

        if (final_record) {
            if (kind != 5 || operand != 0 || value != 0 || stack_depth != 1) {
                throw_malformed(index);
            }
            for (size_t slot = 0; slot < binding_count; ++slot) {
                if (!used_slots[slot]) {
                    throw_malformed(index);
                }
            }
            sink.end(index);
            continue;
        }

        if (kind == 2) {
            if (operand != 0) {
                throw_malformed(index);
            }
            if (stack_depth == 1024) {
                throw_size_limit(index);
            }
            double literal;
            std::memcpy(&literal, &value, sizeof(literal));
            Protected_wipe_guard literal_wipe(
                &literal, sizeof(literal), Protected_wipe_context::DECODED_SCALAR);
            if (!std::isfinite(literal)) {
                throw_malformed(index);
            }
            ++stack_depth;
            sink.literal(literal, index);
        }
        else
        if (kind == 3) {
            if (value != 0 || operand >= binding_count) {
                throw_malformed(index);
            }
            if (stack_depth == 1024) {
                throw_size_limit(index);
            }
            ++stack_depth;
            used_slots[operand] = true;
            sink.variable(operand, index);
        }
        else
        if (kind == 4) {
            uint32_t arity = protected_operation_arity(operand);
            Protected_wipe_guard arity_wipe(
                &arity, sizeof(arity), Protected_wipe_context::DECODED_SCALAR);
            if (value != 0 || arity == 0 || stack_depth < arity) {
                throw_malformed(index);
            }
            stack_depth = stack_depth - arity + 1;
            sink.call(static_cast<Protected_operation>(operand), index);
        }
        else {
            throw_malformed(index);
        }
    }
}


} // namespace impl


inline void evaluator::set_protected_expression(
    const uint8_t* program,
    size_t program_size,
    Protected_expression_key key)
{
    reset_compiled_expression();

    if (m_options.enable_cse) {
        throw Protected_expression_error(
            Protected_expression_error_category::INVALID_ARGUMENT,
            "Protected expressions do not support common subexpression elimination.");
    }

    try {
        impl::Protected_semantic_sink sink(*this);
        impl::decode_protected_expression(
            program, program_size, std::move(key), sink);
        publish_semantic_compilation(sink.take_result());
    }
    catch (const Protected_expression_error&) {
        throw;
    }
    catch (const std::bad_alloc&) {
        throw Protected_expression_error(
            Protected_expression_error_category::RESOURCE_FAILURE,
            "Protected-expression compilation exhausted a resource.");
    }
    catch (const impl::Executable_memory_error&) {
        throw Protected_expression_error(
            Protected_expression_error_category::RESOURCE_FAILURE,
            "Protected-expression executable memory could not be finalized.");
    }
    catch (const std::exception&) {
        throw Protected_expression_error(
            Protected_expression_error_category::COMPILATION_FAILED,
            "Protected-expression compilation failed.");
    }
}


} // namespace mexce


#endif

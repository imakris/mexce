#include "experimental_interpreter.h"

#include <emmintrin.h>

#include <algorithm>
#include <array>
#include <cfenv>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstring>
#include <limits>
#include <map>
#include <mutex>
#include <new>
#include <utility>


namespace mexce {
namespace software_lock_b0 {
namespace {


constexpr uint8_t k_artifact_magic[8]      = {'M', 'X', 'B', '0', 'R', 'E', 'C', 'S'};
constexpr uint8_t k_load_magic[8]          = {'M', 'X', 'B', '0', 'L', 'O', 'A', 'D'};
constexpr uint8_t k_load_response_magic[8] = {'M', 'X', 'B', '0', 'L', 'R', 'S', 'P'};
constexpr uint8_t k_evaluate_magic[8]      = {'M', 'X', 'B', '0', 'E', 'V', 'A', 'L'};
constexpr uint8_t k_unload_magic[8]        = {'M', 'X', 'B', '0', 'U', 'N', 'L', 'D'};

constexpr uint16_t k_abi_major            = 1;
constexpr uint16_t k_abi_minor            = 0;
constexpr uint16_t k_artifact_header_size = 32;
constexpr uint16_t k_record_size          = 32;
constexpr uint32_t k_load_header_size     = 24;
constexpr uint32_t k_load_response_size   = 32;
constexpr uint32_t k_evaluate_header_size = 40;
constexpr uint32_t k_unload_request_size  = 24;
constexpr uint32_t k_max_records          = 16384;
constexpr uint32_t k_max_instructions     = k_max_records - 2;
constexpr uint32_t k_max_slots            = 4096;
constexpr uint32_t k_max_stack_depth      = 1024;
constexpr uint32_t k_max_rows             = 1024;
constexpr uint32_t k_max_contexts         = 10;
constexpr uint32_t k_max_admitted_calls   = k_max_contexts;
constexpr uint64_t k_max_artifact_size =
    k_artifact_header_size + static_cast<uint64_t>(k_max_records) * k_record_size;
constexpr uint64_t k_max_copied_input        = 8ULL * 1024ULL * 1024ULL;
constexpr uint64_t k_max_combined_allocation = 16ULL * 1024ULL * 1024ULL;
constexpr uint64_t k_max_persistent_allocation =
    k_max_combined_allocation - k_max_copied_input -
    static_cast<uint64_t>(k_max_rows) * sizeof(double);

// Accounting charges are conservative model bounds, not measured heap bytes.
// The context charge uses fixed-capacity owners plus a whole KiB for its map
// node, shared ownership control block, and allocator bookkeeping. The manager
// receives a whole 64 KiB charge, and each call receives explicit allocator
// bookkeeping margins.
constexpr uint64_t k_context_external_overhead_charge = 1024;
constexpr uint64_t k_manager_accounting_charge        = 64ULL * 1024ULL;
constexpr uint64_t k_call_allocation_overhead_charge  = 256;

constexpr uint8_t k_record_manifest = 1;
constexpr uint8_t k_record_literal  = 2;
constexpr uint8_t k_record_variable = 3;
constexpr uint8_t k_record_call     = 4;
constexpr uint8_t k_record_end      = 5;

enum class Operation : uint32_t
{
    ADD = 1,
    SUB = 2,
    MUL = 3,
    DIV = 4,
    SIN = 7,
    COS = 8,
};

enum class Instruction_kind : uint32_t
{
    LITERAL,
    VARIABLE,
    CALL,
};

enum class Matrix_layout : uint32_t
{
    ROW_MAJOR,
    SLOT_MAJOR,
};

struct instruction_t
{
    Instruction_kind kind;
    uint32_t         operand;
    uint64_t         literal_bits;
};

static_assert(sizeof(instruction_t) == 16,
    "The provisional normalized-instruction accounting requires 16-byte instructions.");


void wipe_memory(void* bytes, size_t size) noexcept
{
    // This volatile loop covers addressable memory objects on the supported
    // host model. It deliberately makes no CPU-register or cache-wipe claim.
    volatile uint8_t* current = static_cast<volatile uint8_t*>(bytes);
    while (size != 0) {
        *current++ = 0;
        --size;
    }
}


class Memory_wipe_guard
{
public:
    Memory_wipe_guard(void* in_bytes, size_t in_size)
    :
        m_bytes(in_bytes),
        m_size(in_size)
    {}

    ~Memory_wipe_guard() { wipe_memory(m_bytes, m_size); }

    Memory_wipe_guard(const Memory_wipe_guard&) = delete;
    Memory_wipe_guard& operator=(const Memory_wipe_guard&) = delete;

private:
    void*  m_bytes;
    size_t m_size;
};


template<typename T>
class Object_wipe_guard
{
public:
    explicit Object_wipe_guard(T& in_value)
    :
        m_value(in_value)
    {}

    ~Object_wipe_guard() { wipe_memory(&m_value, sizeof(m_value)); }

    Object_wipe_guard(const Object_wipe_guard&) = delete;
    Object_wipe_guard& operator=(const Object_wipe_guard&) = delete;

private:
    T& m_value;
};


template<typename T>
class Owned_buffer
{
public:
    explicit Owned_buffer(size_t in_count)
    :
        m_data(new T[in_count]),
        m_count(in_count)
    {}

    ~Owned_buffer()
    {
        wipe_memory(m_data.get(), m_count * sizeof(T));
    }

    Owned_buffer(const Owned_buffer&) = delete;
    Owned_buffer& operator=(const Owned_buffer&) = delete;

    T* data() noexcept { return m_data.get(); }
    const T* data() const noexcept { return m_data.get(); }

private:
    std::unique_ptr<T[]> m_data;
    size_t               m_count;
};


bool checked_add(uint64_t left, uint64_t right, uint64_t& result)
{
    if (left > (std::numeric_limits<uint64_t>::max)() - right) {
        return false;
    }
    result = left + right;
    return true;
}


bool checked_multiply(uint64_t left, uint64_t right, uint64_t& result)
{
    if (left != 0 && right > (std::numeric_limits<uint64_t>::max)() / left) {
        return false;
    }
    result = left * right;
    return true;
}


bool equals_magic(const uint8_t* bytes, const uint8_t (&magic)[8])
{
    return std::memcmp(bytes, magic, sizeof(magic)) == 0;
}


bool all_zero(const uint8_t* bytes, size_t size)
{
    uint8_t any = 0;
    for (size_t i = 0; i < size; ++i) {
        any |= bytes[i];
    }
    return any == 0;
}


uint16_t load_u16(const uint8_t* bytes)
{
    return static_cast<uint16_t>(bytes[0]) |
        static_cast<uint16_t>(bytes[1]) << 8;
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
    return static_cast<uint64_t>(bytes[0])       |
        static_cast<uint64_t>(bytes[1]) << 8  |
        static_cast<uint64_t>(bytes[2]) << 16 |
        static_cast<uint64_t>(bytes[3]) << 24 |
        static_cast<uint64_t>(bytes[4]) << 32 |
        static_cast<uint64_t>(bytes[5]) << 40 |
        static_cast<uint64_t>(bytes[6]) << 48 |
        static_cast<uint64_t>(bytes[7]) << 56;
}


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


void store_u64(uint8_t* bytes, const uint64_t& in_value)
{
    for (size_t i = 0; i < 8; ++i) {
        bytes[i] = static_cast<uint8_t>(in_value >> (i * 8));
    }
}


void copy_double_from_bits(
    const uint64_t& in_bits,
    double& out_value) noexcept
{
    std::memcpy(&out_value, &in_bits, sizeof(out_value));
}


void copy_double_bits(
    const double& in_value,
    uint64_t& out_bits) noexcept
{
    std::memcpy(&out_bits, &in_value, sizeof(out_bits));
}


uint32_t operation_arity(const uint32_t& in_operation)
{
    switch (in_operation) {
        case static_cast<uint32_t>(Operation::SIN):
        case static_cast<uint32_t>(Operation::COS): return 1;
        case static_cast<uint32_t>(Operation::ADD):
        case static_cast<uint32_t>(Operation::SUB):
        case static_cast<uint32_t>(Operation::MUL):
        case static_cast<uint32_t>(Operation::DIV): return 2;
        default:                                         return 0;
    }
}


class Floating_point_scope
{
public:
    Floating_point_scope()
    :
        m_environment_valid(std::fegetenv(&m_environment) == 0),
        m_mxcsr(_mm_getcsr())
    {
        std::fesetround(FE_TONEAREST);
        constexpr uint32_t k_mxcsr_exception_masks = 0x00001f80U;
        constexpr uint32_t k_mxcsr_daz             = 0x00000040U;
        constexpr uint32_t k_mxcsr_rounding        = 0x00006000U;
        constexpr uint32_t k_mxcsr_ftz             = 0x00008000U;
        const uint32_t strict_mxcsr =
            (m_mxcsr | k_mxcsr_exception_masks) &
            ~(k_mxcsr_daz | k_mxcsr_rounding | k_mxcsr_ftz);
        _mm_setcsr(strict_mxcsr);
    }

    ~Floating_point_scope()
    {
        if (m_environment_valid) {
            std::fesetenv(&m_environment);
        }
        _mm_setcsr(m_mxcsr);
    }

    Floating_point_scope(const Floating_point_scope&) = delete;
    Floating_point_scope& operator=(const Floating_point_scope&) = delete;

private:
    fenv_t   m_environment;
    bool     m_environment_valid;
    uint32_t m_mxcsr;
};


void apply_binary(
    const uint32_t& in_operation,
    const double& in_left,
    const double& in_right,
    double& out_value)
{
    __m128d lhs = _mm_set_sd(in_left);
    __m128d rhs = _mm_set_sd(in_right);
    Memory_wipe_guard lhs_wipe(&lhs, sizeof(lhs));
    Memory_wipe_guard rhs_wipe(&rhs, sizeof(rhs));
    switch (in_operation) {
        case static_cast<uint32_t>(Operation::ADD):
            out_value = _mm_cvtsd_f64(_mm_add_sd(lhs, rhs));
            return;
        case static_cast<uint32_t>(Operation::SUB):
            out_value = _mm_cvtsd_f64(_mm_sub_sd(lhs, rhs));
            return;
        case static_cast<uint32_t>(Operation::MUL):
            out_value = _mm_cvtsd_f64(_mm_mul_sd(lhs, rhs));
            return;
        case static_cast<uint32_t>(Operation::DIV):
            out_value = _mm_cvtsd_f64(_mm_div_sd(lhs, rhs));
            return;
        default:
            out_value = (std::numeric_limits<double>::quiet_NaN)();
            return;
    }
}


} // namespace


class Experimental_interpreter::Impl
{
public:
    using Status     = Experimental_interpreter::Status;
    using Test_fault = Experimental_interpreter::Test_fault;

    enum class Context_state : uint32_t
    {
        LOADED,
        EVALUATING,
        UNLOADING_ACTIVE,
        UNLOADING_IDLE,
        UNLOADED,
    };

    enum class Gate_state : uint32_t
    {
        OPEN,
        HOLD_NEXT,
        HELD,
    };

    class Context
    {
    public:
        Context()
        :
            m_artifact_size(0),
            m_instruction_count(0),
            m_slot_count(0),
            m_max_stack_depth(0),
            m_state(Context_state::LOADED),
            m_wiped(false)
        {}

        ~Context() { wipe_all(); }

        Context(const Context&) = delete;
        Context& operator=(const Context&) = delete;

        uint8_t* artifact_data() noexcept { return m_artifact.data(); }
        const uint8_t* artifact_data() const noexcept { return m_artifact.data(); }
        uint64_t artifact_size() const noexcept { return m_artifact_size; }
        void set_artifact_size(uint64_t in_size) noexcept { m_artifact_size = in_size; }

        instruction_t* instructions_data() noexcept { return m_instructions.data(); }
        const instruction_t* instructions_data() const noexcept { return m_instructions.data(); }
        uint32_t instruction_count() const noexcept { return m_instruction_count; }
        void set_instruction_count(uint32_t in_count) noexcept { m_instruction_count = in_count; }

        double* slots_data() noexcept { return m_slots.data(); }
        double* stack_data() noexcept { return m_stack.data(); }
        uint32_t slot_count() const noexcept { return m_slot_count; }
        void set_slot_count(uint32_t in_count) noexcept { m_slot_count = in_count; }
        uint32_t max_stack_depth() const noexcept { return m_max_stack_depth; }
        void set_max_stack_depth(uint32_t in_depth) noexcept { m_max_stack_depth = in_depth; }

        Context_state state() const noexcept { return m_state; }
        void set_state(Context_state in_state) noexcept { m_state = in_state; }

        static uint64_t accounting_charge() noexcept
        {
            return sizeof(Context) + k_context_external_overhead_charge;
        }

        void wipe_evaluation_memory() noexcept
        {
            wipe_memory(m_slots.data(), sizeof(m_slots));
            wipe_memory(m_stack.data(), sizeof(m_stack));
        }

        void wipe_all() noexcept
        {
            if (m_wiped) {
                return;
            }
            wipe_memory(m_artifact.data(), sizeof(m_artifact));
            wipe_memory(m_instructions.data(), sizeof(m_instructions));
            wipe_evaluation_memory();
            m_wiped = true;
        }

    private:
        std::array<uint8_t, k_max_artifact_size>          m_artifact;
        std::array<instruction_t, k_max_instructions>     m_instructions;
        std::array<double, k_max_slots>                   m_slots;
        std::array<double, k_max_stack_depth>             m_stack;
        uint64_t                                          m_artifact_size;
        uint32_t                                          m_instruction_count;
        uint32_t                                          m_slot_count;
        uint32_t                                          m_max_stack_depth;
        Context_state                                     m_state;
        bool                                              m_wiped;
    };

    class Load_admission
    {
    public:
        explicit Load_admission(Impl& in_owner)
        :
            m_owner(in_owner),
            m_reserved_bytes(0),
            m_active(false),
            m_fault(Test_fault::NONE)
        {}

        ~Load_admission() { cancel(); }

        Load_admission(const Load_admission&) = delete;
        Load_admission& operator=(const Load_admission&) = delete;

        bool admit(uint64_t in_reserved_bytes)
        {
            std::lock_guard<std::mutex> lock(m_owner.m_mutex);
            uint64_t total = 0;
            if (m_owner.m_context_count + m_owner.m_pending_loads >= k_max_contexts ||
                m_owner.m_admitted_calls >= k_max_admitted_calls ||
                !checked_add(
                    m_owner.m_persistent_allocation,
                    m_owner.m_call_local_allocation,
                    total) ||
                !checked_add(total, in_reserved_bytes, total) ||
                total > k_max_combined_allocation)
            {
                return false;
            }
            m_owner.m_call_local_allocation += in_reserved_bytes;
            ++m_owner.m_admitted_calls;
            ++m_owner.m_pending_loads;
            m_reserved_bytes = in_reserved_bytes;
            m_active         = true;
            m_fault          = m_owner.m_next_fault;
            if (m_fault == Test_fault::LOAD_ALLOCATION) {
                m_owner.m_next_fault = Test_fault::NONE;
            }
            return true;
        }

        Test_fault fault() const noexcept { return m_fault; }

        bool publish(
            const std::shared_ptr<Context>& in_context,
            uint64_t& out_handle)
        {
            std::lock_guard<std::mutex> lock(m_owner.m_mutex);
            uint64_t persistent_after = 0;
            if (!m_active || m_owner.m_next_handle == 0 ||
                !checked_add(
                    m_owner.m_persistent_allocation,
                    Context::accounting_charge(),
                    persistent_after) ||
                persistent_after > k_max_persistent_allocation)
            {
                return false;
            }

            const uint64_t handle = m_owner.m_next_handle;
            const auto inserted = m_owner.m_contexts.insert(
                std::make_pair(handle, in_context));
            if (!inserted.second) {
                return false;
            }

            ++m_owner.m_next_handle;
            ++m_owner.m_context_count;
            --m_owner.m_pending_loads;
            --m_owner.m_admitted_calls;
            m_owner.m_call_local_allocation -= m_reserved_bytes;
            m_owner.m_persistent_allocation = persistent_after;
            m_reserved_bytes = 0;
            m_active         = false;
            out_handle       = handle;
            return true;
        }

    private:
        void cancel() noexcept
        {
            if (!m_active) {
                return;
            }
            std::lock_guard<std::mutex> lock(m_owner.m_mutex);
            m_owner.m_call_local_allocation -= m_reserved_bytes;
            --m_owner.m_admitted_calls;
            --m_owner.m_pending_loads;
            m_owner.m_state_changed.notify_all();
            m_active = false;
        }

        Impl&      m_owner;
        uint64_t   m_reserved_bytes;
        bool       m_active;
        Test_fault m_fault;
    };

    class Evaluation_admission
    {
    public:
        explicit Evaluation_admission(Impl& in_owner)
        :
            m_owner(in_owner),
            m_reserved_bytes(0),
            m_active(false),
            m_fault(Test_fault::NONE)
        {}

        ~Evaluation_admission() { release(); }

        Evaluation_admission(const Evaluation_admission&) = delete;
        Evaluation_admission& operator=(const Evaluation_admission&) = delete;

        Status admit(
            uint64_t in_handle,
            uint32_t in_slot_count,
            uint64_t in_reserved_bytes)
        {
            std::unique_lock<std::mutex> lock(m_owner.m_mutex);
            auto found = m_owner.m_contexts.find(in_handle);
            if (found == m_owner.m_contexts.end()) {
                return Status::INPUT_INVALID;
            }
            m_context = found->second;

            if (m_context->state() == Context_state::EVALUATING) {
                m_context.reset();
                return Status::INTERNAL_FAILURE;
            }
            if (m_context->state() != Context_state::LOADED ||
                m_context->slot_count() != in_slot_count)
            {
                m_context.reset();
                return Status::INPUT_INVALID;
            }

            uint64_t total = 0;
            if (m_owner.m_admitted_calls >= k_max_admitted_calls ||
                !checked_add(
                    m_owner.m_persistent_allocation,
                    m_owner.m_call_local_allocation,
                    total) ||
                !checked_add(total, in_reserved_bytes, total) ||
                total > k_max_combined_allocation)
            {
                m_context.reset();
                return Status::INTERNAL_FAILURE;
            }

            m_context->set_state(Context_state::EVALUATING);
            m_owner.m_call_local_allocation += in_reserved_bytes;
            ++m_owner.m_admitted_calls;
            m_reserved_bytes = in_reserved_bytes;
            m_active         = true;
            m_fault          = m_owner.m_next_fault;
            if (m_fault == Test_fault::EVALUATE_ALLOCATION) {
                m_owner.m_next_fault = Test_fault::NONE;
            }
            return Status::SUCCESS;
        }

        Context& context() noexcept { return *m_context; }
        Test_fault fault() const noexcept { return m_fault; }

    private:
        void release() noexcept
        {
            if (!m_active) {
                return;
            }
            std::lock_guard<std::mutex> lock(m_owner.m_mutex);
            if (m_context->state() == Context_state::EVALUATING) {
                m_context->set_state(Context_state::LOADED);
            }
            else
            if (m_context->state() == Context_state::UNLOADING_ACTIVE) {
                m_context->set_state(Context_state::UNLOADING_IDLE);
            }
            m_context.reset();
            m_owner.m_call_local_allocation -= m_reserved_bytes;
            --m_owner.m_admitted_calls;
            m_owner.m_state_changed.notify_all();
            m_active = false;
        }

        Impl&                    m_owner;
        std::shared_ptr<Context> m_context;
        uint64_t                 m_reserved_bytes;
        bool                     m_active;
        Test_fault               m_fault;
    };

    Impl()
    :
        m_next_handle(1),
        m_context_count(0),
        m_pending_loads(0),
        m_admitted_calls(0),
        m_persistent_allocation(k_manager_accounting_charge),
        m_call_local_allocation(0),
        m_next_fault(Test_fault::NONE),
        m_gate_state(Gate_state::OPEN)
    {
        static_assert(sizeof(Impl) <= k_manager_accounting_charge,
            "The manager must fit its conservative accounting charge.");
        static_assert(
            k_manager_accounting_charge +
                k_max_contexts *
                (sizeof(Context) + k_context_external_overhead_charge) <=
                k_max_persistent_allocation,
            "Ten fixed-capacity contexts must fit the persistent allocation bound.");
    }

    Status validate_program(Context& context)
    {
        const uint8_t* artifact = context.artifact_data();
        if (context.artifact_size() < k_artifact_header_size ||
            !equals_magic(artifact, k_artifact_magic) ||
            load_u16(artifact + 8)  != k_abi_major ||
            load_u16(artifact + 10) != k_abi_minor ||
            load_u16(artifact + 12) != k_artifact_header_size ||
            load_u16(artifact + 14) != k_record_size ||
            load_u32(artifact + 20) != 0 ||
            !all_zero(artifact + 24, 8))
        {
            return Status::ARTIFACT_INVALID;
        }

        const uint32_t record_count = load_u32(artifact + 16);
        uint64_t records_size       = 0;
        uint64_t expected_size      = 0;
        if (record_count < 3 || record_count > k_max_records ||
            !checked_multiply(record_count, k_record_size, records_size) ||
            !checked_add(k_artifact_header_size, records_size, expected_size) ||
            expected_size != context.artifact_size())
        {
            return Status::ARTIFACT_INVALID;
        }

        std::array<uint8_t, k_max_slots> used_slots = {};
        Memory_wipe_guard used_slots_wipe(used_slots.data(), sizeof(used_slots));
        uint32_t slot_count        = 0;
        uint32_t stack_depth       = 0;
        uint32_t max_stack_depth   = 0;
        uint32_t instruction_count = 0;

        for (uint32_t index = 0; index < record_count; ++index) {
            const uint8_t* record = artifact + k_artifact_header_size +
                static_cast<size_t>(index) * k_record_size;
            uint8_t kind     = record[0];
            uint32_t operand = load_u32(record + 4);
            uint64_t value   = load_u64(record + 8);
            Object_wipe_guard<uint8_t> kind_wipe(kind);
            Object_wipe_guard<uint32_t> operand_wipe(operand);
            Object_wipe_guard<uint64_t> value_wipe(value);

            if (record[1] != 0 || load_u16(record + 2) != 0 ||
                !all_zero(record + 16, 16))
            {
                return Status::ARTIFACT_INVALID;
            }

            if (index == 0) {
                if (kind != k_record_manifest || operand > k_max_slots) {
                    return Status::ARTIFACT_INVALID;
                }
                if (value == 1) {
                    return Status::POLICY_REJECTED;
                }
                if (value != 0) {
                    return Status::ARTIFACT_INVALID;
                }
                slot_count = operand;
                continue;
            }

            const bool final_record = index + 1 == record_count;
            if (final_record) {
                if (kind != k_record_end || operand != 0 || value != 0 || stack_depth != 1) {
                    return Status::ARTIFACT_INVALID;
                }
                for (uint32_t slot = 0; slot < slot_count; ++slot) {
                    if (used_slots[slot] == 0) {
                        return Status::ARTIFACT_INVALID;
                    }
                }
                continue;
            }

            instruction_t instruction = {};
            Object_wipe_guard<instruction_t> instruction_wipe(instruction);
            if (kind == k_record_literal) {
                double decoded_literal = 0.0;
                Object_wipe_guard<double> literal_wipe(decoded_literal);
                copy_double_from_bits(value, decoded_literal);
                if (operand != 0 || stack_depth == k_max_stack_depth ||
                    !std::isfinite(decoded_literal))
                {
                    return Status::ARTIFACT_INVALID;
                }
                ++stack_depth;
                max_stack_depth = (std::max)(max_stack_depth, stack_depth);
                instruction = {Instruction_kind::LITERAL, 0, value};
            }
            else
            if (kind == k_record_variable) {
                if (value != 0 || operand >= slot_count || stack_depth == k_max_stack_depth) {
                    return Status::ARTIFACT_INVALID;
                }
                ++stack_depth;
                max_stack_depth   = (std::max)(max_stack_depth, stack_depth);
                used_slots[operand] = 1;
                instruction = {Instruction_kind::VARIABLE, operand, 0};
            }
            else
            if (kind == k_record_call) {
                const uint32_t arity = operation_arity(operand);
                if (value != 0 || arity == 0 || stack_depth < arity) {
                    return Status::ARTIFACT_INVALID;
                }
                stack_depth = stack_depth - arity + 1;
                instruction = {Instruction_kind::CALL, operand, 0};
            }
            else {
                return Status::ARTIFACT_INVALID;
            }

            context.instructions_data()[instruction_count++] = instruction;
        }

        context.set_instruction_count(instruction_count);
        context.set_slot_count(slot_count);
        context.set_max_stack_depth(max_stack_depth);
        return Status::SUCCESS;
    }

    bool evaluate_row(Context& context, double& out_result)
    {
        uint32_t depth = 0;
        double operand = 0.0;
        double left    = 0.0;
        double right   = 0.0;
        double value   = 0.0;
        Object_wipe_guard<double> operand_wipe(operand);
        Object_wipe_guard<double> left_wipe(left);
        Object_wipe_guard<double> right_wipe(right);
        Object_wipe_guard<double> value_wipe(value);

        const instruction_t* instructions = context.instructions_data();
        double* stack = context.stack_data();
        for (uint32_t index = 0; index < context.instruction_count(); ++index) {
            const instruction_t& instruction = instructions[index];
            if (instruction.kind == Instruction_kind::LITERAL) {
                copy_double_from_bits(instruction.literal_bits, stack[depth++]);
            }
            else
            if (instruction.kind == Instruction_kind::VARIABLE) {
                stack[depth++] = context.slots_data()[instruction.operand];
            }
            else {
                const uint32_t& operation = instruction.operand;
                const uint32_t arity      = operation_arity(operation);
                if (arity == 1) {
                    operand = stack[depth - 1];
                    value = operation == static_cast<uint32_t>(Operation::SIN)
                        ? std::sin(operand)
                        : std::cos(operand);
                }
                else {
                    right = stack[depth - 1];
                    left  = stack[depth - 2];
                    if (operation == static_cast<uint32_t>(Operation::DIV) && right == 0.0) {
                        return false;
                    }
                    apply_binary(operation, left, right, value);
                    --depth;
                }
                if (!std::isfinite(value)) {
                    return false;
                }
                stack[depth - 1] = value;
            }
        }
        out_result = context.stack_data()[0];
        return std::isfinite(out_result);
    }

    Status unload_context(uint64_t in_handle)
    {
        std::shared_ptr<Context> context;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            const auto found = m_contexts.find(in_handle);
            if (in_handle == 0 || found == m_contexts.end()) {
                return Status::INPUT_INVALID;
            }
            context = found->second;
            if (context->state() == Context_state::LOADED) {
                context->set_state(Context_state::UNLOADING_IDLE);
            }
            else
            if (context->state() == Context_state::EVALUATING) {
                context->set_state(Context_state::UNLOADING_ACTIVE);
            }
            else {
                return Status::INPUT_INVALID;
            }
            m_contexts.erase(found);
            m_state_changed.notify_all();
            while (context->state() == Context_state::UNLOADING_ACTIVE) {
                m_state_changed.wait(lock);
            }
        }

        context->wipe_all();

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            context->set_state(Context_state::UNLOADED);
            context.reset();
            m_persistent_allocation -= Context::accounting_charge();
            --m_context_count;
            m_state_changed.notify_all();
        }
        return Status::SUCCESS;
    }

    void stop_at_test_gate()
    {
        std::unique_lock<std::mutex> lock(m_gate_mutex);
        if (m_gate_state != Gate_state::HOLD_NEXT) {
            return;
        }
        m_gate_state = Gate_state::HELD;
        m_gate_changed.notify_all();
        while (m_gate_state == Gate_state::HELD) {
            m_gate_changed.wait(lock);
        }
    }

    std::mutex                                   m_mutex;
    std::condition_variable                      m_state_changed;
    std::map<uint64_t, std::shared_ptr<Context>> m_contexts;
    uint64_t                                     m_next_handle;
    uint32_t                                     m_context_count;
    uint32_t                                     m_pending_loads;
    uint32_t                                     m_admitted_calls;
    uint64_t                                     m_persistent_allocation;
    uint64_t                                     m_call_local_allocation;
    Test_fault                                   m_next_fault;

    std::mutex              m_gate_mutex;
    std::condition_variable m_gate_changed;
    Gate_state              m_gate_state;
};


Experimental_interpreter::Experimental_interpreter()
:
    m_impl(new Impl)
{}


Experimental_interpreter::~Experimental_interpreter() = default;


Experimental_interpreter::Status Experimental_interpreter::load(
    const uint8_t* in_request,
    uint64_t in_request_size,
    uint8_t* out_response,
    uint64_t in_response_size) noexcept
{
    if (!in_request || !out_response || in_request_size < k_load_header_size ||
        in_request_size > k_max_copied_input || in_response_size != k_load_response_size)
    {
        return Status::INPUT_INVALID;
    }

    try {
        std::array<uint8_t, k_load_header_size> request_header;
        std::memcpy(request_header.data(), in_request, request_header.size());
        if (!equals_magic(request_header.data(), k_load_magic) ||
            load_u16(request_header.data() + 8)  != k_abi_major ||
            load_u16(request_header.data() + 10) != k_abi_minor ||
            load_u32(request_header.data() + 12) != k_load_header_size)
        {
            return Status::INPUT_INVALID;
        }

        const uint64_t artifact_size = load_u64(request_header.data() + 16);
        uint64_t expected_size       = 0;
        if (!checked_add(k_load_header_size, artifact_size, expected_size) ||
            expected_size != in_request_size)
        {
            return Status::INPUT_INVALID;
        }
        if (artifact_size > k_max_artifact_size) {
            return Status::ARTIFACT_INVALID;
        }

        const uint64_t load_reservation =
            Impl::Context::accounting_charge() + k_max_slots +
            k_call_allocation_overhead_charge;
        Impl::Load_admission admission(*m_impl);
        if (!admission.admit(load_reservation)) {
            return Status::INTERNAL_FAILURE;
        }
        if (admission.fault() == Test_fault::LOAD_ALLOCATION) {
            throw std::bad_alloc();
        }

        std::shared_ptr<Impl::Context> context(new Impl::Context);
        std::memcpy(
            context->artifact_data(),
            in_request + k_load_header_size,
            static_cast<size_t>(artifact_size));
        context->set_artifact_size(artifact_size);
        const Status parse_status = m_impl->validate_program(*context);
        if (parse_status != Status::SUCCESS) {
            return parse_status;
        }

        uint64_t handle = 0;
        if (!admission.publish(context, handle)) {
            return Status::INTERNAL_FAILURE;
        }

        std::array<uint8_t, k_load_response_size> response = {};
        std::memcpy(response.data(), k_load_response_magic, sizeof(k_load_response_magic));
        store_u16(response.data() + 8, k_abi_major);
        store_u16(response.data() + 10, k_abi_minor);
        store_u32(response.data() + 12, k_load_response_size);
        store_u64(response.data() + 16, handle);
        store_u32(response.data() + 24, context->slot_count());
        store_u32(response.data() + 28, 1);
        std::memcpy(out_response, response.data(), response.size());
        return Status::SUCCESS;
    }
    catch (const std::bad_alloc&) {
        return Status::INTERNAL_FAILURE;
    }
    catch (...) {
        return Status::INTERNAL_FAILURE;
    }
}


Experimental_interpreter::Status Experimental_interpreter::evaluate_batch(
    const uint8_t* in_request,
    uint64_t in_request_size,
    uint8_t* out_results,
    uint64_t in_results_size) noexcept
{
    if (!in_request || in_request_size < k_evaluate_header_size ||
        in_request_size > k_max_copied_input)
    {
        return Status::INPUT_INVALID;
    }

    try {
        std::array<uint8_t, k_evaluate_header_size> request_header;
        std::memcpy(request_header.data(), in_request, request_header.size());
        if (!equals_magic(request_header.data(), k_evaluate_magic) ||
            load_u16(request_header.data() + 8)  != k_abi_major ||
            load_u16(request_header.data() + 10) != k_abi_minor ||
            load_u32(request_header.data() + 12) != k_evaluate_header_size ||
            load_u32(request_header.data() + 36) != 0)
        {
            return Status::INPUT_INVALID;
        }

        const uint64_t handle     = load_u64(request_header.data() + 16);
        const uint32_t layout_id  = load_u32(request_header.data() + 24);
        const uint32_t row_count  = load_u32(request_header.data() + 28);
        const uint32_t slot_count = load_u32(request_header.data() + 32);
        if (handle == 0 || layout_id > static_cast<uint32_t>(Matrix_layout::SLOT_MAJOR) ||
            row_count == 0 || row_count > k_max_rows || slot_count > k_max_slots)
        {
            return Status::INPUT_INVALID;
        }

        uint64_t cell_count    = 0;
        uint64_t values_size   = 0;
        uint64_t expected_size = 0;
        uint64_t results_size  = 0;
        uint64_t reservation   = 0;
        if (!checked_multiply(row_count, slot_count, cell_count) ||
            !checked_multiply(cell_count, sizeof(double), values_size) ||
            !checked_add(k_evaluate_header_size, values_size, expected_size) ||
            !checked_multiply(row_count, sizeof(double), results_size) ||
            !checked_add(in_request_size, results_size, reservation) ||
            !checked_add(
                reservation,
                2 * k_call_allocation_overhead_charge,
                reservation) ||
            expected_size != in_request_size || results_size != in_results_size ||
            (results_size != 0 && !out_results))
        {
            return Status::INPUT_INVALID;
        }

        Impl::Evaluation_admission admission(*m_impl);
        const Status admission_status = admission.admit(handle, slot_count, reservation);
        if (admission_status != Status::SUCCESS) {
            return admission_status;
        }
        if (admission.fault() == Test_fault::EVALUATE_ALLOCATION) {
            throw std::bad_alloc();
        }

        Owned_buffer<uint8_t> request(static_cast<size_t>(in_request_size));
        std::memcpy(request.data(), in_request, static_cast<size_t>(in_request_size));
        if (!equals_magic(request.data(), k_evaluate_magic) ||
            load_u16(request.data() + 8)  != k_abi_major ||
            load_u16(request.data() + 10) != k_abi_minor ||
            load_u32(request.data() + 12) != k_evaluate_header_size ||
            load_u64(request.data() + 16) != handle ||
            load_u32(request.data() + 24) != layout_id ||
            load_u32(request.data() + 28) != row_count ||
            load_u32(request.data() + 32) != slot_count ||
            load_u32(request.data() + 36) != 0)
        {
            return Status::INPUT_INVALID;
        }

        const uint8_t* values = request.data() + k_evaluate_header_size;
        for (uint64_t cell = 0; cell < cell_count; ++cell) {
            uint64_t input_bits = load_u64(values + cell * sizeof(double));
            double input_value  = 0.0;
            Object_wipe_guard<uint64_t> input_bits_wipe(input_bits);
            Object_wipe_guard<double> input_value_wipe(input_value);
            copy_double_from_bits(input_bits, input_value);
            if (!std::isfinite(input_value)) {
                return Status::INPUT_INVALID;
            }
        }

        m_impl->stop_at_test_gate();

        Impl::Context& context = admission.context();
        Memory_wipe_guard slots_wipe(context.slots_data(), k_max_slots * sizeof(double));
        Memory_wipe_guard stack_wipe(context.stack_data(), k_max_stack_depth * sizeof(double));
        Owned_buffer<double> scratch_results(row_count);
        Floating_point_scope floating_point_scope;
        const Matrix_layout layout = static_cast<Matrix_layout>(layout_id);
        for (uint32_t row = 0; row < row_count; ++row) {
            for (uint32_t slot = 0; slot < slot_count; ++slot) {
                const uint64_t cell = layout == Matrix_layout::ROW_MAJOR
                    ? static_cast<uint64_t>(row) * slot_count + slot
                    : static_cast<uint64_t>(slot) * row_count + row;
                uint64_t input_bits = load_u64(values + cell * sizeof(double));
                Object_wipe_guard<uint64_t> input_bits_wipe(input_bits);
                copy_double_from_bits(input_bits, context.slots_data()[slot]);
            }
            if (!m_impl->evaluate_row(context, scratch_results.data()[row])) {
                return Status::EVALUATION_FAILED;
            }
        }

        for (uint32_t row = 0; row < row_count; ++row) {
            uint64_t result_bits = 0;
            Object_wipe_guard<uint64_t> result_bits_wipe(result_bits);
            copy_double_bits(scratch_results.data()[row], result_bits);
            store_u64(
                out_results + static_cast<size_t>(row) * sizeof(double),
                result_bits);
        }
        return Status::SUCCESS;
    }
    catch (const std::bad_alloc&) {
        return Status::INTERNAL_FAILURE;
    }
    catch (...) {
        return Status::INTERNAL_FAILURE;
    }
}


Experimental_interpreter::Status Experimental_interpreter::unload(
    const uint8_t* in_request,
    uint64_t in_request_size) noexcept
{
    if (!in_request || in_request_size != k_unload_request_size) {
        return Status::INPUT_INVALID;
    }

    try {
        std::array<uint8_t, k_unload_request_size> request;
        std::memcpy(request.data(), in_request, request.size());
        if (!equals_magic(request.data(), k_unload_magic) ||
            load_u16(request.data() + 8)  != k_abi_major ||
            load_u16(request.data() + 10) != k_abi_minor ||
            load_u32(request.data() + 12) != k_unload_request_size)
        {
            return Status::INPUT_INVALID;
        }
        return m_impl->unload_context(load_u64(request.data() + 16));
    }
    catch (...) {
        return Status::INTERNAL_FAILURE;
    }
}


void Experimental_interpreter::fail_next_for_testing(Test_fault in_fault) noexcept
{
    std::lock_guard<std::mutex> lock(m_impl->m_mutex);
    m_impl->m_next_fault = in_fault;
}


void Experimental_interpreter::hold_next_evaluation_for_testing() noexcept
{
    std::lock_guard<std::mutex> lock(m_impl->m_gate_mutex);
    m_impl->m_gate_state = Impl::Gate_state::HOLD_NEXT;
}


void Experimental_interpreter::wait_until_evaluation_held_for_testing() noexcept
{
    std::unique_lock<std::mutex> lock(m_impl->m_gate_mutex);
    while (m_impl->m_gate_state != Impl::Gate_state::HELD) {
        m_impl->m_gate_changed.wait(lock);
    }
}


void Experimental_interpreter::wait_until_unloading_for_testing(uint64_t in_handle) noexcept
{
    std::unique_lock<std::mutex> lock(m_impl->m_mutex);
    while (m_impl->m_contexts.find(in_handle) != m_impl->m_contexts.end()) {
        m_impl->m_state_changed.wait(lock);
    }
}


void Experimental_interpreter::release_evaluation_for_testing() noexcept
{
    std::lock_guard<std::mutex> lock(m_impl->m_gate_mutex);
    m_impl->m_gate_state = Impl::Gate_state::OPEN;
    m_impl->m_gate_changed.notify_all();
}


} // namespace software_lock_b0
} // namespace mexce

#ifndef MEXCE_TEST_ARM64_ABI_PROBE_H
#define MEXCE_TEST_ARM64_ABI_PROBE_H

#include "mexce.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <new>
#include <sstream>
#include <stdexcept>
#include <vector>

#if defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__))

namespace mexce_test {

constexpr uint32_t k_arm64_integer_register_mask = (uint32_t(1) << 10) - 1;
constexpr uint32_t k_arm64_fp_register_mask = ((uint32_t(1) << 8) - 1) << 10;
constexpr uint32_t k_arm64_stack_pointer_mask = uint32_t(1) << 18;
constexpr uint32_t k_arm64_frame_pointer_mask = uint32_t(1) << 19;
constexpr uint32_t k_arm64_all_register_mask = (uint32_t(1) << 20) - 1;

struct arm64_abi_result_t
{
    double   value;
    uint32_t changed_registers;
};

struct arm64_abi_controls_t
{
    arm64_abi_result_t preserved;
    arm64_abi_result_t clobbered;
};

namespace {

struct arm64_abi_state_t
{
    uint64_t saved_general[12];
    double   saved_fp[8];
    uint64_t saved_sp;
    uint64_t call_frame;
    uint64_t actual_general[10];
    double   actual_fp[8];
    uint64_t actual_sp;
    uint64_t actual_frame;
    double   value;
};

static_assert(offsetof(arm64_abi_state_t, saved_fp) == 96, "assembly state offset");
static_assert(offsetof(arm64_abi_state_t, saved_sp) == 160, "assembly state offset");
static_assert(offsetof(arm64_abi_state_t, call_frame) == 168, "assembly state offset");
static_assert(offsetof(arm64_abi_state_t, actual_general) == 176, "assembly state offset");
static_assert(offsetof(arm64_abi_state_t, actual_fp) == 256, "assembly state offset");
static_assert(offsetof(arm64_abi_state_t, actual_sp) == 320, "assembly state offset");
static_assert(offsetof(arm64_abi_state_t, actual_frame) == 328, "assembly state offset");
static_assert(offsetof(arm64_abi_state_t, value) == 336, "assembly state offset");

// The trampoline recovers this address without depending on any preserved
// register or the returned SP. Serialization is confined to this test helper.
static volatile arm64_abi_state_t arm64_abi_state
    __asm__("mexce_test_arm64_abi_state") __attribute__((used)) = {};

__attribute__((naked, noinline))
static void arm64_abi_trampoline(double (*)())
{
    __asm__ volatile(
        "adrp x9, mexce_test_arm64_abi_state@PAGE\n"
        "add x9, x9, mexce_test_arm64_abi_state@PAGEOFF\n"
        "stp x19, x20, [x9, #0]\n"
        "stp x21, x22, [x9, #16]\n"
        "stp x23, x24, [x9, #32]\n"
        "stp x25, x26, [x9, #48]\n"
        "stp x27, x28, [x9, #64]\n"
        "stp x29, x30, [x9, #80]\n"
        "stp d8, d9, [x9, #96]\n"
        "stp d10, d11, [x9, #112]\n"
        "stp d12, d13, [x9, #128]\n"
        "stp d14, d15, [x9, #144]\n"
        "mov x10, sp\n"
        "str x10, [x9, #160]\n"
        // Keep the real frame chain intact for calls into generated code/libm.
        "stp x29, x30, [sp, #-16]!\n"
        "mov x29, sp\n"
        "str x29, [x9, #168]\n"
        "mov x16, x0\n"
        "mov x19, #0x1013\n"
        "movk x19, #0x9abc, lsl #48\n"
        "mov x20, #0x1014\n"
        "movk x20, #0x9abc, lsl #48\n"
        "mov x21, #0x1015\n"
        "movk x21, #0x9abc, lsl #48\n"
        "mov x22, #0x1016\n"
        "movk x22, #0x9abc, lsl #48\n"
        "mov x23, #0x1017\n"
        "movk x23, #0x9abc, lsl #48\n"
        "mov x24, #0x1018\n"
        "movk x24, #0x9abc, lsl #48\n"
        "mov x25, #0x1019\n"
        "movk x25, #0x9abc, lsl #48\n"
        "mov x26, #0x101a\n"
        "movk x26, #0x9abc, lsl #48\n"
        "mov x27, #0x101b\n"
        "movk x27, #0x9abc, lsl #48\n"
        "mov x28, #0x101c\n"
        "movk x28, #0x9abc, lsl #48\n"
        "fmov d8, #1.0\n"
        "fmov d9, #2.0\n"
        "fmov d10, #3.0\n"
        "fmov d11, #4.0\n"
        "fmov d12, #5.0\n"
        "fmov d13, #6.0\n"
        "fmov d14, #7.0\n"
        "fmov d15, #8.0\n"
        "blr x16\n"
        // Do not access the returned stack or frame until both are restored.
        "adrp x9, mexce_test_arm64_abi_state@PAGE\n"
        "add x9, x9, mexce_test_arm64_abi_state@PAGEOFF\n"
        "stp x19, x20, [x9, #176]\n"
        "stp x21, x22, [x9, #192]\n"
        "stp x23, x24, [x9, #208]\n"
        "stp x25, x26, [x9, #224]\n"
        "stp x27, x28, [x9, #240]\n"
        "stp d8, d9, [x9, #256]\n"
        "stp d10, d11, [x9, #272]\n"
        "stp d12, d13, [x9, #288]\n"
        "stp d14, d15, [x9, #304]\n"
        "mov x10, sp\n"
        "str x10, [x9, #320]\n"
        "str x29, [x9, #328]\n"
        "str d0, [x9, #336]\n"
        "ldr x10, [x9, #160]\n"
        "mov sp, x10\n"
        "ldp x19, x20, [x9, #0]\n"
        "ldp x21, x22, [x9, #16]\n"
        "ldp x23, x24, [x9, #32]\n"
        "ldp x25, x26, [x9, #48]\n"
        "ldp x27, x28, [x9, #64]\n"
        "ldp x29, x30, [x9, #80]\n"
        "ldp d8, d9, [x9, #96]\n"
        "ldp d10, d11, [x9, #112]\n"
        "ldp d12, d13, [x9, #128]\n"
        "ldp d14, d15, [x9, #144]\n"
        "ret\n");
}

__attribute__((naked, noinline))
static double arm64_abi_preserved_control()
{
    __asm__ volatile("fmov d0, #1.0\nret\n");
}

__attribute__((naked, noinline))
static double arm64_abi_clobbered_control()
{
    __asm__ volatile(
        "mov x19, xzr\n"
        "mov x20, xzr\n"
        "mov x21, xzr\n"
        "mov x22, xzr\n"
        "mov x23, xzr\n"
        "mov x24, xzr\n"
        "mov x25, xzr\n"
        "mov x26, xzr\n"
        "mov x27, xzr\n"
        "mov x28, xzr\n"
        "fmov d8, xzr\n"
        "fmov d9, xzr\n"
        "fmov d10, xzr\n"
        "fmov d11, xzr\n"
        "fmov d12, xzr\n"
        "fmov d13, xzr\n"
        "fmov d14, xzr\n"
        "fmov d15, xzr\n"
        "add sp, sp, #16\n"
        "mov x29, xzr\n"
        "fmov d0, #1.0\n"
        "ret\n");
}

static arm64_abi_result_t run_arm64_abi_probe(double (*function)())
{
    static std::mutex probe_mutex;
    const std::lock_guard<std::mutex> lock(probe_mutex);
    arm64_abi_trampoline(function);
    arm64_abi_result_t result = {arm64_abi_state.value, 0};
    for (size_t i = 0; i < 10; ++i) {
        if (arm64_abi_state.actual_general[i] != UINT64_C(0x9abc000000001013) + i) {
            result.changed_registers |= uint32_t(1) << i;
        }
    }
    for (size_t i = 0; i < 8; ++i) {
        if (arm64_abi_state.actual_fp[i] != static_cast<double>(i + 1)) {
            result.changed_registers |= uint32_t(1) << (i + 10);
        }
    }
    if (arm64_abi_state.actual_sp != arm64_abi_state.call_frame) {
        result.changed_registers |= k_arm64_stack_pointer_mask;
    }
    if (arm64_abi_state.actual_frame != arm64_abi_state.call_frame) {
        result.changed_registers |= k_arm64_frame_pointer_mask;
    }
    return result;
}

class Arm64_executable_copy
{
public:
    explicit Arm64_executable_copy(const mexce::evaluator& evaluator)
    :
        m_function(nullptr),
        m_size(0)
    {
        std::istringstream input(evaluator.get_byte_representation());
        std::vector<uint8_t> bytes;
        unsigned int byte;
        while (input >> std::hex >> byte) {
            bytes.push_back(static_cast<uint8_t>(byte));
        }
        if (bytes.empty()) {
            throw std::logic_error("ARM64 ABI probe requires a native expression");
        }
        m_size = bytes.size();
        auto buffer = mexce::impl::get_executable_buffer(m_size);
        if (buffer == MAP_FAILED) {
            throw std::bad_alloc();
        }
        mexce::impl::copy_executable_buffer(buffer, bytes.data(), m_size);
        m_function = mexce::impl::lock_executable_buffer(buffer, m_size);
    }

    ~Arm64_executable_copy()
    {
        mexce::impl::free_executable_buffer(m_function, m_size);
    }

    arm64_abi_result_t probe() const { return run_arm64_abi_probe(m_function); }

private:
    Arm64_executable_copy(const Arm64_executable_copy&) = delete;
    Arm64_executable_copy& operator=(const Arm64_executable_copy&) = delete;

    double (*m_function)();
    size_t   m_size;
};

} // namespace

inline arm64_abi_result_t evaluate_arm64_abi(const mexce::evaluator& evaluator)
{
    // Generated ARM64 code uses absolute variable/constant addresses. Its local
    // branches remain valid in the copy; the evaluator owns those addresses.
    const Arm64_executable_copy executable(evaluator);
    return executable.probe();
}

inline arm64_abi_controls_t arm64_abi_control_results()
{
    return {
        run_arm64_abi_probe(arm64_abi_preserved_control),
        run_arm64_abi_probe(arm64_abi_clobbered_control)
    };
}

} // namespace mexce_test

#endif
#endif

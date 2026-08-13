#ifndef MEXCE_TEST_SOFTWARE_LOCK_B0_EXPERIMENTAL_INTERPRETER_H
#define MEXCE_TEST_SOFTWARE_LOCK_B0_EXPERIMENTAL_INTERPRETER_H

#include <cstdint>
#include <memory>


namespace mexce {
namespace software_lock_b0 {


/**
 * Test-only model of the provisional software-lock interpreter boundary.
 *
 * This type is intentionally absent from installed and exported MEXCE targets.
 * Its serialized container carries trusted decoded records and makes no
 * production authentication, confidentiality, enclave, or packaging claim.
 * Addressable decoded-record scalars, evaluation operands, copied inputs,
 * scratch, and context payload memory are explicitly wiped at last use. This
 * experimental model makes no CPU-register or cache-wipe claim.
 */
class Experimental_interpreter
{
public:
    enum class Status : uint32_t
    {
        SUCCESS                  = 0,
        PLATFORM_UNAVAILABLE     = 1,
        AUTHORIZATION_REQUIRED   = 2,
        POLICY_REJECTED          = 3,
        ARTIFACT_INVALID         = 4,
        INPUT_INVALID            = 5,
        EVALUATION_FAILED        = 6,
        ENCLAVE_RESTART_REQUIRED = 7,
        INTERNAL_FAILURE         = 8,
    };

    enum class Test_fault : uint32_t
    {
        NONE,
        LOAD_ALLOCATION,
        EVALUATE_ALLOCATION,
    };

    Experimental_interpreter();
    ~Experimental_interpreter();

    Experimental_interpreter(const Experimental_interpreter&) = delete;
    Experimental_interpreter& operator=(const Experimental_interpreter&) = delete;

    Status load(
        const uint8_t* in_request,
        uint64_t in_request_size,
        uint8_t* out_response,
        uint64_t in_response_size) noexcept;

    Status evaluate_batch(
        const uint8_t* in_request,
        uint64_t in_request_size,
        uint8_t* out_results,
        uint64_t in_results_size) noexcept;

    Status unload(
        const uint8_t* in_request,
        uint64_t in_request_size) noexcept;

    void fail_next_for_testing(Test_fault in_fault) noexcept;

    // Fixed-state synchronization only for deterministic lifecycle tests.
    void hold_next_evaluation_for_testing() noexcept;
    void wait_until_evaluation_held_for_testing() noexcept;
    void wait_until_unloading_for_testing(uint64_t in_handle) noexcept;
    void release_evaluation_for_testing() noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};


} // namespace software_lock_b0
} // namespace mexce


#endif

#include <veil/host/enclave_api.vtl0.h>
#include <VbsEnclave/HostApp/Stubs/Trusted.h>

#include <array>
#include <cstdint>

namespace
{

constexpr std::uint32_t k_abi_version    = 1;
constexpr std::uint32_t k_sequence       = 7;
constexpr int           k_usage_error    = 2;
constexpr int           k_enclave_error  = 3;
constexpr int           k_response_error = 4;

static_assert(sizeof(mexce_a0::Types::a0_request_t)  == 8);
static_assert(sizeof(mexce_a0::Types::a0_response_t) == 8);

} // namespace

int wmain(int in_argument_count, wchar_t** in_arguments)
{
    if (in_argument_count != 2)
    {
        return k_usage_error;
    }

    constexpr DWORD enclave_flags = 0;
    static_assert((enclave_flags & ENCLAVE_VBS_FLAG_DEBUG) == 0);

    try
    {
        std::array<std::uint8_t, IMAGE_ENCLAVE_LONG_ID_LENGTH> owner_id{};
        auto enclave = veil::vtl0::enclave::create(
            ENCLAVE_TYPE_VBS,
            owner_id,
            enclave_flags,
            veil::vtl0::enclave::megabytes(64));

        veil::vtl0::enclave::load_image(enclave.get(), in_arguments[1]);
        veil::vtl0::enclave::initialize(enclave.get(), 1);
        veil::vtl0::enclave_api::register_callbacks(enclave.get());

        mexce_a0::Trusted::Stubs::A0_enclave_entry enclave_entry(enclave.get());
        if (FAILED(enclave_entry.RegisterVtl0Callbacks()))
        {
            return k_enclave_error;
        }

        mexce_a0::Types::a0_request_t in_request{};
        in_request.abi_version = k_abi_version;
        in_request.sequence    = k_sequence;

        mexce_a0::Types::a0_response_t out_response{};
        if (FAILED(enclave_entry.copy_request(in_request, out_response)))
        {
            return k_enclave_error;
        }

        if (out_response.abi_version != in_request.abi_version ||
            out_response.sequence    != in_request.sequence)
        {
            return k_response_error;
        }

        return 0;
    }
    catch (...)
    {
        return k_enclave_error;
    }
}

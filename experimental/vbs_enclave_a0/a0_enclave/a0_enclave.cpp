#include <wil/enclave/wil_for_enclaves.h>

#include <VbsEnclave/Enclave/Implementation/Trusted.h>

#include <cstdint>
#include <type_traits>

namespace
{

constexpr DWORD  k_enclave_policy     = IMAGE_ENCLAVE_POLICY_STRICT_MEMORY;
constexpr SIZE_T k_address_space_size = 64ULL * 1024ULL * 1024ULL;
constexpr DWORD  k_maximum_threads    = 1;

static_assert((k_enclave_policy & IMAGE_ENCLAVE_POLICY_DEBUGGABLE) == 0);
static_assert(k_address_space_size % (2ULL * 1024ULL * 1024ULL) == 0);

} // namespace

static_assert(sizeof(mexce_a0::Types::a0_request_t)  == 8);
static_assert(sizeof(mexce_a0::Types::a0_response_t) == 8);
static_assert(std::is_standard_layout_v<mexce_a0::Types::a0_request_t>);
static_assert(std::is_standard_layout_v<mexce_a0::Types::a0_response_t>);

HRESULT mexce_a0::Trusted::Implementation::copy_request(
    const Types::a0_request_t& in_request,
    Types::a0_response_t& out_response)
{
    out_response.abi_version = in_request.abi_version;
    out_response.sequence    = in_request.sequence;
    return S_OK;
}

extern "C" const IMAGE_ENCLAVE_CONFIG __enclave_config = {
    sizeof(IMAGE_ENCLAVE_CONFIG),
    IMAGE_ENCLAVE_MINIMUM_CONFIG_SIZE,
    k_enclave_policy,
    0,
    0,
    0,
    { 0x39, 0x41, 0x18, 0x70, 0xC5, 0xA0, 0x42, 0xF9,
      0xAC, 0xA0, 0x9E, 0xD1, 0x31, 0x5D, 0x9F, 0x3C },
    { 0x4D, 0x49, 0x0D, 0x56, 0xEC, 0x98, 0x43, 0x5F,
      0x8A, 0x31, 0x5B, 0xC6, 0xEE, 0xCE, 0x1A, 0xD2 },
    1,
    0,
    k_address_space_size,
    k_maximum_threads,
    IMAGE_ENCLAVE_FLAG_PRIMARY_IMAGE
};

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID)
{
    return TRUE;
}

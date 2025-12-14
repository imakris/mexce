#pragma once

#include <chrono>

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <intrin.h>
#endif

namespace mexce {

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
// Use RDTSC on x86/x64 Windows for higher precision
namespace detail {
    inline bool has_invariant_tsc() noexcept
    {
        // Check CPUID.80000007H:EDX[8] for invariant TSC
        int cpu_info[4];
        __cpuid(cpu_info, 0x80000000);
        unsigned int max_ext_level = cpu_info[0];

        if (max_ext_level >= 0x80000007) {
            __cpuid(cpu_info, 0x80000007);
            return (cpu_info[3] & (1 << 8)) != 0; // EDX bit 8
        }
        return false;
    }

    inline double get_tsc_frequency() noexcept
    {
        // Calibrate TSC frequency using chrono over a short period
        typedef std::chrono::steady_clock clock;
        const auto t0 = clock::now();
        const unsigned long long tsc0 = __rdtsc();

        // Busy wait for ~10ms
        auto t1 = clock::now();
        while (std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count() < 10) {
            t1 = clock::now();
        }

        const unsigned long long tsc1 = __rdtsc();
        const double elapsed_sec = std::chrono::duration<double>(t1 - t0).count();

        return (double)(tsc1 - tsc0) / elapsed_sec;
    }
}

inline double get_wtime() noexcept
{
    static const bool use_tsc = detail::has_invariant_tsc();
    if (use_tsc) {
        static const double tsc_freq = detail::get_tsc_frequency();
        const unsigned long long tsc = __rdtsc();
        return (double)tsc / tsc_freq;
    }
    else {
        // Fall back to chrono if no invariant TSC
        typedef std::chrono::steady_clock clock;
        const clock::time_point now = clock::now();
        const std::chrono::duration<double> seconds = now.time_since_epoch();
        return seconds.count();
    }
}
#else
// Fallback to chrono on other platforms
inline double get_wtime() noexcept
{
    typedef std::chrono::steady_clock clock;
    const clock::time_point now = clock::now();
    const std::chrono::duration<double> seconds = now.time_since_epoch();
    return seconds.count();
}
#endif

} // namespace mexce


#pragma once

#include <chrono>
#include <limits>

#ifdef _OPENMP
#  include <omp.h>
#endif

namespace mexce {
namespace detail {

inline double chrono_resolution_seconds() noexcept
{
    typedef std::chrono::steady_clock clock;
    typedef clock::period period;
    return static_cast<double>(period::num) / static_cast<double>(period::den);
}

inline double omp_resolution_seconds() noexcept
{
#ifdef _OPENMP
    const double tick = omp_get_wtick();
    return (tick > 0.0) ? tick : std::numeric_limits<double>::infinity();
#else
    return std::numeric_limits<double>::infinity();
#endif
}

inline bool should_use_chrono() noexcept
{
    const double chrono_res = chrono_resolution_seconds();
    const double omp_res = omp_resolution_seconds();
    if (!(chrono_res > 0.0)) {
        return false;
    }
    return chrono_res < omp_res;
}

} // namespace detail

// Stopwatch class for precise timing that avoids floating-point precision issues
// when subtracting large epoch times. Uses chrono's native duration arithmetic.
class stopwatch {
public:
    using clock = std::chrono::steady_clock;
    using time_point = clock::time_point;

    stopwatch() noexcept : m_start(clock::now()) {}

    void reset() noexcept { m_start = clock::now(); }

    // Returns elapsed time in seconds as a double
    double elapsed_seconds() const noexcept {
        const auto now = clock::now();
        const std::chrono::duration<double> elapsed = now - m_start;
        return elapsed.count();
    }

    // Returns elapsed time in nanoseconds as int64_t for maximum precision
    int64_t elapsed_nanoseconds() const noexcept {
        const auto now = clock::now();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(now - m_start).count();
    }

private:
    time_point m_start;
};

// Legacy function for backward compatibility
// Note: For new code, prefer using the stopwatch class which avoids
// precision issues with large epoch times on some platforms.
inline double get_wtime() noexcept
{
    static const bool use_chrono = detail::should_use_chrono();
#ifdef _OPENMP
    if (!use_chrono) {
        return omp_get_wtime();
    }
#endif
    // Use steady_clock directly and compute time relative to a fixed point
    // to avoid precision loss with large epoch values
    static const std::chrono::steady_clock::time_point epoch = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    const std::chrono::duration<double> seconds = now - epoch;
    return seconds.count();
}

} // namespace mexce


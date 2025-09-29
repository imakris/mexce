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

inline double chrono_now_seconds() noexcept
{
    typedef std::chrono::steady_clock clock;
    const clock::time_point now = clock::now();
    const std::chrono::duration<double> seconds = now.time_since_epoch();
    return seconds.count();
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

inline double get_wtime() noexcept
{
    static const bool use_chrono = detail::should_use_chrono();
#ifdef _OPENMP
    if (!use_chrono) {
        return omp_get_wtime();
    }
#endif
    return detail::chrono_now_seconds();
}

} // namespace mexce


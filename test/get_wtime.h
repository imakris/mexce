#pragma once

#include <chrono>
#include <cstdint>

namespace mexce {

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

} // namespace mexce


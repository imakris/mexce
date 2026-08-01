// The first mexce use in this process must be concurrent, so this test lives in
// its own executable. mexce::evaluator copies the built-in constant map during
// construction, and that map is filled on first use; a serialized first
// construction would never exercise the cold-start path this test exists for.
#include "mexce.h"

#include <atomic>
#include <cmath>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>


namespace {

constexpr int    k_thread_count   = 16;
constexpr double k_expected_value = 3.141592653589793238462643383 + 2.718281828459045235360287471;

struct Cold_start_result
{
    bool        ok = true;
    std::string failure;
};

void construct_and_evaluate(std::atomic<int>& ready, std::atomic<bool>& go, Cold_start_result& result)
{
    ready.fetch_add(1);
    while (!go.load()) {
        std::this_thread::yield();
    }

    mexce::evaluator evaluator;
    evaluator.set_expression("pi+e");
    const double value = evaluator.evaluate();
    if (std::abs(value - k_expected_value) > 1e-12) {
        result.ok = false;
        result.failure = "pi+e evaluated to " + std::to_string(value);
    }
}

} // namespace


int main()
{
    std::atomic<int>  ready(0);
    std::atomic<bool> go(false);

    std::vector<Cold_start_result> results(k_thread_count);
    std::vector<std::thread>       threads;
    threads.reserve(k_thread_count);
    for (int index = 0; index < k_thread_count; ++index) {
        threads.emplace_back(construct_and_evaluate, std::ref(ready), std::ref(go), std::ref(results[index]));
    }

    while (ready.load() < k_thread_count) {
        std::this_thread::yield();
    }
    go.store(true);

    for (std::thread& thread : threads) {
        thread.join();
    }

    int failures = 0;
    for (int index = 0; index < k_thread_count; ++index) {
        if (!results[index].ok) {
            std::cerr << "FAIL: thread " << index << ": " << results[index].failure << "\n";
            ++failures;
        }
    }
    if (failures > 0) {
        std::cerr << "mexce cold-start concurrency test failed (" << failures << " threads)\n";
        return 1;
    }

    std::cout << "Cold-start concurrency test passed\n";
    return 0;
}

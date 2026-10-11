// A separate executable keeps this small arena consistent across its translation
// units and lets the same cases run in a signed Hardened Runtime process.
#define MEXCE_APPLE_JIT_ARENA_SIZE (size_t(12) * size_t(sysconf(_SC_PAGESIZE)))
#include "mexce.h"

#include <sys/mman.h>
#include <unistd.h>

#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>


namespace {

constexpr size_t k_arena_pages = 12;

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_empty(mexce::evaluator& evaluator)
{
    try {
        (void)evaluator.evaluate();
    }
    catch (const std::logic_error& error) {
        require(std::string(error.what()) == "No expression has been compiled.", "unexpected empty-state error");
        return;
    }
    throw std::runtime_error("failed replacement retained an executable expression");
}

void test_exhaustion_and_replacement()
{
    double value = 4.0;
    mexce::evaluator replacement;
    replacement.bind(value, "value");
    replacement.set_expression("7");

    std::vector<std::unique_ptr<mexce::evaluator>> holders;
    for (size_t index = 0; index < k_arena_pages; ++index) {
        std::unique_ptr<mexce::evaluator> holder(new mexce::evaluator);
        holder->bind(value, "value");
        holder->set_expression("value+1");
        holders.push_back(std::move(holder));
    }

    bool exhausted = false;
    try {
        replacement.set_expression("value+2");
    }
    catch (const std::bad_alloc&) {
        exhausted = true;
    }
    require(exhausted, "a full arena must reject another executable allocation");
    require_empty(replacement);
    replacement.unbind("value");
    require_empty(replacement);
    for (const auto& holder : holders) {
        require(holder->evaluate() == 5.0, "arena exhaustion changed a live peer");
    }

    holders[4].reset();
    replacement.bind(value, "value");
    for (size_t iteration = 0; iteration < 64; ++iteration) {
        const bool add_two = iteration % 2 == 0;
        replacement.set_expression(add_two ? "value+2" : "value+3");
        require(replacement.evaluate() == (add_two ? 6.0 : 7.0), "replacement did not reuse its released page");
        for (const auto& holder : holders) {
            if (holder) {
                require(holder->evaluate() == 5.0, "page reuse changed a live peer");
            }
        }
    }

    // Unary calls keep operand depth small while producing more than one page
    // of machine code. The full arena has only the replacement's page to reuse.
    const size_t call_count = (size_t)sysconf(_SC_PAGESIZE) / 8;
    std::string large_expression;
    for (size_t index = 0; index < call_count; ++index) {
        large_expression += "sin(";
    }
    large_expression += "value";
    large_expression.append(call_count, ')');
    exhausted = false;
    try {
        replacement.set_expression(large_expression);
    }
    catch (const std::bad_alloc&) {
        exhausted = true;
    }
    require(exhausted, "multi-page replacement must fail when only one page is free");
    require_empty(replacement);
    for (const auto& holder : holders) {
        if (holder) {
            require(holder->evaluate() == 5.0, "failed multi-page replacement changed a live peer");
        }
    }
    holders.clear();
    replacement.set_expression(large_expression);
    double expected = value;
    for (size_t index = 0; index < call_count; ++index) {
        expected = std::sin(expected);
    }
    require(replacement.evaluate() == expected, "multi-page executable did not survive allocation and publication");
}

void test_multiple_pages()
{
    auto& arena = mexce::impl::apple_jit_arena();
    const size_t page_size = (size_t)sysconf(_SC_PAGESIZE);
    const size_t sizes[] = {3 * page_size - 7, 2 * page_size, 3 * page_size - 1, 4 * page_size};
    uint8_t* blocks[4];
    for (size_t index = 0; index < 4; ++index) {
        const size_t size = sizes[index];
        blocks[index] = arena.allocate(size);
        require(blocks[index] != MAP_FAILED, "multi-page allocation failed before the arena was full");
        std::vector<uint8_t> contents(size, (uint8_t)(index + 1));
        mexce::impl::copy_executable_buffer(blocks[index], contents.data(), size);
    }
    for (size_t index = 0; index < 4; ++index) {
        for (size_t offset = 0; offset < sizes[index]; ++offset) {
            require(blocks[index][offset] == (uint8_t)(index + 1), "multi-page allocations overlap");
        }
    }

    arena.release(blocks[1], sizes[1]);
    arena.release(blocks[3], sizes[3]);
    require(arena.allocate(5 * page_size) == MAP_FAILED, "fragmented pages must not alias a live allocation");
    arena.release(blocks[2], sizes[2]);
    uint8_t* joined = arena.allocate(5 * page_size - 3);
    require(joined != MAP_FAILED, "adjacent released ranges were not reusable");
    std::vector<uint8_t> replacement(5 * page_size - 3, 9);
    mexce::impl::copy_executable_buffer(joined, replacement.data(), replacement.size());
    for (size_t offset = 0; offset < sizes[0]; ++offset) {
        require(blocks[0][offset] == 1, "multi-page reuse overwrote a live allocation");
    }
    arena.release(joined, replacement.size());
    arena.release(blocks[0], sizes[0]);

    uint8_t* entire_arena = arena.allocate(k_arena_pages * page_size);
    require(entire_arena != MAP_FAILED, "multi-page release leaked arena capacity");
    arena.release(entire_arena, k_arena_pages * page_size);
}

class Concurrent_run
{
public:
    void reader_started()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ++m_readers;
        m_ready.notify_all();
    }

    void wait_for_readers()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_ready.wait(lock, [this] { return m_readers == 2; });
    }

    void writer_finished() { ++m_finished_writers; }
    bool writers_finished() const { return m_finished_writers.load() == 2; }

private:
    std::mutex              m_mutex;
    std::condition_variable m_ready;
    int                     m_readers = 0;
    std::atomic<int>         m_finished_writers{0};
};

void evaluate_live_code(Concurrent_run& run, std::string& failure)
{
    bool ready = false;
    try {
        double value = 1.25;
        mexce::evaluator evaluator;
        evaluator.bind(value, "value");
        evaluator.set_expression("(value+2)*4");
        run.reader_started();
        ready = true;
        do {
            require(evaluator.evaluate() == 13.0, "concurrent code publication changed a live evaluator");
        }
        while (!run.writers_finished());
    }
    catch (const std::exception& error) {
        failure = error.what();
        // A failed reader must also release the writers waiting at the gate.
        if (!ready) {
            run.reader_started();
        }
    }
}

void replace_and_destroy_code(Concurrent_run& run, std::string& failure)
{
    run.wait_for_readers();
    try {
        double value = 2.5;
        for (size_t iteration = 0; iteration < 128; ++iteration) {
            mexce::evaluator evaluator;
            evaluator.bind(value, "value");
            evaluator.set_expression("value+3");
            require(evaluator.evaluate() == 5.5, "concurrent allocation produced incorrect code");
            evaluator.set_expression("value*4");
            require(evaluator.evaluate() == 10.0, "concurrent replacement produced incorrect code");
        }
    }
    catch (const std::exception& error) {
        failure = error.what();
    }
    run.writer_finished();
}

void test_concurrent_lifetimes()
{
    Concurrent_run run;
    std::string failures[4];
    std::thread reader_one(evaluate_live_code,       std::ref(run), std::ref(failures[0]));
    std::thread reader_two(evaluate_live_code,       std::ref(run), std::ref(failures[1]));
    std::thread writer_one(replace_and_destroy_code, std::ref(run), std::ref(failures[2]));
    std::thread writer_two(replace_and_destroy_code, std::ref(run), std::ref(failures[3]));
    reader_one.join();
    reader_two.join();
    writer_one.join();
    writer_two.join();
    for (const auto& failure : failures) {
        require(failure.empty(), failure.c_str());
    }
}

} // namespace


int main()
{
    try {
        test_exhaustion_and_replacement();
        test_multiple_pages();
        test_concurrent_lifetimes();
    }
    catch (const std::exception& error) {
        std::cerr << "Apple JIT arena test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "Apple JIT arena tests passed: exhaustion, empty state, multi-page reuse, concurrent lifetimes\n";
    return 0;
}

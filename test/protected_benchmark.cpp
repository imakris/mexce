#include "mexce_protected_encoder.h"

#include <sodium.h>

#ifdef _WIN32
#include <Psapi.h>
#else
#include <sys/resource.h>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif


namespace {


constexpr int k_compile_iterations = 25;
constexpr int k_evaluate_iterations = 100000;
constexpr int k_evaluate_warmup_iterations = 1000;
constexpr size_t k_expected_maximum_size = 802880;
constexpr uint32_t k_expected_maximum_records = 16384;


struct Benchmark_case
{
    const char* name;
    const char* expression;
};


const Benchmark_case k_cases[] = {
    {"arithmetic",     "x+y*z"},
    {"transcendental", "sin(x)+cos(y)-log(z+2)"},
    {"power",          "pow(x+1,2)+sqrt(y+3)+z"},
    {"rounding",       "floor(x)+ceil(y)+round(z)"},
    {"comparison",     "max(x,y)+min(y,z)+(x>z)"},
    {"mixed",          "exp(x/4)+log2(y+1)+z/2"},
};


uint64_t peak_working_set_bytes()
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS counters = {};
    counters.cb = sizeof(counters);
    if (!GetProcessMemoryInfo(
            GetCurrentProcess(), &counters, sizeof(counters)))
    {
        throw std::runtime_error("GetProcessMemoryInfo failed");
    }
    if (counters.PeakWorkingSetSize == 0) {
        throw std::runtime_error("GetProcessMemoryInfo returned an invalid peak");
    }
    return static_cast<uint64_t>(counters.PeakWorkingSetSize);
#else
    struct rusage usage = {};
    if (getrusage(RUSAGE_SELF, &usage) != 0) {
        throw std::runtime_error("getrusage failed");
    }
    if (usage.ru_maxrss <= 0) {
        throw std::runtime_error("getrusage returned an invalid peak");
    }
    return static_cast<uint64_t>(usage.ru_maxrss) * 1024;
#endif
}


template<typename Function>
uint64_t elapsed_nanoseconds(Function function)
{
    const auto start = std::chrono::steady_clock::now();
    function();
    const auto end = std::chrono::steady_clock::now();
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
}


const char* compiler_name()
{
#ifdef BENCHMARK_COMPILER
    return BENCHMARK_COMPILER;
#else
    return "unknown";
#endif
}


const char* compiler_flags()
{
#ifdef BENCHMARK_COMPILER_FLAGS
    return BENCHMARK_COMPILER_FLAGS;
#else
    return "unknown";
#endif
}


std::vector<uint8_t> read_binary(const char* path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("failed to open a prepared benchmark file");
    }
    const std::streamoff end = file.tellg();
    if (end < 0) {
        throw std::runtime_error("failed to size a prepared benchmark file");
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(end));
    file.seekg(0);
    if (!bytes.empty()) {
        file.read(reinterpret_cast<char*>(bytes.data()), end);
    }
    if (!file) {
        throw std::runtime_error("failed to read a prepared benchmark file");
    }
    return bytes;
}


void write_binary(const char* path, const uint8_t* bytes, size_t size)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        throw std::runtime_error("failed to create a prepared benchmark file");
    }
    file.write(reinterpret_cast<const char*>(bytes), static_cast<std::streamsize>(size));
    file.close();
    if (!file) {
        throw std::runtime_error("failed to write a prepared benchmark file");
    }
}


class Key_material
{
public:
    explicit Key_material(const char* path)
    {
        auto bytes = read_binary(path);
        if (bytes.size() != m_bytes.size()) {
            if (!bytes.empty()) {
                sodium_memzero(bytes.data(), bytes.size());
            }
            throw std::runtime_error("a prepared benchmark key must contain 32 bytes");
        }
        std::copy(bytes.begin(), bytes.end(), m_bytes.begin());
        if (!bytes.empty()) {
            sodium_memzero(bytes.data(), bytes.size());
        }
    }

    ~Key_material() { sodium_memzero(m_bytes.data(), m_bytes.size()); }

    mexce::Protected_expression_key copy() const
    {
        return mexce::Protected_expression_key::from_bytes(
            m_bytes.data(), m_bytes.size());
    }

private:
    Key_material(const Key_material&);
    Key_material& operator=(const Key_material&);

    std::array<uint8_t, 32> m_bytes = {};
};


uint32_t record_count(const std::vector<uint8_t>& program)
{
    if (program.size() < 20) {
        return 0;
    }
    return static_cast<uint32_t>(program[16]) |
        static_cast<uint32_t>(program[17]) << 8 |
        static_cast<uint32_t>(program[18]) << 16 |
        static_cast<uint32_t>(program[19]) << 24;
}


void write_bundle(
    mexce::Protected_expression_bundle bundle,
    const char* program_path,
    const char* key_path)
{
    write_binary(program_path, bundle.program.data(), bundle.program.size());
    bundle.key.consume_bytes([&](const uint8_t* bytes, size_t size) {
        write_binary(key_path, bytes, size);
    });
}


std::vector<mexce::Protected_binding> protected_bindings()
{
    return {{"x", 0}, {"y", 1}, {"z", 2}};
}


void bind_clear(mexce::evaluator& evaluator, double& x, double& y, double& z)
{
    evaluator.bind(x, "x", y, "y", z, "z");
}


void bind_protected(mexce::evaluator& evaluator, double& x, double& y, double& z)
{
    evaluator.bind_protected(x, 0);
    evaluator.bind_protected(y, 1);
    evaluator.bind_protected(z, 2);
}


double expected_result(size_t case_index, double x, double y, double z)
{
    switch (case_index) {
        case 0: return x + y * z;
        case 1: return std::sin(x) + std::cos(y) - std::log(z + 2.0);
        case 2: return std::pow(x + 1.0, 2.0) + std::sqrt(y + 3.0) + z;
        case 3: return std::floor(x) + std::ceil(y) + std::nearbyint(z);
        case 4: return std::max(x, y) + std::min(y, z) + (x > z ? 1.0 : 0.0);
        case 5: return std::exp(x / 4.0) + std::log2(y + 1.0) + z / 2.0;
        default: return 0.0;
    }
}


bool correct_result(size_t case_index, double result, double x, double y, double z)
{
    const double expected = expected_result(case_index, x, y, z);
    const double scale = std::max(1.0, std::fabs(expected));
    return std::isfinite(result) && std::fabs(result - expected) <= scale * 1e-12;
}


bool parse_case(const char* text, size_t& case_index)
{
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(text, &end, 10);
    if (!text[0] || !end || *end || parsed >= sizeof(k_cases) / sizeof(k_cases[0])) {
        return false;
    }
    case_index = static_cast<size_t>(parsed);
    return true;
}


void print_common(
    const char* mode,
    size_t case_index,
    int iterations,
    uint64_t elapsed_ns,
    mexce::backend_type backend,
    double result,
    bool correct)
{
    std::cout << std::setprecision(17)
        << "{\"mode\":\"" << mode
        << "\",\"case_index\":" << case_index
        << ",\"case\":\"" << k_cases[case_index].name
        << "\",\"iterations\":" << iterations
        << ",\"elapsed_ns\":" << elapsed_ns
        << ",\"per_expression_ns\":"
        << static_cast<double>(elapsed_ns) / iterations
        << ",\"backend\":" << static_cast<int>(backend)
        << ",\"peak_working_set_bytes\":" << peak_working_set_bytes()
        << ",\"result\":" << result
        << ",\"correct\":" << (correct ? "true" : "false")
        << ",\"compiler\":\"" << compiler_name()
        << "\",\"compiler_flags\":\"" << compiler_flags()
        << "\"}\n";
}


int prepare_case(size_t case_index, const char* program_path, const char* key_path)
{
    auto bundle = mexce::encode_protected_expression(
        k_cases[case_index].expression,
        protected_bindings(),
        mexce::Protected_math_mode::STRICT);
    const size_t artifact_bytes = bundle.program.size();
    write_bundle(std::move(bundle), program_path, key_path);
    std::cout
        << "{\"mode\":\"prepare_case\",\"case_index\":" << case_index
        << ",\"case\":\"" << k_cases[case_index].name
        << "\",\"artifact_bytes\":" << artifact_bytes
        << ",\"correct\":true}\n";
    return 0;
}


int clear_compile(size_t case_index)
{
    double x = 0.75;
    double y = 1.25;
    double z = 2.5;
    mexce::evaluator evaluator;
    bind_clear(evaluator, x, y, z);
    evaluator.set_expression(k_cases[case_index].expression);
    const uint64_t elapsed_ns = elapsed_nanoseconds([&] {
        for (int i = 0; i < k_compile_iterations; ++i) {
            evaluator.set_expression(k_cases[case_index].expression);
        }
    });
    const double result = evaluator.evaluate();
    const bool correct = correct_result(case_index, result, x, y, z);
    print_common(
        "clear_compile", case_index, k_compile_iterations, elapsed_ns,
        evaluator.get_backend(), result, correct);
    return correct ? 0 : 1;
}


int protected_compile(
    size_t case_index,
    const char* program_path,
    const char* key_path)
{
    const auto program = read_binary(program_path);
    const Key_material key(key_path);
    double x = 0.75;
    double y = 1.25;
    double z = 2.5;
    mexce::evaluator evaluator;
    bind_protected(evaluator, x, y, z);
    std::vector<mexce::Protected_expression_key> keys;
    keys.reserve(k_compile_iterations + 1);
    for (int i = 0; i <= k_compile_iterations; ++i) {
        keys.push_back(key.copy());
    }
    evaluator.set_protected_expression(
        program.data(), program.size(), std::move(keys[0]));
    const uint64_t elapsed_ns = elapsed_nanoseconds([&] {
        for (int i = 0; i < k_compile_iterations; ++i) {
            evaluator.set_protected_expression(
                program.data(),
                program.size(),
                std::move(keys[static_cast<size_t>(i + 1)]));
        }
    });
    const double result = evaluator.evaluate();
    const bool correct = correct_result(case_index, result, x, y, z);
    print_common(
        "protected_compile", case_index, k_compile_iterations, elapsed_ns,
        evaluator.get_backend(), result, correct);
    return correct ? 0 : 1;
}


int protected_encode(size_t case_index)
{
    auto bundle = mexce::encode_protected_expression(
        k_cases[case_index].expression,
        protected_bindings(),
        mexce::Protected_math_mode::STRICT);
    const uint64_t elapsed_ns = elapsed_nanoseconds([&] {
        for (int i = 0; i < k_compile_iterations; ++i) {
            bundle = mexce::encode_protected_expression(
                k_cases[case_index].expression,
                protected_bindings(),
                mexce::Protected_math_mode::STRICT);
        }
    });
    std::cout << std::setprecision(17)
        << "{\"mode\":\"protected_encode\",\"case_index\":" << case_index
        << ",\"case\":\"" << k_cases[case_index].name
        << "\",\"iterations\":" << k_compile_iterations
        << ",\"elapsed_ns\":" << elapsed_ns
        << ",\"per_expression_ns\":"
        << static_cast<double>(elapsed_ns) / k_compile_iterations
        << ",\"artifact_bytes\":" << bundle.program.size()
        << ",\"peak_working_set_bytes\":" << peak_working_set_bytes()
        << ",\"correct\":true"
        << ",\"compiler\":\"" << compiler_name()
        << "\",\"compiler_flags\":\"" << compiler_flags()
        << "\"}\n";
    return 0;
}


int evaluate_case(
    size_t case_index,
    bool protected_expression,
    const char* program_path,
    const char* key_path)
{
    double x = 0.75;
    double y = 1.25;
    double z = 2.5;
    mexce::evaluator evaluator;
    if (protected_expression) {
        const auto program = read_binary(program_path);
        const Key_material key(key_path);
        bind_protected(evaluator, x, y, z);
        evaluator.set_protected_expression(
            program.data(), program.size(), key.copy());
    }
    else {
        bind_clear(evaluator, x, y, z);
        evaluator.set_expression(k_cases[case_index].expression);
    }

    volatile double result = 0.0;
    for (int i = 0; i < k_evaluate_warmup_iterations; ++i) {
        result = evaluator.evaluate();
    }
    const uint64_t elapsed_ns = elapsed_nanoseconds([&] {
        for (int i = 0; i < k_evaluate_iterations; ++i) {
            result = evaluator.evaluate();
        }
    });
    const bool correct = correct_result(case_index, result, x, y, z);
    print_common(
        protected_expression ? "protected_evaluate" : "clear_evaluate",
        case_index,
        k_evaluate_iterations,
        elapsed_ns,
        evaluator.get_backend(),
        result,
        correct);
    return correct ? 0 : 1;
}


std::string maximum_expression()
{
    constexpr size_t k_variable_count = 8191;
    std::string expression = "sin(";
    expression.reserve(k_variable_count * 2 + 5);
    for (size_t i = 0; i < k_variable_count; ++i) {
        if (i != 0) {
            expression += '+';
        }
        expression += 'x';
    }
    expression += ')';
    return expression;
}


int prepare_resource(
    bool late_invalid,
    const char* program_path,
    const char* key_path)
{
    auto bundle = mexce::encode_protected_expression(
        maximum_expression(), {{"x", 0}}, mexce::Protected_math_mode::STRICT);
    if (late_invalid) {
        bundle.program.back() ^= 0x01;
    }
    const size_t artifact_bytes = bundle.program.size();
    const uint32_t records = record_count(bundle.program);
    const bool correct =
        artifact_bytes == k_expected_maximum_size &&
        records == k_expected_maximum_records;
    if (correct) {
        write_bundle(std::move(bundle), program_path, key_path);
    }
    std::cout
        << "{\"mode\":\""
        << (late_invalid ? "prepare_late_invalid" : "prepare_maximum_valid")
        << "\",\"record_count\":" << records
        << ",\"artifact_bytes\":" << artifact_bytes
        << ",\"correct\":" << (correct ? "true" : "false")
        << "}\n";
    return correct ? 0 : 1;
}


int resource_diagnostic(
    bool late_invalid,
    const char* program_path,
    const char* key_path)
{
    const auto program = read_binary(program_path);
    const Key_material key(key_path);
    const uint32_t records = record_count(program);
    double x = 0.75;
    mexce::evaluator evaluator;
    evaluator.bind_protected(x, 0);
    auto owned_key = key.copy();
    bool accepted = false;
    const uint64_t elapsed_ns = elapsed_nanoseconds([&] {
        try {
            evaluator.set_protected_expression(
                program.data(), program.size(), std::move(owned_key));
            accepted = true;
        }
        catch (const mexce::Protected_expression_error&) {
        }
    });
    const bool correct =
        program.size() == k_expected_maximum_size &&
        records == k_expected_maximum_records &&
        accepted != late_invalid;
    std::cout
        << "{\"mode\":\"" << (late_invalid ? "late_invalid" : "maximum_valid")
        << "\",\"record_count\":" << records
        << ",\"artifact_bytes\":" << program.size()
        << ",\"elapsed_ns\":" << elapsed_ns
        << ",\"peak_working_set_bytes\":" << peak_working_set_bytes()
        << ",\"accepted\":" << (accepted ? "true" : "false")
        << ",\"correct\":" << (correct ? "true" : "false")
        << ",\"compiler\":\"" << compiler_name()
        << "\",\"compiler_flags\":\"" << compiler_flags()
        << "\"}\n";
    return correct ? 0 : 1;
}


void print_usage()
{
    std::cerr
        << "usage: protected_benchmark <mode> [case-index] [program-file] [key-file]\n";
}


int run(int argc, char** argv)
{
    if (argc < 2) {
        print_usage();
        return 1;
    }
    const std::string mode = argv[1];
    if (mode == "case-count" && argc == 2) {
        std::cout << "{\"mode\":\"case_count\",\"count\":"
            << sizeof(k_cases) / sizeof(k_cases[0])
            << ",\"correct\":true}\n";
        return 0;
    }
    if ((mode == "prepare-maximum-valid" || mode == "prepare-late-invalid") &&
        argc == 4)
    {
        return prepare_resource(mode == "prepare-late-invalid", argv[2], argv[3]);
    }
    if ((mode == "maximum-valid" || mode == "late-invalid") && argc == 4) {
        return resource_diagnostic(mode == "late-invalid", argv[2], argv[3]);
    }

    size_t case_index = 0;
    if (argc < 3 || !parse_case(argv[2], case_index)) {
        print_usage();
        return 1;
    }
    if (mode == "prepare-case" && argc == 5) {
        return prepare_case(case_index, argv[3], argv[4]);
    }
    if (mode == "protected-compile" && argc == 5) {
        return protected_compile(case_index, argv[3], argv[4]);
    }
    if (mode == "protected-evaluate" && argc == 5) {
        return evaluate_case(case_index, true, argv[3], argv[4]);
    }
    if (argc != 3) {
        print_usage();
        return 1;
    }
    if (mode == "protected-encode") {
        return protected_encode(case_index);
    }
    if (mode == "clear-compile") {
        return clear_compile(case_index);
    }
    if (mode == "clear-evaluate") {
        return evaluate_case(case_index, false, nullptr, nullptr);
    }
    print_usage();
    return 1;
}


} // namespace


int main(int argc, char** argv)
{
    try {
        if (sodium_init() < 0) {
            throw std::runtime_error("protected benchmark requires libsodium 1.0.22");
        }
        return run(argc, argv);
    }
    catch (const std::exception& error) {
        std::cerr << "protected benchmark failed: " << error.what() << '\n';
        return 1;
    }
}

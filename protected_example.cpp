#ifdef _WIN32
    #define NOMINMAX
#endif

#include <mexce_protected.h>

#include <fstream>
#include <iterator>
#include <stdexcept>
#include <utility>
#include <vector>


std::vector<uint8_t> read_file(const char* path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Cannot open a protected-expression input file.");
    }
    return std::vector<uint8_t>(
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}


class Raw_key_wipe_guard
{
public:
    explicit Raw_key_wipe_guard(std::vector<uint8_t>& key)
    :
        m_key(key)
    {}

    ~Raw_key_wipe_guard()
    {
        if (!m_key.empty()) {
            sodium_memzero(m_key.data(), m_key.size());
        }
    }

private:
    std::vector<uint8_t>& m_key;
};


int run_protected_example(char* argv[])
{
    const std::vector<uint8_t> program = read_file(argv[1]);
    std::vector<uint8_t> key           = read_file(argv[2]);
    Raw_key_wipe_guard key_wipe(key);
    auto protected_key = mexce::Protected_expression_key::from_bytes(
        key.data(), key.size());

    double value = 2.0;
    mexce::evaluator evaluator;
    evaluator.bind_protected(value, 0);
    evaluator.set_protected_expression(
        program.data(), program.size(), std::move(protected_key));
    return evaluator.evaluate() == 3.0 ? 0 : 1;
}


int main(int argc, char* argv[])
{
    if (argc != 3) {
        return 2;
    }
    try {
        return run_protected_example(argv);
    }
    catch (const std::exception&) {
        return 1;
    }
}

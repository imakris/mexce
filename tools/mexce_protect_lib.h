#ifndef MEXCE_PROTECT_LIB_H
#define MEXCE_PROTECT_LIB_H

#include <functional>
#include <stdexcept>
#include <string>


namespace mexce {
namespace issuer {


#ifdef _WIN32
using Native_path = std::wstring;
#else
using Native_path = std::string;
#endif


enum class Test_failure
{
    NONE,
    KEY_TEMPORARY_COLLISION,
    BEFORE_PUBLICATION,
    AFTER_KEY_PUBLICATION,
};


struct Test_hooks
{
    std::function<void()> after_output_directories_opened;
    std::function<void(const Native_path&, const Native_path&)>
        after_temporaries_written;
};


class Issuer_error : public std::runtime_error
{
public:
    explicit Issuer_error(const std::string& message)
    :
        std::runtime_error(message)
    {}
};


void protect_expression(
    const Native_path& expression_path,
    const Native_path& schema_path,
    const Native_path& program_path,
    const Native_path& key_path,
    Test_failure failure = Test_failure::NONE,
    Test_hooks* hooks = nullptr);


} // namespace issuer
} // namespace mexce


#endif

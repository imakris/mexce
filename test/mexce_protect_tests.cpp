#ifdef _WIN32
    #define NOMINMAX
#endif

#include "mexce_protect_lib.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#ifdef _WIN32
    #include <aclapi.h>
    #include <windows.h>
#else
    #include <dirent.h>
    #include <sys/stat.h>
    #include <unistd.h>
#endif


namespace {


using mexce::issuer::Native_path;


class Test_suite
{
public:
    void expect(bool condition, const std::string& name)
    {
        if (!condition) {
            m_failures.push_back(name);
        }
    }

    void expect_issuer_error(
        const std::string& name,
        const std::function<void()>& operation)
    {
        try {
            operation();
            m_failures.push_back(name + ": expected Issuer_error");
        }
        catch (const mexce::issuer::Issuer_error&) {
        }
        catch (const std::exception& error) {
            m_failures.push_back(name + ": unexpected exception: " + error.what());
        }
    }

    void run(const std::string& name, const std::function<void()>& test)
    {
        try {
            test();
        }
        catch (const std::exception& error) {
            m_failures.push_back(name + ": unexpected exception: " + error.what());
        }
    }

    int finish() const
    {
        for (const auto& failure : m_failures) {
            std::fprintf(stderr, "FAIL: %s\n", failure.c_str());
        }
        if (!m_failures.empty()) {
            std::fprintf(stderr, "%zu issuer utility test(s) failed\n", m_failures.size());
            return 1;
        }
        std::printf("All issuer utility tests passed\n");
        return 0;
    }

private:
    std::vector<std::string> m_failures;
};


#ifdef _WIN32
Native_path join(const Native_path& directory, const wchar_t* name)
{
    return directory + L"\\" + name;
}


void write_bytes(const Native_path& path, const std::vector<uint8_t>& bytes)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("test setup could not create a file");
    }
    size_t offset = 0;
    while (offset < bytes.size()) {
        DWORD written = 0;
        if (!WriteFile(file, bytes.data() + offset,
                static_cast<DWORD>(bytes.size() - offset), &written, nullptr) ||
            written == 0)
        {
            CloseHandle(file);
            throw std::runtime_error("test setup could not write a file");
        }
        offset += written;
    }
    CloseHandle(file);
}


bool exists(const Native_path& path)
{
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}


void remove_path(const Native_path& path)
{
    if (!DeleteFileW(path.c_str())) {
        RemoveDirectoryW(path.c_str());
    }
}


size_t file_size(const Native_path& path)
{
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        return 0;
    }
    ULARGE_INTEGER size;
    size.HighPart = data.nFileSizeHigh;
    size.LowPart  = data.nFileSizeLow;
    return static_cast<size_t>(size.QuadPart);
}


bool owner_only(const Native_path& path)
{
    PSID owner = nullptr;
    PACL dacl  = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    const DWORD result = GetNamedSecurityInfoW(
        const_cast<wchar_t*>(path.c_str()), SE_FILE_OBJECT,
        OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
        &owner, nullptr, &dacl, nullptr, &descriptor);
    if (result != ERROR_SUCCESS || !owner || !dacl || !descriptor) {
        return false;
    }

    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;
    ACL_SIZE_INFORMATION information = {};
    const bool protected_dacl =
        GetSecurityDescriptorControl(descriptor, &control, &revision) &&
        (control & SE_DACL_PROTECTED) != 0;
    bool result_value = protected_dacl &&
        GetAclInformation(dacl, &information, sizeof(information), AclSizeInformation) &&
        information.AceCount == 1;
    if (result_value) {
        void* raw_ace = nullptr;
        result_value = GetAce(dacl, 0, &raw_ace) != FALSE;
        if (result_value) {
            const auto* ace = static_cast<const ACCESS_ALLOWED_ACE*>(raw_ace);
            const PSID ace_sid = const_cast<DWORD*>(&ace->SidStart);
            result_value = ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE &&
                (ace->Header.AceFlags & INHERITED_ACE) == 0 &&
                EqualSid(owner, ace_sid) != FALSE;
        }
    }
    LocalFree(descriptor);
    return result_value;
}


Native_path make_directory()
{
    wchar_t temporary[MAX_PATH];
    wchar_t candidate[MAX_PATH];
    if (!GetTempPathW(MAX_PATH, temporary) ||
        !GetTempFileNameW(temporary, L"mxc", 0, candidate) ||
        !DeleteFileW(candidate) || !CreateDirectoryW(candidate, nullptr))
    {
        throw std::runtime_error("test setup could not create a directory");
    }
    return candidate;
}


bool create_directory_link(const Native_path& link, const Native_path& target)
{
    constexpr DWORD k_allow_unprivileged = 0x2;
    return CreateSymbolicLinkW(link.c_str(), target.c_str(),
        SYMBOLIC_LINK_FLAG_DIRECTORY | k_allow_unprivileged) != FALSE;
}


size_t temporary_count(const Native_path& directory)
{
    WIN32_FIND_DATAW data;
    HANDLE search = FindFirstFileW((directory + L"\\.mexce-*.tmp").c_str(), &data);
    if (search == INVALID_HANDLE_VALUE) {
        return 0;
    }
    size_t count = 1;
    while (FindNextFileW(search, &data)) {
        ++count;
    }
    FindClose(search);
    return count;
}


#else


Native_path join(const Native_path& directory, const char* name)
{
    return directory + "/" + name;
}


void write_bytes(const Native_path& path, const std::vector<uint8_t>& bytes)
{
    FILE* file = std::fopen(path.c_str(), "wb");
    if (!file || (!bytes.empty() &&
        std::fwrite(bytes.data(), 1, bytes.size(), file) != bytes.size()))
    {
        if (file) {
            std::fclose(file);
        }
        throw std::runtime_error("test setup could not write a file");
    }
    std::fclose(file);
}


bool exists(const Native_path& path)
{
    struct stat status;
    return lstat(path.c_str(), &status) == 0;
}


void remove_path(const Native_path& path)
{
    if (unlink(path.c_str()) != 0) {
        rmdir(path.c_str());
    }
}


size_t file_size(const Native_path& path)
{
    struct stat status;
    return stat(path.c_str(), &status) == 0 ? static_cast<size_t>(status.st_size) : 0;
}


bool owner_only(const Native_path& path)
{
    struct stat status;
    return stat(path.c_str(), &status) == 0 && (status.st_mode & 0777) == 0600;
}


Native_path make_directory()
{
    std::vector<char> pattern(64);
    std::strcpy(pattern.data(), "/tmp/mexce-protect-XXXXXX");
    if (!mkdtemp(pattern.data())) {
        throw std::runtime_error("test setup could not create a directory");
    }
    return pattern.data();
}


bool create_directory_link(const Native_path& link, const Native_path& target)
{
    return symlink(target.c_str(), link.c_str()) == 0;
}


size_t temporary_count(const Native_path& directory)
{
    DIR* entries = opendir(directory.c_str());
    if (!entries) {
        return 0;
    }
    size_t count = 0;
    while (const dirent* entry = readdir(entries)) {
        const std::string name(entry->d_name);
        if (name.compare(0, 7, ".mexce-") == 0 &&
            name.size() >= 4 && name.compare(name.size() - 4, 4, ".tmp") == 0)
        {
            ++count;
        }
    }
    closedir(entries);
    return count;
}


#endif


std::vector<uint8_t> bytes(const std::string& value)
{
    return std::vector<uint8_t>(value.begin(), value.end());
}


class Fixture
{
public:
    Fixture()
    :
        root(make_directory()),
        expression(join(root,
#ifdef _WIN32
            L"expression.txt"
#else
            "expression.txt"
#endif
        )),
        schema(join(root,
#ifdef _WIN32
            L"schema.txt"
#else
            "schema.txt"
#endif
        )),
        program(join(root,
#ifdef _WIN32
            L"program.bin"
#else
            "program.bin"
#endif
        )),
        key(join(root,
#ifdef _WIN32
            L"key.bin"
#else
            "key.bin"
#endif
        ))
    {
        write_bytes(expression, bytes("x+1\n"));
        write_bytes(schema, bytes("x=0\n"));
    }

    ~Fixture()
    {
        remove_path(expression);
        remove_path(schema);
        remove_path(program);
        remove_path(key);
        remove_path(root);
    }

    void reset_outputs()
    {
        remove_path(program);
        remove_path(key);
    }

    void protect(mexce::issuer::Test_failure failure = mexce::issuer::Test_failure::NONE)
    {
        mexce::issuer::protect_expression(expression, schema, program, key, failure);
    }

    Native_path root;
    Native_path expression;
    Native_path schema;
    Native_path program;
    Native_path key;
};


void test_success_and_permissions(Test_suite& suite)
{
    Fixture fixture;
    fixture.protect();
    suite.expect(file_size(fixture.program) > 64, "success publishes a program");
    suite.expect(file_size(fixture.key) == 32, "success publishes exactly 32 key bytes");
    suite.expect(owner_only(fixture.key), "key output is owner-only");
    suite.expect(temporary_count(fixture.root) == 0,
        "success leaves no temporary files");

    fixture.reset_outputs();
    write_bytes(fixture.expression, bytes("1+2\r\n"));
    write_bytes(fixture.schema, {});
    fixture.protect();
    suite.expect(exists(fixture.program) && exists(fixture.key),
        "constant expression accepts an empty schema and terminal CRLF");

    fixture.reset_outputs();
    write_bytes(fixture.expression, bytes("x+1\n"));
    write_bytes(fixture.schema, bytes("x=0\n"));
    const Native_path program_directory = join(fixture.root,
#ifdef _WIN32
        L"program-output"
#else
        "program-output"
#endif
    );
    const Native_path key_directory = join(fixture.root,
#ifdef _WIN32
        L"key-output"
#else
        "key-output"
#endif
    );
#ifdef _WIN32
    const bool directories_created =
        CreateDirectoryW(program_directory.c_str(), nullptr) != FALSE &&
        CreateDirectoryW(key_directory.c_str(), nullptr) != FALSE;
    const Native_path split_program = join(program_directory, L"program.bin");
    const Native_path split_key     = join(key_directory, L"key.bin");
#else
    const bool directories_created = mkdir(program_directory.c_str(), 0700) == 0 &&
        mkdir(key_directory.c_str(), 0700) == 0;
    const Native_path split_program = join(program_directory, "program.bin");
    const Native_path split_key     = join(key_directory, "key.bin");
#endif
    suite.expect(directories_created, "distinct output parent setup");
    mexce::issuer::protect_expression(
        fixture.expression, fixture.schema, split_program, split_key);
    suite.expect(exists(split_program) && exists(split_key) && owner_only(split_key),
        "distinct verified output parents publish successfully");
    remove_path(split_program);
    remove_path(split_key);
    remove_path(program_directory);
    remove_path(key_directory);
}


void test_expression_validation(Test_suite& suite)
{
    Fixture fixture;
    const std::vector<std::pair<std::string, std::vector<uint8_t>>> cases = {
        {"expression BOM", {0xef, 0xbb, 0xbf, 'x'}},
        {"expression NUL", {'x', 0, '+', '1'}},
        {"expression size", std::vector<uint8_t>(1024 * 1024 + 1, '1')},
    };
    for (const auto& test_case : cases) {
        write_bytes(fixture.expression, test_case.second);
        suite.expect_issuer_error(test_case.first, [&] { fixture.protect(); });
        suite.expect(!exists(fixture.program) && !exists(fixture.key),
            test_case.first + " leaves no final output");
    }

    write_bytes(fixture.expression, bytes("secret_identifier+1"));
    try {
        fixture.protect();
        suite.expect(false, "parser diagnostic rejects unknown source identifier");
    }
    catch (const mexce::issuer::Issuer_error& error) {
        suite.expect(std::string(error.what()).find("secret_identifier") == std::string::npos,
            "parser diagnostic does not print source content");
    }
}


void test_schema_validation(Test_suite& suite)
{
    Fixture fixture;
    const std::vector<std::pair<std::string, std::vector<uint8_t>>> cases = {
        {"blank schema line", bytes("x=0\n\ny=1")},
        {"schema comment", bytes("# x=0")},
        {"schema built-in name", bytes("pi=0")},
        {"schema NUL", {'x', '=', '0', 0}},
        {"schema UTF-8", {0xc0, 0x80, '=', '0'}},
        {"duplicate name", bytes("x=0\nx=1")},
        {"duplicate slot", bytes("x=0\ny=0")},
        {"sparse slot", bytes("x=1")},
        {"oversized name", bytes(std::string(256, 'x') + "=0")},
        {"trailing slot characters", bytes("x=0 ")},
        {"slot overflow", bytes("x=4294967296")},
        {"schema size", std::vector<uint8_t>(256 * 1024 + 1, 'x')},
    };
    for (const auto& test_case : cases) {
        write_bytes(fixture.schema, test_case.second);
        suite.expect_issuer_error(test_case.first, [&] { fixture.protect(); });
        suite.expect(!exists(fixture.program) && !exists(fixture.key),
            test_case.first + " leaves no final output");
    }

    std::string excessive_bindings;
    for (size_t i = 0; i < 4097; ++i) {
        excessive_bindings += "v" + std::to_string(i) + "=" + std::to_string(i) + "\n";
    }
    write_bytes(fixture.schema, bytes(excessive_bindings));
    suite.expect_issuer_error("schema binding count", [&] { fixture.protect(); });
}


void test_output_validation(Test_suite& suite)
{
    Fixture fixture;
    suite.expect_issuer_error("identical output paths", [&] {
        mexce::issuer::protect_expression(
            fixture.expression, fixture.schema, fixture.program, fixture.program);
    });
    const Native_path aliased_program = join(fixture.root,
#ifdef _WIN32
        L".\\program.bin"
#else
        "./program.bin"
#endif
    );
    suite.expect_issuer_error("normalized identical output paths", [&] {
        mexce::issuer::protect_expression(
            fixture.expression, fixture.schema, fixture.program, aliased_program);
    });

    write_bytes(fixture.program, bytes("existing"));
    write_bytes(fixture.key, bytes("existing"));
    suite.expect_issuer_error("existing output paths", [&] { fixture.protect(); });
    suite.expect(file_size(fixture.program) == 8 && file_size(fixture.key) == 8,
        "existing outputs are not overwritten");

    fixture.reset_outputs();
    write_bytes(fixture.key, bytes("orphan"));
    suite.expect_issuer_error("partial final state", [&] { fixture.protect(); });
    suite.expect(!exists(fixture.program) && file_size(fixture.key) == 6,
        "partial final state is not changed");

    fixture.reset_outputs();
    write_bytes(fixture.program, bytes("orphan"));
    suite.expect_issuer_error("inverse partial final state", [&] { fixture.protect(); });
    suite.expect(file_size(fixture.program) == 6 && !exists(fixture.key),
        "inverse partial final state is not changed");
}


void test_publication_failures(Test_suite& suite)
{
    Fixture fixture;
#ifdef _WIN32
    const Native_path collision = join(fixture.root,
        L".mexce-test-collision.tmp"
    );
    write_bytes(collision, bytes("caller-owned"));
    suite.expect_issuer_error("exclusive temporary collision", [&] {
        fixture.protect(mexce::issuer::Test_failure::KEY_TEMPORARY_COLLISION);
    });
    suite.expect(file_size(collision) == 12,
        "temporary collision does not delete or overwrite an existing file");
    suite.expect(temporary_count(fixture.root) == 1,
        "temporary collision cleans only the temporary owned by the invocation");
    remove_path(collision);
#endif

    suite.expect_issuer_error("failure before publication", [&] {
        fixture.protect(mexce::issuer::Test_failure::BEFORE_PUBLICATION);
    });
    suite.expect(!exists(fixture.program) && !exists(fixture.key),
        "pre-publication failure leaves no final output");
    suite.expect(temporary_count(fixture.root) == 0,
        "pre-publication failure removes temporary files");

    suite.expect_issuer_error("failure after key publication", [&] {
        fixture.protect(mexce::issuer::Test_failure::AFTER_KEY_PUBLICATION);
    });
    suite.expect(!exists(fixture.program) && file_size(fixture.key) == 32,
        "post-key failure leaves only an owner-only raw key");
    suite.expect(temporary_count(fixture.root) == 0,
        "post-key failure removes the program temporary");
    suite.expect(owner_only(fixture.key), "orphan key remains owner-only");
    suite.expect_issuer_error("orphan recovery detection", [&] { fixture.protect(); });
    suite.expect(!exists(fixture.program) && file_size(fixture.key) == 32,
        "recovery detection does not overwrite the orphan key");
}


void test_reparse_traversal(Test_suite& suite)
{
    Fixture fixture;
    const Native_path real_directory = join(fixture.root,
#ifdef _WIN32
        L"real"
#else
        "real"
#endif
    );
    const Native_path linked_directory = join(fixture.root,
#ifdef _WIN32
        L"linked"
#else
        "linked"
#endif
    );
#ifdef _WIN32
    const bool made_directory = CreateDirectoryW(real_directory.c_str(), nullptr) != FALSE;
#else
    const bool made_directory = mkdir(real_directory.c_str(), 0700) == 0;
#endif
    suite.expect(made_directory, "reparse test creates a real directory");
    const bool made_link = made_directory &&
        create_directory_link(linked_directory, real_directory);
    suite.expect(made_link, "reparse test creates a directory link");
    if (made_link) {
        const Native_path linked_program = join(linked_directory,
#ifdef _WIN32
            L"program.bin"
#else
            "program.bin"
#endif
        );
        suite.expect_issuer_error("output reparse traversal", [&] {
            mexce::issuer::protect_expression(
                fixture.expression, fixture.schema, linked_program, fixture.key);
        });
        suite.expect(!exists(fixture.key), "reparse rejection leaves no key output");
    }
    remove_path(linked_directory);
    remove_path(real_directory);
}


void test_ancestor_swap(Test_suite& suite)
{
    Fixture fixture;
#ifdef _WIN32
    const Native_path moved    = fixture.root + L"-moved";
    const Native_path redirect = fixture.root + L"-redirect";
    bool swap_completed = false;
    mexce::issuer::Test_hooks hooks;
    hooks.after_output_directories_opened = [&] {
        swap_completed = MoveFileW(fixture.root.c_str(), moved.c_str()) != FALSE &&
            CreateDirectoryW(redirect.c_str(), nullptr) != FALSE &&
            create_directory_link(fixture.root, redirect);
    };
    mexce::issuer::protect_expression(
        fixture.expression, fixture.schema, fixture.program, fixture.key,
        mexce::issuer::Test_failure::NONE, &hooks);
    suite.expect(swap_completed, "Windows ancestor replacement seam completes");
    suite.expect(!exists(fixture.program) && !exists(fixture.key),
        "Windows publication is not redirected through a replacement ancestor");
    suite.expect(exists(join(moved, L"program.bin")) && exists(join(moved, L"key.bin")),
        "Windows publication stays bound to the verified directory");
    remove_path(fixture.root);
    remove_path(redirect);
    suite.expect(MoveFileW(moved.c_str(), fixture.root.c_str()) != FALSE,
        "Windows ancestor replacement test restores its fixture");
#else
    const Native_path moved   = fixture.root + "-moved";
    const Native_path redirect = fixture.root + "-redirect";
    bool swap_completed = false;
    mexce::issuer::Test_hooks hooks;
    hooks.after_output_directories_opened = [&] {
        swap_completed = rename(fixture.root.c_str(), moved.c_str()) == 0 &&
            mkdir(redirect.c_str(), 0700) == 0 &&
            symlink(redirect.c_str(), fixture.root.c_str()) == 0;
    };
    mexce::issuer::protect_expression(
        fixture.expression, fixture.schema, fixture.program, fixture.key,
        mexce::issuer::Test_failure::NONE, &hooks);
    suite.expect(swap_completed, "Linux ancestor replacement seam completes");
    suite.expect(!exists(fixture.program) && !exists(fixture.key),
        "Linux publication is not redirected through a replacement ancestor");
    suite.expect(exists(join(moved, "program.bin")) && exists(join(moved, "key.bin")),
        "Linux publication stays bound to the verified directory");
    remove_path(fixture.root);
    remove_path(redirect);
    suite.expect(rename(moved.c_str(), fixture.root.c_str()) == 0,
        "Linux ancestor replacement test restores its fixture");
#endif
}


void test_temporary_replacement(Test_suite& suite)
{
    Fixture fixture;
    Native_path caller_file;
#ifdef _WIN32
    bool replacement_blocked = false;
#endif
    mexce::issuer::Test_hooks hooks;
    hooks.after_temporaries_written = [&](const Native_path&, const Native_path& key) {
#ifdef _WIN32
        caller_file = key;
        replacement_blocked = DeleteFileW(key.c_str()) == FALSE;
#else
        (void)key;
        caller_file = fixture.key;
        write_bytes(caller_file, bytes("caller-owned"));
#endif
    };
#ifdef _WIN32
    mexce::issuer::protect_expression(
        fixture.expression, fixture.schema, fixture.program, fixture.key,
        mexce::issuer::Test_failure::NONE, &hooks);
    suite.expect(replacement_blocked,
        "open Windows temporary identity blocks pathname replacement");
    suite.expect(exists(fixture.program) && exists(fixture.key),
        "blocked Windows temporary replacement preserves publication");
#else
    suite.expect_issuer_error("Linux final-name collision", [&] {
        mexce::issuer::protect_expression(
            fixture.expression, fixture.schema, fixture.program, fixture.key,
            mexce::issuer::Test_failure::NONE, &hooks);
    });
    suite.expect(file_size(caller_file) == 12,
        "Linux final-name collision does not overwrite the caller file");
    suite.expect(!exists(fixture.program) && exists(fixture.key),
        "Linux final-name collision prevents program publication");
    suite.expect(temporary_count(fixture.root) == 0,
        "Linux unpublished unnamed temporaries leave no directory entries");
    remove_path(caller_file);
#endif
}


#ifndef _WIN32
void test_unsupported_unnamed_temporary_filesystem(Test_suite& suite)
{
    Fixture fixture;
    const std::string suffix = std::to_string(static_cast<long long>(getpid()));
    const Native_path program = "/proc/mexce-program-" + suffix;
    const Native_path key     = "/proc/mexce-key-" + suffix;
    try {
        mexce::issuer::protect_expression(
            fixture.expression, fixture.schema, program, key);
        suite.expect(false, "unsupported unnamed-temporary filesystem rejects");
    }
    catch (const mexce::issuer::Issuer_error& error) {
        suite.expect(!std::string(error.what()).empty(),
            "unsafe output filesystem reports the rejection");
        suite.expect(!exists(program) && !exists(key),
            "unsafe output filesystem leaves no output");
    }
}
#endif


} // namespace


int main()
{
    Test_suite suite;
    suite.run("success and permissions", [&] { test_success_and_permissions(suite); });
    suite.run("expression validation", [&] { test_expression_validation(suite); });
    suite.run("schema validation", [&] { test_schema_validation(suite); });
    suite.run("output validation", [&] { test_output_validation(suite); });
    suite.run("publication failures", [&] { test_publication_failures(suite); });
    suite.run("reparse traversal", [&] { test_reparse_traversal(suite); });
    suite.run("ancestor swap", [&] { test_ancestor_swap(suite); });
    suite.run("temporary replacement", [&] { test_temporary_replacement(suite); });
#ifndef _WIN32
    suite.run("unsupported unnamed temporary filesystem", [&] {
        test_unsupported_unnamed_temporary_filesystem(suite);
    });
#endif
    return suite.finish();
}

#ifdef _WIN32
    #define NOMINMAX
#else
    #ifndef _GNU_SOURCE
        #define _GNU_SOURCE
    #endif
#endif

#include "mexce_protect_lib.h"

#include "mexce_protected_encoder.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <set>
#include <vector>

#ifdef _WIN32
    #include <windows.h>
    #include <winternl.h>
#else
    #include <fcntl.h>
    #include <sys/stat.h>
    #include <unistd.h>
#endif


namespace mexce {
namespace issuer {
namespace {


constexpr size_t k_expression_limit = 1024 * 1024;
constexpr size_t k_schema_limit     = 256 * 1024;


struct Output_paths
{
    Native_path program;
    Native_path key;
};


bool valid_utf8(const std::vector<uint8_t>& bytes)
{
    size_t i = 0;
    while (i < bytes.size()) {
        const uint8_t first = bytes[i++];
        if (first <= 0x7f) {
            continue;
        }

        uint32_t value;
        size_t   continuation_count;
        uint32_t minimum;
        if ((first & 0xe0) == 0xc0) {
            value              = first & 0x1f;
            continuation_count = 1;
            minimum            = 0x80;
        }
        else
        if ((first & 0xf0) == 0xe0) {
            value              = first & 0x0f;
            continuation_count = 2;
            minimum            = 0x800;
        }
        else
        if ((first & 0xf8) == 0xf0) {
            value              = first & 0x07;
            continuation_count = 3;
            minimum            = 0x10000;
        }
        else {
            return false;
        }

        if (continuation_count > bytes.size() - i) {
            return false;
        }
        for (size_t j = 0; j < continuation_count; ++j) {
            const uint8_t next = bytes[i++];
            if ((next & 0xc0) != 0x80) {
                return false;
            }
            value = (value << 6) | (next & 0x3f);
        }
        if (value < minimum || value > 0x10ffff ||
            (value >= 0xd800 && value <= 0xdfff))
        {
            return false;
        }
    }
    return true;
}


std::vector<Protected_binding> parse_schema(const std::vector<uint8_t>& bytes)
{
    if (std::find(bytes.begin(), bytes.end(), 0) != bytes.end()) {
        throw Issuer_error("the binding schema contains a NUL byte");
    }
    if (!valid_utf8(bytes)) {
        throw Issuer_error("the binding schema is not valid UTF-8");
    }

    std::vector<Protected_binding> bindings;
    std::set<std::string> names;
    std::set<uint32_t> slots;
    size_t line_start = 0;
    while (line_start < bytes.size()) {
        size_t line_end = line_start;
        while (line_end < bytes.size() && bytes[line_end] != '\n') {
            ++line_end;
        }
        size_t content_end = line_end;
        if (content_end > line_start && bytes[content_end - 1] == '\r') {
            --content_end;
        }
        if (content_end == line_start) {
            throw Issuer_error("the binding schema contains a blank line");
        }

        const auto equals = std::find(
            bytes.begin() + line_start, bytes.begin() + content_end, '=');
        if (equals == bytes.begin() + content_end) {
            throw Issuer_error("the binding schema contains an invalid entry");
        }
        const size_t equals_offset = static_cast<size_t>(equals - bytes.begin());
        const std::string name(
            bytes.begin() + line_start, bytes.begin() + equals_offset);
        if (name.empty() || name.size() > 255) {
            throw Issuer_error("the binding schema contains an invalid name");
        }
        if (!impl::valid_protected_name(name) ||
            impl::function_map().find(name) != impl::function_map().end() ||
            impl::built_in_constants_map().find(name) !=
                impl::built_in_constants_map().end())
        {
            throw Issuer_error("the binding schema contains an invalid name");
        }

        const size_t slot_start = equals_offset + 1;
        if (slot_start == content_end) {
            throw Issuer_error("the binding schema contains an invalid slot");
        }
        uint64_t slot = 0;
        for (size_t i = slot_start; i < content_end; ++i) {
            if (bytes[i] < '0' || bytes[i] > '9') {
                throw Issuer_error("the binding schema contains trailing characters");
            }
            const uint32_t digit = bytes[i] - '0';
            if (slot > ((std::numeric_limits<uint32_t>::max)() - digit) / 10) {
                throw Issuer_error("the binding schema slot is too large");
            }
            slot = slot * 10 + digit;
        }

        const uint32_t slot_value = static_cast<uint32_t>(slot);
        if (!names.insert(name).second) {
            throw Issuer_error("the binding schema contains a duplicate name");
        }
        if (!slots.insert(slot_value).second) {
            throw Issuer_error("the binding schema contains a duplicate slot");
        }
        bindings.push_back({name, slot_value});
        if (bindings.size() > 4096) {
            throw Issuer_error("the binding schema contains too many entries");
        }
        line_start = line_end == bytes.size() ? line_end : line_end + 1;
    }
    for (uint32_t slot = 0; slot < bindings.size(); ++slot) {
        if (slots.find(slot) == slots.end()) {
            throw Issuer_error("the binding schema contains sparse slots");
        }
    }
    return bindings;
}


std::string expression_text(std::vector<uint8_t> bytes)
{
    if (bytes.size() >= 3 && bytes[0] == 0xef &&
        bytes[1] == 0xbb && bytes[2] == 0xbf)
    {
        throw Issuer_error("the expression starts with a UTF-8 BOM");
    }
    if (std::find(bytes.begin(), bytes.end(), 0) != bytes.end()) {
        throw Issuer_error("the expression contains a NUL byte");
    }
    if (!bytes.empty() && bytes.back() == '\n') {
        bytes.pop_back();
        if (!bytes.empty() && bytes.back() == '\r') {
            bytes.pop_back();
        }
    }
    return std::string(bytes.begin(), bytes.end());
}


#ifdef _WIN32


class Handle
{
public:
    explicit Handle(HANDLE value = INVALID_HANDLE_VALUE)
    :
        m_value(value)
    {}

    ~Handle() { close(); }

    Handle(Handle&& other)
    :
        m_value(other.m_value)
    {
        other.m_value = INVALID_HANDLE_VALUE;
    }

    Handle& operator=(Handle&& other)
    {
        if (this != &other) {
            close();
            m_value       = other.m_value;
            other.m_value = INVALID_HANDLE_VALUE;
        }
        return *this;
    }

    HANDLE get() const { return m_value; }
    bool valid() const { return m_value != INVALID_HANDLE_VALUE; }

    void close()
    {
        if (valid()) {
            CloseHandle(m_value);
            m_value = INVALID_HANDLE_VALUE;
        }
    }

private:
    Handle(const Handle&);
    Handle& operator=(const Handle&);

    HANDLE m_value;
};


NTSTATUS create_relative(
    HANDLE parent,
    const Native_path& name,
    ACCESS_MASK access,
    ULONG file_attributes,
    ULONG share_access,
    ULONG disposition,
    ULONG options,
    PSECURITY_DESCRIPTOR security_descriptor,
    HANDLE& file)
{
    UNICODE_STRING native_name;
    native_name.Buffer        = const_cast<wchar_t*>(name.data());
    native_name.Length        = static_cast<USHORT>(name.size() * sizeof(wchar_t));
    native_name.MaximumLength = native_name.Length;
    OBJECT_ATTRIBUTES attributes;
    InitializeObjectAttributes(
        &attributes, &native_name, OBJ_CASE_INSENSITIVE, parent,
        security_descriptor);
    IO_STATUS_BLOCK status;
    using Nt_create_file = NTSTATUS (NTAPI*)(
        PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK,
        PLARGE_INTEGER, ULONG, ULONG, ULONG, ULONG, PVOID, ULONG);
    const auto create_file = reinterpret_cast<Nt_create_file>(GetProcAddress(
        GetModuleHandleW(L"ntdll.dll"), "NtCreateFile"));
    file = INVALID_HANDLE_VALUE;
    return create_file
        ? create_file(&file, access, &attributes, &status, nullptr,
            file_attributes, share_access, disposition, options, nullptr, 0)
        : static_cast<NTSTATUS>(-1);
}


std::vector<uint8_t> read_limited(const Native_path& path, size_t limit, const char* kind)
{
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
    {
        throw Issuer_error(std::string("cannot safely open the ") + kind + " file");
    }

    Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!file.valid()) {
        throw Issuer_error(std::string("cannot open the ") + kind + " file");
    }

    LARGE_INTEGER size;
    if (!GetFileSizeEx(file.get(), &size) || size.QuadPart < 0 ||
        static_cast<uint64_t>(size.QuadPart) > limit)
    {
        throw Issuer_error(std::string("the ") + kind + " file exceeds its size limit");
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(size.QuadPart));
    size_t offset = 0;
    while (offset < bytes.size()) {
        DWORD amount = 0;
        const DWORD request = static_cast<DWORD>(std::min<size_t>(
            bytes.size() - offset, (std::numeric_limits<DWORD>::max)()));
        if (!ReadFile(file.get(), bytes.data() + offset, request, &amount, nullptr) ||
            amount == 0)
        {
            throw Issuer_error(std::string("cannot read the ") + kind + " file");
        }
        offset += amount;
    }
    uint8_t trailing = 0;
    DWORD trailing_size = 0;
    if (!ReadFile(file.get(), &trailing, 1, &trailing_size, nullptr)) {
        throw Issuer_error(std::string("cannot finish reading the ") + kind + " file");
    }
    if (trailing_size != 0) {
        throw Issuer_error(std::string("the ") + kind + " file changed while reading");
    }
    return bytes;
}


Native_path absolute_path(const Native_path& path)
{
    const DWORD needed = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (needed == 0) {
        throw Issuer_error("an output path cannot be normalized");
    }
    std::vector<wchar_t> buffer(needed);
    const DWORD written = GetFullPathNameW(
        path.c_str(), needed, buffer.data(), nullptr);
    if (written == 0 || written >= needed) {
        throw Issuer_error("an output path cannot be normalized");
    }
    Native_path result(buffer.data(), written);
    std::replace(result.begin(), result.end(), L'/', L'\\');
    return result;
}


Native_path parent_path(const Native_path& path)
{
    const size_t slash = path.find_last_of(L'\\');
    if (slash == Native_path::npos || slash + 1 == path.size()) {
        throw Issuer_error("an output path has no filename");
    }
    Native_path parent = path.substr(0, slash);
    wchar_t volume[MAX_PATH];
    if (GetVolumePathNameW(path.c_str(), volume, MAX_PATH)) {
        Native_path root(volume);
        Native_path parent_with_separator = parent + L'\\';
        if (CompareStringOrdinal(parent_with_separator.c_str(), -1,
                root.c_str(), -1, TRUE) == CSTR_EQUAL)
        {
            parent = root;
        }
    }
    return parent;
}


void validate_windows_filename(const Native_path& path)
{
    if (path.compare(0, 4, L"\\\\?\\") == 0 ||
        path.compare(0, 4, L"\\\\.\\") == 0)
    {
        throw Issuer_error("an output path uses an unsupported device namespace");
    }
    const size_t slash = path.find_last_of(L'\\');
    const Native_path filename = path.substr(slash + 1);
    if (filename.empty() || filename.back() == L'.' || filename.back() == L' ' ||
        filename.find_first_of(L"<>:\"|?*") != Native_path::npos)
    {
        throw Issuer_error("an output filename has indeterminate identity");
    }
    for (const wchar_t value : filename) {
        if (value < 32) {
            throw Issuer_error("an output filename has indeterminate identity");
        }
    }

    Native_path stem = filename.substr(0, filename.find(L'.'));
    for (wchar_t& value : stem) {
        if (value >= L'a' && value <= L'z') {
            value = static_cast<wchar_t>(value - L'a' + L'A');
        }
    }
    const bool numbered_device = stem.size() == 4 &&
        (stem.compare(0, 3, L"COM") == 0 || stem.compare(0, 3, L"LPT") == 0) &&
        stem[3] >= L'1' && stem[3] <= L'9';
    if (stem == L"CON" || stem == L"PRN" || stem == L"AUX" || stem == L"NUL" ||
        numbered_device)
    {
        throw Issuer_error("an output filename uses a reserved device name");
    }
}


bool same_path(const Native_path& left, const Native_path& right)
{
    return CompareStringOrdinal(
        left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_EQUAL;
}


Handle open_pinned_directory(const Native_path& path)
{
    Handle directory(CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    FILE_ATTRIBUTE_TAG_INFO attributes;
    if (!directory.valid() || !GetFileInformationByHandleEx(
            directory.get(), FileAttributeTagInfo, &attributes, sizeof(attributes)) ||
        !(attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
    {
        throw Issuer_error("an output path traverses an unsafe component");
    }
    return directory;
}


Handle open_pinned_child(HANDLE parent, const Native_path& name)
{
    HANDLE raw_directory = INVALID_HANDLE_VALUE;
    const NTSTATUS result = create_relative(
        parent, name, FILE_READ_ATTRIBUTES | SYNCHRONIZE,
        FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ | FILE_SHARE_WRITE,
        FILE_OPEN, FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT |
            FILE_OPEN_REPARSE_POINT,
        nullptr, raw_directory);
    Handle directory(result < 0 ? INVALID_HANDLE_VALUE : raw_directory);
    FILE_ATTRIBUTE_TAG_INFO file_attributes;
    if (!directory.valid() || !GetFileInformationByHandleEx(
            directory.get(), FileAttributeTagInfo,
            &file_attributes, sizeof(file_attributes)) ||
        !(file_attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (file_attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
    {
        throw Issuer_error("an output path traverses an unsafe component");
    }
    return directory;
}


std::vector<Handle> pin_ancestors(const Native_path& path)
{
    wchar_t volume[MAX_PATH];
    if (!GetVolumePathNameW(path.c_str(), volume, MAX_PATH)) {
        throw Issuer_error("an output path volume cannot be identified");
    }
    const Native_path root(volume);
    const Native_path parent = parent_path(path);
    if (parent.size() < root.size()) {
        throw Issuer_error("an output path parent cannot be identified");
    }

    std::vector<Handle> directories;
    directories.push_back(open_pinned_directory(root));
    size_t start = root.size();
    while (start < parent.size()) {
        const size_t slash = parent.find(L'\\', start);
        const size_t end   = slash == Native_path::npos ? parent.size() : slash;
        if (end > start) {
            const Native_path component = parent.substr(start, end - start);
            directories.push_back(
                open_pinned_child(directories.back().get(), component));
        }
        if (slash == Native_path::npos) {
            break;
        }
        start = slash + 1;
    }
    return directories;
}


bool entry_exists(HANDLE directory, const Native_path& name)
{
    HANDLE raw_file = INVALID_HANDLE_VALUE;
    const NTSTATUS result = create_relative(
        directory, name, FILE_READ_ATTRIBUTES | SYNCHRONIZE,
        FILE_ATTRIBUTE_NORMAL,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        FILE_OPEN, FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_REPARSE_POINT,
        nullptr, raw_file);
    Handle file(result < 0 ? INVALID_HANDLE_VALUE : raw_file);
    if (file.valid()) {
        return true;
    }
    using Status_to_error = ULONG (NTAPI*)(NTSTATUS);
    const auto status_to_error = reinterpret_cast<Status_to_error>(GetProcAddress(
        GetModuleHandleW(L"ntdll.dll"), "RtlNtStatusToDosError"));
    const ULONG error = status_to_error ? status_to_error(result) : ERROR_GEN_FAILURE;
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
        return false;
    }
    throw Issuer_error("an output path identity is indeterminate");
}


struct Restricted_security
{
    Restricted_security()
    :
        attributes(),
        descriptor(),
        token_user(),
        acl()
    {
        Handle token;
        HANDLE raw_token = INVALID_HANDLE_VALUE;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw_token)) {
            throw Issuer_error("cannot establish owner-only key permissions");
        }
        token = Handle(raw_token);

        DWORD user_size = 0;
        GetTokenInformation(token.get(), TokenUser, nullptr, 0, &user_size);
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
            throw Issuer_error("cannot establish owner-only key permissions");
        }
        token_user.resize(user_size);
        if (!GetTokenInformation(
                token.get(), TokenUser, token_user.data(), user_size, &user_size))
        {
            throw Issuer_error("cannot establish owner-only key permissions");
        }
        const PSID sid = reinterpret_cast<TOKEN_USER*>(token_user.data())->User.Sid;
        acl.resize(sizeof(ACL) + sizeof(ACCESS_ALLOWED_ACE) + GetLengthSid(sid));
        PACL native_acl = reinterpret_cast<PACL>(acl.data());
        if (!InitializeAcl(native_acl, static_cast<DWORD>(acl.size()), ACL_REVISION) ||
            !AddAccessAllowedAce(
                native_acl, ACL_REVISION, GENERIC_ALL, sid) ||
            !InitializeSecurityDescriptor(
                &descriptor, SECURITY_DESCRIPTOR_REVISION) ||
            !SetSecurityDescriptorOwner(&descriptor, sid, FALSE) ||
            !SetSecurityDescriptorDacl(&descriptor, TRUE, native_acl, FALSE))
        {
            throw Issuer_error("cannot establish owner-only key permissions");
        }
        if (!SetSecurityDescriptorControl(
                &descriptor, SE_DACL_PROTECTED, SE_DACL_PROTECTED))
        {
            throw Issuer_error("cannot protect owner-only key permissions");
        }
        attributes.nLength              = sizeof(attributes);
        attributes.lpSecurityDescriptor = &descriptor;
        attributes.bInheritHandle       = FALSE;
    }

    SECURITY_ATTRIBUTES        attributes;
    SECURITY_DESCRIPTOR        descriptor;
    std::vector<uint8_t>       token_user;
    std::vector<uint8_t>       acl;
};


class Pinned_outputs
{
public:
    explicit Pinned_outputs(const Output_paths& paths)
    :
        m_paths(paths),
        m_program_directories(pin_ancestors(paths.program)),
        m_key_directories(pin_ancestors(paths.key)),
        m_program_name(paths.program.substr(paths.program.find_last_of(L'\\') + 1)),
        m_key_name(paths.key.substr(paths.key.find_last_of(L'\\') + 1))
    {
        BY_HANDLE_FILE_INFORMATION program_parent;
        BY_HANDLE_FILE_INFORMATION key_parent;
        if (!GetFileInformationByHandle(
                m_program_directories.back().get(), &program_parent) ||
            !GetFileInformationByHandle(m_key_directories.back().get(), &key_parent))
        {
            throw Issuer_error("an output path identity is indeterminate");
        }
        const bool same_parent =
            program_parent.dwVolumeSerialNumber == key_parent.dwVolumeSerialNumber &&
            program_parent.nFileIndexHigh == key_parent.nFileIndexHigh &&
            program_parent.nFileIndexLow == key_parent.nFileIndexLow;
        if (same_path(m_paths.program, m_paths.key) ||
            (same_parent && same_path(m_program_name, m_key_name)))
        {
            throw Issuer_error("the program and key outputs identify the same path");
        }

        const bool program_exists = entry_exists(
            m_program_directories.back().get(), m_program_name);
        const bool key_exists = entry_exists(
            m_key_directories.back().get(), m_key_name);
        if (program_exists != key_exists) {
            throw Issuer_error("a partial final output state already exists");
        }
        if (program_exists) {
            throw Issuer_error("the output destinations already exist");
        }
    }

    const Native_path& program() const { return m_paths.program; }
    const Native_path& key() const { return m_paths.key; }
    const Native_path& program_name() const { return m_program_name; }
    const Native_path& key_name() const { return m_key_name; }
    HANDLE program_directory() const { return m_program_directories.back().get(); }
    HANDLE key_directory() const { return m_key_directories.back().get(); }

private:
    Output_paths        m_paths;
    std::vector<Handle> m_program_directories;
    std::vector<Handle> m_key_directories;
    Native_path         m_program_name;
    Native_path         m_key_name;
};


Native_path temporary_path(const Native_path& final_path)
{
    uint8_t random[16];
    randombytes_buf(random, sizeof(random));
    static const wchar_t hex[] = L"0123456789abcdef";
    Native_path path = parent_path(final_path) + L"\\.mexce-";
    for (size_t i = 0; i < sizeof(random); ++i) {
        path += hex[random[i] >> 4];
        path += hex[random[i] & 0x0f];
    }
    path += L".tmp";
    return path;
}


Native_path collision_temporary_path(const Native_path& final_path)
{
    return parent_path(final_path) + L"\\.mexce-test-collision.tmp";
}


class Temporary_file
{
public:
    Temporary_file(
        HANDLE directory,
        const Native_path& path,
        bool restricted)
    :
        m_path(path),
        m_file(),
        m_cleanup_active(false)
    {
        std::unique_ptr<Restricted_security> security;
        if (restricted) {
            security.reset(new Restricted_security);
        }

        const Native_path name = path.substr(path.find_last_of(L'\\') + 1);
        HANDLE raw_file = INVALID_HANDLE_VALUE;
        const NTSTATUS result = create_relative(
            directory, name, GENERIC_WRITE | DELETE | SYNCHRONIZE,
            FILE_ATTRIBUTE_TEMPORARY, 0, FILE_CREATE,
            FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT |
                FILE_OPEN_REPARSE_POINT,
            restricted ? security->attributes.lpSecurityDescriptor : nullptr,
            raw_file);
        if (result < 0 || raw_file == INVALID_HANDLE_VALUE) {
            throw Issuer_error("cannot exclusively create an output temporary file");
        }
        m_file = Handle(raw_file);
        m_cleanup_active = true;
    }

    ~Temporary_file()
    {
        if (m_cleanup_active) {
            FILE_DISPOSITION_INFO disposition = {};
            disposition.DeleteFile = TRUE;
            SetFileInformationByHandle(
                m_file.get(), FileDispositionInfo, &disposition, sizeof(disposition));
        }
    }

    HANDLE get() const { return m_file.get(); }
    const Native_path& path() const { return m_path; }

    void publish(HANDLE directory, const Native_path& final_name)
    {
        const DWORD name_size = static_cast<DWORD>(final_name.size() * sizeof(wchar_t));
        struct Native_rename_information
        {
            BOOLEAN ReplaceIfExists;
            HANDLE  RootDirectory;
            ULONG   FileNameLength;
            WCHAR   FileName[1];
        };
        std::vector<uint8_t> storage(sizeof(Native_rename_information) + name_size);
        auto* information =
            reinterpret_cast<Native_rename_information*>(storage.data());
        std::memset(information, 0, storage.size());
        information->ReplaceIfExists = FALSE;
        information->RootDirectory   = directory;
        information->FileNameLength  = name_size;
        std::memcpy(information->FileName, final_name.data(), name_size);
        using Nt_set_information_file = NTSTATUS (NTAPI*)(
            HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG, FILE_INFORMATION_CLASS);
        const auto set_information =
            reinterpret_cast<Nt_set_information_file>(GetProcAddress(
                GetModuleHandleW(L"ntdll.dll"), "NtSetInformationFile"));
        IO_STATUS_BLOCK status;
        // The public SDK's reduced FILE_INFORMATION_CLASS declaration omits
        // native FileRenameInformation, whose stable class number is 10.
        const auto rename_information_class =
            static_cast<FILE_INFORMATION_CLASS>(10);
        const NTSTATUS result = set_information
            ? set_information(m_file.get(), &status, information,
                static_cast<ULONG>(storage.size()), rename_information_class)
            : static_cast<NTSTATUS>(-1);
        if (result < 0)
        {
            throw Issuer_error("cannot publish an output without overwriting");
        }
        m_cleanup_active = false;
    }

private:
    Temporary_file(const Temporary_file&);
    Temporary_file& operator=(const Temporary_file&);

    Native_path m_path;
    Handle      m_file;
    bool        m_cleanup_active;
};


void write_all(HANDLE file, const uint8_t* bytes, size_t size)
{
    size_t offset = 0;
    while (offset < size) {
        DWORD written = 0;
        const DWORD request = static_cast<DWORD>(std::min<size_t>(
            size - offset, (std::numeric_limits<DWORD>::max)()));
        if (!WriteFile(file, bytes + offset, request, &written, nullptr) || written == 0) {
            throw Issuer_error("cannot write an output temporary file");
        }
        offset += written;
    }
    if (!FlushFileBuffers(file)) {
        throw Issuer_error("cannot flush an output temporary file");
    }
}


#else


class File_descriptor
{
public:
    explicit File_descriptor(int value = -1)
    :
        m_value(value)
    {}

    ~File_descriptor() { close(); }

    File_descriptor(File_descriptor&& other)
    :
        m_value(other.m_value)
    {
        other.m_value = -1;
    }

    File_descriptor& operator=(File_descriptor&& other)
    {
        if (this != &other) {
            close();
            m_value       = other.m_value;
            other.m_value = -1;
        }
        return *this;
    }

    int get() const { return m_value; }
    bool valid() const { return m_value >= 0; }

    void close()
    {
        if (valid()) {
            ::close(m_value);
            m_value = -1;
        }
    }

private:
    File_descriptor(const File_descriptor&);
    File_descriptor& operator=(const File_descriptor&);

    int m_value;
};


std::vector<uint8_t> read_limited(const Native_path& path, size_t limit, const char* kind)
{
    File_descriptor file(open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
    if (!file.valid()) {
        throw Issuer_error(std::string("cannot safely open the ") + kind + " file");
    }
    struct stat status;
    if (fstat(file.get(), &status) != 0 || !S_ISREG(status.st_mode) ||
        status.st_size < 0 || static_cast<uint64_t>(status.st_size) > limit)
    {
        throw Issuer_error(std::string("the ") + kind + " file is invalid or too large");
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(status.st_size));
    size_t offset = 0;
    while (offset < bytes.size()) {
        const ssize_t amount = read(file.get(), bytes.data() + offset, bytes.size() - offset);
        if (amount < 0 && errno == EINTR) {
            continue;
        }
        if (amount <= 0) {
            throw Issuer_error(std::string("cannot read the ") + kind + " file");
        }
        offset += static_cast<size_t>(amount);
    }
    uint8_t trailing = 0;
    ssize_t trailing_size;
    do {
        trailing_size = read(file.get(), &trailing, 1);
    }
    while (trailing_size < 0 && errno == EINTR);
    if (trailing_size < 0) {
        throw Issuer_error(std::string("cannot finish reading the ") + kind + " file");
    }
    if (trailing_size != 0) {
        throw Issuer_error(std::string("the ") + kind + " file changed while reading");
    }
    return bytes;
}


Native_path lexical_absolute(const Native_path& path)
{
    Native_path absolute = path;
    if (absolute.empty() || absolute[0] != '/') {
        std::vector<char> cwd(PATH_MAX);
        if (!getcwd(cwd.data(), cwd.size())) {
            throw Issuer_error("an output path cannot be normalized");
        }
        absolute = Native_path(cwd.data()) + "/" + absolute;
    }

    std::vector<std::string> components;
    size_t start = 1;
    while (start <= absolute.size()) {
        const size_t slash = absolute.find('/', start);
        const size_t end   = slash == Native_path::npos ? absolute.size() : slash;
        const std::string component = absolute.substr(start, end - start);
        if (component.empty() || component == ".") {
            // Nothing to retain.
        }
        else
        if (component == "..") {
            if (!components.empty()) {
                components.pop_back();
            }
        }
        else {
            components.push_back(component);
        }
        if (slash == Native_path::npos) {
            break;
        }
        start = slash + 1;
    }

    Native_path result = "/";
    for (size_t i = 0; i < components.size(); ++i) {
        if (i != 0) {
            result += '/';
        }
        result += components[i];
    }
    return result;
}


Native_path parent_path(const Native_path& path)
{
    const size_t slash = path.find_last_of('/');
    if (slash == Native_path::npos || slash + 1 == path.size()) {
        throw Issuer_error("an output path has no filename");
    }
    return slash == 0 ? "/" : path.substr(0, slash);
}


Native_path normalize_output(const Native_path& path)
{
    const Native_path absolute = lexical_absolute(path);
    (void)parent_path(absolute);
    return absolute;
}


class Pinned_directory
{
public:
    explicit Pinned_directory(const Native_path& path)
    :
        m_directory(open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW))
    {
        if (!m_directory.valid()) {
            throw Issuer_error("an output path root cannot be opened");
        }
        size_t start = 1;
        while (start <= path.size()) {
            const size_t slash = path.find('/', start);
            const size_t end   = slash == Native_path::npos ? path.size() : slash;
            if (end > start) {
                const std::string component = path.substr(start, end - start);
                File_descriptor next(openat(m_directory.get(), component.c_str(),
                    O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
                if (!next.valid()) {
                    throw Issuer_error("an output path traverses an unsafe component");
                }
                m_directory = std::move(next);
            }
            if (slash == Native_path::npos) {
                break;
            }
            start = slash + 1;
        }
    }

    int get() const { return m_directory.get(); }

private:
    File_descriptor m_directory;
};


bool entry_exists(int directory, const Native_path& name)
{
    struct stat status;
    if (fstatat(directory, name.c_str(), &status, AT_SYMLINK_NOFOLLOW) == 0) {
        return true;
    }
    if (errno == ENOENT) {
        return false;
    }
    throw Issuer_error("an output path identity is indeterminate");
}


class Pinned_outputs
{
public:
    explicit Pinned_outputs(const Output_paths& paths)
    :
        m_paths(paths),
        m_program_directory(parent_path(paths.program)),
        m_key_directory(parent_path(paths.key)),
        m_program_name(paths.program.substr(paths.program.find_last_of('/') + 1)),
        m_key_name(paths.key.substr(paths.key.find_last_of('/') + 1))
    {
        struct stat program_parent;
        struct stat key_parent;
        if (fstat(m_program_directory.get(), &program_parent) != 0 ||
            fstat(m_key_directory.get(), &key_parent) != 0)
        {
            throw Issuer_error("an output path identity is indeterminate");
        }
        if (paths.program == paths.key ||
            (program_parent.st_dev == key_parent.st_dev &&
                program_parent.st_ino == key_parent.st_ino &&
                m_program_name == m_key_name))
        {
            throw Issuer_error("the program and key outputs identify the same path");
        }

        const bool program_exists = entry_exists(
            m_program_directory.get(), m_program_name);
        const bool key_exists = entry_exists(m_key_directory.get(), m_key_name);
        if (program_exists != key_exists) {
            throw Issuer_error("a partial final output state already exists");
        }
        if (program_exists) {
            throw Issuer_error("the output destinations already exist");
        }
    }

    const Native_path& program() const { return m_paths.program; }
    const Native_path& key() const { return m_paths.key; }
    const Native_path& program_name() const { return m_program_name; }
    const Native_path& key_name() const { return m_key_name; }
    int program_directory() const { return m_program_directory.get(); }
    int key_directory() const { return m_key_directory.get(); }

private:
    Output_paths     m_paths;
    Pinned_directory m_program_directory;
    Pinned_directory m_key_directory;
    Native_path      m_program_name;
    Native_path      m_key_name;
};


class Temporary_file
{
public:
    Temporary_file(int directory, bool restricted)
    :
        m_directory(directory),
        m_file()
    {
        const mode_t mode = restricted ? 0600 : 0666;
        m_file = File_descriptor(openat(
            directory, ".", O_TMPFILE | O_RDWR | O_CLOEXEC, mode));
        if (!m_file.valid()) {
            if (errno == EOPNOTSUPP || errno == EINVAL || errno == EISDIR) {
                throw Issuer_error(
                    "the output filesystem does not support required unnamed temporary files");
            }
            throw Issuer_error("cannot create an unnamed output temporary file");
        }
        if (restricted && fchmod(m_file.get(), 0600) != 0) {
            throw Issuer_error("cannot establish owner-only key permissions");
        }
    }

    int get() const { return m_file.get(); }

    void publish(const Native_path& final_name)
    {
        if (linkat(m_file.get(), "", m_directory,
                final_name.c_str(), AT_EMPTY_PATH) == 0)
        {
            return;
        }
        int error = errno;
        if (error == ENOENT || error == EPERM) {
            // Linux documents /proc/self/fd with AT_SYMLINK_FOLLOW as the
            // unprivileged equivalent of AT_EMPTY_PATH for O_TMPFILE inodes.
            const std::string descriptor =
                "/proc/self/fd/" + std::to_string(m_file.get());
            if (linkat(AT_FDCWD, descriptor.c_str(), m_directory,
                    final_name.c_str(), AT_SYMLINK_FOLLOW) == 0)
            {
                return;
            }
            error = errno;
        }
        if (error == EEXIST) {
            throw Issuer_error("cannot publish an output without overwriting");
        }
        if (error == ENOENT || error == EPERM || error == EOPNOTSUPP ||
            error == EINVAL)
        {
            throw Issuer_error(
                "the output filesystem does not support required inode-bound publication");
        }
        throw Issuer_error("cannot publish an output temporary file");
    }

private:
    Temporary_file(const Temporary_file&);
    Temporary_file& operator=(const Temporary_file&);

    int             m_directory;
    File_descriptor m_file;
};


void write_all(int file, const uint8_t* bytes, size_t size)
{
    size_t offset = 0;
    while (offset < size) {
        const ssize_t amount = write(file, bytes + offset, size - offset);
        if (amount < 0 && errno == EINTR) {
            continue;
        }
        if (amount <= 0) {
            throw Issuer_error("cannot write an output temporary file");
        }
        offset += static_cast<size_t>(amount);
    }
    if (fsync(file) != 0) {
        throw Issuer_error("cannot flush an output temporary file");
    }
}


#endif


Output_paths normalize_outputs(
    const Native_path& program_path,
    const Native_path& key_path)
{
#ifdef _WIN32
    Output_paths paths = {absolute_path(program_path), absolute_path(key_path)};
    validate_windows_filename(paths.program);
    validate_windows_filename(paths.key);
#else
    Output_paths paths = {normalize_output(program_path), normalize_output(key_path)};
#endif
    return paths;
}


Protected_expression_bundle encode_bundle(
    const std::string& expression,
    const std::vector<Protected_binding>& bindings)
{
    try {
        return encode_protected_expression(
            expression, bindings, Protected_math_mode::STRICT);
    }
    catch (const std::exception&) {
        // Parser diagnostics can contain source identifiers. The command-line
        // issuer reports only the failed operation, never source content.
        throw Issuer_error("the expression or binding schema could not be encoded");
    }
}


} // namespace


void protect_expression(
    const Native_path& expression_path,
    const Native_path& schema_path,
    const Native_path& program_path,
    const Native_path& key_path,
    Test_failure failure,
    Test_hooks* hooks)
{
    const std::string expression = expression_text(
        read_limited(expression_path, k_expression_limit, "expression"));
    const std::vector<Protected_binding> bindings = parse_schema(
        read_limited(schema_path, k_schema_limit, "binding schema"));
    const Output_paths path_names = normalize_outputs(program_path, key_path);
    Pinned_outputs outputs(path_names);
    if (hooks && hooks->after_output_directories_opened) {
        hooks->after_output_directories_opened();
    }

    Protected_expression_bundle bundle = encode_bundle(expression, bindings);
#ifdef _WIN32
    const Native_path program_temporary = temporary_path(outputs.program());
    const Native_path key_temporary = failure == Test_failure::KEY_TEMPORARY_COLLISION
        ? collision_temporary_path(outputs.key())
        : temporary_path(outputs.key());
    Temporary_file program_file(
        outputs.program_directory(), program_temporary, false);
    Temporary_file key_file(outputs.key_directory(), key_temporary, true);
#else
    Temporary_file program_file(outputs.program_directory(), false);
    Temporary_file key_file(outputs.key_directory(), true);
#endif
    write_all(program_file.get(), bundle.program.data(), bundle.program.size());
    bundle.key.consume_bytes([&](const uint8_t* bytes, size_t size) {
        if (size != 32) {
            throw Issuer_error("the generated key has an invalid size");
        }
        write_all(key_file.get(), bytes, size);
    });
    if (hooks && hooks->after_temporaries_written) {
#ifdef _WIN32
        hooks->after_temporaries_written(program_file.path(), key_file.path());
#else
        hooks->after_temporaries_written(Native_path(), Native_path());
#endif
    }

    if (failure == Test_failure::BEFORE_PUBLICATION) {
        throw Issuer_error("injected failure before publication");
    }
#ifdef _WIN32
    key_file.publish(outputs.key_directory(), outputs.key_name());
#else
    key_file.publish(outputs.key_name());
#endif
    if (failure == Test_failure::AFTER_KEY_PUBLICATION) {
        throw Issuer_error("injected failure after key publication");
    }
#ifdef _WIN32
    program_file.publish(outputs.program_directory(), outputs.program_name());
#else
    program_file.publish(outputs.program_name());
#endif
}


} // namespace issuer
} // namespace mexce

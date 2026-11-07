#include "storage.hpp"
#include <array>
#include <random>
#include <stdexcept>
#include <system_error>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace oi {
namespace {
[[noreturn]] void fail(const char* action) {
#ifdef _WIN32
    throw std::system_error(GetLastError(), std::system_category(), action);
#else
    throw std::system_error(errno, std::generic_category(), action);
#endif
}
fs::path local(const fs::path& path) {
    if (path.empty()) throw std::runtime_error("Empty file path");
#ifdef _WIN32
    const auto absolute = fs::absolute(path).lexically_normal();
    const auto& name = absolute.native();
    if (name.size() < 3 || name[1] != L':' || name[2] != L'\\' ||
        name.find(L':', 2) != std::wstring::npos)
        throw std::runtime_error("Expected a local file path");
    return absolute;
#else
    return fs::absolute(path);
#endif
}
}

File::File(const fs::path& path, bool create) {
    const auto name = local(path);
#ifdef _WIN32
    handle_ = CreateFileW(name.c_str(), create ? GENERIC_READ | GENERIC_WRITE : GENERIC_READ,
                          FILE_SHARE_READ, nullptr, create ? CREATE_NEW : OPEN_EXISTING,
                          FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (handle_ == INVALID_HANDLE_VALUE) fail("Opening file");
    BY_HANDLE_FILE_INFORMATION info{};
    const bool regular = GetFileType(handle_) == FILE_TYPE_DISK &&
        GetFileInformationByHandle(handle_, &info) &&
        !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT));
#else
    handle_ = ::open(name.c_str(), O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK |
                     (create ? O_RDWR | O_CREAT | O_EXCL : O_RDONLY), 0600);
    if (handle_ < 0) fail("Opening file");
    struct stat info{};
    const bool regular = fstat(handle_, &info) == 0 && S_ISREG(info.st_mode);
#endif
    if (!regular) {
        close();
        throw std::runtime_error("Only regular, non-symlink files are supported");
    }
#ifndef _WIN32
    if (flock(handle_, (create ? LOCK_EX : LOCK_SH) | LOCK_NB) != 0) {
        const int error = errno;
        close();
        errno = error;
        fail("Locking file");
    }
#endif
}

File::~File() { close(); }
void File::close() noexcept {
#ifdef _WIN32
    CloseHandle(handle_);
#else
    ::close(handle_);
#endif
}

std::uint64_t File::size() const {
#ifdef _WIN32
    LARGE_INTEGER result{};
    if (!GetFileSizeEx(handle_, &result)) fail("Reading file size");
    return result.QuadPart;
#else
    struct stat result{};
    if (fstat(handle_, &result) != 0) fail("Reading file size");
    return result.st_size;
#endif
}
void File::rewind() {
#ifdef _WIN32
    if (!SetFilePointerEx(handle_, LARGE_INTEGER{}, nullptr, FILE_BEGIN)) fail("Seeking file");
#else
    if (lseek(handle_, 0, SEEK_SET) < 0) fail("Seeking file");
#endif
}
void File::read(Bytes data) {
    while (!data.empty()) {
#ifdef _WIN32
        DWORD count = 0;
        if (!ReadFile(handle_, data.data(), static_cast<DWORD>(data.size()), &count, nullptr))
            fail("Reading file");
#else
        const auto count = ::read(handle_, data.data(), data.size());
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) fail("Reading file");
#endif
        if (count == 0) throw std::runtime_error("Unexpected end of file");
        data = data.subspan(count);
    }
}
void File::write(View data) {
    while (!data.empty()) {
#ifdef _WIN32
        DWORD count = 0;
        if (!WriteFile(handle_, data.data(), static_cast<DWORD>(data.size()), &count, nullptr))
            fail("Writing staging file");
#else
        const auto count = ::write(handle_, data.data(), data.size());
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) fail("Writing staging file");
#endif
        if (count == 0) throw std::runtime_error("Write made no progress");
        data = data.subspan(count);
    }
}
void File::flush() {
#ifdef _WIN32
    if (!FlushFileBuffers(handle_)) fail("Flushing staging file");
#else
    if (fsync(handle_) != 0) fail("Flushing staging file");
#endif
}

Output::Output(const fs::path& target) : target_(local(target)) {
    if (fs::symlink_status(target_).type() != fs::file_type::not_found)
        throw std::runtime_error("Destination already exists. Choose a new file.");
    std::random_device random;
    for (int attempt = 0; attempt < 8; ++attempt) {
        directory_ = target_.parent_path() / (".openimage-" + std::to_string(random()));
        staging_ = directory_ / "incomplete";
#ifdef _WIN32
        if (CreateDirectoryW(directory_.c_str(), nullptr)) return;
        if (GetLastError() != ERROR_ALREADY_EXISTS) fail("Creating staging directory");
#else
        if (mkdir(directory_.c_str(), 0700) == 0) return;
        if (errno != EEXIST) fail("Creating staging directory");
#endif
    }
    throw std::runtime_error("Could not allocate a staging directory");
}
Output::~Output() {
    std::error_code ignored;
    fs::remove(staging_, ignored);
    fs::remove(directory_, ignored);
}
void Output::publish() {
#ifdef _WIN32
    if (!MoveFileExW(staging_.c_str(), target_.c_str(), MOVEFILE_WRITE_THROUGH))
        fail("Publishing new output");
#else
    const int parent = open(target_.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (parent < 0) fail("Opening output directory");
    if (link(staging_.c_str(), target_.c_str()) != 0) {
        const int error = errno;
        ::close(parent);
        errno = error;
        fail("Publishing new output");
    }
    const int result = fsync(parent);
    const int error = errno;
    ::close(parent);
    if (result != 0) {
        errno = error;
        fail("Output published, but directory durability could not be confirmed");
    }
#endif
}
}


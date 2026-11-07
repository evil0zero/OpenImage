#pragma once
#include <cstdint>
#include <filesystem>
#include <span>

namespace oi {
namespace fs = std::filesystem;
using Bytes = std::span<unsigned char>;
using View = std::span<const unsigned char>;

class File {
public:
    explicit File(const fs::path& path, bool create = false);
    ~File();
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    std::uint64_t size() const;
    void rewind();
    void read(Bytes data);
    void write(View data);
    void flush();
private:
    void close() noexcept;
#ifdef _WIN32
    void* handle_;
#else
    int handle_;
#endif
};

class Output {
public:
    explicit Output(const fs::path& target);
    ~Output();
    Output(const Output&) = delete;
    Output& operator=(const Output&) = delete;
    const fs::path& staging() const { return staging_; }
    void publish();
private:
    fs::path target_, directory_, staging_;
};
}


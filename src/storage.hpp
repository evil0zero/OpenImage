#pragma once
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace oi {
namespace fs = std::filesystem;
using Bytes = std::span<unsigned char>;
using View = std::span<const unsigned char>;

struct Partition {
    std::uint32_t number;
    std::uint64_t offset;
    std::uint64_t bytes;
    std::string type;
    fs::path mount;
    std::string filesystem;
};

struct Disk {
    fs::path path;
    fs::path interface_path;
    std::string model, serial, bus, layout;
    std::uint64_t bytes = 0;
    std::uint32_t sector = 0;
    bool attributes_known = false;
    bool offline = false;
    bool read_only = false;
    bool running_os = false;
    std::vector<Partition> partitions;
};

std::vector<Disk> discover_disks();

class File {
public:
    explicit File(const fs::path& path, bool create = false);
    File(const Disk& disk, bool write);
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
    std::uint64_t disk_size_ = 0;
    bool disk_write_ = false;
#ifdef _WIN32
    void* handle_;
    void* disk_buffer_ = nullptr;
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


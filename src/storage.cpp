#include "storage.hpp"
#include <array>
#include <algorithm>
#include <cstring>
#include <cwchar>
#include <memory>
#include <random>
#include <stdexcept>
#include <system_error>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <cfgmgr32.h>
#include <initguid.h>
#include <winioctl.h>
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

#ifdef _WIN32
constexpr DWORD disk_share = FILE_SHARE_READ | FILE_SHARE_WRITE;
constexpr DWORD transfer_size = 1U << 20;

template<class T>
bool query(HANDLE handle, DWORD code, T& result) {
    DWORD count = 0;
    return DeviceIoControl(handle, code, nullptr, 0, &result, sizeof(result), &count, nullptr)
        && count >= sizeof(result);
}

bool disk_attributes(HANDLE handle, GET_DISK_ATTRIBUTES& attributes) {
    return query(handle, IOCTL_DISK_GET_DISK_ATTRIBUTES, attributes);
}

void require_offline(HANDLE handle, bool write) {
    GET_DISK_ATTRIBUTES attributes{};
    if (!disk_attributes(handle, attributes) || !(attributes.Attributes & DISK_ATTRIBUTE_OFFLINE))
        throw std::runtime_error("Physical disk access requires a disk already offline in Windows");
    if (write && (attributes.Attributes & DISK_ATTRIBUTE_READ_ONLY))
        throw std::runtime_error("The physical disk is read-only");
}

Disk probe(HANDLE handle, const fs::path& interface_path) {
    Disk disk;
    disk.interface_path = interface_path;
    STORAGE_DEVICE_NUMBER number{};
    if (query(handle, IOCTL_STORAGE_GET_DEVICE_NUMBER, number) &&
        number.DeviceType == FILE_DEVICE_DISK && number.DeviceNumber != MAXDWORD)
        disk.path = LR"(\\.\PhysicalDrive)" + std::to_wstring(number.DeviceNumber);

    GET_DISK_ATTRIBUTES attributes{};
    disk.attributes_known = disk_attributes(handle, attributes);
    disk.offline = (attributes.Attributes & DISK_ATTRIBUTE_OFFLINE) != 0;
    disk.read_only = (attributes.Attributes & DISK_ATTRIBUTE_READ_ONLY) != 0;

    alignas(DISK_GEOMETRY_EX) std::array<unsigned char, 1024> geometry_buffer{};
    DWORD count = 0;
    if (DeviceIoControl(handle, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, nullptr, 0,
                        geometry_buffer.data(), static_cast<DWORD>(geometry_buffer.size()), &count, nullptr)
        && count >= offsetof(DISK_GEOMETRY_EX, Data)) {
        DISK_GEOMETRY_EX geometry{};
        std::memcpy(&geometry, geometry_buffer.data(), offsetof(DISK_GEOMETRY_EX, Data));
        if (geometry.DiskSize.QuadPart > 0) disk.bytes = geometry.DiskSize.QuadPart;
        disk.sector = geometry.Geometry.BytesPerSector;
    }
    GET_LENGTH_INFORMATION length{};
    if (query(handle, IOCTL_DISK_GET_LENGTH_INFO, length) && length.Length.QuadPart > 0)
        disk.bytes = length.Length.QuadPart;

    STORAGE_PROPERTY_QUERY property{};
    property.PropertyId = StorageDeviceProperty;
    property.QueryType = PropertyStandardQuery;
    alignas(STORAGE_DEVICE_DESCRIPTOR) std::array<unsigned char, 4096> descriptor{};
    if (DeviceIoControl(handle, IOCTL_STORAGE_QUERY_PROPERTY, &property, sizeof(property),
                        descriptor.data(), static_cast<DWORD>(descriptor.size()), &count, nullptr)
        && count >= offsetof(STORAGE_DEVICE_DESCRIPTOR, RawDeviceProperties) && count <= descriptor.size()) {
        STORAGE_DEVICE_DESCRIPTOR info{};
        std::memcpy(&info, descriptor.data(), offsetof(STORAGE_DEVICE_DESCRIPTOR, RawDeviceProperties));
        const auto field = [&](DWORD offset) -> std::string {
            if (offset < offsetof(STORAGE_DEVICE_DESCRIPTOR, RawDeviceProperties) || offset >= count) return {};
            const auto* first = reinterpret_cast<const char*>(descriptor.data() + offset);
            const auto* end = static_cast<const char*>(std::memchr(first, 0, count - offset));
            return end ? std::string(first, end) : std::string{};
        };
        disk.model = field(info.ProductIdOffset);
        disk.serial = field(info.SerialNumberOffset);
        switch (info.BusType) {
        case BusTypeNvme: disk.bus = "NVMe"; break;
        case BusTypeSata: disk.bus = "SATA"; break;
        case BusTypeUsb: disk.bus = "USB"; break;
        case BusTypeSd: disk.bus = "SD"; break;
        case BusTypeMmc: disk.bus = "MMC"; break;
        case BusTypeScsi: disk.bus = "SCSI"; break;
        case BusTypeSas: disk.bus = "SAS"; break;
        case BusTypeRAID: disk.bus = "RAID"; break;
        case BusTypeVirtual:
        case BusTypeFileBackedVirtual: disk.bus = "Virtual"; break;
        default: break;
        }
    }
    return disk;
}

void read_layout(HANDLE handle, Disk& disk) {
    constexpr auto header_size = offsetof(DRIVE_LAYOUT_INFORMATION_EX, PartitionEntry);
    std::vector<unsigned char> buffer(65536);
    DWORD count = 0;
    if (!DeviceIoControl(handle, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, nullptr, 0, buffer.data(),
                         static_cast<DWORD>(buffer.size()), &count, nullptr)
        || count < header_size || count > buffer.size()) return;
    DRIVE_LAYOUT_INFORMATION_EX header{};
    std::memcpy(&header, buffer.data(), header_size);
    if (header.PartitionCount > (count - header_size) / sizeof(PARTITION_INFORMATION_EX)) return;
    disk.layout = header.PartitionStyle == PARTITION_STYLE_GPT ? "GPT" :
                  header.PartitionStyle == PARTITION_STYLE_MBR ? "MBR" : "Raw";
    constexpr GUID efi{0xc12a7328, 0xf81f, 0x11d2, {0xba, 0x4b, 0x00, 0xa0, 0xc9, 0x3e, 0xc9, 0x3b}};
    constexpr GUID msr{0xe3c9e316, 0x0b5c, 0x4db8, {0x81, 0x7d, 0xf9, 0x2d, 0xf0, 0x02, 0x15, 0xae}};
    constexpr GUID recovery{0xde94bba4, 0x06d1, 0x4d40, {0xa1, 0x6a, 0xbf, 0xd5, 0x01, 0x79, 0xd6, 0xac}};
    for (DWORD index = 0; index < header.PartitionCount; ++index) {
        PARTITION_INFORMATION_EX item{};
        std::memcpy(&item, buffer.data() + header_size + index * sizeof(item), sizeof(item));
        if (!item.PartitionNumber || item.PartitionLength.QuadPart <= 0 ||
            item.StartingOffset.QuadPart < 0) continue;
        const char* type = "Partition";
        if (item.PartitionStyle == PARTITION_STYLE_GPT) {
            if (item.Gpt.PartitionType == efi) type = "EFI System";
            if (item.Gpt.PartitionType == msr) type = "Microsoft Reserved";
            if (item.Gpt.PartitionType == recovery) type = "Windows Recovery";
        }
        disk.partitions.push_back({item.PartitionNumber, static_cast<std::uint64_t>(item.StartingOffset.QuadPart),
                                   static_cast<std::uint64_t>(item.PartitionLength.QuadPart), type, {}, {}});
    }
    std::ranges::sort(disk.partitions, {}, &Partition::offset);
}

void map_drive_letters(std::vector<Disk>& disks) {
    wchar_t windows_directory[MAX_PATH]{};
    const auto windows_length = GetWindowsDirectoryW(windows_directory, MAX_PATH);
    const DWORD mask = GetLogicalDrives();
    for (DWORD index = 0; index < 26; ++index) {
        if (!(mask & (1UL << index))) continue;
        const auto letter = static_cast<wchar_t>(L'A' + index);
        const wchar_t root[]{letter, L':', L'\\', L'\0'};
        const UINT type = GetDriveTypeW(root);
        if (type != DRIVE_FIXED && type != DRIVE_REMOVABLE) continue;
        const wchar_t name[]{L'\\', L'\\', L'.', L'\\', letter, L':', L'\0'};
        std::unique_ptr<void, decltype(&CloseHandle)> handle(
            CreateFileW(name, 0, disk_share, nullptr, OPEN_EXISTING, 0, nullptr), CloseHandle);
        if (handle.get() == INVALID_HANDLE_VALUE) {
            handle.release();
            continue;
        }
        STORAGE_DEVICE_NUMBER number{};
        if (!query(handle.get(), IOCTL_STORAGE_GET_DEVICE_NUMBER, number) ||
            number.DeviceType != FILE_DEVICE_DISK) continue;
        const auto path = fs::path(LR"(\\.\PhysicalDrive)" + std::to_wstring(number.DeviceNumber));
        const auto disk = std::ranges::find(disks, path, &Disk::path);
        if (disk == disks.end()) continue;
        if (windows_length > 2 && windows_length < MAX_PATH && windows_directory[0] == letter &&
            windows_directory[1] == L':') disk->running_os = true;
        const auto part = std::ranges::find(disk->partitions, number.PartitionNumber, &Partition::number);
        if (part == disk->partitions.end()) continue;
        part->mount = root;
        char filesystem[MAX_PATH]{};
        const char ansi_root[]{static_cast<char>('A' + index), ':', '\\', '\0'};
        if (GetVolumeInformationA(ansi_root, nullptr, 0, nullptr, nullptr, nullptr, filesystem, MAX_PATH))
            part->filesystem = filesystem;
    }
}
#endif
}

std::vector<Disk> discover_disks() {
    std::vector<Disk> disks;
#ifdef _WIN32
    std::vector<wchar_t> paths;
    GUID disk_interface = GUID_DEVINTERFACE_DISK;
    for (int attempt = 0; attempt < 3; ++attempt) {
        ULONG size = 0;
        if (CM_Get_Device_Interface_List_SizeW(&size, &disk_interface,
                nullptr, CM_GET_DEVICE_INTERFACE_LIST_PRESENT) != CR_SUCCESS)
            throw std::runtime_error("Could not enumerate Windows disk interfaces");
        paths.resize(size);
        const auto result = CM_Get_Device_Interface_ListW(&disk_interface,
            nullptr, paths.data(), size, CM_GET_DEVICE_INTERFACE_LIST_PRESENT);
        if (result == CR_SUCCESS) break;
        if (result != CR_BUFFER_SMALL || attempt == 2)
            throw std::runtime_error("Could not read Windows disk interfaces");
    }
    for (const auto* path = paths.data(); *path; path += std::wcslen(path) + 1) {
        std::unique_ptr<void, decltype(&CloseHandle)> handle(
            CreateFileW(path, 0, disk_share, nullptr, OPEN_EXISTING, 0, nullptr), CloseHandle);
        if (handle.get() == INVALID_HANDLE_VALUE) {
            handle.release();
            Disk unavailable;
            unavailable.interface_path = path;
            disks.push_back(std::move(unavailable));
            continue;
        }
        auto disk = probe(handle.get(), path);
        read_layout(handle.get(), disk);
        disks.push_back(std::move(disk));
    }
    map_drive_letters(disks);
    std::ranges::sort(disks, {}, &Disk::path);
#endif
    return disks;
}

File::File(const Disk& disk, bool write) {
#ifdef _WIN32
    if (disk.path.empty() || disk.interface_path.empty() || !disk.bytes || !disk.sector ||
        transfer_size % disk.sector || disk.bytes % disk.sector)
        throw std::runtime_error("Disk identity or geometry is unavailable or unsupported");
    handle_ = CreateFileW(disk.interface_path.c_str(), GENERIC_READ | (write ? GENERIC_WRITE : 0),
                          0, nullptr, OPEN_EXISTING,
                          FILE_FLAG_NO_BUFFERING | (write ? FILE_FLAG_WRITE_THROUGH : 0), nullptr);
    if (handle_ == INVALID_HANDLE_VALUE) fail("Opening physical disk (administrator privileges required)");
    try {
        const auto actual = probe(handle_, disk.interface_path);
        if (actual.path != disk.path || actual.serial != disk.serial || actual.model != disk.model ||
            actual.bytes != disk.bytes || actual.sector != disk.sector)
            throw std::runtime_error("Disk identity or geometry changed. Refresh and select it again.");
        require_offline(handle_, write);
        disk_buffer_ = VirtualAlloc(nullptr, transfer_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!disk_buffer_) fail("Allocating aligned disk I/O buffer");
        disk_size_ = actual.bytes;
        disk_write_ = write;
    } catch (...) {
        close();
        throw;
    }
#else
    static_cast<void>(disk);
    static_cast<void>(write);
    throw std::runtime_error("Raw disk access is unavailable on this platform");
#endif
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
    if (disk_buffer_) VirtualFree(disk_buffer_, 0, MEM_RELEASE);
    CloseHandle(handle_);
#else
    ::close(handle_);
#endif
}

std::uint64_t File::size() const {
    if (disk_size_) return disk_size_;
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
        if (disk_buffer_) require_offline(handle_, false);
        auto* buffer = disk_buffer_ ? disk_buffer_ : data.data();
        const auto size = disk_buffer_ ? std::min<std::size_t>(data.size(), transfer_size) : data.size();
        if (!ReadFile(handle_, buffer, static_cast<DWORD>(size), &count, nullptr))
            fail("Reading file");
        if (disk_buffer_) std::memcpy(data.data(), disk_buffer_, count);
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
        auto size = data.size();
        const void* buffer = data.data();
        if (disk_buffer_) {
            if (!disk_write_) throw std::runtime_error("Physical disk was opened for reading");
            require_offline(handle_, true);
            size = std::min<std::size_t>(size, transfer_size);
            std::memcpy(disk_buffer_, data.data(), size);
            buffer = disk_buffer_;
        }
        if (!WriteFile(handle_, buffer, static_cast<DWORD>(size), &count, nullptr))
            fail(disk_buffer_ ? "Writing physical disk" : "Writing staging file");
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


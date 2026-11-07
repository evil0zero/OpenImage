#pragma once

#include "storage.hpp"
#include <functional>

namespace oi {
enum class Codec : unsigned char { raw, zstd, lz4, lzma2 };
struct ImageInfo {
    std::uint64_t bytes;
    Codec codec;
};
using Progress = std::function<void(std::uint64_t, std::uint64_t)>;

void backup(const fs::path& source, const fs::path& image, Codec codec,
            const Progress& progress = {});
ImageInfo verify(const fs::path& image);
ImageInfo inspect(const fs::path& image);
void restore(const fs::path& image, const fs::path& target, const Progress& progress = {});
void clone(const fs::path& source, const fs::path& target, const Progress& progress = {});
void compare(const fs::path& image, const fs::path& restored);
const char* codec_name(Codec codec);
}

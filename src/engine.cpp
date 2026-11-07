#include "engine.hpp"

#include <Lzma2Dec.h>
#include <Lzma2Enc.h>
#include <blake3.h>
#include <lz4.h>
#include <zstd.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <vector>

namespace oi {
namespace {
constexpr std::size_t chunk_size = 1U << 20;
constexpr std::size_t payload_limit = 2 * chunk_size;
constexpr std::uint64_t logical_limit = 1ULL << 50;
constexpr std::array<unsigned char, 8> signature{'O','P','E','N','O','M','I',1};
constexpr std::array<unsigned char, 8> commit{'O','M','I','D','O','N','E',1};
using Digest = std::array<unsigned char, BLAKE3_OUT_LEN>;
using Buffer = std::vector<unsigned char>;

[[noreturn]] void invalid(const char* message) { throw std::runtime_error(message); }

class Hash {
public:
    Hash() { blake3_hasher_init(&state_); }
    void add(View data) { blake3_hasher_update(&state_, data.data(), data.size()); }
    Digest finish() const {
        Digest result{};
        blake3_hasher_finalize(&state_, result.data(), result.size());
        return result;
    }
private:
    blake3_hasher state_{};
};

Digest digest(View data) {
    Hash hash;
    hash.add(data);
    return hash.finish();
}

void encode_integer(Bytes field, std::uint64_t value) {
    for (auto& byte : field) {
        byte = static_cast<unsigned char>(value & 255U);
        value >>= 8;
    }
}

std::uint64_t decode_integer(View field) {
    std::uint64_t value = 0;
    for (auto byte = field.rbegin(); byte != field.rend(); ++byte)
        value = (value << 8) | *byte;
    return value;
}

void validate_size(std::uint64_t size) {
    if (size == 0 || size > logical_limit) invalid("Unsupported logical image size");
}

void* allocate(ISzAllocPtr, size_t size) { return std::malloc(size); }
void release(ISzAllocPtr, void* data) { std::free(data); }
const ISzAlloc allocator{allocate, release};

std::size_t compress(Codec codec, View input, Bytes output) {
    switch (codec) {
    case Codec::raw:
        std::ranges::copy(input, output.begin());
        return input.size();
    case Codec::zstd: {
        const auto length = ZSTD_compress(output.data(), output.size(), input.data(), input.size(), 3);
        if (ZSTD_isError(length)) invalid("Zstandard compression failed");
        return length;
    }
    case Codec::lz4: {
        const int length = LZ4_compress_default(reinterpret_cast<const char*>(input.data()),
            reinterpret_cast<char*>(output.data()), static_cast<int>(input.size()),
            static_cast<int>(output.size()));
        if (length <= 0) invalid("LZ4 compression failed");
        return static_cast<std::size_t>(length);
    }
    case Codec::lzma2: {
        std::unique_ptr<CLzma2Enc, decltype(&Lzma2Enc_Destroy)> encoder(
            Lzma2Enc_Create(&allocator, &allocator), Lzma2Enc_Destroy);
        if (!encoder) invalid("Cannot allocate LZMA2 encoder");
        CLzma2EncProps options{};
        Lzma2EncProps_Init(&options);
        options.lzmaProps.level = 5;
        options.lzmaProps.dictSize = chunk_size;
        options.numTotalThreads = 1;
        if (Lzma2Enc_SetProps(encoder.get(), &options) != SZ_OK)
            invalid("LZMA2 settings rejected");
        Lzma2Enc_SetDataSize(encoder.get(), input.size());
        output[0] = Lzma2Enc_WriteProperties(encoder.get());
        auto length = output.size() - 1;
        if (Lzma2Enc_Encode2(encoder.get(), nullptr, output.data() + 1, &length,
                nullptr, input.data(), input.size(), nullptr) != SZ_OK)
            invalid("LZMA2 compression failed");
        return length + 1;
    }
    }
    invalid("Unsupported codec");
}

void decompress(Codec codec, View input, Bytes output) {
    switch (codec) {
    case Codec::raw:
        if (input.size() == output.size()) {
            std::ranges::copy(input, output.begin());
            return;
        }
        break;
    case Codec::zstd:
        if (ZSTD_getFrameContentSize(input.data(), input.size()) == output.size() &&
            ZSTD_findFrameCompressedSize(input.data(), input.size()) == input.size() &&
            ZSTD_decompress(output.data(), output.size(), input.data(), input.size()) == output.size())
            return;
        break;
    case Codec::lz4:
        if (LZ4_decompress_safe(reinterpret_cast<const char*>(input.data()),
                reinterpret_cast<char*>(output.data()), static_cast<int>(input.size()),
                static_cast<int>(output.size())) == static_cast<int>(output.size()))
            return;
        break;
    case Codec::lzma2:
        if (input.size() > 1 && input[0] <= 16) {
            auto written = output.size();
            auto consumed = input.size() - 1;
            ELzmaStatus status{};
            if (Lzma2Decode(output.data(), &written, input.data() + 1, &consumed,
                    input[0], LZMA_FINISH_END, &status, &allocator) == SZ_OK &&
                status == LZMA_STATUS_FINISHED_WITH_MARK && written == output.size() &&
                consumed == input.size() - 1)
                return;
        }
        break;
    }
    invalid("Malformed or incompatible compressed chunk");
}

using Header = std::array<unsigned char, 24>;

ImageInfo read_header(File& image, Header& header) {
    image.read(header);
    if (!std::ranges::equal(signature, View(header).first(8)) ||
        header[8] > static_cast<unsigned char>(Codec::lzma2) ||
        !std::ranges::all_of(View(header).subspan(9, 7), [](auto byte) { return byte == 0; }))
        invalid("Unsupported OMI signature, version or feature");
    const auto bytes = decode_integer(View(header).last(8));
    validate_size(bytes);
    return {bytes, static_cast<Codec>(header[8])};
}

std::size_t next_chunk(std::uint64_t remaining) {
    return static_cast<std::size_t>(std::min<std::uint64_t>(remaining, chunk_size));
}

ImageInfo read_image(File& image, File* output, File* comparison, const Progress& progress) {
    const auto physical_size = image.size();
    Header header{};
    const auto info = read_header(image, header);
    if (comparison && comparison->size() != info.bytes)
        invalid("Restored data length mismatch");
    Hash transcript;
    transcript.add(header);
    Buffer payload(payload_limit);
    Buffer decoded(chunk_size);
    Buffer compared(comparison ? chunk_size : 0);
    std::uint64_t position = header.size();
    for (std::uint64_t offset = 0; offset < info.bytes;) {
        std::array<unsigned char, 36> record{};
        constexpr auto trailer_size = commit.size() + Digest{}.size();
        if (position > physical_size || physical_size - position < record.size() + trailer_size)
            invalid("Truncated chunk record");
        image.read(record);
        position += record.size();
        const auto length = decode_integer(View(record).first(4));
        if (length == 0 || length > payload_limit ||
            length > physical_size - position - trailer_size)
            invalid("Invalid chunk payload length");
        auto encoded = Bytes(payload).first(static_cast<std::size_t>(length));
        image.read(encoded);
        position += length;
        transcript.add(record);
        transcript.add(encoded);
        auto raw = Bytes(decoded).first(next_chunk(info.bytes - offset));
        decompress(info.codec, encoded, raw);
        if (!std::ranges::equal(digest(raw), View(record).last(Digest{}.size())))
            invalid("Chunk checksum mismatch");
        if (output) output->write(raw);
        if (comparison) {
            auto actual = Bytes(compared).first(raw.size());
            comparison->read(actual);
            if (!std::ranges::equal(raw, actual)) invalid("Restored data mismatch");
        }
        offset += raw.size();
        if (progress) progress(offset, info.bytes);
    }
    if (physical_size - position != commit.size() + Digest{}.size())
        invalid("Missing commit or trailing data");
    std::array<unsigned char, commit.size()> marker{};
    Digest recorded{};
    image.read(marker);
    image.read(recorded);
    if (marker != commit || recorded != transcript.finish() || image.size() != physical_size)
        invalid("OMI commit validation failed");
    return info;
}
}

const char* codec_name(Codec codec) {
    switch (codec) {
    case Codec::raw: return "raw";
    case Codec::zstd: return "zstd";
    case Codec::lz4: return "lz4";
    case Codec::lzma2: return "lzma2";
    }
    invalid("Unsupported codec");
}

void backup(const fs::path& source, const fs::path& image, Codec codec, const Progress& progress,
            const Disk* source_disk) {
    File input = source_disk ? File(*source_disk, false) : File(source);
    const auto total = input.size();
    validate_size(total);
    Output output(image);
    {
        File writer(output.staging(), true);
        Header header{};
        std::ranges::copy(signature, header.begin());
        header[8] = static_cast<unsigned char>(codec);
        encode_integer(Bytes(header).last(8), total);
        Hash transcript;
        transcript.add(header);
        writer.write(header);
        Buffer decoded(chunk_size);
        Buffer payload(payload_limit);
        for (std::uint64_t offset = 0; offset < total;) {
            auto raw = Bytes(decoded).first(next_chunk(total - offset));
            input.read(raw);
            const auto encoded = View(payload).first(compress(codec, raw, payload));
            std::array<unsigned char, 36> record{};
            encode_integer(Bytes(record).first(4), encoded.size());
            std::ranges::copy(digest(raw), record.begin() + 4);
            transcript.add(record);
            transcript.add(encoded);
            writer.write(record);
            writer.write(encoded);
            offset += raw.size();
            if (progress) progress(offset, total);
        }
        writer.write(commit);
        writer.write(transcript.finish());
        writer.flush();
    }
    input.rewind();
    {
        File staged(output.staging());
        read_image(staged, nullptr, &input, {});
    }
    output.publish();
}

ImageInfo inspect(const fs::path& image) {
    File input(image);
    Header header{};
    return read_header(input, header);
}

ImageInfo verify(const fs::path& image) {
    File input(image);
    return read_image(input, nullptr, nullptr, {});
}

void restore(const fs::path& image, const fs::path& target, const Progress& progress,
             const Disk* target_disk) {
    File input(image);
    const auto info = read_image(input, nullptr, nullptr, {});
    input.rewind();
    if (target_disk) {
        if (info.bytes != target_disk->bytes)
            invalid("Exact restore requires an image and disk of the same size");
        File writer(*target_disk, true);
        read_image(input, &writer, nullptr, progress);
        writer.flush();
        writer.rewind();
        input.rewind();
        read_image(input, nullptr, &writer, {});
        return;
    }
    Output output(target);
    {
        File writer(output.staging(), true);
        read_image(input, &writer, nullptr, progress);
        writer.flush();
    }
    input.rewind();
    {
        File staged(output.staging());
        read_image(input, nullptr, &staged, {});
    }
    output.publish();
}

void clone(const fs::path& source, const fs::path& target, const Progress& progress,
           const Disk* source_disk, const Disk* target_disk) {
    if (source_disk && target_disk) {
        if (source_disk->path == target_disk->path ||
            source_disk->interface_path == target_disk->interface_path)
            invalid("Source and destination must be different physical disks");
        if (source_disk->sector != target_disk->sector)
            invalid("Exact cloning requires matching logical sector sizes");
    }
    File input = source_disk ? File(*source_disk, false) : File(source);
    const auto total = input.size();
    validate_size(total);
    Buffer buffer(chunk_size);
    Hash original;
    const auto copy = [&](File& writer) {
        for (std::uint64_t offset = 0; offset < total;) {
            auto chunk = Bytes(buffer).first(next_chunk(total - offset));
            input.read(chunk);
            original.add(chunk);
            writer.write(chunk);
            offset += chunk.size();
            if (progress) progress(offset, total);
        }
        writer.flush();
    };
    const auto verify_copy = [&](File& staged) {
        if (input.size() != total || staged.size() != total)
            invalid("Source or staged copy length changed");
        Hash source_hash;
        Hash target_hash;
        input.rewind();
        for (std::uint64_t offset = 0; offset < total;) {
            auto chunk = Bytes(buffer).first(next_chunk(total - offset));
            input.read(chunk);
            source_hash.add(chunk);
            staged.read(chunk);
            target_hash.add(chunk);
            offset += chunk.size();
        }
        const auto expected = original.finish();
        if (source_hash.finish() != expected || target_hash.finish() != expected)
            invalid("Source changed or copied data did not verify");
    };
    if (target_disk) {
        if (total != target_disk->bytes)
            invalid("Exact clone requires a source and disk of the same size");
        File writer(*target_disk, true);
        copy(writer);
        writer.rewind();
        verify_copy(writer);
        return;
    }
    Output output(target);
    {
        File writer(output.staging(), true);
        copy(writer);
    }
    {
        File staged(output.staging());
        verify_copy(staged);
    }
    output.publish();
}

void compare(const fs::path& image, const fs::path& restored) {
    File input(image);
    File comparison(restored);
    read_image(input, nullptr, &comparison, {});
}
}

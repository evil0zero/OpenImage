#include "engine.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace {
using Arguments = std::vector<oi::fs::path>;

void usage() {
    std::cout << "OpenImage | exact disk and file imaging\n"
                 "  openimage [--cli] backup SOURCE IMAGE [raw|zstd|lz4|lzma2]\n"
                 "  openimage [--cli] restore IMAGE NEW_FILE\n"
                 "  openimage [--cli] clone SOURCE NEW_FILE\n"
                 "  openimage [--cli] verify IMAGE\n"
                 "  openimage [--cli] explore IMAGE\n"
                 "  openimage [--cli] compare IMAGE RESTORED_FILE\n"
                 "  openimage [--cli] disks\n"
                 "  openimage [--cli] disk-backup DEVICE IMAGE [raw|zstd|lz4|lzma2]\n"
                 "  openimage [--cli] disk-restore IMAGE DEVICE --overwrite\n"
                 "  openimage [--cli] disk-clone SOURCE_DEVICE TARGET_DEVICE --overwrite\n"
                 "  openimage --tui\n"
                 "Imaging commands emit JSON. Raw disk access requires exclusive volume access.\n";
}

void print_disks(const std::vector<oi::Disk>& disks) {
    if (disks.empty()) std::cout << "No disk interfaces discovered.\n";
    for (const auto& disk : disks) {
        std::cout << (disk.path.empty() ? "Unknown disk" : disk.path.string())
                  << "  " << (disk.model.empty() ? "Unknown model" : disk.model)
                  << "  " << disk.bus << "  " << disk.bytes << " bytes  "
                  << (disk.attributes_known ? (disk.offline ? "offline" : "online") : "state unknown")
                  << (disk.read_only ? ", read-only" : "")
                  << (disk.running_os ? ", running OS" : "") << '\n';
        for (const auto& part : disk.partitions)
            std::cout << "  " << part.number << "  " << part.type << "  "
                      << part.bytes << " bytes at " << part.offset << "  "
                      << part.mount.string() << "  " << part.filesystem << '\n';
    }
}

const oi::Disk& find_disk(const std::vector<oi::Disk>& disks, const oi::fs::path& path) {
    const auto result = std::ranges::find(disks, path, &oi::Disk::path);
    if (path.empty() || result == disks.end())
        throw std::invalid_argument("Device not found. Run openimage disks and use its exact device path.");
    return *result;
}

oi::Codec parse_codec(const oi::fs::path& argument) {
    for (const auto codec : {oi::Codec::raw, oi::Codec::zstd, oi::Codec::lz4, oi::Codec::lzma2})
        if (argument == oi::codec_name(codec)) return codec;
    throw std::invalid_argument("Unknown compression codec");
}

void run(const Arguments& args, const oi::Progress& progress = {}) {
    const auto& command = args.at(0);
    if (command == "disks" && args.size() == 1) {
        print_disks(oi::discover_disks());
        return;
    } else if (command == "disk-backup" && (args.size() == 3 || args.size() == 4)) {
        const auto disks = oi::discover_disks();
        oi::backup({}, args[2], args.size() == 4 ? parse_codec(args[3]) : oi::Codec::zstd,
                   progress, &find_disk(disks, args[1]));
    } else if (command == "disk-restore" && args.size() == 4 && args[3] == "--overwrite") {
        const auto disks = oi::discover_disks();
        oi::restore(args[1], {}, progress, &find_disk(disks, args[2]));
    } else if (command == "disk-clone" && args.size() == 4 && args[3] == "--overwrite") {
        const auto disks = oi::discover_disks();
        oi::clone({}, {}, progress, &find_disk(disks, args[1]), &find_disk(disks, args[2]));
    } else if (command == "backup" && (args.size() == 3 || args.size() == 4)) {
        oi::backup(args[1], args[2], args.size() == 4 ? parse_codec(args[3]) : oi::Codec::zstd,
                   progress);
    } else if (command == "restore" && args.size() == 3) {
        oi::restore(args[1], args[2], progress);
    } else if (command == "clone" && args.size() == 3) {
        oi::clone(args[1], args[2], progress);
    } else if (command == "compare" && args.size() == 3) {
        oi::compare(args[1], args[2]);
    } else if ((command == "verify" || command == "explore") && args.size() == 2) {
        const bool verify = command == "verify";
        const auto info = verify ? oi::verify(args[1]) : oi::inspect(args[1]);
        std::cout << "{\"status\":\"" << (verify ? "verified" : "unverified_header")
                  << "\",\"bytes\":" << info.bytes << ",\"codec\":\""
                  << oi::codec_name(info.codec) << "\"}\n";
        return;
    } else {
        throw std::invalid_argument("Invalid command or arguments. Run openimage --help.");
    }
    std::cout << "{\"status\":\"verified\"}\n";
}

bool prompt(std::string_view label, std::string& value) {
    std::cout << label << std::flush;
    return static_cast<bool>(std::getline(std::cin, value));
}

int terminal() {
#ifdef _WIN32
    const bool interactive = _isatty(_fileno(stdin)) && _isatty(_fileno(stdout));
#else
    const bool interactive = isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
#endif
    if (!interactive) throw std::invalid_argument("Terminal mode requires interactive input and output");
    try {
        print_disks(oi::discover_disks());
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
    }
    std::string command, source, target, codec, confirmation;
    while (prompt("\nOpenImage [disks/backup/restore/clone/disk-backup/disk-restore/disk-clone/verify/explore/compare/quit] > ", command)) {
        if (command == "quit") return 0;
        if (command == "disks") {
            try {
                print_disks(oi::discover_disks());
            } catch (const std::exception& error) {
                std::cerr << error.what() << '\n';
            }
            continue;
        }
        const bool destructive = command == "disk-restore" || command == "disk-clone";
        if (command != "backup" && command != "restore" && command != "clone" &&
            command != "disk-backup" && !destructive &&
            command != "verify" && command != "explore" && command != "compare") {
            std::cerr << "Unknown operation\n";
            continue;
        }
        if (!prompt("Input file or device path > ", source)) break;
        Arguments args{command, oi::fs::path(std::u8string(source.begin(), source.end()))};
        if (command != "verify" && command != "explore") {
            if (!prompt("Output file or device path > ", target)) break;
            args.push_back(oi::fs::path(std::u8string(target.begin(), target.end())));
        }
        if (command == "backup" || command == "disk-backup") {
            if (!prompt("Codec [zstd/raw/lz4/lzma2] > ", codec)) break;
            if (!codec.empty()) args.emplace_back(codec);
        }
        if (destructive) {
            if (!prompt("All target data will be overwritten. Retype the target path > ", confirmation)) break;
            if (confirmation != target || target.empty()) continue;
            args.emplace_back("--overwrite");
        } else {
            if (!prompt("Type run to execute the operation above > ", confirmation)) break;
            if (confirmation != "run") continue;
        }
        try {
            std::uint64_t last_percent = 101;
            run(args, [&last_percent](std::uint64_t bytes, std::uint64_t total) {
                const auto percent = bytes * 100 / total;
                if (percent != last_percent) {
                    std::cerr << "\rProcessing " << percent << '%' << std::flush;
                    if (bytes == total) std::cerr << "\n";
                    last_percent = percent;
                }
            });
        } catch (const std::exception& error) {
            std::cerr << "\n" << error.what() << '\n';
        }
    }
    return 0;
}
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
#else
int main(int argc, char** argv) {
#endif
    try {
        Arguments args;
        for (int index = 1; index < argc; ++index) args.emplace_back(argv[index]);
        if (!args.empty() && args.front() == "--cli") {
            args.erase(args.begin());
        } else if (args.size() == 1 && args.front() == "--tui") {
            return terminal();
        }
#ifndef _WIN32
        else if (args.empty() && isatty(STDIN_FILENO) && isatty(STDOUT_FILENO) &&
                 !std::getenv("DISPLAY") && !std::getenv("WAYLAND_DISPLAY")) {
            return terminal();
        }
#endif
        if (args.empty() || (args.size() == 1 && args.front() == "--help")) {
            usage();
            return 0;
        }
        run(args);
        return 0;
    } catch (const std::invalid_argument& error) {
        std::cerr << "OpenImage: " << error.what() << '\n';
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "OpenImage: " << error.what() << '\n';
        return 1;
    }
}



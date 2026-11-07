#include "engine.hpp"

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
    std::cout << "OpenImage 0.1.0 | regular-file operations\n"
                 "  openimage [--cli] backup SOURCE IMAGE [raw|zstd|lz4|lzma2]\n"
                 "  openimage [--cli] restore IMAGE NEW_FILE\n"
                 "  openimage [--cli] clone SOURCE NEW_FILE\n"
                 "  openimage [--cli] verify IMAGE\n"
                 "  openimage [--cli] explore IMAGE\n"
                 "  openimage [--cli] compare IMAGE RESTORED_FILE\n"
                 "  openimage --tui\n"
                 "Successful commands emit JSON. Existing outputs are refused.\n";
}

oi::Codec parse_codec(const oi::fs::path& argument) {
    for (const auto codec : {oi::Codec::raw, oi::Codec::zstd, oi::Codec::lz4, oi::Codec::lzma2})
        if (argument == oi::codec_name(codec)) return codec;
    throw std::invalid_argument("Unknown compression codec");
}

void run(const Arguments& args, const oi::Progress& progress = {}) {
    const auto& command = args.at(0);
    if (command == "backup" && (args.size() == 3 || args.size() == 4)) {
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
    std::string command, source, target, codec, confirmation;
    while (prompt("\nOpenImage [backup/restore/clone/verify/explore/compare/quit] > ", command)) {
        if (command == "quit") return 0;
        if (command != "backup" && command != "restore" && command != "clone" &&
            command != "verify" && command != "explore" && command != "compare") {
            std::cerr << "Unknown operation\n";
            continue;
        }
        if (!prompt("Input file > ", source)) break;
        Arguments args{command, oi::fs::path(std::u8string(source.begin(), source.end()))};
        if (command != "verify" && command != "explore") {
            if (!prompt("Second file > ", target)) break;
            args.push_back(oi::fs::path(std::u8string(target.begin(), target.end())));
        }
        if (command == "backup") {
            if (!prompt("Codec [zstd/raw/lz4/lzma2] > ", codec)) break;
            if (!codec.empty()) args.emplace_back(codec);
        }
        if (!prompt("Type run to execute the operation above > ", confirmation)) break;
        if (confirmation != "run") continue;
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



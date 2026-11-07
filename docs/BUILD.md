# Build and install

Builds use CMake 3.20 or newer, a C++20 compiler, and Python 3 for integration tests. Windows requires MSVC 2022 with the Desktop C++ workload and Windows SDK. Linux presets require Ninja and GCC, or Clang and its sanitizer runtimes for the instrumented preset. Dependency sources for the core are already under vendor. Configuration makes no network requests.

## Windows x64

    cmake --preset windows-msvc
    cmake --build --preset windows-release
    ctest --preset windows-release
    cmake --install build/windows-msvc --config Release --prefix dist/windows-x64

Debug uses the windows-debug build preset. From an MSVC x64 Developer Command Prompt, Ninja is also supported with `cmake -S . -B build/windows-ninja -G Ninja -DCMAKE_BUILD_TYPE=Release`, followed by `cmake --build build/windows-ninja` and `ctest --test-dir build/windows-ninja --output-on-failure`.

## Linux x86-64

    cmake --preset linux-gcc
    cmake --build --preset linux-release
    ctest --preset linux-release
    cmake --install build/linux-gcc --prefix dist/linux-x64

The Linux commands are supplied for qualification. See the qualification report for the builds actually executed.

## Desktop

Configure with `-DOPENIMAGE_QT=ON` and make Qt 6.5 or newer Widgets and Concurrent available to CMake. Qt is currently an external SDK and its source and runtime deployment are release blockers. Launch `openimage-desktop` from the build environment. The form uses the same engine as the terminal and command-line interfaces.

## Dependencies

| Source directory | Bundled version | Terms used |
| --- | --- | --- |
| zstd | 1.5.7 | BSD-3-Clause |
| lz4 | 1.10.0 | BSD-2-Clause for library sources |
| blake3 | 1.8.2 | CC0-1.0 option |
| lzma-sdk | 26.05 | Public domain, see SDK notices |

A root directory with the same name overrides its bundled counterpart. CMake reports the selected source paths and checks for expected source files. Compiler and linker checks cover the APIs in use. Dependency version policy, provenance audit and broader replacement-version qualification remain open.

On this Windows host the Visual Studio generator did not complete a build. The Ninja MSVC build succeeded after specifying `-DCMAKE_TRY_COMPILE_CONFIGURATION=Release`, which avoids the host's PDB error in the compiler probe. The packaged binaries use the Microsoft Visual C++ 2015–2022 x64 runtime. For the desktop executable, use Qt 6.8.3 MSVC x64 and make its bin and plugins directories available. The Windows core archive carries the CLI and the desktop executable without Qt runtime libraries. The source archive carries the core codec and checksum libraries.


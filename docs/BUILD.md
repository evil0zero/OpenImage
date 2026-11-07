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

The bundled preset builds Qt Base from the source tree, stages a static Qt SDK, then builds and installs OpenImage against it. It needs CMake 3.21 or newer and Ninja. On Windows, use the MSVC x64 Developer Command Prompt. On Linux, install the Qt X11/XCB development prerequisites before configuring. The bundled profile uses X11 on Linux and the native Windows platform plugin on Windows.

    cmake --preset bundled-qt-release
    cmake --build --preset bundled-qt-release
    ctest --test-dir build/bundled-qt-release/application --output-on-failure

The installed desktop executable is under `build/bundled-qt-release/install/bin`. The build uses `vendor/qtbase` unless a compatible `qtbase` source tree exists at the project root. Configuration and builds run without downloading dependencies. Windows system DLLs and the compiler runtime remain platform prerequisites.

The older external-SDK path is still available via `-DOPENIMAGE_QT=ON` and a Qt 6.5 or newer SDK with Widgets and Concurrent. It uses that SDK's linkage and deployment requirements. See [bundled Qt provenance and distribution notes](QT.md).

## Dependencies

| Source directory | Bundled version | Terms used |
| --- | --- | --- |
| zstd | 1.5.7 | BSD-3-Clause |
| lz4 | 1.10.0 | BSD-2-Clause for library sources |
| blake3 | 1.8.2 | CC0-1.0 option |
| lzma-sdk | 26.05 | Public domain, see SDK notices |

A root directory with the same name overrides its bundled counterpart. CMake reports the selected source paths and checks for expected source files. Compiler and linker checks cover the APIs in use. Dependency version policy, provenance audit and broader replacement-version qualification remain open.

On this Windows host the Visual Studio generator did not complete a build. The Ninja MSVC build succeeded after specifying `-DCMAKE_TRY_COMPILE_CONFIGURATION=Release`, which avoids the host's PDB error in the compiler probe. The previously published Windows development archive uses the Microsoft Visual C++ 2015–2022 x64 runtime and a separate Qt 6.8.3 MSVC x64 SDK. For a desktop build with Qt compiled in, use the bundled preset above.



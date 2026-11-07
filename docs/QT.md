# Bundled Qt Base

OpenImage uses the Qt Base subset of Qt 6.8.3, including Core, Gui, Widgets, Concurrent and the platform integration plugins. The complete Qt Base source is in `vendor/qtbase`, including upstream license notices and the Windows build fixes documented below. Qt uses a separate CMake build and install staging directory to keep its compiler settings and dependencies scoped to Qt. The application is then configured against that static install.

- [Official Qt Base source archive](https://download.qt.io/archive/qt/6.8/6.8.3/submodules/qtbase-everywhere-src-6.8.3.tar.xz)
- [Qt archive size and checksums](https://download.qt.io/archive/qt/6.8/6.8.3/submodules/qtbase-everywhere-src-6.8.3.tar.xz.mirrorlist)
- SHA-256 `56001b905601bb9023d399f3ba780d7fa940f3e4861e496a7c490331f49e0b80`
- [Qt build and platform prerequisites](https://doc.qt.io/qt-6.8/build-sources.html)
- [Qt LGPL obligations](https://www.qt.io/licensing/open-source-lgpl-obligations)

The bundled profile builds without OpenGL, Vulkan, OpenSSL or Qt's Zstandard integration. OpenImage continues to use its own Zstandard codec. The Qt Widgets desktop uses the operating system's display interfaces. On Linux this profile requires X11/XCB and its development libraries. The Qt Wayland module is outside this profile.

Qt Base has multiple license options and third-party terms. Review the license terms applicable to the selected modules and the obligations for distributing a statically linked binary, including corresponding source and the means for recipients to relink with a modified Qt where applicable. Qt's upstream license files are under `vendor/qtbase/LICENSES`.

To rebuild against modified Qt sources, place a compatible Qt Base source tree in `qtbase/` at the project root. Configure the bundled preset with a fresh output directory, for example `cmake --preset bundled-qt-release -B build/qt-relink`. Then run `cmake --build build/qt-relink` and launch the resulting desktop executable under `build/qt-relink/install/bin`. This rebuild path applies to the same OpenImage source and CMake configuration used for the bundled executable.


## Local Qt source changes

In `src/plugins/platforms/windows/qwindowswindow.cpp`, the `isSoftwareGl()` helper now requires both `QT_CONFIG(opengl)` and `QT_CONFIG(dynamicgl)` before accessing OpenGL types. On Windows, upstream Qt Base 6.8.3 left the dynamic OpenGL flag enabled when the bundled preset disabled OpenGL. This caused the platform plugin build to fail on MSVC. The local guard selects the existing false branch in that configuration. An unpatched Qt Base 6.8.3 replacement needs the same fix for this profile. The published archive checksum above applies to the original upstream source.

In `cmake/QtToolHelpers.cmake`, the generated Windows tool self-check quotes the executable and output paths and invokes the batch file through `cmd`. This lets the checks for moc, rcc and the other generated tools run from a workspace whose path contains spaces.

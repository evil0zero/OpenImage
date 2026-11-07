include(ExternalProject)

if(CMAKE_VERSION VERSION_LESS 3.21)
    message(FATAL_ERROR "Building static Qt requires CMake 3.21 or newer.")
endif()

if(NOT CMAKE_GENERATOR STREQUAL "Ninja" OR NOT CMAKE_BUILD_TYPE)
    message(FATAL_ERROR "The bundled Qt build requires single-config Ninja and CMAKE_BUILD_TYPE.")
endif()

source_root(qtbase src/corelib/CMakeLists.txt)
set(qt_install "${CMAKE_BINARY_DIR}/qt-install")
set(common_args
    "-DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}"
    "-DCMAKE_TRY_COMPILE_CONFIGURATION:STRING=${CMAKE_BUILD_TYPE}"
    "-DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}"
    "-DCMAKE_CXX_COMPILER:FILEPATH=${CMAKE_CXX_COMPILER}")
if(CMAKE_TOOLCHAIN_FILE)
    list(APPEND common_args "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${CMAKE_TOOLCHAIN_FILE}")
endif()
if(UNIX AND NOT APPLE)
    set(qt_platform_args -DQT_FEATURE_xcb=ON)
endif()

ExternalProject_Add(openimage_qtbase
    SOURCE_DIR "${qtbase_root}"
    BINARY_DIR "${CMAKE_BINARY_DIR}/qt-build"
    INSTALL_DIR "${qt_install}"
    CMAKE_ARGS
        ${common_args}
        "-DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR>"
        -DBUILD_SHARED_LIBS=OFF
        -DQT_BUILD_TESTS=OFF
        -DQT_BUILD_EXAMPLES=OFF
        -DQT_FEATURE_opengl=OFF
        -DQT_FEATURE_vulkan=OFF
        -DQT_FEATURE_openssl=OFF
        -DQT_FEATURE_zstd=OFF
        -DQT_FEATURE_system_zlib=OFF
        -DQT_FEATURE_system_pcre2=OFF
        -DQT_FEATURE_system_freetype=OFF
        -DQT_FEATURE_system_harfbuzz=OFF
        -DQT_FEATURE_system_png=OFF
        -DQT_FEATURE_system_jpeg=OFF
        ${qt_platform_args}
    BUILD_ALWAYS ON
    USES_TERMINAL_CONFIGURE ON
    USES_TERMINAL_BUILD ON
)

ExternalProject_Add(openimage_application
    DEPENDS openimage_qtbase
    SOURCE_DIR "${PROJECT_SOURCE_DIR}"
    BINARY_DIR "${CMAKE_BINARY_DIR}/application"
    INSTALL_DIR "${CMAKE_BINARY_DIR}/install"
    CMAKE_ARGS
        ${common_args}
        "-DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR>"
        "-DCMAKE_PREFIX_PATH:PATH=${qt_install}"
        "-DQt6_DIR:PATH=${qt_install}/lib/cmake/Qt6"
        "-DBUILD_TESTING:BOOL=${BUILD_TESTING}"
        -DOPENIMAGE_BUNDLED_QT=OFF
        -DOPENIMAGE_QT=ON
        -DOPENIMAGE_REQUIRE_STATIC_QT=ON
    BUILD_ALWAYS ON
    USES_TERMINAL_CONFIGURE ON
    USES_TERMINAL_BUILD ON
)



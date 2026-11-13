cmake_minimum_required(VERSION 3.20)

if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)  # MSVC runtime selection via CMAKE_MSVC_RUNTIME_LIBRARY
endif()
# Dynamic MSVC runtime (matches Node.js addons and CPython extensions).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

# The version lives in include/btoon/btoon.h (single source of truth).
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/include/btoon/btoon.h" _btoon_version_lines
     REGEX "^#define BTOON_VERSION_(MAJOR|MINOR|PATCH) [0-9]+$")
foreach(_line IN LISTS _btoon_version_lines)
    string(REGEX REPLACE "^#define BTOON_VERSION_([A-Z]+) ([0-9]+)$" "\\1;\\2" _pair "${_line}")
    list(GET _pair 0 _part)
    list(GET _pair 1 _num)
    set(_btoon_${_part} ${_num})
endforeach()

project(btoon VERSION ${_btoon_MAJOR}.${_btoon_MINOR}.${_btoon_PATCH} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Only build tests/tools/examples by default when this is the top-level project
# (bindings vendor core via add_subdirectory and just want the library).
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(_btoon_top ON)
else()
    set(_btoon_top OFF)
endif()

option(BUILD_SHARED_LIBS "Build shared library" OFF)
option(BUILD_TESTS "Build tests" ${_btoon_top})
option(BUILD_TOOLS "Build CLI tools" ${_btoon_top})
option(BUILD_EXAMPLES "Build examples" ${_btoon_top})
option(BUILD_BENCHMARKS "Build the benchmark executable" OFF)
option(BUILD_FUZZ_TESTS "Build libFuzzer targets (Clang only)" OFF)
option(ENABLE_SANITIZERS "Build with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
option(ENABLE_COVERAGE "Build with coverage instrumentation" OFF)
option(BTOON_NATIVE_ARCH "Optimize for the build machine's CPU (-march=native). Not for distributed binaries." OFF)
option(BTOON_WARNINGS_AS_ERRORS "Treat warnings in btoon sources as errors" OFF)
option(BTOON_INSTALL "Generate install rules" ${_btoon_top})
option(BTOON_BUNDLED_ZLIB "Compile zlib 1.3.2 into the library instead of using the system zlib (portable wheels/binaries)" OFF)

option(BTOON_WITH_LZ4 "Enable LZ4 compression" OFF)
option(BTOON_WITH_ZSTD "Enable Zstandard compression" OFF)
option(BTOON_WITH_BROTLI "Enable Brotli compression" OFF)
option(BTOON_WITH_SNAPPY "Enable Snappy compression" OFF)

# --- Instrumentation ------------------------------------------------------------
# Must precede add_library(): add_compile_options() only affects targets created later.

if(ENABLE_SANITIZERS AND NOT MSVC)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
    add_link_options(-fsanitize=address,undefined)
endif()
if(BUILD_FUZZ_TESTS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # Instrument the library too, so libFuzzer gets coverage feedback from the decoder.
    add_compile_options(-fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()
if(ENABLE_COVERAGE AND NOT MSVC)
    add_compile_options(--coverage)
    add_link_options(--coverage)
endif()

# --- Library -------------------------------------------------------------------

set(BTOON_SOURCES
    src/btoon.cpp
    src/internal.cpp
    src/encoder.cpp
    src/decoder.cpp
    src/compression.cpp
    src/security.cpp
    src/sha256.cpp
    src/capi.cpp
    src/stream_encoder.cpp
    src/stream_decoder.cpp
    src/schema.cpp
    src/pattern.cpp
    src/memory_pool.cpp
    src/validator.cpp
    src/zero_copy.cpp
    src/delta_codec.cpp
    src/rle_codec.cpp
    src/dictionary_codec.cpp
)

set(BTOON_PUBLIC_HEADERS
    include/btoon/btoon.h
    include/btoon/capi.h
    include/btoon/compression.h
    include/btoon/decoder.h
    include/btoon/delta_codec.h
    include/btoon/dictionary_codec.h
    include/btoon/encoder.h
    include/btoon/memory_pool.h
    include/btoon/rle_codec.h
    include/btoon/schema.h
    include/btoon/security.h
    include/btoon/stream_decoder.h
    include/btoon/stream_encoder.h
    include/btoon/utils.h
    include/btoon/validator.h
    include/btoon/zero_copy.h
)

add_library(btoon_core ${BTOON_SOURCES} ${BTOON_PUBLIC_HEADERS} src/internal.h src/pattern.h)
add_library(btoon::btoon_core ALIAS btoon_core)
set_target_properties(btoon_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(btoon_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(btoon_core PUBLIC cxx_std_20)

if(BTOON_BUNDLED_ZLIB)
    # Built from the release sources (signature-verified upstream tarball, pinned by hash)
    # rather than zlib's own CMakeLists, whose minimum version CMake 4 rejects.
    # Hidden visibility keeps these symbols internal to the module that links them, so
    # they can't clash with another zlib in the same process (e.g. CPython's or Node's).
    # (Z_PREFIX isn't used: its macros would also rename btoon::compress.)
    enable_language(C)
    include(FetchContent)
    FetchContent_Declare(btoon_zlib
        URL https://github.com/madler/zlib/releases/download/v1.3.2/zlib-1.3.2.tar.gz
        URL_HASH SHA256=bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SOURCE_SUBDIR do-not-add-subdirectory)
    FetchContent_MakeAvailable(btoon_zlib)
    set(_z "${btoon_zlib_SOURCE_DIR}")
    add_library(btoon_zlib STATIC
        ${_z}/adler32.c ${_z}/compress.c ${_z}/crc32.c ${_z}/deflate.c ${_z}/infback.c ${_z}/inffast.c
        ${_z}/inflate.c ${_z}/inftrees.c ${_z}/trees.c ${_z}/uncompr.c ${_z}/zutil.c)
    target_include_directories(btoon_zlib PUBLIC $<BUILD_INTERFACE:${_z}>)
    set_target_properties(btoon_zlib PROPERTIES POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden)
    if(MSVC)
        target_compile_definitions(btoon_zlib PRIVATE _CRT_SECURE_NO_DEPRECATE _CRT_NONSTDC_NO_DEPRECATE)
    endif()
    target_link_libraries(btoon_core PUBLIC btoon_zlib)
else()
    find_package(ZLIB REQUIRED)
    target_link_libraries(btoon_core PUBLIC ZLIB::ZLIB)
endif()
if(WIN32)
    target_compile_definitions(btoon_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# Warnings apply to our own sources only.
if(MSVC)
    target_compile_options(btoon_core PRIVATE /W4 /permissive- /Zc:__cplusplus /wd4100 /wd4127)
    if(BTOON_WARNINGS_AS_ERRORS)
        target_compile_options(btoon_core PRIVATE /WX)
    endif()
else()
    target_compile_options(btoon_core PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter)
    if(BTOON_WARNINGS_AS_ERRORS)
        target_compile_options(btoon_core PRIVATE -Werror)
    endif()
    if(BTOON_NATIVE_ARCH)
        target_compile_options(btoon_core PRIVATE -march=native)
    endif()
endif()

# Optional compression libraries: CMake package configs first (vcpkg, Conan, most
# distributions), then pkg-config. Each dependency that's found is recorded so the
# installed btoonConfig.cmake can find it again for consumers of the static library.
find_package(PkgConfig QUIET)
set_property(GLOBAL PROPERTY BTOON_CONFIG_DEPENDENCIES "")
function(btoon_optional_dep option_var define pc_modules cmake_pkg cmake_targets)
    if(NOT ${option_var})
        return()
    endif()
    set(_found FALSE)
    if(cmake_pkg)
        find_package(${cmake_pkg} CONFIG QUIET)
        foreach(_t IN LISTS cmake_targets)
            if(TARGET ${_t})
                target_link_libraries(btoon_core PUBLIC ${_t})
                set_property(GLOBAL APPEND_STRING PROPERTY BTOON_CONFIG_DEPENDENCIES
                    "find_dependency(${cmake_pkg} CONFIG)\n")
                set(_found TRUE)
                break()
            endif()
        endforeach()
    endif()
    if(NOT _found AND PkgConfig_FOUND)
        pkg_check_modules(_dep_${define} QUIET IMPORTED_TARGET ${pc_modules})
        if(_dep_${define}_FOUND)
            target_link_libraries(btoon_core PUBLIC PkgConfig::_dep_${define})
            string(REPLACE ";" " " _mods "${pc_modules}")
            set_property(GLOBAL APPEND_STRING PROPERTY BTOON_CONFIG_DEPENDENCIES
                "find_dependency(PkgConfig)\npkg_check_modules(_dep_${define} REQUIRED IMPORTED_TARGET ${_mods})\n")
            set(_found TRUE)
        endif()
    endif()
    if(_found)
        target_compile_definitions(btoon_core PUBLIC ${define})
        message(STATUS "btoon: ${define} enabled")
    else()
        message(WARNING "btoon: ${option_var}=ON but the library was not found; disabling")
        set(${option_var} OFF PARENT_SCOPE)
    endif()
endfunction()

btoon_optional_dep(BTOON_WITH_LZ4 BTOON_WITH_LZ4 "liblz4" lz4 "lz4::lz4;LZ4::lz4_static;LZ4::lz4_shared")
btoon_optional_dep(BTOON_WITH_ZSTD BTOON_WITH_ZSTD "libzstd" zstd "zstd::libzstd_static;zstd::libzstd_shared;zstd::libzstd")
btoon_optional_dep(BTOON_WITH_BROTLI BTOON_WITH_BROTLI "libbrotlienc;libbrotlidec" unofficial-brotli "unofficial::brotli::brotlienc")
btoon_optional_dep(BTOON_WITH_SNAPPY BTOON_WITH_SNAPPY "snappy" Snappy "Snappy::snappy")
if(BTOON_WITH_BROTLI AND TARGET unofficial::brotli::brotlidec)
    target_link_libraries(btoon_core PUBLIC unofficial::brotli::brotlidec)
endif()

# --- Examples ------------------------------------------------------------------

if(BUILD_EXAMPLES)
    add_executable(example_basic examples/basic.cpp)
    target_link_libraries(example_basic PRIVATE btoon_core)
    add_executable(example_tabular examples/tabular.cpp)
    target_link_libraries(example_tabular PRIVATE btoon_core)
endif()

# --- Tests ---------------------------------------------------------------------

if(BUILD_TESTS)
    enable_testing()
    find_package(GTest QUIET)
    if(NOT GTest_FOUND)
        include(FetchContent)
        FetchContent_Declare(
            googletest
            URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.tar.gz
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
    endif()

    add_executable(btoon_tests
        tests/test_btoon.cpp
        tests/test_encoder.cpp
        tests/test_encoder_tabular.cpp
        tests/test_decoder.cpp
        tests/test_decoder_tabular.cpp
        tests/test_hardening.cpp
        tests/test_vectors.cpp
        tests/test_security.cpp
        tests/test_capi.cpp
        tests/test_streaming.cpp
        tests/test_schema.cpp
        tests/test_schema_versioning.cpp
        tests/test_schema_inference.cpp
        tests/test_performance.cpp
        tests/test_compression_levels.cpp
        tests/test_validator.cpp
        tests/test_codecs.cpp
        tests/test_pattern.cpp
    )
    target_link_libraries(btoon_tests PRIVATE btoon_core GTest::gtest GTest::gtest_main)
    target_include_directories(btoon_tests PRIVATE include/third_party src)
    target_compile_definitions(btoon_tests PRIVATE
        BTOON_VECTORS_FILE="${CMAKE_CURRENT_SOURCE_DIR}/tests/vectors/btoon-vectors.json")

    include(GoogleTest)
    gtest_discover_tests(btoon_tests DISCOVERY_TIMEOUT 60)

    # Mutation fuzzer that runs without libFuzzer (works on any compiler).
    add_executable(btoon_fuzz_smoke tests/fuzz_smoke.cpp)
    target_link_libraries(btoon_fuzz_smoke PRIVATE btoon_core)
    target_include_directories(btoon_fuzz_smoke PRIVATE src)
    target_compile_definitions(btoon_fuzz_smoke PRIVATE
        BTOON_VECTORS_FILE="${CMAKE_CURRENT_SOURCE_DIR}/tests/vectors/btoon-vectors.json")
    target_include_directories(btoon_fuzz_smoke PRIVATE include/third_party)
    add_test(NAME fuzz_smoke COMMAND btoon_fuzz_smoke 20000)

    # tools/json_mapping.h: the JSON <-> BTOON mapping of the CLI tools (header-only).
    add_executable(btoon_tools_tests tests/test_json_mapping.cpp)
    target_link_libraries(btoon_tools_tests PRIVATE btoon_core GTest::gtest GTest::gtest_main)
    target_include_directories(btoon_tools_tests PRIVATE include/third_party tools)
    target_compile_definitions(btoon_tools_tests PRIVATE
        BTOON_VECTORS_FILE="${CMAKE_CURRENT_SOURCE_DIR}/tests/vectors/btoon-vectors.json")
    gtest_discover_tests(btoon_tools_tests DISCOVERY_TIMEOUT 60)

    # The built tools on the fixtures in tests/tools: round trips of every type, deep
    # JSON, an empty key file, failed writes. tests/tools/make_fixtures.py writes them.
    if(BUILD_TOOLS)
        add_test(NAME tools_cli
            COMMAND ${CMAKE_COMMAND}
                -DBTOON=$<TARGET_FILE:btoon>
                -DCONVERT=$<TARGET_FILE:btoon-convert>
                -DSCHEMA=$<TARGET_FILE:btoon-schema>
                -DFIXTURES=${CMAKE_CURRENT_SOURCE_DIR}/tests/tools
                -DWORK=${CMAKE_CURRENT_BINARY_DIR}/tools_cli
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/run_tools_test.cmake)
    endif()

    # Vectors are generated independently by tests/vectors/generate.py (msgpack-python oracle).
endif()

if(BUILD_BENCHMARKS)
    add_executable(btoon_bench tests/bench.cpp)
    target_link_libraries(btoon_bench PRIVATE btoon_core)
endif()

if(BUILD_FUZZ_TESTS)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        add_executable(fuzz_decoder fuzz/fuzz_decoder.cpp)
        target_link_libraries(fuzz_decoder PRIVATE btoon_core)
        target_compile_options(fuzz_decoder PRIVATE -fsanitize=fuzzer,address,undefined)
        target_link_options(fuzz_decoder PRIVATE -fsanitize=fuzzer,address,undefined)
    else()
        message(WARNING "BUILD_FUZZ_TESTS requires Clang (libFuzzer)")
    endif()
endif()

# --- Tools -----------------------------------------------------------------------

if(BUILD_TOOLS)
    add_executable(btoon tools/btooncli.cpp)
    target_include_directories(btoon PRIVATE include/third_party)
    target_link_libraries(btoon PRIVATE btoon_core)

    add_executable(btoon-schema tools/btoon-schema.cpp)
    target_include_directories(btoon-schema PRIVATE include/third_party)
    target_link_libraries(btoon-schema PRIVATE btoon_core)

    add_executable(btoon-convert tools/btoon-convert.cpp)
    target_include_directories(btoon-convert PRIVATE include/third_party)
    target_link_libraries(btoon-convert PRIVATE btoon_core)

    add_executable(generate-test-data tests/interop/generate-test-data.cpp)
    target_link_libraries(generate-test-data PRIVATE btoon_core)
    add_executable(validate-test-data tests/interop/validate-test-data.cpp)
    target_link_libraries(validate-test-data PRIVATE btoon_core)
endif()

# --- Install ---------------------------------------------------------------------

if(BTOON_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)
    set(_btoon_install_targets btoon_core)
    if(BTOON_BUNDLED_ZLIB)
        list(APPEND _btoon_install_targets btoon_zlib)
    endif()
    install(TARGETS ${_btoon_install_targets} EXPORT btoonTargets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(FILES ${BTOON_PUBLIC_HEADERS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/btoon)
    if(BUILD_TOOLS)
        install(TARGETS btoon btoon-schema btoon-convert RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
        install(FILES docs/btoon.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
    endif()
    install(EXPORT btoonTargets FILE btoonTargets.cmake NAMESPACE btoon::
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/btoon)
    if(BTOON_BUNDLED_ZLIB)
        set(_btoon_find_zlib "")
    else()
        set(_btoon_find_zlib "find_dependency(ZLIB)\n")
    endif()
    get_property(_btoon_codec_deps GLOBAL PROPERTY BTOON_CONFIG_DEPENDENCIES)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/btoonConfig.cmake"
         "include(CMakeFindDependencyMacro)\n${_btoon_find_zlib}${_btoon_codec_deps}include(\"\${CMAKE_CURRENT_LIST_DIR}/btoonTargets.cmake\")\n")
    write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/btoonConfigVersion.cmake"
        VERSION ${PROJECT_VERSION} COMPATIBILITY SameMinorVersion)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/btoonConfig.cmake"
                  "${CMAKE_CURRENT_BINARY_DIR}/btoonConfigVersion.cmake"
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/btoon)
    set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
    include(CPack)
endif()

cmake_minimum_required(VERSION 3.20)
cmake_policy(VERSION 3.20)

# Enable MSVC runtime library selection
if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

# Use dynamic runtime library for MSVC (must be set before project())
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

project(btoon VERSION 0.0.1 LANGUAGES CXX)

# Include CMake modules
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# C++20 required (C++23 features used where available)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build options
option(BUILD_SHARED_LIBS "Build shared library" OFF)
option(BUILD_TESTS "Build tests" ON)
option(BUILD_BENCHMARKS "Build benchmarks" ON)
option(BUILD_EXAMPLES "Build examples" ON)
option(BUILD_TOOLS "Build CLI tools" ON)
option(BUILD_PYTHON_BINDINGS "Build Python bindings" OFF)
option(BUILD_JAVASCRIPT_BINDINGS "Build JavaScript/Node.js bindings" OFF)
option(ENABLE_SANITIZERS "Enable sanitizers (ASan, MSan, UBSan)" OFF)
option(ENABLE_FUZZING "Enable fuzzing support" OFF)
option(BUILD_FUZZ_TESTS "Build fuzz testing targets" OFF)
option(ENABLE_COVERAGE "Enable code coverage" OFF)

# Compiler-specific options
if(MSVC)
    # MSVC specific flags for C++20 conformance
    add_compile_options(
        /permissive-        # Strict conformance mode
        /Zc:__cplusplus     # Enable updated __cplusplus macro
        /std:c++20          # C++20 standard
        /EHsc               # Enable C++ exception handling
        /W4                 # Warning level 4
        /wd4100             # Disable unreferenced parameter warning
        /wd4127             # Disable conditional expression is constant
        /wd4996             # Disable deprecated warnings
    )
    
    # Optimization flags
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(/O2 /GL)
    else()
        add_compile_options(/Od /Zi)
    endif()
    
    # Fix for if constexpr issues
    add_compile_definitions(_HAS_CXX17=1 _HAS_CXX20=1)

    # Prevent Windows min/max macros from conflicting with std::numeric_limits
    add_compile_definitions(NOMINMAX)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
        -Wno-unused-parameter
    )
    
    # Optimization flags
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-O3 -march=native)
    else()
        add_compile_options(-O0 -g)
    endif()
    
    # C++20 features
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 14.0)
        add_compile_options(-std=c++20)
    endif()
endif()

# Sanitizers
if(ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address,undefined")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-omit-frame-pointer")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-optimize-sibling-calls")
    
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=memory")
    endif()
endif()

# Fuzzing support
if(ENABLE_FUZZING AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=fuzzer,address,undefined")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-omit-frame-pointer")
endif()

# Code coverage
if(ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --coverage")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --coverage")
endif()

# Find dependencies
find_package(ZLIB REQUIRED)
find_package(OpenSSL REQUIRED)

# PkgConfig is only needed on Unix systems for optional compression libraries
# On Windows, we skip PkgConfig and rely on vcpkg or manual library finding
if(WIN32)
    set(PKG_CONFIG_FOUND FALSE)
else()
    find_package(PkgConfig REQUIRED)
    set(PKG_CONFIG_FOUND TRUE)
endif()

# Optional dependencies
option(BTOON_WITH_LZ4 "Enable LZ4 compression" OFF)
option(BTOON_WITH_ZSTD "Enable Zstandard compression" OFF)
option(BTOON_WITH_BROTLI "Enable Brotli compression" OFF)
option(BTOON_WITH_SNAPPY "Enable Snappy compression" OFF)


# Source files
set(BTOON_SOURCES
    src/btoon.cpp
    src/encoder.cpp
    src/decoder.cpp
    src/compression.cpp
    src/security.cpp
    src/capi.cpp
    src/stream_encoder.cpp
    src/stream_decoder.cpp
    src/schema.cpp
    src/memory_pool.cpp
    src/validator.cpp
    src/zero_copy.cpp
    src/delta_codec.cpp
    src/rle_codec.cpp
    src/dictionary_codec.cpp
)

set(BTOON_HEADERS
    include/btoon/btoon.h
    include/btoon/encoder.h
    include/btoon/decoder.h
    include/btoon/compression.h
    include/btoon/security.h
    include/btoon/utils.h
    include/btoon/capi.h
    include/btoon/stream_encoder.h
    include/btoon/stream_decoder.h
    include/btoon/schema.h
)

# Main library
add_library(btoon_core ${BTOON_SOURCES} ${BTOON_HEADERS})

target_include_directories(btoon_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(btoon_core
    PUBLIC
        ZLIB::ZLIB
        OpenSSL::SSL
)

# Link Windows socket library for byte order functions (htonl, ntohl, etc.)
if(WIN32)
    target_link_libraries(btoon_core PUBLIC ws2_32)
endif()

# Find and link optional compression libraries
# PkgConfig is only available on Unix systems
if(PKG_CONFIG_FOUND)
    # LZ4
    if(BTOON_WITH_LZ4)
        pkg_check_modules(LZ4 liblz4)
        if(LZ4_FOUND)
            target_include_directories(btoon_core PUBLIC ${LZ4_INCLUDE_DIRS})
            target_link_libraries(btoon_core PUBLIC ${LZ4_LIBRARIES})
            target_compile_definitions(btoon_core PUBLIC BTOON_WITH_LZ4)
            message(STATUS "LZ4 compression enabled")
        else()
            message(STATUS "LZ4 not found, disabling LZ4 support")
            set(BTOON_WITH_LZ4 OFF)
        endif()
    endif()
    
    # ZSTD
    if(BTOON_WITH_ZSTD)
        pkg_check_modules(ZSTD libzstd)
        if(ZSTD_FOUND)
            target_include_directories(btoon_core PUBLIC ${ZSTD_INCLUDE_DIRS})
            target_link_libraries(btoon_core PUBLIC ${ZSTD_LIBRARIES})
            target_compile_definitions(btoon_core PUBLIC BTOON_WITH_ZSTD)
            message(STATUS "ZSTD compression enabled")
        else()
            message(STATUS "ZSTD not found, disabling ZSTD support")
            set(BTOON_WITH_ZSTD OFF)
        endif()
    endif()
    
    # Brotli
    if(BTOON_WITH_BROTLI)
        pkg_check_modules(BROTLI_ENC libbrotlienc)
        pkg_check_modules(BROTLI_DEC libbrotlidec)
        if(BROTLI_ENC_FOUND AND BROTLI_DEC_FOUND)
            target_include_directories(btoon_core PUBLIC ${BROTLI_ENC_INCLUDE_DIRS} ${BROTLI_DEC_INCLUDE_DIRS})
            target_link_directories(btoon_core PUBLIC ${BROTLI_ENC_LIBRARY_DIRS} ${BROTLI_DEC_LIBRARY_DIRS})
            target_link_libraries(btoon_core PUBLIC ${BROTLI_ENC_LIBRARIES} ${BROTLI_DEC_LIBRARIES})
            target_compile_definitions(btoon_core PUBLIC BTOON_WITH_BROTLI)
            message(STATUS "Brotli compression enabled")
        else()
            message(STATUS "Brotli not found, disabling Brotli support")
            set(BTOON_WITH_BROTLI OFF)
        endif()
    endif()
    
    # Snappy
    if(BTOON_WITH_SNAPPY)
        pkg_check_modules(SNAPPY snappy)
        if(SNAPPY_FOUND)
            target_include_directories(btoon_core PUBLIC ${SNAPPY_INCLUDE_DIRS})
            target_link_libraries(btoon_core PUBLIC ${SNAPPY_LIBRARIES})
            target_compile_definitions(btoon_core PUBLIC BTOON_WITH_SNAPPY)
            message(STATUS "Snappy compression enabled")
        else()
            message(STATUS "Snappy not found, disabling Snappy support")
            set(BTOON_WITH_SNAPPY OFF)
        endif()
    endif()
endif()

# Version info
target_compile_definitions(btoon_core
    PUBLIC
        BTOON_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
        BTOON_VERSION_MINOR=${PROJECT_VERSION_MINOR}
        BTOON_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)

# Examples
if(BUILD_EXAMPLES)
    add_executable(example_basic examples/basic.cpp)
    target_link_libraries(example_basic btoon_core)
    
    add_executable(example_tabular examples/tabular.cpp)
    target_link_libraries(example_tabular btoon_core)
    
    # Examples are not installed - they are for testing/demonstration only
endif()

# Tests
if(BUILD_TESTS)
    enable_testing()
    
    # Find or fetch GoogleTest
    find_package(GTest QUIET)
    if(NOT GTest_FOUND)
        include(FetchContent)
        FetchContent_Declare(
            googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG main
        )
        FetchContent_MakeAvailable(googletest)
    endif()
    
    # Unit tests
    add_executable(btoon_tests
        tests/test_encoder.cpp
        tests/test_encoder_tabular.cpp
        tests/test_decoder.cpp
        tests/test_decoder_tabular.cpp
        tests/test_security.cpp
        tests/test_capi.cpp
        tests/test_streaming.cpp
        tests/test_schema.cpp
        tests/test_schema_versioning.cpp
        tests/test_schema_inference.cpp
        tests/test_performance.cpp
        tests/test_compression_levels.cpp
        tests/test_validator.cpp
    )
    target_link_libraries(btoon_tests
        PRIVATE
            btoon_core
            GTest::gtest
            GTest::gtest_main
    )
    
    include(GoogleTest)
    gtest_discover_tests(btoon_tests)
    
    # Fuzz targets (if enabled)
    # TODO: Re-enable when linker issue is resolved
    # if(ENABLE_FUZZING)
    #     add_executable(btoon_fuzz_decoder tests/fuzz_decoder.cpp)
    #     target_compile_options(btoon_fuzz_decoder PRIVATE -fsanitize=fuzzer)
    #     target_link_libraries(btoon_fuzz_decoder PRIVATE btoon_core -fsanitize=fuzzer)
    #     set_target_properties(btoon_fuzz_decoder PROPERTIES
    #         CXX_STANDARD 20
    #     )
    # endif()
endif()

# Benchmarks
# if(BUILD_BENCHMARKS)
#     add_executable(btoon_benchmark tests/benchmark.cpp)
#     target_link_libraries(btoon_benchmark
#         PRIVATE
#             btoon_core
#             benchmark::benchmark
#             benchmark::benchmark_main
#     )
# endif()

# CLI tools
if(BUILD_TOOLS)
    # btoon CLI tool
    add_executable(btoon tools/btooncli.cpp)
    target_include_directories(btoon PRIVATE include/third_party)
    target_link_libraries(btoon btoon_core)
    install(TARGETS btoon RUNTIME DESTINATION bin)
    install(FILES docs/btoon.1 DESTINATION share/man/man1)
    
    # btoon-schema compiler tool
    add_executable(btoon-schema tools/btoon-schema.cpp)
    target_include_directories(btoon-schema PRIVATE include/third_party)
    target_link_libraries(btoon-schema btoon_core)
    target_compile_features(btoon-schema PRIVATE cxx_std_20)
    install(TARGETS btoon-schema RUNTIME DESTINATION bin)
    
    # btoon-convert format converter tool
    add_executable(btoon-convert tools/btoon-convert.cpp)
    target_include_directories(btoon-convert PRIVATE include/third_party)
    target_link_libraries(btoon-convert btoon_core)
    target_compile_features(btoon-convert PRIVATE cxx_std_20)
    install(TARGETS btoon-convert RUNTIME DESTINATION bin)
    
    # Interoperability test tools
    add_executable(generate-test-data tests/interop/generate-test-data.cpp)
    target_link_libraries(generate-test-data btoon_core)
    target_compile_features(generate-test-data PRIVATE cxx_std_20)
    
    add_executable(validate-test-data tests/interop/validate-test-data.cpp)
    target_link_libraries(validate-test-data btoon_core)
    target_compile_features(validate-test-data PRIVATE cxx_std_20)
endif()

# Language bindings
if(BUILD_PYTHON_BINDINGS)
    message(STATUS "Building Python bindings")
    find_package(pybind11 QUIET)
    if(pybind11_FOUND)
        pybind11_add_module(_btoon_native 
            bindings/python/module.cpp
            bindings/python/btoon_python.hpp
        )
        target_link_libraries(_btoon_native PRIVATE btoon_core)
        target_compile_features(_btoon_native PRIVATE cxx_std_20)
    else()
        message(WARNING "pybind11 not found, Python bindings will not be built")
    endif()
endif()

if(BUILD_JAVASCRIPT_BINDINGS)
    message(STATUS "JavaScript/Node.js bindings should be built using node-gyp")
    message(STATUS "Run: cd bindings/javascript && npm install")
endif()

# Fuzzing targets
if(BUILD_FUZZ_TESTS)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        # LibFuzzer setup
        add_executable(fuzz_decoder fuzz/fuzz_decoder.cpp)
        target_link_libraries(fuzz_decoder PRIVATE btoon_core)
        target_compile_options(fuzz_decoder PRIVATE -fsanitize=fuzzer,address)
        target_link_options(fuzz_decoder PRIVATE -fsanitize=fuzzer,address)
        
        # Create fuzzing corpus directory
        file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/fuzz_corpus)
    else()
        message(WARNING "Fuzzing targets require Clang compiler for LibFuzzer support")
    endif()
endif()

# Installation
install(TARGETS btoon_core
    EXPORT btoonTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)

install(DIRECTORY include/btoon
    DESTINATION include
    FILES_MATCHING PATTERN "*.h"
)

install(EXPORT btoonTargets
    FILE btoonTargets.cmake
    NAMESPACE btoon::
    DESTINATION lib/cmake/btoon
)

# CPack configuration
include(CPack)
# Optional CPack customization (if file exists locally)
# Note: CPackConfig.cmake is gitignored, so this is for local development only
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CPackConfig.cmake")
    include(cmake/CPackConfig.cmake)
endif()

# Generate config file
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/btoonConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/btoonConfigVersion.cmake"
    DESTINATION lib/cmake/btoon
)

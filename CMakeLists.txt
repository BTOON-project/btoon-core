cmake_minimum_required(VERSION 3.20)
cmake_policy(VERSION 3.20)
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
option(BUILD_TOOLS "Build CLI tools" OFF)
option(ENABLE_SANITIZERS "Enable sanitizers (ASan, MSan, UBSan)" OFF)
option(ENABLE_FUZZING "Enable fuzzing support" OFF)
option(ENABLE_COVERAGE "Enable code coverage" OFF)

# Compiler-specific options
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
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

find_package(PkgConfig REQUIRED)

# Optional dependencies
option(BTOON_WITH_LZ4 "Enable LZ4 compression" ON)
option(BTOON_WITH_ZSTD "Enable Zstandard compression" ON)

if(BTOON_WITH_LZ4)
    pkg_check_modules(LZ4 QUIET liblz4)
    if(LZ4_FOUND)
        message(STATUS "Found LZ4: ${LZ4_VERSION}")
    else()
        message(STATUS "LZ4 not found, disabling LZ4 support.")
        set(BTOON_WITH_LZ4 OFF)
    endif()
endif()

if(BTOON_WITH_ZSTD)
    pkg_check_modules(ZSTD QUIET libzstd)
    if(ZSTD_FOUND)
        message(STATUS "Found Zstd: ${ZSTD_VERSION}")
    else()
        message(STATUS "Zstd not found, disabling Zstd support.")
        set(BTOON_WITH_ZSTD OFF)
    endif()
endif()

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

# Optional compression libraries
if(BTOON_WITH_LZ4 AND LZ4_FOUND)
    target_include_directories(btoon_core PUBLIC ${LZ4_INCLUDE_DIRS})
    target_link_directories(btoon_core PUBLIC ${LZ4_LIBRARY_DIRS})
    target_link_libraries(btoon_core PUBLIC ${LZ4_LIBRARIES})
    target_compile_definitions(btoon_core PUBLIC BTOON_WITH_LZ4)
endif()

if(BTOON_WITH_ZSTD AND ZSTD_FOUND)
    target_include_directories(btoon_core PUBLIC ${ZSTD_INCLUDE_DIRS})
    target_link_directories(btoon_core PUBLIC ${ZSTD_LIBRARY_DIRS})
    target_link_libraries(btoon_core PUBLIC ${ZSTD_LIBRARIES})
    target_compile_definitions(btoon_core PUBLIC BTOON_WITH_ZSTD)
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
    
    install(TARGETS example_basic example_tabular
        RUNTIME DESTINATION bin
    )
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
            GIT_TAG v1.14.0
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
if(BUILD_BENCHMARKS)
    find_package(benchmark QUIET)
    if(NOT benchmark_FOUND)
        include(FetchContent)
        FetchContent_Declare(
            googlebenchmark
            GIT_REPOSITORY https://github.com/google/benchmark.git
            GIT_TAG v1.8.3
        )
        FetchContent_MakeAvailable(googlebenchmark)
    endif()
    
    add_executable(btoon_benchmark tests/benchmark.cpp)
    target_link_libraries(btoon_benchmark
        PRIVATE
            btoon_core
            benchmark::benchmark
            benchmark::benchmark_main
    )
endif()

# CLI tool
if(BUILD_TOOLS)
    add_executable(btoon tools/btooncli.cpp)
    target_include_directories(btoon PRIVATE include/third_party)
    target_link_libraries(btoon btoon_core)
    install(TARGETS btoon RUNTIME DESTINATION bin)
    install(FILES docs/btoon.1 DESTINATION share/man/man1)
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
include(cmake/CPackConfig.cmake)

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

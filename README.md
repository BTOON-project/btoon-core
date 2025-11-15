# BTOON Core

[![Version](https://img.shields.io/badge/version-0.0.1-blue.svg)](docs/CHANGELOG.md)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)
[![Specification](https://img.shields.io/badge/spec-v0.0.1-orange.svg)](docs/btoon-spec.md)
[![C++ Standard](https://img.shields.io/badge/C++-20-blue.svg)](https://en.cppreference.com/w/cpp/20)

**BTOON** (Binary Tree Object Notation) is a high-performance binary serialization format that combines the simplicity of JSON with the efficiency of binary encoding. It provides robust type safety, schema support, built-in compression, and comprehensive security features.

**Version**: 0.0.1

## Features

### Core Capabilities
- **MessagePack Compatible**: Fully compliant with the MessagePack specification
- **Type System**: Comprehensive support for primitives, containers, and custom types
- **High Performance**: 4-7x faster than JSON with SIMD optimizations
- **Zero-Copy APIs**: Efficient memory management with minimal allocations
- **Cross-Platform**: Works on Windows, macOS, Linux, ARM, and WebAssembly

### Advanced Features
- **Compression**: Built-in support for ZLIB, LZ4, and ZSTD with adaptive selection
- **Schema Support**: Define, validate, and evolve schemas with versioning
- **Schema Inference**: Automatically generate schemas from data
- **Tabular Optimization**: Columnar encoding for homogeneous data (20-40% size reduction)
- **Extension Types**: Support for custom types with forward compatibility
- **Streaming API**: Efficiently process large datasets

### Security & Validation
- **Input Validation**: Comprehensive validation against malformed data
- **Security Features**: Protection against zip bombs, buffer overflows, and DoS attacks
- **UTF-8 Validation**: Ensures string encoding correctness
- **Size Limits**: Configurable limits for depth, size, and complexity
- **Fuzz Testing**: Continuous testing with libFuzzer and AFL++

### Developer Tools
- **CLI Tools**: `btoon`, `btoon-schema`, `btoon-convert` utilities
- **C API**: Stable C interface for language bindings
- **Rich Documentation**: Specification, guides, and examples
- **Test Vectors**: Comprehensive test suite for validation

## Documentation

- **[Format Specification](docs/btoon-spec.md)**: Complete technical specification
- **[Quick Reference](docs/QUICK-REFERENCE.md)**: Handy encoding reference
- **[Implementation Guide](docs/IMPLEMENTATION-GUIDE.md)**: Guide for implementors
- **[Architecture & Release Strategy](docs/ARCHITECTURE.md)**: Multi-repo architecture and release process
- **[API Documentation](docs/api/)**: Generated API docs (Doxygen)
- **[Changelog](docs/CHANGELOG.md)**: Version history and roadmap
- **[Contributing](CONTRIBUTING.md)**: How to contribute

## Project Organization

BTOON is organized as a multi-repository project under the [BTOON-project](https://github.com/BTOON-project) organization:

### Core Repository
- **[btoon-core](https://github.com/BTOON-project/btoon-core)** - Core C++ implementation (this repository)

### Language Bindings
- **[btoon-python](https://github.com/BTOON-project/btoon-python)** - Python bindings
- **[btoon-javascript](https://github.com/BTOON-project/btoon-javascript)** - JavaScript library (WebAssembly)
- **[btoon-nodejs](https://github.com/BTOON-project/btoon-nodejs)** - Node.js native bindings
- **[btoon-go](https://github.com/BTOON-project/btoon-go)** - Go implementation
- **[btoon-php](https://github.com/BTOON-project/btoon-php)** - PHP extension

### Documentation
- **[btoon-web](https://github.com/BTOON-project/btoon-web)** - Project website and documentation

## Requirements

- **C++20** compatible compiler (GCC 10+, Clang 12+, MSVC 2019 v16.8+)
- **CMake** 3.20 or higher
- **zlib** (required)
- **OpenSSL** (required)
- **lz4** (optional, for LZ4 compression)
- **zstd** (optional, for Zstd compression)
- **GoogleTest** (optional, for tests - automatically fetched if not found)

### Installing Dependencies

#### macOS (Homebrew)

```bash
brew install cmake zlib openssl lz4 zstd
```

#### Ubuntu/Debian

```bash
sudo apt-get update
sudo apt-get install build-essential cmake libz-dev libssl-dev liblz4-dev libzstd-dev
```

## Installation

### Pre-built Binaries

Pre-built binaries are available for each release on the [Releases page](https://github.com/BTOON-project/btoon-core/releases).

#### Download and Install

**Linux/macOS:**
```bash
# Download the appropriate binary for your platform
wget https://github.com/BTOON-project/btoon-core/releases/download/v0.0.1/btoon-0.0.1-linux-x64.tar.gz

# Extract
tar xzf btoon-0.0.1-linux-x64.tar.gz

# Install (optional)
cd btoon-0.0.1-linux-x64
sudo cp btoon* /usr/local/bin/
sudo cp lib* /usr/local/lib/
sudo cp -r include/* /usr/local/include/
```

**Windows:**
```powershell
# Download and extract the Windows ZIP
# Add the extracted directory to your PATH environment variable
```

**Available Platforms:**
- Linux x64/ARM64
- macOS x64/ARM64 (Intel/Apple Silicon)
- Windows x64
- WebAssembly

### Building from Source

```bash
git clone https://github.com/BTOON-project/btoon-core.git
cd btoon-core
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
ctest
sudo cmake --install . --prefix /usr/local
```

### Build Options

- `BTOON_WITH_LZ4`: Enable LZ4 compression support (default: ON)
- `BTOON_WITH_ZSTD`: Enable Zstd compression support (default: ON)
- `BUILD_SHARED_LIBS`: Build shared library instead of static (default: OFF)
- `BUILD_TESTS`: Build test suite (default: ON)

## Basic Usage

```cpp
#include "btoon/btoon.h"
#include <iostream>

int main() {
    using namespace btoon;

    // 1. Construct a btoon::Value object
    Value original = Map{
        {"name", String("Alice")},
        {"age", Int(30)},
        {"active", Bool(true)}
    };

    // 2. Encode the value
    auto encoded = encode(original);

    // 3. Decode the data
    auto decoded = decode(encoded);

    // 4. Access the data
    if (auto* map = std::get_if<Map>(&decoded)) {
        std::cout << "Name: " << std::get<String>((*map)["name"]) << std::endl;
    }
    return 0;
}
```

## API Reference

### Core Types

```cpp
// The primary variant type for all BTOON values
struct Value : std::variant<...>;

// Options for encoding
struct EncodeOptions {
    bool compress = false;
    CompressionAlgorithm compression_algorithm = CompressionAlgorithm::ZLIB;
    int compression_level = 0; // 0 for default
    bool auto_tabular = true;
};

// Options for decoding
struct DecodeOptions {
    bool decompress = false;
    bool strict = true;
};
```

### Functions

```cpp
// Encode a value to a byte vector
std::vector<uint8_t> encode(const Value& value, const EncodeOptions& options = {});

// Decode a byte span to a value
Value decode(std::span<const uint8_t> data, const DecodeOptions& options = {});
```

### Streaming API

```cpp
#include "btoon/stream_encoder.h"
#include "btoon/stream_decoder.h"
#include <sstream>

// --- Encoding ---
std::stringstream ss;
StreamEncoder encoder(ss);
encoder.write(Int(1));
encoder.write(String("two"));
encoder.close();

// --- Decoding ---
StreamDecoder decoder(ss);
while(auto value = decoder.read()) {
    // process *value
}
```

### Schema Validation

```cpp
#include "btoon/schema.h"

Schema schema(Map{
    {"name", String("string")},
    {"age", String("int")}
});

Value user = Map{{"name", String("Alice")}, {"age", Int(30)}};
bool is_valid = schema.validate(user); // true
```

## Roadmap

- [x] Core C++ library
- [x] MessagePack compatibility
- [x] Tabular data optimization
- [x] Compression support (Zlib, LZ4, Zstd)
- [x] Streaming API for large datasets
- [x] Schema validation
- [x] C-API for language bindings
- [ ] Python bindings (pybind11)
- [ ] Go bindings (cgo)
- [ ] JavaScript bindings (N-API + WASM)

## Changelog

### Version 0.0.1

- **Features**
    - Unified, variant-based C++20 API.
    - Added LZ4 and Zstandard compression.
    - Added a streaming API for encoding/decoding.
    - Added schema validation capabilities.
    - Added a stable C-API for language bindings.
- **Fixes**
    - Resolved major API inconsistencies between documentation and implementation.
    - Implemented missing `TIMESTAMP` data type handling.
- **Project**
    - Updated all examples and tests to the new unified API.
    - Switched from Catch2 to GTest for testing consistency.

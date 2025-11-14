# BTOON Core

[![Version](https://img.shields.io/badge/version-0.0.1-blue.svg)](https://github.com/yourorg/btoon-core)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)
[![C++ Standard](https://img.shields.io/badge/C++-20-blue.svg)](https://en.cppreference.com/w/cpp/20)

**BTOON** (Binary TOON) is a high-performance binary serialization format that extends MessagePack with a custom tabular data optimization. It provides 20-40% size reduction for uniform tabular datasets while maintaining full MessagePack compatibility.

**Version**: 0.0.1

## Features

- **MessagePack Compatible**: Fully compliant with the MessagePack specification.
- **Tabular Optimization**: Custom extension (type `-1`) for compact tabular data encoding.
- **High Performance**: 4-7x faster than JSON, comparable to MessagePack.
- **Optional Compression**: Built-in support for Zlib, LZ4, and Zstd.
- **Streaming API**: Efficiently encode and decode sequences of objects.
- **Schema Validation**: Validate data against a user-defined schema.
- **C-API**: A stable C API for creating language bindings.
- **Type Safe**: Comprehensive, modern C++20 type system.
- **Zero-Copy Decoding**: Efficient `std::span` API for minimal allocations.

## Requirements

- **C++20** compatible compiler (GCC 10+, Clang 12+, MSVC 2019+)
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

### Building from Source

```bash
git clone https://github.com/yourorg/btoon-core.git
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

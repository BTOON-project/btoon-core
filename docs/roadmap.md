# BTOON Core Roadmap

## Version 0.0.1 (Current)

### Core Features
- [x] Unified, variant-based C++20 API
- [x] MessagePack-compatible binary encoding/decoding
- [x] Tabular extension type (-1) for schema-aware compression
- [x] Streaming API for large datasets
- [x] Schema validation
- [x] C-API for language bindings
- [x] CLI tool (`btoon`) for encoding/decoding JSON

### Compression & Security
- [x] Zlib compression support
- [x] LZ4 compression support
- [x] Zstd compression support
- [x] HMAC security signing

### Extended Types
- [x] `Timestamp` extension type
- [x] `Date` extension type
- [x] `BigInt` extension type

### Testing & Quality
- [x] Comprehensive test suite using GoogleTest
- [x] All decoding is bounds-checked and UB-free

## Version 0.0.2 - Performance & Bindings

### Language Bindings
- [x] Node.js bindings (N-API) - `btoon-nodejs` - **Implemented**
- [ ] Python bindings (pybind11) - `btoon-python` - In progress
- [ ] Go bindings (cgo) - `btoon-go` - Planned
- [ ] PHP bindings - `btoon-php` - Planned
- [ ] Javascript bindings - `btoon-javascript` - Planned

### Performance Optimizations
- [x] Zero-copy decoding with `std::span` API - **Implemented**
- [ ] SIMD optimizations for bulk operations
- [ ] Memory pool allocator for reduced allocations
- [ ] Columnar compression codecs (delta, RLE, dictionary)

### Testing & Quality
- [x] Performance benchmarks vs JSON/BSON/MessagePack - **Implemented** (benchmark.cpp)
- [x] libFuzzer fuzz targets - **Implemented** (fuzz_decoder.cpp)
- [ ] Performance regression tests

## Version 0.0.3 - Advanced Features

### Schema Evolution
- [ ] Schema versioning support
- [ ] Forward/backward compatibility
- [ ] Schema inference from data

### Tooling
- [ ] Schema compiler (btoon-schema)
- [ ] Format converter (btoon-convert)

## Version 0.1.0 - Ecosystem & Distribution

### Distribution
- [ ] CPack packaging (DEB, RPM, TGZ)
- [ ] Conan package manager support
- [ ] vcpkg package manager support
- [ ] Prebuilt binaries for major platforms

### Documentation
- [ ] API reference (Doxygen)
- [ ] Tutorial guide
- [ ] Performance tuning guide

## Contributing

See the main `README.md` for guidelines on contributing to BTOON.


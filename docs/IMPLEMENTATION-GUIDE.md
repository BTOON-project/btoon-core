# BTOON Implementation Guide

Version: 0.0.1  
Date: November 15, 2025

This guide provides detailed information for implementing BTOON-compatible libraries and understanding the reference implementation.

## Implementation Status (v0.0.1)

### ✅ Fully Implemented Features

#### Core Type System
- ✅ **Nil** type (0xc0)
- ✅ **Boolean** types (0xc2, 0xc3)
- ✅ **Integer** encoding
  - ✅ Positive fixint (0x00-0x7f)
  - ✅ Negative fixint (0xe0-0xff)
  - ✅ uint8/16/32/64
  - ✅ int8/16/32/64
- ✅ **Float** types
  - ✅ float32 (IEEE 754)
  - ✅ float64 (IEEE 754)
- ✅ **String** encoding
  - ✅ fixstr (0-31 bytes)
  - ✅ str8/16/32
  - ✅ UTF-8 validation
- ✅ **Binary** data
  - ✅ bin8/16/32
- ✅ **Array** support
  - ✅ fixarray (0-15)
  - ✅ array16/32
- ✅ **Map** support
  - ✅ fixmap (0-15)
  - ✅ map16/32
  - ✅ Key uniqueness check

#### Extended Types
- ✅ **Timestamp** with nanosecond precision and timezone
- ✅ **Decimal** type for financial calculations
- ✅ **Currency** with fixed precision
- ✅ **Extension** type framework
- ✅ **Graph** data structures
- ✅ **Time-series** optimizations

#### Compression
- ✅ **ZLIB** compression
- ✅ **LZ4** fast compression
- ✅ **ZSTD** balanced compression
- ✅ **Brotli** high compression
- ✅ **Snappy** ultra-fast compression
- ✅ **Adaptive** algorithm selection
- ✅ **Compression profiles** (realtime, network, storage, streaming)
- ✅ **Delta encoding** for time-series
- ✅ **RLE encoding** for repeated values

#### Schema Management
- ✅ **Schema definition** format
- ✅ **Schema validation**
- ✅ **Schema versioning** (semantic)
- ✅ **Evolution strategies**
  - ✅ STRICT mode
  - ✅ ADDITIVE mode
  - ✅ BACKWARD_COMPATIBLE mode
  - ✅ FLEXIBLE mode
- ✅ **Field constraints**
  - ✅ Required fields
  - ✅ Default values
  - ✅ Min/max ranges
  - ✅ Pattern matching
  - ✅ Enum values
- ✅ **Schema inference** from data
- ✅ **Schema migration** with custom functions
- ✅ **Schema registry** for version management
- 🟡 **GraphQL schema** integration (header-only, implementation pending)
- 🟡 **JSON Schema** compatibility (header-only, implementation pending)

#### Performance Optimizations
- ✅ **Memory pooling** with aligned allocations
- ✅ **SIMD acceleration**
  - ✅ AVX2 support
  - ✅ SSE2 support
  - ✅ ARM NEON support
- ✅ **Zero-copy APIs**
- ✅ **Memory-mapped files**
- ✅ **Streaming encoder/decoder**
- ✅ **Tabular encoding** for structured data
- ✅ **Batch processing** with parallel execution
- ✅ **Window-based processing**

#### Security & Validation
- ✅ **Input validation** with configurable limits
- ✅ **Maximum depth checking** (default: 128)
- ✅ **Size limit enforcement**
  - ✅ String size limits (default: 10MB)
  - ✅ Binary size limits (default: 100MB)
  - ✅ Array size limits (default: 1M)
  - ✅ Map size limits (default: 100K)
- ✅ **UTF-8 validation** for strings
- ✅ **Zip bomb detection**
- ✅ **Buffer overflow protection**
- ✅ **Fuzz testing** support (LibFuzzer, AFL++)

### Language Support Status

| Language | Core | Schema | Compression | Extensions | Advanced* | Status |
|----------|------|--------|-------------|------------|-----------|---------|
| C++ | ✅ | ✅ | ✅ | ✅ | ✅ | Reference Implementation |
| Python | ✅ | ✅ | ✅ | ✅ | ✅ | Enhanced Bindings |
| JavaScript | ✅ | ✅ | ✅ | ✅ | 🟡 | WebAssembly + Native |
| Go | ✅ | 🟡 | ✅ | 🟡 | ❌ | Independent Implementation |
| PHP | ✅ | 🟡 | ✅ | 🟡 | ❌ | Independent Implementation |
| Rust | 🚧 | 🚧 | 🚧 | 🚧 | 🚧 | In Development |
| Java | 🚧 | 🚧 | 🚧 | 🚧 | 🚧 | In Development |

*Advanced features include: Graph structures, Time-series, Batch processing, Zero-copy APIs


## Implementation Checklist for New Languages

When implementing BTOON in a new language, follow this checklist:

### Essential (Core Compatibility)
- [ ] All basic types (nil, bool, int, uint, float, string, binary, array, map)
- [ ] Proper endianness handling (big-endian)
- [ ] UTF-8 string validation
- [ ] Size-optimal encoding
- [ ] Extension type support
- [ ] Basic error handling

### Important (Production Ready)
- [ ] Input validation with limits
- [ ] Memory safety checks
- [ ] At least one compression algorithm
- [ ] Schema validation
- [ ] Unit test suite
- [ ] Benchmarks

### Advanced (Full Feature Parity)
- [ ] All compression algorithms
- [ ] Schema versioning and evolution
- [ ] Zero-copy APIs
- [ ] SIMD optimizations
- [ ] Memory pooling
- [ ] Streaming support
- [ ] Graph/TimeSeries types
- [ ] Batch processing

## Platform-Specific Implementation Notes

### Windows
```cpp
// Endian conversion
#include <stdlib.h>
uint32_t htonl(uint32_t hostlong) { return _byteswap_ulong(hostlong); }
uint16_t htons(uint16_t hostshort) { return _byteswap_ushort(hostshort); }

// 64-bit endian conversion
uint64_t htonll(uint64_t value) { return _byteswap_uint64(value); }
```

### macOS/iOS
```cpp
// Use OSByteOrder.h
#include <libkern/OSByteOrder.h>
uint32_t htonl(uint32_t x) { return OSSwapHostToBigInt32(x); }
uint64_t htonll(uint64_t x) { return OSSwapHostToBigInt64(x); }
```

### Linux
```cpp
// Use endian.h
#include <endian.h>
uint64_t htonll(uint64_t x) { return htobe64(x); }
uint64_t ntohll(uint64_t x) { return be64toh(x); }
```

### WebAssembly
```javascript
// Manual endian conversion in JS
function htonl(value) {
    return new DataView(new ArrayBuffer(4)).setUint32(0, value, false);
}
```

## Performance Optimization Guidelines

### Memory Management
1. **Pool Allocations**: Reuse buffers for repeated operations
2. **Aligned Allocations**: Use 64-byte alignment for SIMD
3. **Avoid Small Allocations**: Batch small objects
4. **Zero-Copy Views**: Use spans/views instead of copies

### Encoding Optimizations
1. **Type Selection**: Use fixint (1 byte) over int8 (2 bytes) when possible
2. **Batch Encoding**: Encode multiple values in single pass
3. **Pre-size Buffers**: Reserve expected size upfront
4. **SIMD Memcpy**: Use vectorized copy for large data

### Decoding Optimizations
1. **Lazy Evaluation**: Decode only accessed fields
2. **Memory Mapping**: For large files
3. **Skip Validation**: After first validation pass
4. **Direct Access**: Return views into buffer when safe

### Compression Strategy
```cpp
// Choose compression based on data characteristics
CompressionAlgorithm select_algorithm(size_t size, bool realtime) {
    if (size < 256) return CompressionAlgorithm::NONE;
    if (realtime) return CompressionAlgorithm::LZ4;
    if (size > 1048576) return CompressionAlgorithm::ZSTD;
    return CompressionAlgorithm::ZLIB;
}
```

## Common Implementation Pitfalls

### 1. Integer Encoding
❌ **Wrong**: Always using int64 for all integers  
✅ **Right**: Use smallest encoding (fixint → uint8 → uint16 → ...)

### 2. String Handling
❌ **Wrong**: Assuming strings are null-terminated  
✅ **Right**: Use length-prefixed strings, validate UTF-8

### 3. Endianness
❌ **Wrong**: Using native byte order  
✅ **Right**: Always convert to/from big-endian

### 4. Memory Safety
❌ **Wrong**: Trusting size claims in untrusted data  
✅ **Right**: Validate all sizes before allocation

### 5. Extension Types
❌ **Wrong**: Using negative type IDs for custom extensions  
✅ **Right**: Use 0-127 for custom, -128 to -1 are reserved

## Testing Requirements

### Unit Tests
- All type encodings (minimum, maximum, edge values)
- Round-trip encode/decode
- Invalid data handling
- Schema validation
- Compression/decompression

### Security Tests
- Malformed data
- Oversized claims
- Deep nesting
- Circular references
- Memory exhaustion

### Performance Tests
- Encoding speed
- Decoding speed  
- Memory usage
- Compression ratios
- Concurrent access

### Compatibility Tests
- Cross-language round-trip
- Schema evolution
- Version compatibility
- Platform differences

## Migration from Other Formats

### From JSON
```cpp
// Automatic type inference
Value json_to_btoon(const json& j) {
    if (j.is_null()) return Nil{};
    if (j.is_boolean()) return Bool(j.get<bool>());
    if (j.is_number_integer()) return Int(j.get<int64_t>());
    if (j.is_number_float()) return Float(j.get<double>());
    if (j.is_string()) return String(j.get<std::string>());
    if (j.is_array()) {
        Array arr;
        for (const auto& item : j) {
            arr.push_back(json_to_btoon(item));
        }
        return arr;
    }
    if (j.is_object()) {
        Map map;
        for (const auto& [key, val] : j.items()) {
            map[key] = json_to_btoon(val);
        }
        return map;
    }
    throw std::runtime_error("Unknown JSON type");
}
```

### From MessagePack
- Direct mapping for most types
- Convert MessagePack ext to BTOON Extension
- Handle timestamp format differences

### From Protocol Buffers
```cpp
// Requires schema mapping
Schema protobuf_to_btoon_schema(const Descriptor* desc) {
    SchemaBuilder builder;
    builder.name(desc->name());
    
    for (int i = 0; i < desc->field_count(); i++) {
        const FieldDescriptor* field = desc->field(i);
        builder.field(field->name(), 
                     map_protobuf_type(field->type()),
                     field->is_required());
    }
    
    return builder.build();
}
```

## Debugging Tools

### BTOON Inspector
```bash
# Decode and pretty-print
btoon decode file.btoon

# Validate against schema
btoon validate --schema=user.schema file.btoon

# Show internal structure
btoon inspect --hex file.btoon

# Compare files
btoon diff file1.btoon file2.btoon
```

### Hex Analysis
```bash
# First 256 bytes with annotations
xxd -l 256 file.btoon | head -20

# Find specific patterns
xxd file.btoon | grep "c0"  # Find nil values

# Binary diff
cmp -l file1.btoon file2.btoon | head
```

### Performance Profiling
```cpp
// Measure encoding time
auto start = std::chrono::high_resolution_clock::now();
auto encoded = encode(value);
auto end = std::chrono::high_resolution_clock::now();
auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
std::cout << "Encoding took " << ms.count() << "ms\n";
std::cout << "Size: " << encoded.size() << " bytes\n";
std::cout << "Throughput: " << (data_size / ms.count()) << " MB/s\n";
```

## Resources

### Documentation
- [Format Specification](btoon-spec.md) - Complete binary format
- [API Reference](API.md) - Full API documentation
- [Examples](../examples/) - Code examples
- [Test Vectors](../tests/test_vectors/) - Validation data

### Tools
- [btoon-schema](../tools/btoon-schema.cpp) - Schema compiler
- [btoon-convert](../tools/btoon-convert.cpp) - Format converter
- [Validator](../include/btoon/validator.h) - Validation library

### Community
- [GitHub Issues](https://github.com/BTOON-project/btoon-core/issues) - Bug reports and discussions

## License

This implementation guide is part of the BTOON project and is licensed under the MIT License.
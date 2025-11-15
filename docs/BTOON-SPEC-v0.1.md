# BTOON Format Specification v0.1

**Binary Tree Object Notation**  
**Version**: 0.0.1  
**Status**: Draft  
**Date**: November 2025  

## Table of Contents

1. [Introduction](#1-introduction)
2. [Design Principles](#2-design-principles)
3. [Type System](#3-type-system)
4. [Binary Format](#4-binary-format)
5. [Extension Types](#5-extension-types)
6. [Compression](#6-compression)
7. [Schema Definition](#7-schema-definition)
8. [Security Considerations](#8-security-considerations)
9. [Implementation Notes](#9-implementation-notes)
10. [Version History](#10-version-history)
11. [Appendices](#11-appendices)

---

## 1. Introduction

BTOON (Binary Tree Object Notation) is a high-performance binary serialization format designed for efficient data exchange in modern distributed systems. It combines the simplicity of JSON with the efficiency of binary encoding, providing a robust foundation for data serialization across different platforms and languages.

### 1.1 Goals

- **Efficiency**: Minimal encoding/decoding overhead
- **Compactness**: Small wire format with optional compression
- **Type Safety**: Strong typing with schema support
- **Extensibility**: Support for custom types and future evolution
- **Security**: Built-in validation and protection against malicious inputs
- **Interoperability**: Clean mapping to/from JSON and other formats

### 1.2 Non-Goals

- Human readability (use JSON export for debugging)
- In-place modification of serialized data
- Streaming of infinite data sequences

---

## 2. Design Principles

### 2.1 Core Principles

1. **Self-Describing**: Data carries type information
2. **Zero-Copy Friendly**: Designed for efficient memory access patterns
3. **Platform Neutral**: No assumptions about endianness or word size
4. **Forward Compatible**: Older parsers can skip unknown extensions

### 2.2 Encoding Strategy

BTOON uses a hybrid encoding strategy:
- **Fixed-size encoding** for small integers and common values
- **Variable-length encoding** for larger values
- **Type-prefixed encoding** for dynamic typing
- **Length-prefixed encoding** for strings and binary data

---

## 3. Type System

### 3.1 Primitive Types

| Type | Description | Range/Size |
|------|-------------|------------|
| Nil | Null value | Single byte (0xc0) |
| Boolean | True/False | Single byte (0xc2/0xc3) |
| Integer | Signed integer | -2^63 to 2^63-1 |
| Unsigned | Unsigned integer | 0 to 2^64-1 |
| Float | IEEE 754 floating point | 32-bit or 64-bit |
| String | UTF-8 text | 0 to 2^32-1 bytes |
| Binary | Raw bytes | 0 to 2^32-1 bytes |

### 3.2 Container Types

| Type | Description | Element Limit |
|------|-------------|---------------|
| Array | Ordered sequence | 2^32-1 elements |
| Map | Key-value pairs | 2^32-1 entries |

### 3.3 Extension Types

Extension types allow custom data types while maintaining compatibility:
- Reserved range: -128 to -1 (system extensions)
- User range: 0 to 127 (application extensions)

---

## 4. Binary Format

### 4.1 Type Markers

BTOON uses single-byte type markers inspired by MessagePack:

```
+--------+--------+--------+
|  Type  | Marker | Range  |
+--------+--------+--------+
| positive fixint | 0x00-0x7f | 0 to 127 |
| fixmap          | 0x80-0x8f | 0 to 15 entries |
| fixarray        | 0x90-0x9f | 0 to 15 elements |
| fixstr          | 0xa0-0xbf | 0 to 31 bytes |
| nil             | 0xc0      | - |
| (never used)    | 0xc1      | - |
| false           | 0xc2      | - |
| true            | 0xc3      | - |
| bin8            | 0xc4      | 0 to 255 bytes |
| bin16           | 0xc5      | 0 to 65535 bytes |
| bin32           | 0xc6      | 0 to 2^32-1 bytes |
| ext8            | 0xc7      | 0 to 255 bytes |
| ext16           | 0xc8      | 0 to 65535 bytes |
| ext32           | 0xc9      | 0 to 2^32-1 bytes |
| float32         | 0xca      | IEEE 754 |
| float64         | 0xcb      | IEEE 754 |
| uint8           | 0xcc      | 0 to 255 |
| uint16          | 0xcd      | 0 to 65535 |
| uint32          | 0xce      | 0 to 2^32-1 |
| uint64          | 0xcf      | 0 to 2^64-1 |
| int8            | 0xd0      | -128 to 127 |
| int16           | 0xd1      | -32768 to 32767 |
| int32           | 0xd2      | -2^31 to 2^31-1 |
| int64           | 0xd3      | -2^63 to 2^63-1 |
| fixext1         | 0xd4      | 1 byte |
| fixext2         | 0xd5      | 2 bytes |
| fixext4         | 0xd6      | 4 bytes |
| fixext8         | 0xd7      | 8 bytes |
| fixext16        | 0xd8      | 16 bytes |
| str8            | 0xd9      | 0 to 255 bytes |
| str16           | 0xda      | 0 to 65535 bytes |
| str32           | 0xdb      | 0 to 2^32-1 bytes |
| array16         | 0xdc      | 0 to 65535 elements |
| array32         | 0xdd      | 0 to 2^32-1 elements |
| map16           | 0xde      | 0 to 65535 entries |
| map32           | 0xdf      | 0 to 2^32-1 entries |
| negative fixint | 0xe0-0xff | -32 to -1 |
```

### 4.2 Encoding Rules

#### 4.2.1 Integers

Integers use the most compact representation:
1. Positive fixint (0-127): Single byte
2. Negative fixint (-32 to -1): Single byte  
3. uint8/int8: 2 bytes (marker + value)
4. uint16/int16: 3 bytes (marker + value)
5. uint32/int32: 5 bytes (marker + value)
6. uint64/int64: 9 bytes (marker + value)

#### 4.2.2 Strings

Strings are UTF-8 encoded with length prefix:
```
fixstr: [101xxxxx] [utf-8 bytes]
str8:   [0xd9] [length:1] [utf-8 bytes]
str16:  [0xda] [length:2] [utf-8 bytes]
str32:  [0xdb] [length:4] [utf-8 bytes]
```

#### 4.2.3 Binary Data

Binary data uses similar encoding to strings:
```
bin8:   [0xc4] [length:1] [raw bytes]
bin16:  [0xc5] [length:2] [raw bytes]
bin32:  [0xc6] [length:4] [raw bytes]
```

#### 4.2.4 Arrays

Arrays encode element count followed by elements:
```
fixarray: [1001xxxx] [elements...]
array16:  [0xdc] [count:2] [elements...]
array32:  [0xdd] [count:4] [elements...]
```

#### 4.2.5 Maps

Maps encode pair count followed by key-value pairs:
```
fixmap: [1000xxxx] [key1] [value1] ...
map16:  [0xde] [count:2] [key1] [value1] ...
map32:  [0xdf] [count:4] [key1] [value1] ...
```

### 4.3 Byte Order

All multi-byte integers are encoded in **big-endian** (network) byte order.

---

## 5. Extension Types

### 5.1 Extension Format

```
fixext1:  [0xd4] [type:1] [data:1]
fixext2:  [0xd5] [type:1] [data:2]
fixext4:  [0xd6] [type:1] [data:4]
fixext8:  [0xd7] [type:1] [data:8]
fixext16: [0xd8] [type:1] [data:16]
ext8:     [0xc7] [length:1] [type:1] [data...]
ext16:    [0xc8] [length:2] [type:1] [data...]
ext32:    [0xc9] [length:4] [type:1] [data...]
```

### 5.2 Reserved Extension Types

| Type ID | Name | Description |
|---------|------|-------------|
| -1 | Timestamp | Unix timestamp with nanoseconds |
| -2 | Date | Calendar date (year, month, day) |
| -3 | DateTime | Date with time and timezone |
| -4 | BigInt | Arbitrary precision integer |
| -5 | VectorFloat | Float32 vector |
| -6 | VectorDouble | Float64 vector |
| -10 | Tabular | Columnar data encoding |
| -128 to -11 | Reserved | Future system extensions |

### 5.3 Tabular Data Extension (Type -10)

Optimized columnar format for homogeneous data:

```
Header:
  [ext marker] [type:-10] [total_length:4]
  
Data:
  [num_rows:4] [num_columns:4]
  
Schema Section:
  Column Names:
    [name_length:4] [name_utf8] ...
  Column Types:
    [type:1] ...
    
Data Section:
  For each column:
    [column_size:4] [encoded_values...]
```

---

## 6. Compression

### 6.1 Compression Header

When compression is applied, data is prefixed with a header:

```
struct CompressionHeader {
    uint32_t magic;           // 0x42544F4E ("BTON")
    uint32_t version;         // Format version
    uint32_t compressed_size; // Size of compressed data
    uint32_t uncompressed_size; // Original size
    uint8_t  algorithm;       // Compression algorithm
    uint8_t  reserved[3];     // Reserved for future use
}
```

### 6.2 Supported Algorithms

| ID | Algorithm | Use Case |
|----|-----------|----------|
| 0 | None | No compression |
| 1 | ZLIB | General purpose, good ratio |
| 2 | LZ4 | Fast compression/decompression |
| 3 | ZSTD | Best ratio, adaptive |
| 4 | AUTO | Automatic selection |

### 6.3 Compression Profiles

| Profile | Algorithm | Level | Min Size | Use Case |
|---------|-----------|-------|----------|----------|
| Realtime | LZ4 | 1 | 128 | Low latency |
| Network | ZLIB | 6 | 512 | Bandwidth optimization |
| Storage | ZSTD | 9 | 1024 | Maximum compression |
| Streaming | LZ4 | 3 | 256 | Continuous data |

---

## 7. Schema Definition

### 7.1 Schema Format

Schemas are BTOON-encoded maps with required fields:

```javascript
{
  "name": "string",           // Schema name
  "version": "string",         // Semantic version (X.Y.Z)
  "description": "string",     // Optional description
  "evolution": "string",       // Evolution strategy
  "fields": [                  // Field definitions
    {
      "name": "string",
      "type": "string",        // BTOON type name
      "required": boolean,
      "default": any,          // Default value
      "description": "string",
      "constraints": {         // Optional constraints
        "min": number,
        "max": number,
        "pattern": "string",   // Regex pattern
        "enum": [...]          // Allowed values
      }
    }
  ]
}
```

### 7.2 Evolution Strategies

| Strategy | Description |
|----------|-------------|
| STRICT | No changes allowed |
| ADDITIVE | Only new optional fields |
| BACKWARD_COMPATIBLE | New fields with defaults |
| FLEXIBLE | Any changes allowed |

### 7.3 Type Names

Standard BTOON type names for schemas:
- `nil`, `bool`, `int`, `uint`, `float`, `double`
- `string`, `binary`
- `array`, `map`
- `timestamp`, `date`, `datetime`
- `any` (dynamic type)

---

## 8. Security Considerations

### 8.1 Input Validation

Implementations MUST validate:
- **Size limits**: Prevent excessive memory allocation
- **Nesting depth**: Prevent stack overflow
- **UTF-8 encoding**: Validate string encoding
- **Type consistency**: Verify type markers match data

### 8.2 Recommended Limits

| Limit | Default | Maximum |
|-------|---------|---------|
| Max depth | 128 | 1000 |
| Max string | 10 MB | 100 MB |
| Max binary | 100 MB | 1 GB |
| Max array size | 1M elements | 100M |
| Max map size | 100K entries | 10M |
| Max total size | 1 GB | - |

### 8.3 Attack Vectors and Mitigations

#### 8.3.1 Zip Bombs
- **Attack**: Highly compressed data expanding to huge size
- **Mitigation**: Check compression ratio (max 1000:1), limit uncompressed size

#### 8.3.2 Deep Nesting
- **Attack**: Deeply nested structures causing stack overflow
- **Mitigation**: Enforce maximum nesting depth

#### 8.3.3 Memory Exhaustion
- **Attack**: Large size claims causing OOM
- **Mitigation**: Validate size claims before allocation

#### 8.3.4 Integer Overflow
- **Attack**: Malformed sizes causing overflow
- **Mitigation**: Use safe integer arithmetic

---

## 9. Implementation Notes

### 9.1 Performance Optimizations

1. **Memory Pooling**: Reuse allocations for temporary buffers
2. **SIMD Instructions**: Use for bulk operations (memcpy, compression)
3. **Zero-Copy**: Design APIs to minimize data copying
4. **Lazy Evaluation**: Defer decoding until data is accessed

### 9.2 Language Bindings

Recommended API patterns:

#### C++
```cpp
btoon::Value value = btoon::decode(data);
std::vector<uint8_t> encoded = btoon::encode(value);
```

#### Python
```python
value = btoon.decode(data)
encoded = btoon.encode(value)
```

#### JavaScript/TypeScript
```javascript
const value = btoon.decode(data);
const encoded = btoon.encode(value);
```

### 9.3 Error Handling

Implementations should provide:
- Detailed error messages with position information
- Recovery mechanisms for partial data
- Validation-only mode for untrusted input

---

## 10. Version History

### Version 0.0.1 (Current)
- Initial specification
- Core type system
- Extension mechanism
- Compression support
- Schema versioning
- Security guidelines

---

## 11. Appendices

### Appendix A: Examples

#### A.1 Simple Object
```
Input (JSON):
{
  "name": "Alice",
  "age": 30,
  "active": true
}

Output (BTOON hex):
83 a4 6e61 6d65 a5 416c 6963 65
a3 6167 65 1e a6 6163 7469 7665 c3
```

#### A.2 Array of Numbers
```
Input: [1, 2, 3, 100, 1000]

Output (BTOON hex):
95 01 02 03 64 cd 03 e8
```

#### A.3 Compressed Data
```
Original: Large JSON object (10KB)
Compressed with LZ4: ~2KB
Header + compressed data
```

### Appendix B: Test Vectors

Standard test vectors for implementation validation:

1. **Edge Cases**:
   - Empty string: `a0`
   - Empty array: `90`
   - Empty map: `80`
   - Max positive fixint: `7f` (127)
   - Min negative fixint: `e0` (-32)

2. **Type Coverage**:
   - All integer sizes
   - All string sizes
   - All container sizes
   - All extension formats

3. **Security Tests**:
   - Truncated data
   - Invalid UTF-8
   - Excessive nesting
   - Large size claims

### Appendix C: Reference Implementation

The reference implementation is available at:
https://github.com/BTOON-project/btoon-core

Key files:
- `include/btoon/btoon.h` - Core types
- `src/encoder.cpp` - Encoding logic
- `src/decoder.cpp` - Decoding logic
- `src/validator.cpp` - Security validation

### Appendix D: Comparison with Other Formats

| Feature | BTOON | JSON | MessagePack | Protocol Buffers |
|---------|-------|------|-------------|------------------|
| Human Readable | No | Yes | No | No |
| Self-Describing | Yes | Yes | Yes | No (needs schema) |
| Compact | Yes | No | Yes | Yes |
| Schema Support | Yes | Limited | No | Required |
| Compression | Built-in | External | External | External |
| Type Safety | Yes | Weak | Yes | Strong |
| Zero-Copy | Yes | No | Partial | Yes |

---

## License

This specification is released under the MIT License.

## Contributing

Proposals for specification changes should be submitted as issues to the BTOON repository with the `spec-change` label.

## Contact

- Specification: spec@btoon.net
- Security: security@btoon.net
- General: info@btoon.net

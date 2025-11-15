# BTOON Binary Format Specification

## Version 0.0.1

BTOON (Binary TOON) is a high-performance binary serialization format designed specifically for TOON (Token-Oriented Object Notation) tabular data. It extends MessagePack with custom optimizations while maintaining full MessagePack compatibility.

## Overview

BTOON provides:
- **MessagePack Compatibility**: All standard MessagePack types are supported
- **Tabular Optimization**: Custom extension type `-1` for schema-aware table compression
- **Zero-Copy Decoding**: Efficient `std::span`-based API for minimal allocations
- **Optional Compression**: Built-in support for zlib/LZ4/Zstd compression
- **Security**: Optional HMAC signing for authenticated data

## Binary Layout

### MessagePack Base Format

BTOON uses MessagePack as its base format. All standard MessagePack types are supported:

- **Nil**: `0xc0`
- **Boolean**: `0xc2` (false), `0xc3` (true)
- **Integers**: Fixint (`0x00-0x7f`, `0xe0-0xff`), int8 (`0xd0`), int16 (`0xd1`), int32 (`0xd2`), int64 (`0xd3`)
- **Unsigned Integers**: uint8 (`0xcc`), uint16 (`0xcd`), uint32 (`0xce`), uint64 (`0xcf`)
- **Floats**: float32 (`0xca`), float64 (`0xcb`)
- **Strings**: Fixstr (`0xa0-0xbf`), str8 (`0xd9`), str16 (`0xda`), str32 (`0xdb`)
- **Binary**: bin8 (`0xc4`), bin16 (`0xc5`), bin32 (`0xc6`)
- **Arrays**: Fixarray (`0x90-0x9f`), array16 (`0xdc`), array32 (`0xdd`)
- **Maps**: Fixmap (`0x80-0x8f`), map16 (`0xde`), map32 (`0xdf`)
- **Extensions**: fixext1 (`0xd4`), fixext2 (`0xd5`), fixext4 (`0xd6`), fixext8 (`0xd7`), fixext16 (`0xd8`), ext8 (`0xc7`), ext16 (`0xc8`), ext32 (`0xc9`)

### Tabular Extension Format (Type -10)

BTOON introduces a custom extension type `-10` for tabular data optimization. This format is used when encoding arrays of uniform objects (same keys, primitive values only).

#### Format Structure

```
[ext header] [type=-10] [schema] [rows]
```

#### Extension Header

- Uses MessagePack extension format: `fixext16` (`0xd8`) or `ext8/ext16/ext32`
- Extension type: `-10`

#### Schema Section

```
version: uint32 (big-endian)
num_columns: uint32 (big-endian)
column_names: [
    length: uint32 (big-endian)
    name: utf8_string (length bytes)
] * num_columns
column_types: [
    type: uint8 (DataType enum)
] * num_columns
```

#### Rows Section

```
num_rows: uint32 (big-endian)
values: [
    row 0: [value_0_0, value_0_1, ..., value_0_n]
    row 1: [value_1_0, value_1_1, ..., value_1_n]
    ...
    row m: [value_m_0, value_m_1, ..., value_m_n]
] (row-major order, no type tags)
```

#### Value Encoding

Values are encoded in row-major order without type tags (type information is in schema):
- **Boolean**: `0x00` (false), `0x01` (true)
- **Integer**: Variable-length encoding (varint) for signed/unsigned integers
- **Float**: IEEE 754 binary64 (8 bytes, big-endian)
- **String**: Length-prefixed UTF-8 (length: uint32, data: bytes)

#### Tabular Requirements

1. Minimum 2 rows required
2. All objects must have identical keys (same keys, same order)
3. Only primitive types allowed (no nested arrays/maps)
4. Column names must be valid UTF-8 strings

### Compression Format

When compression is enabled, the entire BTOON payload (after encoding) is compressed using the selected algorithm:

#### Compression Header

```
magic: uint32 = 0x42544F4E ("BTON")
version: uint8 = 1
algorithm: uint8 (0=zlib, 1=LZ4, 2=Zstd)
reserved: uint16 = 0
compressed_size: uint32 (big-endian)
uncompressed_size: uint32 (big-endian)
compressed_data: [bytes]
```

### Security Format (HMAC)

When security is enabled, an HMAC signature is prepended to the encoded data:

#### Security Header

```
signature_length: uint16 (big-endian, typically 32 for SHA-256)
signature: [bytes] (HMAC-SHA256)
data: [bytes] (BTOON payload)
```

## Type System

BTOON's type system is implicitly defined by the `btoon::Value` variant in the C++ reference implementation. It includes standard MessagePack types as well as custom extensions for `Timestamp`, `Date`, and `BigInt`.

### Type Normalization Rules

1. **Integers**: Use smallest representation (fixint → int8 → int16 → int32 → int64)
2. **Floats**: Non-finite values (NaN, Infinity) → `null`
3. **Strings**: Must be valid UTF-8
4. **Maps**: Keys sorted alphabetically for deterministic encoding
5. **Arrays**: Preserve order

## Versioning

BTOON format version is encoded in the compression header (if used) or can be inferred from extension types. Current version: `0.0.1`.

### Version Compatibility

- **0.0.1**: Initial release
  - MessagePack compatibility
  - Tabular extension type -1
  - zlib compression
  - HMAC security

## Security Constraints

### Size Limits

- Maximum document size: 256 MB (configurable)
- Maximum nesting depth: 64 levels (configurable)
- Maximum string length: 16 MB (configurable)
- Maximum array/map size: 16M elements (configurable)

### Validation

- All buffer accesses are bounds-checked
- UTF-8 validation for strings
- Depth checking for nested structures
- Type validation for tabular data

### HMAC Signing

- Algorithm: HMAC-SHA256
- Key: User-provided (minimum 32 bytes recommended)
- Signature prepended to payload
- Verification on decode (if enabled)

## Byte Order

All multi-byte integers and floats use **big-endian** (network byte order) for cross-platform compatibility.

## Examples

### Simple Integer

```
42 → [0x2a]
```

### Tabular Data

```
[
  {"id": 1, "name": "Alice", "age": 30},
  {"id": 2, "name": "Bob", "age": 25}
]

→ [0xd8, 0xff, ...schema..., ...rows...]
```

### Compressed Payload

```
[0x42, 0x54, 0x4F, 0x4E, 0x01, 0x00, 0x00, 0x00, ...compressed_data...]
```

## Reference Implementation

See `btoon-core` C++ implementation for authoritative encoding/decoding logic.

## References

- [MessagePack Specification](https://github.com/msgpack/msgpack/blob/master/spec.md)
- [TOON Format Specification](https://github.com/toon-format/toon)
- [RFC 2119](https://tools.ietf.org/html/rfc2119) (Keywords: MUST, SHOULD, MAY)


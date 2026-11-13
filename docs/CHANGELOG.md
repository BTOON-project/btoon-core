# Changelog

All notable changes to BTOON will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.1.5] - 2026-11-13

### Security
- **Schema patterns crashed on long strings.** `pattern` constraints used `std::regex`.
  libstdc++ and MSVC match recursively, one stack frame per character, so validating a
  30 KB string crashed the process with any pattern, and a 100 KB `version` string
  crashed `Schema::fromValue`.
  - Patterns now run on a built-in matcher (Thompson NFA, Pike VM). It takes linear time
    and doesn't recurse on the text; the supported syntax is in API.md.
  - Version strings are parsed by hand.
- **Nested containers pre-allocated against the same input at every level.** 1 MB of
  nested array headers reserved 5 GB, and nested tabulars built 2.6 GB.
  - Reservations now draw on one budget per document, equal to its size. Every element
    owns at least one byte, so a valid document never exhausts it. A container it
    doesn't cover starts empty and grows as its elements decode.
  - Tabular rows are created as their cells are read.
  - The same inputs now peak at 100 MB and 176 MB. A single large array still reserves
    exactly, so legitimate documents don't pay for reallocation.
- **`RleCodec` and `DictionaryCodec` could expand 100 KB into about 1 GB.** One run, or
  one-byte indices, copied a large value many times. `decode()` gains a `max_bytes` limit
  (default 256 MiB) on the decoded data's estimated memory.
- **`utils::BufferReader` bounds checks could wrap.** `readBytes(SIZE_MAX)` passed the
  check and read out of bounds.

### Fixed
- `Schema::validate`:
  - optional fields that are present are validated;
  - `min`/`max` compare exactly across `Int`, `Uint`, `Float` and `BigInt` (before, a
    `Uint` or `Float` value skipped an `Int` bound);
  - NaN fails any bound;
  - broken constraints throw `invalid_argument` when the schema is built.
- `Validator`:
  - byte input is decoded with the options' `max_depth` and size limit;
  - signed documents validate given the new `ValidationOptions::hmac_key`;
  - timestamps before 1970 or after 2100 are valid;
  - `validate(std::vector<uint8_t>)` is no longer ambiguous.
- `RleCodec` kept `0.0` and `-0.0` in one run, so `-0.0` decoded as `0.0`.
- `utils::crc32` had two wrong table entries, so 23% of inputs differed from zlib.
  The table is now generated at compile time.
- `utils::decodeVarint` silently dropped bits above 64. It now throws.

### Fixed: the command-line tools
- **btoon-convert corrupted data and exited 0** (review 2026-11-13, critical):
  - integers above INT64_MAX came out negative;
  - date, datetime, bigint, vectors and NaN/±Inf became `null`;
  - timestamps lost their fractions.

  btoon-convert and btoon-schema now use the `btoon` CLI's lossless JSON mapping, so
  every type round-trips byte for byte.
- **Deeply nested JSON crashed all three tools** (100k `[` overflowed the stack).
  - Parsing is now non-recursive (SAX).
  - Conversion stops at 128 BTOON levels with a `max_depth` error.
- **bigint ↔ decimal conversion was quadratic per byte.** A 16 KB bigint took 55 s; it
  now takes 0.16 s with 32-bit limbs. Values above 40,000 bytes are written as exact
  hex (`"0x…"`); both forms are accepted, and decimal input over 100,000 digits is
  refused.
- **`btoon --hmac-key-file` with an empty file signed nothing and verified nothing**,
  exiting 0. It's now a usage error (exit 2).
- **The JSON mapping is strict:**
  - `$float` accepts only `NaN`, `Infinity` and `-Infinity`;
  - `$ext` ids must be in −128…127 and not one of BTOON's own types;
  - `$timestamp` is strict RFC 3339, with no overflow at extreme years;
  - a one-key map whose key starts with `$` is written as `{"$map": …}`, so it can't be
    mistaken for a tag;
  - NaN payloads inside vectors are kept;
  - integers beyond 64 bits become bigints instead of floats.
- **btoon-convert's other formats:**
  - CSV follows RFC 4180, and only strict JSON numbers become numbers ("Nan", "0x10"
    stay strings);
  - XML output is escaped;
  - batch output paths are checked;
  - bad numeric options are usage errors.
- All tools exit nonzero when the output can't be written (a full disk, a closed pipe).
- `SchemaInferrer`'s min/max are the extreme values themselves, compared exactly.
  Before, they went through `double`, and UINT64_MAX overflowed when cast back.

### Changed
- `pattern` supports the ECMAScript subset listed in API.md. Back-references, lookaround
  and `\p{…}` were accepted by `std::regex` and are now rejected when the schema is built.
  Patterns match code points, so `.` matches one UTF-8 character, not one byte.

## [0.1.4] - 2026-11-13

### Security
- **`Decoder` ignored `DecodeOptions::hmac_key`.** `Decoder(DecodeOptions{.hmac_key = key})`
  returned unsigned, possibly forged input instead of failing with
  `authentication_failed`. `decode_and_get_pos()` and `StreamDecoder` did the same.
  `btoon::decode()` was not affected. A `Decoder` now verifies the signed envelope
  whenever it has a key, from a `Security` object or from `hmac_key`.
- **Dangling `Security` reference.** `Decoder` and `Encoder` kept a pointer to the
  `Security` object they were given, so `Decoder dec{Security{key}}` read a destroyed
  object. They now keep a copy. As a result, changing the `Security` object afterwards
  no longer affects them.

### Changed
- `Decoder::decode()` opens envelopes exactly like `btoon::decode()`, which now
  delegates to it. It decompresses a compression envelope, and without a key it rejects
  a signed envelope with `authentication_failed`. Before, it read both envelopes as
  bare data; in non-strict mode the result was the integer 66. `decode_and_get_pos()`
  still reads bare values. As a side effect, `Validator::validate()` and `sanitize()`
  now accept compressed documents.
- `StreamEncoder` throws `invalid_argument` when `compress` or `hmac_key` is set, and
  `StreamDecoder` when `hmac_key` is set. They used to ignore them, but a stream holds
  bare documents only.
- `Encoder` still writes the bare document and doesn't apply `compress` or `hmac_key`.
  This is now documented: `encode()` and `apply_envelopes()` add the envelopes.

The wire format is unchanged.

## [0.1.3] - 2026-11-08

### Security
- **Use after free in the zero-copy C API.** `btoon_mmap_file()` and `btoon_shm_create()`
  returned views into a mapping that was released before the function returned, so
  any read through the view touched unmapped memory. `btoon_shm_create()` also deleted
  a newly created segment right away. Views now own their mapping, and
  `btoon_munmap_file()` / `btoon_shm_close()` release it.
- **Shared memory permissions.** `SharedMemoryBuffer` created POSIX segments with mode
  0666, so other local users could read them (and write them under a permissive umask).
  Segments are now owner-only (0600).
- **Size check when opening a segment.** Opening an existing segment mapped the
  requested size without checking the segment's real size. Touching pages past its end
  raised SIGBUS, which another user who pre-created a smaller segment could trigger.
  Smaller segments are now refused, and size 0 maps the whole segment.

### Fixed
- Windows: shared memory segments larger than 4 GiB were truncated (the high 32 bits
  of the size were dropped).
- `MemoryMappedFile::open()` accepts regular, non-empty files only (not devices or
  FIFOs).

The wire format and the encoder and decoder are unchanged.

## [0.1.2] - 2026-11-08

### Fixed
- Android: the library builds again. `SharedMemoryBuffer::create` returns nullptr there,
  because Android's C library has no POSIX named shared memory (`shm_open`). CI now
  builds for Android arm64-v8a, armeabi-v7a and x86_64.
- `SharedMemoryBuffer`: only the process that created a named segment removes its name
  on destruction. Before, a process that merely opened the segment also unlinked it.

## [0.1.1] - 2026-11-07

### Fixed
- CMake package: `btoonConfig.cmake` now finds the codec libraries the build used (LZ4,
  Zstandard, Brotli, Snappy), so `find_package(btoon)` works for builds with optional
  codecs. Codecs are looked up as CMake packages first (as vcpkg and Conan provide them),
  then with pkg-config.
- `Value != Value` compiles and uses the same semantic comparison as `==` (before, it
  picked `std::variant`'s operator and failed to compile).

## [0.1.0] - 2026-11-07

The first release in which the specification, the reference implementation and the
conformance vectors agree. **Wire-format note:** 0.0.x encoders produced Date, DateTime,
BigInt and vector values (and user extensions) that their own decoder couldn't read back.
0.1.0 fixes the format, so data in those types written by 0.0.x is not readable.
Everything else, including MessagePack types, tabular data and the compression envelope,
is unchanged and still decodes.

### Format (docs/SPEC.md v0.1)
- One spec, `docs/SPEC.md`, replaces the contradictory `btoon-spec.md` and
  `BTOON-SPEC-v0.1.md`.
- Extension ids are fixed: date -2, datetime -3, bigint -4 (two's complement),
  float32 vector -5, float64 vector -6 (both little-endian), tabular -10.
- Timestamps (-1) use MessagePack's exact 4/8/12-byte layouts, plus a 14-byte form
  that carries a UTC offset.
- Canonical encoding: integers use the smallest form by value regardless of
  signedness, floats are always float64 with a canonical NaN, and map keys are sorted
  by UTF-8 bytes.
- Tabular encoding is used only when it is smaller than a plain array. The size rule is
  in §5.6.2. The byte layout is unchanged.
- New signed envelope (`"BTSG"`, HMAC-SHA256) and defined error codes.
- 260 conformance vectors (`tests/vectors/btoon-vectors.json`), generated by an
  independent Python implementation (`tests/vectors/generate.py`). The pure Go, PHP and
  TypeScript ports pass them too.

### Security
- Fixed a heap buffer over-read on truncated maps. The 1-byte input `81` triggered it.
- Fixed stack exhaustion from deep nesting. The default `max_depth` is now 128.
- Container and tabular sizes are checked against the remaining input before
  allocating. A 17-byte input could previously allocate billions of rows.
- Decompression is capped (`max_decompressed_size`, default 256 MiB) and must produce
  exactly the declared size, with no bytes after the compressed stream. Zstd frames may
  not declare a window larger than their content needs. Corrupt envelopes no longer
  fall back to being decoded as bare data.
- Strict mode, the default, rejects trailing bytes, invalid UTF-8, duplicate keys,
  non-string keys, and malformed extensions.
- Fixed out-of-bounds reads in `DictionaryCodec` and unbounded output in `RleCodec`,
  and fixed misaligned loads in the validator.
- Fuzzed with libFuzzer and a built-in mutation fuzzer, under ASan and UBSan.

### Changed
- The encoder was rewritten to use a single buffer. It no longer allocates a 64 KB
  pool per element, and options now apply to nested values. Encoding is 5–17× faster.
- `compress=true` now defaults to zlib (it used to do nothing). An unavailable
  algorithm is an error instead of a silent no-op.
- Integers that fit in int64 decode as `Int`. `Value::operator==` compares integers by
  value across `Int` and `Uint`.
- `is_tabular()` now applies the size rule.
- The LZ4 payload is the raw block, without the old host-endian length prefix.
- HMAC-SHA256 is built in, so OpenSSL is no longer a dependency. `Decoder` verifies
  signatures, which it never did before.
- `StreamDecoder` reads values incrementally (before, it read only the first value).
- C API: exceptions no longer cross the C boundary. Added `btoon_free_error`.
  `btoon_version()` reports the real version.
- CLI: `btoon encode|decode|info` with stdin/stdout support, compression algorithms,
  HMAC keys, and a lossless JSON mapping for non-JSON types.
- Build: no `-march=native` by default, GoogleTest pinned to v1.17.0, the version comes
  from `btoon.h`, sanitizer flags now apply to the library itself, the installed CMake
  package config is fixed, and tests/tools are skipped when built as a subproject.

### Removed
- Headers that never compiled or had no implementation (`batch_processor.h`,
  `decimal.h`, `graph.h`, `graphql_schema.h`, `json_schema.h`, `timeseries.h`) moved to
  `experimental/` and are no longer installed. The prototype in-tree bindings moved
  there too.

## [0.0.1] - 2025-11-15

### 🎉 Initial Pre-Release

This is the first pre-release of BTOON (Binary Tree Object Notation), a high-performance binary serialization format designed for modern applications.

### ✨ Core Features

#### Serialization & Encoding
- High-performance binary serialization with minimal overhead
- Rich type system supporting all common data types
- Tabular data optimization with columnar encoding
- Streaming support for large datasets
- Zero-copy APIs to minimize memory allocations

#### Compression
- Multiple compression algorithms: ZLIB, LZ4, ZSTD, Brotli, Snappy
- Adaptive compression with automatic algorithm selection
- Compression profiles for different use cases (realtime, network, storage)
- Compression levels from FASTEST to MAXIMUM
- Delta and RLE codecs for specialized data

#### Data Types
- **Extended Timestamps**: Nanosecond precision with timezone support
- **Decimal Type**: Arbitrary precision for financial calculations
- **Currency**: Fixed-precision monetary values
- **Graph Structures**: Nodes, edges, and graph algorithms
- **Time-Series**: Optimized storage and analysis for temporal data

#### Schema Management
- Schema versioning with evolution strategies
- Forward and backward compatibility
- Schema inference from data
- Schema validation with detailed error reporting
- Schema builder with fluent API
- Schema registry for managing multiple versions

#### Performance Optimizations
- SIMD acceleration (AVX2, SSE2, ARM NEON)
- Memory pooling with aligned allocations
- Parallel batch processing with worker threads
- Memory-mapped file support
- Optimized string and binary encoding

### 🔧 Developer Tools

#### CLI Tools
- **btoon**: Main CLI for encoding/decoding
- **btoon-schema**: Schema compiler and code generator
- **btoon-convert**: Universal format converter (JSON, YAML, MessagePack, CBOR, CSV, XML)

#### Language Bindings
- **Python**: Enhanced bindings with async support, NumPy/Pandas integration
- **JavaScript**: Node.js and browser support via WebAssembly
- **C API**: FFI-friendly interface for other languages

### 🔌 Integration Features

#### GraphQL Integration
- Bidirectional BTOON ↔ GraphQL schema conversion
- GraphQL query execution on BTOON data
- Subscription support
- Federation and Relay compliance

#### JSON Schema Compatibility
- Full JSON Schema draft support (2020-12, 2019-09, etc.)
- Validation with detailed error reporting
- OpenAPI/Swagger integration
- TypeScript generation from schemas

#### Batch Processing
- Parallel batch processor with configurable workers
- Streaming processor for large files
- MapReduce patterns for aggregations
- Window-based processing (tumbling, sliding, session)

### 🔒 Security & Validation
- Strict input validation with configurable options
- Fuzz testing support (LibFuzzer, AFL++)
- Security checks for zip bombs and buffer overflows
- UTF-8 validation for strings
- Depth and size limits

### 📦 Distribution
- Pre-built binaries for major platforms (Linux, macOS, Windows)
- Multi-architecture support (x64, ARM64)
- Docker images with multi-arch support
- GitHub Actions CI/CD pipeline
- Package managers: pip, npm

### 📚 Documentation
- Complete format specification (v0.0.1)
- Implementation guide for library authors
- Quick reference guide
- Architecture documentation
- Comprehensive test vectors
- API documentation with examples

### 🧪 Testing
- Comprehensive unit tests
- Cross-language interoperability tests
- Performance benchmarks
- Fuzz testing harness
- Memory leak detection

### 🛠️ Technical Details

#### Compiler Support
- C++20 standard
- GCC 10+
- Clang 12+
- MSVC 2019+

#### Dependencies
- Required: zlib, OpenSSL
- Optional: lz4, zstd, brotli, snappy
- Test: Google Test

#### Platform Support
- Linux (x64, ARM64)
- macOS (x64, ARM64, Universal)
- Windows (x64, x86, ARM64)
- WebAssembly

### 📊 Performance Metrics

Benchmark results on Intel Core i9-12900K:
- Encoding: 2.1ms for 1MB data
- Decoding: 1.8ms for 1MB data
- Compression ratio: up to 4.7x with Brotli
- Tabular encoding: 8.3ms for 10K rows

### 🚧 Known Limitations

- GraphQL and JSON Schema implementations are header-only (implementation pending)
- Some advanced time-series forecasting models not yet implemented
- Graph visualization layouts require external libraries
- WebAssembly build requires manual configuration

### 🔮 Roadmap for Next Releases

**v0.1.0** (Q1 2025)
- Complete implementation of all header-only features
- Rust and Java bindings
- GPU acceleration for batch processing
- Advanced time-series forecasting

**v0.2.0** (Q2 2025)
- Distributed processing support
- Cloud storage integration
- Real-time streaming protocols
- Advanced security features

**v1.0.0** (Q4 2025)
- API stabilization
- Performance guarantees
- Enterprise features
- Long-term support (LTS)

### Contributors

- Alvar Laigna (@alvarlaigna) - Project Lead
- BTOON Community Contributors

### License

MIT License - See LICENSE file for details.

---

For questions or support, please visit:
- GitHub: https://github.com/BTOON-project/btoon-core
- Documentation: https://btoon.readthedocs.io
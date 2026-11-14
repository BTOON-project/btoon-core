# Changelog

All notable changes to BTOON will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.3.0] - 2026-11-14

Implements spec 0.3 (docs/SPEC.md §11.1). The wire format and the limits are unchanged.
Spec 0.3 requires LZ4 blocks to be valid under the LZ4 block format, end-of-block
conditions and offsets included, and requires every library to write timestamp, date and
datetime values. The vectors grow to 311. This release also fixes the remaining findings
of the 2026-11-13 review and trims the installed API. Entries marked **(behavior change)**
or **(removed)** can affect existing code.

### Security
- **LZ4 blocks are checked against SPEC §6 before liblz4 decodes them.** liblz4 skips the
  end-of-block checks, and accepts a match offset of 0, for a sequence it decodes on its
  fast path, which it takes while at least 32 bytes of output remain. Core therefore
  accepted blocks the spec rejects (the vectors `lz4-block/long-output-ends-with-match` and
  `lz4-block/offset-zero`). A structural pass now parses the tokens, lengths and offsets
  first and fails with `invalid_envelope` on:
  - an offset of 0, or one that reaches back before the start of the output;
  - output that isn't exactly the declared size;
  - a block that ends with a match, or input that ends inside a sequence;
  - in a block with a match, a final literal run shorter than 5 bytes, or a last match
    that starts less than 12 bytes before the end.

  A block without matches is one literal run, valid at any length, and the final token's
  match-length bits are ignored. So an empty output may also be any single token 01 to
  0f, which liblz4 rejected **(behavior change)**. This applies to
  `decompress(CompressionAlgorithm::LZ4, …)` too.
- **Type restrictions didn't apply to tabular rows.** A `Decoder` whose `Security`
  disallowed maps rejected a plain array of maps but accepted the same rows in tabular
  form. Rows now need the map type, like any other map, and fail with `invalid_argument`
  otherwise **(behavior change)**.
- **32-bit builds rejected valid LZ4 envelopes.** The LZ4 and Snappy plausibility bounds
  were computed in `size_t` and wrapped on 32-bit targets: a valid 51 MB document with a
  17 MB LZ4 payload failed with `invalid_envelope` on i386. They're computed in 64 bits
  now, and CI runs the tests on linux-i386. (The vector tests' harness also failed there:
  it returned floats through the x87 stack, which quiets signaling NaNs.)
- **`MemoryMappedFile` and `SharedMemoryBuffer` were copyable.** A copy unmapped the
  region under the original, and destroyed a segment's name twice. Both are now neither
  copyable nor movable; keep them in the `std::unique_ptr` that `open()`/`create()` return
  **(behavior change: copies don't compile)**.
- **`MemoryPool::allocate(SIZE_MAX - 16)` returned memory the next allocation
  overlapped**, because rounding the size up wrapped. Such sizes throw `std::bad_alloc`.
  Freed chunks were also reused and split at unaligned sizes; every allocation is now
  32-byte aligned, as documented.
- **The C API let `std::length_error` escape into C** from `btoon_value_create_string(s,
  SIZE_MAX)`, which terminated the process. The create functions now return `NULL` on any
  failure.

### Changed: the C API (ABI break)
- Timestamps keep their nanoseconds and UTC offset. `btoon_timestamp_t` gains
  `nanoseconds`, `timezone_offset` and `has_timezone`, and
  `btoon_value_create_timestamp(seconds, nanoseconds)` takes the nanoseconds;
  `btoon_value_create_timestamp_with_offset()` is new. Before, both were lost on decode.
- Errors have codes: `btoon_status_t` (`BTOON_OK`, the SPEC §7 codes,
  `BTOON_ERROR_INVALID_ARGUMENT`, `BTOON_ERROR_OUT_OF_MEMORY`) and `btoon_status_name()`.
  `btoon_result_t` gains `status`, and `btoon_decode()` gains a `btoon_status_t* status`
  parameter before `error`.
- A zero-initialized options struct means the defaults. Before, zeroed encode options
  turned tabular off, and zeroed decode options turned off strict mode and
  decompression. `btoon_encode_options_t::auto_tabular` is now `no_tabular`, and
  `btoon_decode_options_t`'s `decompress` and `strict` are now `no_decompress` and
  `non_strict`. Both structs gain an HMAC key (`hmac_key`, `hmac_key_len`; an empty key
  is an error), and the decode options gain `max_depth`, `max_decompressed_size` and
  `max_values` (0 means the default).
- Every type can be created: `btoon_value_create_date`, `_datetime`, `_bigint`,
  `_vector_float` and `_vector_double` are new, as is `btoon_spec_version()`.
- Input is checked instead of trusted: a `NULL` pointer with a nonzero length, an
  unknown type tag, a duplicate map key or a `NULL` element is `invalid_argument`, and a
  hand-built tree deeper than 128 levels, the encoder's limit (or a cycle), is `max_depth`.
- Empty buffers decode to `ptr == NULL, len == 0`. Before, they were copied with `memcpy`
  from a null pointer (undefined behavior).
- capi.h documents ownership: who frees values, results and messages, and with what.
- `BTOON_API` no longer depends on `BTOON_BUILD_SHARED` (see Windows below).

### Changed: the C++ API
- `Timestamp::to_milliseconds()` and `to_microseconds()` overflowed (undefined behavior)
  for valid 12-byte timestamps. They now throw `invalid_argument` when the instant
  doesn't fit in int64, or when nanoseconds are 10^9 or more, and INT64_MIN stays
  reachable **(behavior change)**. They're no longer inline.
- `StreamDecoder` couldn't read anything when `max_decompressed_size` was below 64 KiB.
  Its reads now shrink to the limit; a value larger than the limit still fails with
  `size_limit`.
- Schemas:
  - Removed `SchemaRegistry::validateWithSchema`, `migrateToLatest`, `saveToFile` and
    `loadFromFile`. They were declared but never defined, so any call failed to link.
    Persist a registry with `encode(registry.toValue())` and `SchemaRegistry::fromValue()`
    **(removed)**.
  - `isCompatibleWith()` did the opposite of the `EvolutionStrategy` comments. STRICT now
    accepts an identical schema, ADDITIVE rejects new required fields, and
    BACKWARD_COMPATIBLE rejects type changes other than widening, new required fields and
    new constraints. The rules are documented on `EvolutionStrategy` **(behavior
    change)**.
  - `canMigrateTo()` ran the migration on an empty map to see whether one existed, so
    user code ran on data it wasn't written for and its exceptions escaped
    (`std::out_of_range` from `map::at`). It's now a lookup **(behavior change)**.
  - `SchemaInferrer::infer()`, `inferFromArray()` and `inferSchema()` take a map or an
    array of maps. Anything else throws `invalid_argument`; it used to produce a one-field
    `"value"` or `"items"` schema that rejected its own input **(behavior change)**.
  - Inferred schemas accepted neither float32 nor float64 vectors; they infer as
    `vector_float` and `vector_double` now.
  - `merge()` kept the first schema's constraints, so the merged schema rejected the
    others' data. Constraints now merge: the smaller `min`, the larger `max`, the union of
    `enum`s; others are kept only if equal.
  - A schema or registry definition that isn't a map, and `registerSchema(nullptr)`
    (which crashed), throw `invalid_argument`. Schema APIs throw only `BtoonException` for
    bad input.
- Validator:
  - `SecurityValidator` moved to experimental/. Its byte-scanning checks reported a flat
    array of 200 empty maps as "excessive nesting", and `validate()` passed that on as a
    warning. The decoder enforces depth, length and decompression limits exactly, and
    `validate()` reports them as errors **(removed)**.
  - `quickCheck()` and the free `isValid()` looked only at the first byte, so truncated
    input passed. They now report whether the data decodes, with the validator's
    settings **(behavior change)**.
  - `UTF8Validator::sanitize()` copied overlong forms, surrogates and code points above
    U+10FFFF into its output, and a bad continuation byte swallowed the valid characters
    after it. Each maximal invalid subsequence now becomes one U+FFFD, and the libFuzzer
    target checks that the output is always valid.
  - validator.h no longer defines `NOMINMAX`. `Stats::total_size` is filled in. The
    `ValidationOptions` that do nothing (`check_circular_references`, `limit_recursion`,
    and now `fast_mode`) say so.
- zero_copy.h:
  - `ZeroCopyDecoder` and `ValueView` (with `ArrayView` and `MapView`) moved to
    experimental/. The decoder handled only part of the format and ignored its options
    **(removed)**.
  - `btoon_decode_zero_copy()` was declared but never defined **(removed)**.
  - `BufferPool` buffers went back to the pool only when non-empty, so
    `buffers_in_use` never went down. Every buffer returns now.
  - `ZeroCopyEncoder::encode_into()` fails with `size_limit` instead of `generic` when
    the buffer is too small.

### Changed: building, and Windows
- `NOMINMAX` and `WIN32_LEAN_AND_MEAN` were PUBLIC compile definitions, so they leaked
  into every consumer and changed what its `<windows.h>` declared (`ShellExecuteA` stopped
  compiling). They're private to the library now, and the public headers compile with
  `<windows.h>`'s `min`/`max` macros **(behavior change for consumers that relied on
  them)**.
- CMakeLists.txt overrode `CMAKE_MSVC_RUNTIME_LIBRARY`, so the release workflow's static
  runtime was ignored and `btoon.exe` needed the VC++ redistributable. The library now
  leaves the setting to the caller (CMake's default is still the DLL runtime). The
  released Windows tools use the static runtime, and the release workflow fails if they
  import the VC++ runtime.
- On Windows the library is always static; `BUILD_SHARED_LIBS` is ignored for it, with a
  warning. A shared build exported nothing, since the C++ API has no DLL export markers.
  vcpkg's port is static too **(behavior change)**.

### Fixed: the command-line tools
- `btoon-schema generate`:
  - The C++ threw `bad_variant_access` on valid data: an integer in a `uint` or `number`
    field (a decoder returns the smallest form). It now reads every type a field
    accepts, and throws `BtoonException` (`invalid_argument`) for a missing field or the
    wrong type. Field names become identifiers (`first-name` is `first_name`, keywords
    get a `_` suffix; two fields that collide are an error), and the struct is written
    outside `namespace btoon`; `-n NAMESPACE` wraps it in one **(behavior change)**.
  - The Python failed at import when an optional field came before a required one. The
    dataclass now lists required fields first, uses identifiers, doesn't import `btoon`,
    and `from_dict()` raises `KeyError` for a missing required field.
  - TypeScript quotes property names that aren't identifiers.
  - `-o -` writes to standard output. The help listed go and rust, which never existed.
- `btoon-schema migrate` always failed between versions, since the command line can't
  register a migration function. It now carries the data unchanged to a newer version
  that `isCompatibleWith()` accepts, then validates it against that version.
- `btoon-schema infer` fails on data that isn't a map or an array of maps.

### Docs
- Corrected: performance.md said `Validator` checks structure without allocating, and
  delta_codec.h said its element cap protected against amplification. `MemoryPool`'s
  header promised a thread-safe option and configurable growth, which don't exist.
- API.md documents the C API's codes, defaults and ownership, the schema evolution
  rules, and the zero-copy buffers.

Checked and already fixed in 0.1.5: btoon-convert's CSV numbers (`Nan`, `0x10`), batch
output paths, `--batch-size abc`, XML escaping, NaN payloads in the CLI's `$f32`/`$f64`
tags, and btoon.1's exit codes.

## [0.2.0] - 2026-11-14

Implements spec 0.2 (docs/SPEC.md §11.2). The wire format is unchanged: every valid 0.1
document that fits the new limits decodes to the same value. Decisions D23–D26 in the
workspace's docs/DECISIONS.md record why.

### Security
- **Decoding has a memory budget (D23, SPEC §7.1).** A few KB of compressed input could
  make the decoder allocate gigabytes. Three limits now apply:
  - `DecodeOptions::max_values` (default 2^20) bounds the values a document decodes to.
    Every value counts, including each tabular row and cell; map keys don't.
  - The column-name bytes a table copies into its rows (rows × Σ name lengths) can't
    exceed `max_decompressed_size`.
  - `max_decompressed_size` now defaults to 64 MiB, down from 256 MiB.

  Pre-allocation is capped by the remaining value budget too. On the review's inputs:
  - an 8 KB zlib envelope of empty maps fails with `size_limit` at a 112 MB peak;
  - a 58 KB envelope announcing 60M elements fails at 288 MB instead of reserving
    2.5 GB;
  - a 97-byte tabular key bomb fails before allocating (258 MB in 0.1.4);
  - a valid one-million-map document still decodes in 44 MB.
- **An empty HMAC key is an error (D25, SPEC §8).** `EncodeOptions::hmac_key` and
  `DecodeOptions::hmac_key` are now `std::optional<std::string>`. `std::nullopt` means no
  key, and an empty key throws `invalid_argument` instead of turning signing and
  verification off. Assignments such as `opts.hmac_key = key` still compile.
- `max_depth` above 1000 (`kMaxDepthLimit`) throws `invalid_argument` (SPEC §7).

### Changed
- In non-strict mode, map keys and tabular column names must still be valid UTF-8
  (SPEC §7). Keys compare by bytes, and replacing invalid sequences could merge them.
- Tabular column names are checked front to back: each one for UTF-8, then for
  duplicates. The first failing name sets the error (SPEC §5.6.3).
- `Encoder::encodeExtension`, `encodeExtensionHeader` and `Value(Extension)` reject the
  ids BTOON defines (-1…-6 and -10) with `invalid_argument` (SPEC §5.7). Encode those
  types from their typed values. Ids reserved for future versions are written as given.
- The vectors (296) add:
  - NaN payloads in float vectors;
  - limit cases with the new decoder options;
  - FDICT;
  - non-strict key rules;
  - `encode_error` cases.
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
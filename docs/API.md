# C++ API reference

Everything is in `namespace btoon`. Include `<btoon/btoon.h>` for values, options, and
the top-level `encode`/`decode`. The other headers are needed only for the parts
below that mention them. For the format itself, see [SPEC.md](SPEC.md).

## Values

`btoon::Value` is a `std::variant` holding exactly one of:

| Alternative | C++ type | Notes |
|---|---|---|
| `Nil` | `std::nullptr_t` | |
| `Bool` | `bool` | |
| `Int` | `int64_t` | Every decoded integer that fits int64 |
| `Uint` | `uint64_t` | Decoded only for values above `INT64_MAX` |
| `Float` | `double` | float32 on the wire decodes to `double` |
| `String` | `std::string` | UTF-8 |
| `Binary` | `std::vector<uint8_t>` | |
| `Array` | `std::vector<Value>` | |
| `Map` | `std::map<std::string, Value>` | Iterates in UTF-8 byte order, the canonical key order |
| `Extension` | `{int8_t type; std::vector<uint8_t> data;}` | Any extension not listed below |
| `Timestamp` | `{int64_t seconds; uint32_t nanoseconds; int16_t timezone_offset; bool has_timezone;}` | Ext -1. `Timestamp::now()`, `from_milliseconds()`, `from_microseconds()`. `to_milliseconds()` and `to_microseconds()` round down and throw `invalid_argument` when the instant doesn't fit in int64 |
| `Date` | `{int64_t milliseconds;}` | Ext -2 |
| `DateTime` | `{int64_t nanoseconds;}` | Ext -3 |
| `BigInt` | `{std::vector<uint8_t> bytes;}` | Ext -4, two's complement BE. Canonicalized to `Int`/`Uint` when it fits |
| `VectorFloat` | `{std::vector<float> data;}` | Ext -5 |
| `VectorDouble` | `{std::vector<double> data;}` | Ext -6 |

Helpers:
- `v.type_name()` returns the spec name.
- `v.is_nil()` and `v.is_integer()` test the type.
- `v.as_int64()`, `v.as_uint64()` and `v.as_double()` return `std::optional`.

`operator==` compares integers by value, so `Value(Int(5)) == Value(Uint(5))`. Floats
use IEEE `==`.

```cpp
Value doc = Map{
    {"id", Int(42)},
    {"tags", Array{String("a"), String("b")}},
    {"at", Timestamp::now()},
    {"embedding", VectorFloat{{0.1f, 0.2f, 0.3f}}},
};
int64_t id = *std::get<Map>(doc)["id"].as_int64();
```

## encode / decode

```cpp
std::vector<uint8_t> encode(const Value& value, const EncodeOptions& options = {});
Value decode(std::span<const uint8_t> data, const DecodeOptions& options = {});
```

`encode` produces the canonical encoding, then applies the compression envelope and
then the signed envelope if those options ask for them. `decode` undoes the same
layers automatically.

| `EncodeOptions` | Default | |
|---|---|---|
| `compress` | `false` | Wrap in a compression envelope. Skipped if smaller than `min_compression_size` or not ≥5% smaller |
| `compression_algorithm` | `NONE` (means zlib) | `ZLIB`, `LZ4`, `ZSTD`, `BROTLI`, `SNAPPY`, `AUTO`. Unavailable → `unsupported_compression` |
| `compression_level` / `compression_preset` | `0` / `DEFAULT` | Numeric level, or a preset from `FASTEST` to `MAXIMUM` |
| `use_profile` + `compression_profile` | off | `CompressionProfile::realtime()`, `network()`, `storage()`, `streaming()` |
| `adaptive_compression` | `false` | Pick the algorithm from the data |
| `min_compression_size` | `256` | |
| `auto_tabular` | `true` | Columnar encoding when smaller (SPEC §5.6.2) |
| `max_depth` | `128` | Deeper or cyclic values throw `max_depth`. At most `kMaxDepthLimit` (1000); more is `invalid_argument` |
| `validate_utf8` | `true` | Throw `invalid_utf8` for bad strings and keys |
| `hmac_key` | `std::nullopt` | Set: sign with HMAC-SHA256. An empty key is `invalid_argument` (SPEC §8) |

| `DecodeOptions` | Default | |
|---|---|---|
| `strict` | `true` | SPEC §7 strict mode. Non-strict mode still requires UTF-8 map keys and column names |
| `max_depth` | `128` | At most `kMaxDepthLimit` (1000); more is `invalid_argument` |
| `max_decompressed_size` | 64 MiB | Also bounds the column-name bytes a table copies into its rows (SPEC §7.1) |
| `max_values` | 2^20 (1,048,576) | Values a document may decode to, rows and cells included; map keys don't count (SPEC §7.1). Raise it for large tables |
| `auto_decompress` | `true` | When `false`, compressed input is rejected |
| `hmac_key` | `std::nullopt` | Set: require and verify a signed envelope. An empty key is `invalid_argument` |

```cpp
auto bytes = encode(doc, {.compress = true, .compression_algorithm = CompressionAlgorithm::ZSTD});
Value back = decode(bytes, {.max_decompressed_size = 16 << 20});
```

Other functions: `is_tabular(const Array&)` reports whether the encoder would use the
columnar form. `version()` returns `"0.3.0"` and `spec_version()` returns `"0.3"`.

## Errors

Every failure throws `btoon::BtoonException` (a `std::runtime_error`):

```cpp
try {
    decode(untrusted);
} catch (const BtoonException& e) {
    if (e.code() == ErrorCode::max_depth) { /* … */ }
    std::cerr << e.code_name() << ": " << e.what() << "\n";  // "truncated: unexpected end of input (at byte 7)"
}
```

The `ErrorCode` values are the SPEC §7 codes: `truncated`, `invalid_marker`,
`trailing_data`, `invalid_utf8`, `non_string_key`, `duplicate_key`, `max_depth`,
`invalid_extension`, `invalid_tabular`, `invalid_envelope`, `size_limit`,
`unsupported_compression` and `authentication_failed`. There are also `invalid_argument`
(bad input to an encoder) and `generic`.

## Encoder (`<btoon/encoder.h>`)

Use the encoder directly to emit values incrementally, without building a `Value` tree:

```cpp
Encoder enc;                    // or Encoder enc(EncodeOptions{...});
enc.encodeMapHeader(2);
enc.encodeString("ids");
enc.encodeArrayHeader(3);
for (int64_t i : {1, 2, 3}) enc.encodeInt(i);
enc.encodeString("ok");
enc.encodeBool(true);
std::vector<uint8_t> bytes = enc.release();   // moves the buffer out
```

The primitives always produce the canonical form:
- `encodeNil`, `encodeBool`, `encodeInt`, `encodeUint`, `encodeFloat`
- `encodeString(std::string_view)`, `encodeBinary`
- `encodeTimestamp`, `encodeDate`, `encodeDateTime`, `encodeBigInt`
- `encodeVectorFloat`, `encodeVectorDouble`, `encodeExtension`
- headers: `encodeArrayHeader`, `encodeMapHeader`, `encodeExtensionHeader`
- `encodeColumnar(const Array&)`, which uses tabular when it qualifies, and `encodeRaw`

Use `encode(const Value&)` for whole values. Output methods:
- `getBuffer()` is idempotent and returns a span.
- `release()` moves the buffer out.
- `size()` and `clear()` do what they say.

The `Encoder` writes the bare document. It doesn't apply `compress` or `hmac_key` from
`EncodeOptions`: `encode()` does, and `apply_envelopes(enc.release(), options)` adds them
to an incrementally encoded document. `Encoder(const Security&)` is the exception: it
wraps the output in a signed envelope when it is finalized. The `Security` object is
copied.

## Decoder (`<btoon/decoder.h>`)

```cpp
Decoder dec(DecodeOptions{.strict = true});
Value v = dec.decode(bytes);                               // whole buffer, one value
auto [first, used] = dec.decode_and_get_pos(stream);       // one value; trailing bytes allowed
```

`decode()` reads a complete document exactly like the free `decode()`. It verifies and
removes a signed envelope, then decompresses a compression envelope. With a key, from
`DecodeOptions::hmac_key` or a `Security` object (which is copied), input must be signed.
Without one, signed input fails with `authentication_failed`.

`decode_and_get_pos()` reads bare values, because `BTON` and `BTSG` are also valid
bare values in a stream. When the decoder has a key, though, the buffer must be a signed
envelope, and the value is read from its verified payload.

## Streams (`<btoon/stream_encoder.h>`, `<btoon/stream_decoder.h>`)

A stream is a sequence of bare documents written back to back. Envelopes can't be used
in a stream: their magic bytes are also valid bare values, so a reader couldn't tell where a
document ends. `StreamEncoder` therefore throws `invalid_argument` if `compress` or
`hmac_key` is set, and `StreamDecoder` does if `hmac_key` is set.

```cpp
std::ofstream out("events.btoon", std::ios::binary);
StreamEncoder writer(out);
for (const auto& e : events) writer.write(e);

std::ifstream in("events.btoon", std::ios::binary);
StreamDecoder reader(in);
while (auto v = reader.read()) handle(*v);    // reads incrementally
```

## Signing (`<btoon/security.h>`)

```cpp
Security sec("32+ random bytes of key material..");
auto sealed = sec.wrap(bytes);                 // "BTSG" envelope (SPEC §8)
std::span<const uint8_t> payload = sec.unwrap(sealed);  // throws authentication_failed
```

The same thing through options: `encode(v, {.hmac_key = key})` and
`decode(b, {.hmac_key = key})`.

Also available:
- `sha256(data)` and `hmac_sha256(key, a, b)`, both built in with no OpenSSL;
- `constant_time_equal(a, b)`;
- `Security::setAllowedTypes({...})`, which restricts which `Value` alternatives (by
  variant index) a `Decoder` built with this `Security` will accept. A disallowed type
  fails with `invalid_argument`. The rows of a tabular array are maps, so they need the
  map type (index 8) like any other array of maps.

## Compression (`<btoon/compression.h>`)

```cpp
bool compression_available(CompressionAlgorithm);
std::vector<uint8_t> compress(CompressionAlgorithm, std::span<const uint8_t>, int level = 0);
std::vector<uint8_t> decompress(CompressionAlgorithm, std::span<const uint8_t>, size_t expected_size); // exact
std::vector<uint8_t> decompress_bounded(CompressionAlgorithm, std::span<const uint8_t>, size_t max_size);
```

These work on raw codec payloads. The envelope adds the header that records the size.
LZ4 block payloads can only be decompressed when you supply `expected_size`, and must be
valid under SPEC §6's block rules (no offset 0, end-of-block conditions met).

## Schemas and validation (`<btoon/schema.h>`, `<btoon/validator.h>`)

- `SchemaBuilder("user").version(1,0,0).field(...).build()` builds a `Schema`. A schema
  can `validate(value)`, list errors with `validateWithErrors`, check
  `isCompatibleWith`, and round-trip through `toValue()`/`fromValue()`.
  `SchemaRegistry` stores versions; persist it with `encode(registry.toValue())` and
  `SchemaRegistry::fromValue()`.
- Evolution: `old.isCompatibleWith(newer)` applies `old`'s `EvolutionStrategy`:
  - `STRICT`: the same fields (name, type, `required`, constraints).
  - `ADDITIVE`: every old field unchanged, and only optional fields added.
  - `BACKWARD_COMPATIBLE` (the default): data written for the old schema validates under
    the newer one. Optional fields may be added and fields removed (unless `newer` is
    `STRICT`); types may widen (`int`/`uint` to `number` or `bigint`, `float` to
    `number`, anything to `any`); a field may become optional or lose its constraints.
  - `FLEXIBLE`: anything; use migrations.
- `addMigration(from, to, fn)` registers a function; `migrate(value, to)` runs it.
  `canMigrateTo(target)` only checks that the versions match or a migration exists; it
  doesn't run anything.
- Validation:
  - Required fields must be present.
  - Every present field, required or optional, must have its type and meet its
    constraints (a map such as `{"min": 0, "max": 10, "pattern": "[a-z]+", "enum": [...]}`).
  - `min` and `max` apply to numbers and compare exactly across `Int`, `Uint`, `Float`
    and `BigInt`. NaN never satisfies them.
- A constraint that can't be evaluated throws `invalid_argument` when the schema is
  built or parsed, not during validation: a non-numeric `min`/`max`, or a `pattern` that
  isn't a string or doesn't compile.
- `pattern`:
  - The whole string must match, as with `std::regex_match`.
  - Matching runs in linear time and never recurses on the string, so untrusted values
    are safe.
  - The syntax is the common ECMAScript subset: literals, `.`, `^`, `$`, `\b`, `|`,
    groups (`(…)`, `(?:…)`, `(?<name>…)`), the quantifiers `* + ? {n} {n,} {n,m}` (lazy
    forms too), classes `[…]` and `[^…]`, and the escapes `\d \w \s \D \W \S \t \n \xHH
    \uHHHH \u{…}`.
  - Back-references, lookaround and `\p{…}` are rejected.
  - It matches code points, and `\d`, `\w` and `\b` are ASCII.
  - Limits: patterns up to 4 KB, repetition counts up to 1000, and 10,000 compiled
    states.
- `SchemaVersion::fromString` accepts `MAJOR.MINOR.PATCH` with uint32 parts and throws
  `invalid_argument` otherwise.
- `SchemaInferrer(InferenceOptions{...}).inferFromArray(rows)` derives a schema from
  data. Integer fields infer as `"int"`. Mixed numeric fields become `"number"` when
  `merge_numeric_types` is set.
  - A schema describes maps: `infer()` takes a map or an array of maps (the rows), and
    anything else throws `invalid_argument`.
  - `merge()` combines schemas into one that accepts each one's data: types widen, and
    constraints merge (the smaller `min`, the larger `max`, the union of `enum`s).
- `Validator(ValidationOptions{...}).validate(bytes)` decodes the bytes and checks the
  result against the options.
  - Decoding is strict, with `max_depth`, a decompression limit of
    `min(max_total_size, kDefaultMaxDecompressedSize)`, and `hmac_key` for signed
    documents. An empty key is an error.
  - Strict decoding always rejects duplicate keys and invalid UTF-8.
    `allow_duplicate_map_keys` and `require_utf8_strings` matter only for
    `validate(value)`.
  - Validating bytes decodes them, so it allocates like `decode()`.
  - `quickCheck(bytes)` (and the free `isValid(bytes)`) reports whether the bytes decode,
    without the value checks.
  - `sanitize(bytes)` returns the canonical re-encoding of valid input, or nothing.
  - It also offers `UTF8Validator` (`sanitize` replaces each invalid sequence with
    U+FFFD) and `TypeValidator`.

## Utility codecs (`<btoon/delta_codec.h>`, `rle_codec.h`, `dictionary_codec.h`)

`DeltaCodec`, `RleCodec` and `DictionaryCodec` turn an `Array` into a compact sequence of
BTOON values and back. They are standalone helpers, not part of the wire format.
`decode()` takes two caps:
- `max_elements`, default 2^24;
- `max_bytes`, default 256 MiB, on the decoded data's estimated memory: one `Value` per
  node plus string and binary bytes. A run or dictionary index copies a whole value, so
  this bounds what a few bytes can expand to.

Exceeding either fails with `size_limit`. RLE runs hold values with identical canonical
encodings, so `0.0` and `-0.0` stay apart.

## C API (`<btoon/capi.h>`)

```c
btoon_value_t* v = btoon_value_create_timestamp_with_offset(1704067200, 500000000, 120);
btoon_encode_options_t eopts = {0};          /* zero-initialized: the defaults */
eopts.compress = true;
btoon_result_t r = btoon_encode(v, &eopts);
if (r.status != BTOON_OK) fprintf(stderr, "%s: %s\n", btoon_status_name(r.status), r.error);

btoon_status_t status;
char* err = NULL;
btoon_value_t* back = btoon_decode(r.data, r.size, NULL, &status, &err);
if (!back) { fprintf(stderr, "%s: %s\n", btoon_status_name(status), err); btoon_free_error(err); }
btoon_free_result(r);
btoon_value_destroy(v);
btoon_value_destroy(back);
```

- No C++ exception crosses the C boundary. `btoon_encode` and `btoon_decode` report a
  `btoon_status_t` (`BTOON_OK`, or the SPEC §7 codes as `BTOON_ERROR_*`, plus
  `BTOON_ERROR_INVALID_ARGUMENT` and `BTOON_ERROR_OUT_OF_MEMORY`) and a message.
- The `btoon_value_create_*` functions return `NULL` when memory runs out or a pointer is
  `NULL` with a nonzero length. Every type has one, including dates, datetimes, bigints,
  vectors, and timestamps with nanoseconds and an offset.
- A zero-initialized `btoon_encode_options_t` or `btoon_decode_options_t` (or `NULL`)
  means the defaults. The decode options take `max_depth`, `max_decompressed_size`,
  `max_values` (0 = default) and an HMAC key, as `DecodeOptions` does.
- Ownership: values from `btoon_value_create_*` and `btoon_decode` belong to the caller
  and are freed with `btoon_value_destroy`, which frees the whole tree. The create
  functions copy their input; arrays and maps deep-copy their elements. Free results with
  `btoon_free_result` and decode messages with `btoon_free_error`. Strings are
  NUL-terminated; other buffers have `ptr == NULL` exactly when `len == 0`.

## Zero-copy buffers (`<btoon/zero_copy.h>`)

`MemoryMappedFile::open(path)` maps a file read-only and `SharedMemoryBuffer::create(name,
size)` maps a named shared-memory segment (owner-only, POSIX mode 0600; not on Android).
Decode from either with `decode(buffer->view().span())`. Both own their mapping and are
neither copyable nor movable: keep them in the returned `std::unique_ptr`. `BufferPool`
recycles byte buffers, and `ZeroCopyEncoder` encodes into a caller's buffer, a pool buffer
or shared memory. The C functions `btoon_mmap_file` and `btoon_shm_create` return views
that own their mapping.

`MemoryPool` (`<btoon/memory_pool.h>`) is an arena allocator with 32-byte aligned
allocations. The codec doesn't use it.

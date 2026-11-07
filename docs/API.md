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
| `Timestamp` | `{int64_t seconds; uint32_t nanoseconds; int16_t timezone_offset; bool has_timezone;}` | Ext -1. `Timestamp::now()`, `from_milliseconds()`, `from_microseconds()` |
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
| `max_depth` | `128` | Deeper or cyclic values throw `max_depth` |
| `validate_utf8` | `true` | Throw `invalid_utf8` for bad strings and keys |
| `hmac_key` | empty | Non-empty: sign with HMAC-SHA256 |

| `DecodeOptions` | Default | |
|---|---|---|
| `strict` | `true` | SPEC §7 strict mode |
| `max_depth` | `128` | |
| `max_decompressed_size` | 256 MiB | |
| `auto_decompress` | `true` | When `false`, compressed input is rejected |
| `hmac_key` | empty | Non-empty: require and verify a signed envelope |

```cpp
auto bytes = encode(doc, {.compress = true, .compression_algorithm = CompressionAlgorithm::ZSTD});
Value back = decode(bytes, {.max_decompressed_size = 16 << 20});
```

Other functions: `is_tabular(const Array&)` reports whether the encoder would use the
columnar form. `version()` returns `"0.1.2"` and `spec_version()` returns `"0.1"`.

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

`Encoder(const Security&)` wraps the output in a signed envelope when it is finalized.

## Decoder (`<btoon/decoder.h>`)

```cpp
Decoder dec(DecodeOptions{.strict = true});
Value v = dec.decode(bytes);                               // whole buffer, one value
auto [first, used] = dec.decode_and_get_pos(stream);       // one value; trailing bytes allowed
```

The `Decoder` handles bare documents, plus signed envelopes when constructed with a
`Security`. Compression envelopes are handled by the free `decode()`.

## Streams (`<btoon/stream_encoder.h>`, `<btoon/stream_decoder.h>`)

A stream is a sequence of bare documents written back to back.

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
- `Security::setAllowedTypes({...})`, which restricts which `Value` alternatives a
  `Decoder` built with this `Security` will accept.

## Compression (`<btoon/compression.h>`)

```cpp
bool compression_available(CompressionAlgorithm);
std::vector<uint8_t> compress(CompressionAlgorithm, std::span<const uint8_t>, int level = 0);
std::vector<uint8_t> decompress(CompressionAlgorithm, std::span<const uint8_t>, size_t expected_size); // exact
std::vector<uint8_t> decompress_bounded(CompressionAlgorithm, std::span<const uint8_t>, size_t max_size);
```

These work on raw codec payloads. The envelope adds the header that records the size.
LZ4 block payloads can only be decompressed when you supply `expected_size`.

## Schemas and validation (`<btoon/schema.h>`, `<btoon/validator.h>`)

- `SchemaBuilder("user").version(1,0,0).field(...).build()` builds a `Schema`. A schema
  can `validate(value)`, list errors with `validateWithErrors`, check
  `isCompatibleWith`, and round-trip through `toValue()`/`fromValue()`.
  `SchemaRegistry` stores versions.
- `SchemaInferrer(InferenceOptions{...}).inferFromArray(rows)` derives a schema from
  data. Integer fields infer as `"int"`. Mixed numeric fields become `"number"` when
  `merge_numeric_types` is set.
- `Validator(ValidationOptions{...}).validate(bytes)` checks structure without building
  values. It also offers `UTF8Validator`, `TypeValidator` and `SecurityValidator`.

## Utility codecs (`<btoon/delta_codec.h>`, `rle_codec.h`, `dictionary_codec.h`)

`DeltaCodec`, `RleCodec` and `DictionaryCodec` turn an `Array` into a compact sequence of
BTOON values and back. They are standalone helpers, not part of the wire format.
`decode()` takes a `max_elements` cap, default 2^24.

## C API (`<btoon/capi.h>`)

```c
btoon_value_t* v = btoon_value_create_string("hi", 2);
btoon_result_t r = btoon_encode(v, NULL);        /* r.error is set on failure */
char* err = NULL;
btoon_value_t* back = btoon_decode(r.data, r.size, NULL, &err);
if (!back) { fprintf(stderr, "%s\n", err); btoon_free_error(err); }
btoon_free_result(r);
btoon_value_destroy(v);
btoon_value_destroy(back);
```

No C++ exception crosses the C boundary. Free values only with `btoon_value_destroy`,
results with `btoon_free_result`, and error strings with `btoon_free_error`.

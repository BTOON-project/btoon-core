# Tutorial

A tour of BTOON from C++. Every snippet compiles against 0.1.1. Link with
`btoon::btoon_core` (CMake) or `-lbtoon_core -lz`.

## 1. Values in, bytes out

```cpp
#include <btoon/btoon.h>
#include <iostream>
using namespace btoon;

int main() {
    Value v = Map{{"name", String("Alice")}, {"age", Int(30)}, {"admin", Bool(true)}};
    std::vector<uint8_t> bytes = encode(v);
    std::cout << bytes.size() << " bytes\n";   // 24 bytes; keys are written in sorted order
    Value back = decode(bytes);
    std::cout << std::get<String>(std::get<Map>(back)["name"]) << "\n";
}
```

BTOON is MessagePack with sorted keys, so any MessagePack decoder reads these bytes.

## 2. Tables get smaller automatically

When an array holds maps that all share the same keys, BTOON stores it column by column
and writes each key only once. It does this only when the result is smaller:

```cpp
Array rows;
for (int i = 0; i < 1000; ++i) {
    rows.push_back(Map{{"id", Int(i)}, {"name", String("user" + std::to_string(i))}, {"score", Float(i * 0.5)}});
}
auto tabular = encode(Value(rows));
auto plain = encode(Value(rows), {.auto_tabular = false});   // plain MessagePack
// tabular.size() is about 57% of plain.size() (19,560 vs 34,509 bytes); both decode to the same rows.
```

Small or non-uniform arrays stay plain. The size rule is in SPEC §5.6.2, and
`is_tabular(rows)` tells you which encoding will be used.

## 3. More than JSON

```cpp
Value event = Map{
    {"at", Timestamp::now()},                       // MessagePack timestamp (ns precision)
    {"day", Date{1704067200000}},                   // milliseconds since epoch
    {"blob", Binary{0xde, 0xad, 0xbe, 0xef}},
    {"embedding", VectorFloat{{0.12f, -0.5f, 0.33f}}},  // packed float32, little-endian
    {"huge", BigInt{{0x01, 0, 0, 0, 0, 0, 0, 0, 0}}},  // 2^64
};
```

Integers that fit in 64 bits always decode as `Int`, or as `Uint` above `INT64_MAX`, and
they compare equal across the two. Use `as_int64()`/`as_uint64()` to read them.

## 4. Compression and signing

```cpp
auto packed = encode(event, {.compress = true});                 // zlib envelope
auto zstd   = encode(event, {.compress = true, .compression_algorithm = CompressionAlgorithm::ZSTD});
Value same  = decode(packed);                                     // envelopes are detected

std::string key = "a key with at least 32 random bytes!";
auto sealed = encode(event, {.compress = true, .hmac_key = key});  // compress, then sign
Value ok    = decode(sealed, {.hmac_key = key});                   // verified before parsing
```

Compression is skipped when it wouldn't save at least 5%. Zstd needs a build with
`-DBTOON_WITH_ZSTD=ON`. If you request an unavailable algorithm, you get an error rather
than uncompressed output.

## 5. Untrusted input

The decoder is safe on hostile bytes. Tune its limits per call:

```cpp
try {
    Value v = decode(untrusted, {.strict = true, .max_depth = 32, .max_decompressed_size = 4 << 20});
} catch (const BtoonException& e) {
    // e.code(): ErrorCode::truncated, max_depth, size_limit, invalid_utf8, ...
    std::cerr << e.code_name() << ": " << e.what() << "\n";
}
```

## 6. Streaming many values

```cpp
std::ofstream out("log.btoon", std::ios::binary);
StreamEncoder writer(out);                     // <btoon/stream_encoder.h>
writer.write(event);
writer.write(event);
writer.close();

std::ifstream in("log.btoon", std::ios::binary);
StreamDecoder reader(in);                      // <btoon/stream_decoder.h>
while (auto v = reader.read()) { /* ... */ }
```

## 7. Building values without a tree

For hot paths, write straight into an `Encoder`:

```cpp
Encoder enc;                                    // <btoon/encoder.h>
enc.encodeArrayHeader(3);
enc.encodeInt(1);
enc.encodeString("two");
enc.encodeFloat(3.0);
std::vector<uint8_t> bytes = enc.release();     // == encode(Array{Int(1), String("two"), Float(3.0)})
```

Next: [API.md](API.md) for everything else, and [SPEC.md](SPEC.md) for the bytes.

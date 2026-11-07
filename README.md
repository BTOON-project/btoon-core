# BTOON Core

**BTOON** ("Binary TOON") is a compact binary serialization format: **MessagePack plus a
columnar encoding for arrays of uniform objects**. Keys are written once per table instead
of once per row, which typically makes record-shaped data 40–50% smaller than plain
MessagePack before any compression.

This repository holds the **format specification**, the **conformance test vectors**, and
the **reference C++20 implementation** (library + `btoon` CLI).

- Spec: [docs/SPEC.md](docs/SPEC.md) (version 0.1)
- Vectors: [tests/vectors/btoon-vectors.json](tests/vectors/btoon-vectors.json) (260 cases)
- Language libraries: [Python](https://github.com/BTOON-project/btoon-python) ·
  [Node.js](https://github.com/BTOON-project/btoon-nodejs) ·
  [JavaScript (browser)](https://github.com/BTOON-project/btoon-javascript) ·
  [Go](https://github.com/BTOON-project/btoon-go) · [PHP](https://github.com/BTOON-project/btoon-php)

## What you get

- **MessagePack-compatible.** Every BTOON document is valid MessagePack. With tabular
  encoding turned off, the output is byte-for-byte what a canonical MessagePack encoder
  writes.
- **Tabular encoding.** Arrays of maps that share the same keys are stored column by
  column, but only when that is actually smaller (a fixed size rule, SPEC §5.6.2).
- **Canonical.** One value has one encoding: smallest integers, float64, keys sorted by
  UTF-8 bytes. Every BTOON library produces identical bytes, which the conformance vectors
  check.
- **Extra types.** MessagePack timestamps (plus an optional UTC offset), date, datetime,
  arbitrary-precision integers, and float32/float64 vectors for embeddings.
- **Envelopes.** Optional compression (zlib built in; LZ4, Zstandard, Brotli and Snappy
  when built with them) and HMAC-SHA256 signing.
- **Hardened decoder.** Bounds-checked, depth-limited, allocation-guarded, strict UTF-8,
  and decompression-bomb safe. It is fuzzed with libFuzzer, and the tests run under
  AddressSanitizer and UndefinedBehaviorSanitizer.

## Size

200 user records (`id`, `name`, `email`, `score`, `active`) plus a small `meta` object:

| Encoding | Bytes | vs JSON |
|---|---:|---:|
| JSON (minified) | 16,345 | 100% |
| MessagePack | 12,686 | 77.6% |
| **BTOON** | **7,166** | **43.8%** |
| JSON + zlib | 2,228 | 13.6% |
| **BTOON + zlib** | **1,845** | **11.3%** |

The gain comes from not repeating keys, so it grows with the number of rows and the length
of the key names. Data that isn't tabular encodes exactly like MessagePack.

## Speed

`btoon_bench` with 10,000 records, Release build, Apple M4 Max. Best of 7 runs:

| Case | Size | Encode | Decode |
|---|---:|---:|---:|
| records, tabular | 447 KB | 0.60 ms (741 MB/s) | 1.54 ms (291 MB/s) |
| records, plain | 767 KB | 0.69 ms (1.1 GB/s) | 2.05 ms (374 MB/s) |
| nested, mixed | 814 KB | 0.86 ms (943 MB/s) | 2.16 ms (378 MB/s) |

Decoding builds a full `btoon::Value` tree (`std::map`/`std::string`), and that is where
most of its time goes. Reproduce with
`cmake -B build -DBUILD_BENCHMARKS=ON && cmake --build build && build/btoon_bench`.

## Build

Requirements: a C++20 compiler (Clang 14+, GCC 11+, MSVC 2022), CMake 3.20+, zlib.
OpenSSL is **not** required: SHA-256 and HMAC are built in.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build              # unit tests + conformance vectors + fuzz smoke test
cmake --install build               # library, headers, CMake package, btoon CLI
```

| Option | Default | |
|---|---|---|
| `BTOON_WITH_LZ4` / `_ZSTD` / `_BROTLI` / `_SNAPPY` | OFF | Optional codecs (found via pkg-config or CMake configs) |
| `ENABLE_SANITIZERS` | OFF | ASan + UBSan for everything, including the library |
| `BUILD_FUZZ_TESTS` | OFF | libFuzzer target `fuzz_decoder` (Clang) |
| `BUILD_BENCHMARKS` | OFF | `btoon_bench` |
| `BTOON_NATIVE_ARCH` | OFF | `-march=native`. Never for binaries you distribute. |

Use it from CMake:

```cmake
find_package(btoon 0.1 REQUIRED)            # after install
target_link_libraries(app PRIVATE btoon::btoon_core)
# or: add_subdirectory(btoon-core) — tests/tools are skipped when not top-level
```

## C++ quick start

```cpp
#include <btoon/btoon.h>

using namespace btoon;

int main() {
    Array users;
    for (int i = 0; i < 100; ++i) {
        users.push_back(Map{{"id", Int(i)}, {"name", String("user" + std::to_string(i))}});
    }
    Value doc = Map{{"users", users}, {"created", Timestamp::now()}};

    std::vector<uint8_t> bytes = encode(doc);                    // tabular kicks in automatically
    std::vector<uint8_t> packed = encode(doc, {.compress = true}); // zlib envelope

    Value back = decode(packed);                                 // envelopes are detected
    return back == doc ? 0 : 1;
}
```

Errors are thrown as `btoon::BtoonException`. `e.code()` returns an `ErrorCode` that
matches the spec's error names (`truncated`, `max_depth`, `invalid_utf8`, …). See
[docs/API.md](docs/API.md) for the full API, including incremental encoding, streams,
signing, schemas, and the C API.

## CLI

```bash
btoon encode data.json data.btoon --compress      # JSON -> BTOON (zlib)
btoon decode data.btoon -                         # BTOON -> JSON on stdout
btoon info data.btoon                             # envelope, sizes, structure
cat data.json | btoon encode - - | btoon decode - - --compact
```

BTOON types that JSON lacks round-trip as tagged objects, for example
`{"$binary": "<base64>"}`, `{"$timestamp": "2024-01-01T00:00:00Z"}` and
`{"$bigint": "…"}`. See `tools/json_mapping.h`. Also included: `btoon-convert` (JSON, CSV,
MessagePack, XML out) and `btoon-schema` (validate, infer, diff, generate code).

## Layout

```
include/btoon/   public headers (btoon.h, encoder.h, decoder.h, security.h, capi.h, …)
src/             implementation (internal.h is private)
tests/           GoogleTest suite, vectors/ (+ generate.py oracle), fuzz_smoke.cpp, bench.cpp
fuzz/            libFuzzer target and seed corpus
tools/           btoon, btoon-convert, btoon-schema
docs/            SPEC.md, API.md, IMPLEMENTATION-GUIDE.md, QUICK-REFERENCE.md, …
experimental/    design sketches that aren't built or installed
```

## Status

Version **0.1.0** implements spec **0.1**. The format may still change before 1.0. When it
does, the spec, the vectors and every library change together. Changes are tracked in
[docs/CHANGELOG.md](docs/CHANGELOG.md).

## License

MIT. See [LICENSE](LICENSE).

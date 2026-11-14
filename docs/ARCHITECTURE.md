# Architecture

## Pipeline

```
encode(value, opts):  Encoder (canonical) ──► compression envelope? ──► signed envelope?
decode(bytes, opts):  verify signed envelope? ──► decompress (bounded)? ──► Decoder (hardened)
```

`src/btoon.cpp` owns the layering. `Encoder` and `Decoder` deal only with bare documents,
plus signed envelopes when they're constructed with a `Security`.

## Encoder (`src/encoder.cpp`)

- One `std::vector<uint8_t>` serves as the buffer. Its `size()` is the capacity in use
  and `len_` is the logical end. `reserve(n)` hands back a write pointer, so a multi-byte
  write costs a single capacity check.
- Everything is recursive with an explicit `depth`. Containers check `max_depth`.
- **Tabular** (`tryEncodeTabular`): `detail::tabular_layout` applies the qualification test
  and the size rule from SPEC §5.6.2. The encoder reserves a 6-byte ext32 header, writes
  the payload, back-patches each column's `u32` length, and at the end moves the payload
  down if a shorter ext8 or ext16 header suffices. Columns are written by advancing one
  `std::map` iterator per row in lockstep. All rows share the same sorted keys, so this is
  O(N·C) with no lookups.
- Integers go through `encodeUint` whenever they're non-negative, which makes the output
  canonical. NaN is canonicalized there too.

## Decoder (`src/decoder.cpp`)

- A `Reader` holds `{origin, p, end}`. Every read goes through `need(n)`, which throws
  `truncated` with the byte offset. No path indexes the input without checking.
- Arrays check `n ≤ remaining` before `reserve(n)`. Maps check `n ≤ remaining / 2`.
  Tabular checks `N × C ≤ remaining` and `C ≤ remaining / 9` before allocating rows.
- Tabular columns are decoded through a sub-`Reader` bounded to `col_len`. A truncated
  cell becomes `invalid_tabular`, and so does leftover data.
- Maps insert with `try_emplace(end(), …)`. That is O(1) for canonical input, and it
  leaves the value untouched when a duplicate key shows up, so non-strict mode can keep
  the last value.
- Error paths are `[[noreturn]]` and out of line, which keeps the hot paths small.

## Envelopes

- **Compression** (`src/compression.cpp`): every codec decompresses into a `GrowBuf`. The
  buffer grows geometrically up to the declared size, so a lying header can't force a
  large allocation up front. At the limit, a 1-byte probe tells "the stream finished
  exactly here" apart from "the stream wants more". LZ4 and Snappy expose block APIs
  that need the whole output buffer, so their size claims are checked against a
  worst-case expansion ratio first. An LZ4 block's structure is also checked before
  liblz4 runs (offsets, end-of-block conditions, exact size; SPEC §6), because liblz4's
  fast path skips those checks.
- **Signing** (`src/security.cpp`, `src/sha256.cpp`): the HMAC-SHA256 implementation is
  in-tree and checked against FIPS 180-4 and RFC 4231 vectors. MACs are compared in
  constant time, and the MAC is verified before any parsing.

## Conformance and testing

- `tests/vectors/generate.py` is an **independent** Python implementation, built on
  msgpack-python plus the BTOON extensions written from the spec. It generates
  `btoon-vectors.json`. The C++ suite (`tests/test_vectors.cpp`) must decode every vector
  and re-encode the canonical ones byte for byte. CI runs `generate.py --check` to catch
  drift.
- `tests/test_hardening.cpp` covers:
  - truncation sweeps, random bytes, and huge length claims;
  - decompression bombs, lying size headers, and every codec;
  - signing and tampering;
  - the encoder's depth and UTF-8 limits.
- Fuzzing: `tests/fuzz_smoke.cpp` (any compiler, part of ctest) and `fuzz/fuzz_decoder.cpp`
  (libFuzzer). Both run against a library built with ASan and UBSan. See `fuzz/README.md`.
- CI (`.github/workflows/ci.yml`) runs these:
  - Linux GCC with every codec, Linux Clang, macOS and Windows MSVC;
  - ASan+UBSan with every codec;
  - the vectors check;
  - a 2-minute libFuzzer run.

## How the other repositories use core

- **btoon-python** and **btoon-nodejs** vendor this repo as a git submodule at `core/`.
  They compile `src/*.cpp` straight into their extension modules, so neither needs
  OpenSSL or a prebuilt library. They run the shared vectors from the submodule.
- **btoon-javascript**, **btoon-go** and **btoon-php** are independent implementations
  of `docs/SPEC.md`. Each keeps a copy of `tests/vectors/btoon-vectors.json`.
- When the format changes, update the spec and `generate.py` first, regenerate the
  vectors, then the C++ code, then every library.

## Layout

| Path | Contents |
|---|---|
| `include/btoon/` | Public headers (the installed set is listed in `BTOON_PUBLIC_HEADERS`) |
| `src/internal.h` | Private helpers: endian load/store, UTF-8, bigint, tabular rule, `fail()` |
| `src/schema.cpp`, `validator.cpp`, `zero_copy.cpp` | Schemas, validation, memory-mapped and shared-memory buffers |
| `src/*_codec.cpp` | Delta, RLE and dictionary utility codecs |
| `tools/` | `btoon`, `btoon-convert`, `btoon-schema`, and `json_mapping.h` |
| `experimental/` | Design sketches that aren't built (see its README) |

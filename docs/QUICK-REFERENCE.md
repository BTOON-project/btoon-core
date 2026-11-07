# BTOON quick reference

A one-page cheat sheet. [SPEC.md](SPEC.md) is authoritative.

## Markers (MessagePack, big-endian)

| Byte | Type | Byte | Type |
|---|---|---|---|
| `00`–`7f` | positive fixint | `cc`/`cd`/`ce`/`cf` | uint 8/16/32/64 |
| `80`–`8f` | fixmap (N = low 4 bits) | `d0`/`d1`/`d2`/`d3` | int 8/16/32/64 |
| `90`–`9f` | fixarray | `d4`…`d8` | fixext 1/2/4/8/16 |
| `a0`–`bf` | fixstr (N = low 5 bits) | `d9`/`da`/`db` | str 8/16/32 |
| `c0` | nil | `dc`/`dd` | array 16/32 |
| `c1` | **invalid** | `de`/`df` | map 16/32 |
| `c2`/`c3` | false/true | `e0`–`ff` | negative fixint (-32…-1) |
| `c4`/`c5`/`c6` | bin 8/16/32 | | |
| `c7`/`c8`/`c9` | ext 8/16/32 | | |
| `ca`/`cb` | float32/float64 | | |

## Canonical encoding (what encoders must emit)

- Integers: smallest form by **value** (`200` → `cc c8`, `-200` → `d1 ff38`, `5` → `05`).
- Floats: always `cb` + 8 bytes; NaN = `7ff8000000000000`.
- Map keys sorted by UTF-8 bytes (`"Z" < "a" < "aa" < "b" < "é"`).
- Smallest length headers; ext payloads of 1/2/4/8/16 bytes use fixext.

## BTOON extension types

| Id | Byte | Type | Payload |
|---|---|---|---|
| -1 | `ff` | timestamp | 4: `u32 sec` · 8: `nsec<<34 \| sec` · 12: `u32 nsec, i64 sec` · 14: + `i16 offset_min` |
| -2 | `fe` | date | `i64` ms since epoch (always fixext 8) |
| -3 | `fd` | datetime | `i64` ns since epoch (always fixext 8) |
| -4 | `fc` | bigint | two's complement, big-endian, minimal; only outside [-2^63, 2^64-1] |
| -5 | `fb` | float32 vector | packed binary32, **little-endian** |
| -6 | `fa` | float64 vector | packed binary64, **little-endian** |
| -10 | `f6` | tabular | see below |

`2024-01-01T00:00:00Z` → `d6 ff 65920080`

## Tabular (-10)

```
u32 version=1 | u32 C | u32 N | C × (u32 len, name) | C × u8 type_code | C × (u32 col_len, N values)
```

Used when the array has N ≥ 2 elements, all of them maps with the same C ≥ 1 keys, and

```
N × (maphdr(C) + Σ(strhdr(len k) + len k))  >  18 + Σ(9 + len k)
```

Type codes come from the first row: 0 nil · 1 bool · 2 int (int64 range) · 3 uint (above
int64) · 4 float · 5 string · 6 binary · 7 array · 8 map · 9 other.

## Envelopes

```
compressed:  "BTON" | 01 | algo | 0000 | u32 compressed_size | u32 uncompressed_size | payload
signed:      "BTSG" | 01 | 01   | 0000 | 32-byte HMAC-SHA256(key, first 8 bytes ‖ payload) | payload
```

Compression algorithms: 0 zlib (required) · 1 LZ4 block · 2 zstd · 3 brotli · 4 snappy.
Layering: `sign(compress(encode(v)))`.

## Decoder limits and errors

| Default | Value |
|---|---|
| `max_depth` | 128 nested containers |
| `max_decompressed_size` | 256 MiB |
| strict mode | on |

Error codes: `truncated` · `invalid_marker` · `trailing_data` · `invalid_utf8` ·
`non_string_key` · `duplicate_key` · `max_depth` · `invalid_extension` ·
`invalid_tabular` · `invalid_envelope` · `size_limit` · `unsupported_compression` ·
`authentication_failed`

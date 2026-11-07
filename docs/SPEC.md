# BTOON Format Specification

**Version:** 0.1 (draft)
**Date:** 2026-11-07
**Reference implementation:** [btoon-core](https://github.com/BTOON-project/btoon-core)
**Conformance vectors:** [`tests/vectors/btoon-vectors.json`](../tests/vectors/btoon-vectors.json)

BTOON ("Binary TOON") is a compact, self-describing binary serialization format. It is
[MessagePack](https://github.com/msgpack/msgpack/blob/master/spec.md) with four
additions:

1. a **columnar ("tabular") encoding** for arrays of uniform objects, where each key
   is written once instead of once per row;
2. a small set of **extension types**: timestamp, date, datetime, bigint, and float32
   and float64 vectors;
3. an optional **compression envelope**;
4. an optional **HMAC-signed envelope**.

The key words MUST, MUST NOT, SHOULD, SHOULD NOT and MAY are to be interpreted as in
[RFC 2119](https://www.rfc-editor.org/rfc/rfc2119).

---

## 1. Design goals

- **MessagePack-compatible.** Every BTOON document is a valid MessagePack document, so
  generic MessagePack tools can always read it. A MessagePack document is valid BTOON
  when all of the following hold:
  - its map keys are strings, with no key repeated within a map;
  - its strings are valid UTF-8;
  - it doesn't give other meanings to the extension ids BTOON reserves (§5).
- **Canonical.** Every logical value has exactly one canonical encoding. Conforming
  encoders in every language produce identical bytes for the same value.
- **Safe to decode.** Decoders are hardened against hostile input. Decoding never reads
  out of bounds, never recurses without limit, and never allocates much more memory than
  the input justifies.
- **Compact for tables.** Arrays of records shrink substantially (typically 30–45%
  before compression) because column names aren't repeated per row.

Non-goals: human readability, in-place mutation, schema-required decoding.

---

## 2. Data model

A BTOON value is one of:

| Logical type | Notes |
|---|---|
| nil | |
| boolean | |
| integer | Arbitrary size. Values in [-2^63, 2^64-1] use MessagePack integers; others use bigint (§5.4). |
| float | IEEE 754 binary64. NaN, ±Infinity, and -0.0 are preserved. |
| string | Unicode text, encoded as well-formed UTF-8 (RFC 3629). |
| binary | Byte string. |
| array | Ordered sequence of values. |
| map | Unordered collection of **string** keys to values, with unique keys. |
| timestamp | An instant: seconds and nanoseconds since the Unix epoch, optionally with a UTC offset (§5.1). |
| date | Milliseconds since the Unix epoch (§5.2). |
| datetime | Nanoseconds since the Unix epoch (§5.3). |
| float32 vector | Packed array of binary32 (§5.5). |
| float64 vector | Packed array of binary64 (§5.5). |
| extension | Any other `(type, bytes)` pair, opaque to BTOON (§5.7). |

**Map keys are strings.** This is the one restriction relative to MessagePack, and it
matches the data model of JSON. Decoders MUST reject maps with non-string keys.

The tabular encoding (§5.6) is not a separate logical type. It is an alternative wire
encoding of an *array of maps*, and it decodes to exactly that.

---

## 3. Encoding: the MessagePack layer

All multi-byte integers, lengths, and floats in this layer are **big-endian**.

| Format | First byte | Payload |
|---|---|---|
| positive fixint | `0x00`–`0x7f` | value is the byte |
| fixmap | `0x80`–`0x8f` | N = low 4 bits; N key/value pairs |
| fixarray | `0x90`–`0x9f` | N = low 4 bits; N values |
| fixstr | `0xa0`–`0xbf` | N = low 5 bits; N bytes UTF-8 |
| nil | `0xc0` | — |
| *(never used)* | `0xc1` | decoders MUST reject |
| false / true | `0xc2` / `0xc3` | — |
| bin 8/16/32 | `0xc4` / `0xc5` / `0xc6` | u8/u16/u32 length, bytes |
| ext 8/16/32 | `0xc7` / `0xc8` / `0xc9` | u8/u16/u32 length, i8 type, bytes |
| float 32/64 | `0xca` / `0xcb` | binary32 / binary64 |
| uint 8/16/32/64 | `0xcc`–`0xcf` | unsigned integer |
| int 8/16/32/64 | `0xd0`–`0xd3` | two's-complement integer |
| fixext 1/2/4/8/16 | `0xd4`–`0xd8` | i8 type, then 1/2/4/8/16 bytes |
| str 8/16/32 | `0xd9` / `0xda` / `0xdb` | u8/u16/u32 length, UTF-8 bytes |
| array 16/32 | `0xdc` / `0xdd` | u16/u32 count, values |
| map 16/32 | `0xde` / `0xdf` | u16/u32 count, key/value pairs |
| negative fixint | `0xe0`–`0xff` | value is the byte as int8 (-32…-1) |

The length of an `ext` counts payload bytes only, not the type byte.

---

## 4. Canonical encoding

Encoders MUST produce the canonical encoding defined here. (Decoders MUST still accept
any valid MessagePack encoding; see §7.)

1. **Integers** use the smallest format that can hold the *value*:
   - 0…127 → positive fixint; 128…2^8-1 → uint8; …2^16-1 → uint16; …2^32-1 → uint32;
     …2^64-1 → uint64.
   - -32…-1 → negative fixint; -128…-33 → int8; -2^15… → int16; -2^31… → int32;
     -2^63… → int64.
   - The signedness of the source type is irrelevant: a signed `5` and an unsigned `5`
     both encode as `05`.
   - Integers outside [-2^63, 2^64-1] encode as bigint (§5.4).
2. **Floats** always use float64 (`cb`). NaN MUST be written as `7ff8000000000000`.
   Other values, including `-0.0` and ±Infinity, are written bit-exactly.
3. **Strings, binaries, arrays, maps, extensions** use the smallest header that fits
   the length. An extension payload of exactly 1, 2, 4, 8, or 16 bytes uses fixext.
4. **Map keys** are sorted in ascending order by their UTF-8 bytes (unsigned bytewise
   comparison, so a shorter prefix sorts first).
5. **Arrays of maps** use the tabular encoding when the rule in §5.6.2 says so, unless
   the caller disabled tabular encoding.
6. **Timestamps** use the selection algorithm in §5.1.
7. No trailing bytes follow the value.

> **Example.** `{"name": "Alice", "age": 30, "active": true}` canonically encodes as
> `83 a6 616374697665 c3 a3 616765 1e a4 6e616d65 a5 416c696365`
> (keys in the order `active`, `age`, `name`).

---

## 5. Extension types

Extension type ids are signed bytes. MessagePack reserves the negative ids for
predefined types. BTOON assigns:

| Id | Byte | Type | Payload size |
|---|---|---|---|
| -1 | `ff` | timestamp | 4, 8, 12, or 14 |
| -2 | `fe` | date | 8 |
| -3 | `fd` | datetime | 8 |
| -4 | `fc` | bigint | ≥ 1 |
| -5 | `fb` | float32 vector | multiple of 4 |
| -6 | `fa` | float64 vector | multiple of 8 |
| -10 | `f6` | tabular | ≥ 12 |

The ids -9…-7 and -128…-11 are reserved for future BTOON versions. Decoders MUST
surface them as generic extensions (§5.7), not reject them. Ids 0…127 belong to
applications.

A payload whose size doesn't match the table, or whose content violates its section
below, is malformed. Decoders MUST reject it with `invalid_extension`.

### 5.1 Timestamp (-1)

This is identical to the MessagePack timestamp extension, plus one BTOON-only form.

| Payload | Layout (big-endian) | Range |
|---|---|---|
| 4 bytes | `u32 seconds` | 1970…2106, nanoseconds = 0 |
| 8 bytes | `u64 = (nanoseconds << 34) \| seconds` | seconds in [0, 2^34) |
| 12 bytes | `u32 nanoseconds, i64 seconds` | full range |
| 14 bytes | `u32 nanoseconds, i64 seconds, i16 offset_minutes` | full range, with a UTC offset |

`seconds` and `nanoseconds` always describe the instant **in UTC**. `offset_minutes` is
metadata for presentation, giving the local time as UTC + offset. Decoders MUST reject
`nanoseconds ≥ 1,000,000,000` and `|offset_minutes| > 1080`.

Encoders MUST select the form as follows:

```
if value has a UTC offset:            → 14-byte form
elif seconds >> 34 == 0:              # arithmetic shift; negative seconds fail this
    data64 = (nanoseconds << 34) | seconds
    if data64 >> 32 == 0:             → 4-byte form (u32 data64)
    else:                             → 8-byte form (u64 data64)
else:                                 → 12-byte form
```

The 14-byte form is a BTOON extension. Generic MessagePack decoders will reject it.
Encoders SHOULD emit offsets only when the caller explicitly supplies one, and SHOULD
normalize ordinary language datetimes to plain UTC timestamps.

> `2024-01-01T00:00:00Z` → `d6 ff 65920080`
> `1969-12-31T23:59:59Z` (seconds = -1) → `c7 0c ff 00000000 ffffffffffffffff`

### 5.2 Date (-2)

`i64` big-endian: milliseconds since the Unix epoch (UTC). Always `fixext 8`.

### 5.3 DateTime (-3)

`i64` big-endian: nanoseconds since the Unix epoch (UTC). Always `fixext 8`.
This covers 1677-09-21 to 2262-04-11.

### 5.4 BigInt (-4)

A signed integer as two's-complement bytes, **big-endian**, with at least 1 byte.

- Encoders MUST use bigint only for integers outside [-2^63, 2^64-1], and MUST emit
  the minimal length. The first byte is neither `0x00` followed by a byte < `0x80`, nor
  `0xff` followed by a byte ≥ `0x80`.
- Decoders MUST accept any length ≥ 1, including non-minimal payloads and values that
  would fit in 64 bits. They decode to the language's arbitrary-precision integer, or
  to a bigint wrapper type.

> 2^64 → `c7 09 fc 010000000000000000`
> -2^63 - 1 → `c7 09 fc ff7fffffffffffffff`

### 5.5 Vectors (-5, -6)

Packed IEEE 754 values, **little-endian**: binary32 for -5 and binary64 for -6. The
element count is the payload size divided by 4 (or 8). An empty vector has a 0-byte
payload (`c7 00 fb`).

The byte order is an intentional exception to §3. Vectors exist for embeddings and
numeric payloads, and little-endian makes them a single memory copy or a zero-copy view
in numpy, `Float32Array`/`Float64Array`, Go, and C++ on every mainstream CPU.

Encoders MUST NOT canonicalize NaN payload bits inside vectors. Vectors are opaque
numeric buffers.

> float32 vector `[1.0, 2.0]` → `d7 fb 0000803f 00000040`

### 5.6 Tabular (-10)

An array of N maps that all have the same C keys is written column by column. Each key
is written once, followed by that column's N values.

#### 5.6.1 Layout

All integers below are `u32` big-endian.

```
ext header (ext 8/16/32, type 0xf6), then payload:

  version        u32   = 1
  num_columns    u32   = C   (C ≥ 1 when N ≥ 1)
  num_rows       u32   = N
  C × { name_len u32, name: name_len bytes of UTF-8 }    -- column names, in order
  C × { type_code u8 }                                     -- informational, see below
  C × { col_len u32, N canonical BTOON values }            -- column data, row order
```

Row *r* of the decoded array is the map `{ name_i: value(column i, row r) }`.

Each column holds exactly N complete, self-describing BTOON values, so cells may be any
value, including nested arrays, maps, or tabular arrays. `col_len` is the total byte
length of those N values.

`type_code` describes the logical type of the column's value in the **first row**:

| code | type | code | type |
|---|---|---|---|
| 0 | nil | 5 | string |
| 1 | boolean | 6 | binary |
| 2 | integer in [-2^63, 2^63-1] | 7 | array |
| 3 | integer in [2^63, 2^64-1] | 8 | map |
| 4 | float | 9 | anything else (extension types, bigint, timestamp, …) |

Encoders read the code off the first cell's canonical encoding: an extension is 9,
except the tabular extension, which is an array (7). A `uint 64` (`cf`) is 3 only when
its value exceeds 2^63 - 1. A canonical encoder never writes an extension for a value
that has a native form (§5.4), so the encoding always shows the logical type.

Decoders MUST NOT rely on `type_code`, because values carry their own types. Encoders
MUST write it as defined above so that output is canonical.

#### 5.6.2 When encoders use tabular

An array qualifies when all of the following hold:

- it has N ≥ 2 elements and every element is a map;
- the first map has C ≥ 1 keys;
- every other map has exactly the same set of keys;
- tabular encoding is enabled (the default).

A qualifying array MUST be encoded as tabular if and only if the following holds, and
as a plain array otherwise:

```
N × (maphdr(C) + Σ_k (strhdr(len(k)) + len(k)))  >  18 + Σ_k (9 + len(k))
```

Here `len(k)` is the UTF-8 byte length of key k. `maphdr(C)` is 1 if C ≤ 15, 3 if
C ≤ 65535, else 5. `strhdr(L)` is 1 if L ≤ 31, 2 if L ≤ 255, 3 if L ≤ 65535, else 5.
The left side is what plain MessagePack spends repeating keys. The right side is the
tabular overhead, using a worst-case 6-byte ext header. In practice, records with a few
short keys switch to tabular at around 3 to 8 rows.

Columns are emitted in canonical key order (§4.4). Cell values are canonical encodings,
and tabular encoding applies recursively to nested arrays.

#### 5.6.3 Decoder requirements

Decoders MUST reject the payload with `invalid_tabular` if any of the following hold:

- `version ≠ 1`, or any field runs past the end of the extension payload;
- `num_columns = 0` and `num_rows > 0`;
- two column names are equal. This holds in non-strict mode too: unlike duplicate map
  keys, it is a structural error;
- a column's values don't consume exactly `col_len` bytes, or a column holds fewer
  than N values;
- the payload has bytes left over after the last column.

A column name that isn't valid UTF-8 is rejected in strict mode with `invalid_utf8`, the
same as any other string.

Before allocating N rows, decoders MUST check that the remaining payload could hold N
values per column. Every value is at least 1 byte, so the check is `N × C ≤ remaining
bytes`.

### 5.7 Other extension types

Decoders MUST return any extension with an id not listed above as a generic
`(type, data)` pair without interpreting it. Encoders write generic extensions with
the smallest header (§4.3).

An encoder that accepts raw `(type, data)` extensions from callers SHOULD reject ids
that this section defines (-1 to -6 and -10), or validate and normalize them. Otherwise
a payload such as a bigint that fits in 64 bits makes the output non-canonical.

---

## 6. Compression envelope

A BTOON document MAY be wrapped in a compression envelope:

```
offset size field
0      4    magic              "BTON" = 42 54 4f 4e
4      1    version            = 1
5      1    algorithm          see table
6      2    reserved           = 0
8      4    compressed_size    u32 BE, byte length of the payload
12     4    uncompressed_size  u32 BE, byte length after decompression
16     …    payload
```

| Id | Algorithm | Payload format | Support |
|---|---|---|---|
| 0 | zlib | RFC 1950 zlib stream (deflate with zlib header and Adler-32) | MUST |
| 1 | LZ4 | LZ4 **block** format (no frame; size from `uncompressed_size`) | MAY |
| 2 | Zstandard | Zstd frame (RFC 8878) | MAY |
| 3 | Brotli | Brotli stream (RFC 7932) | MAY |
| 4 | Snappy | Snappy raw (block) format | MAY |

**Encoders**

- When compression is requested without naming an algorithm, they MUST use zlib.
- An algorithm the encoder doesn't support is an error (`unsupported_compression`),
  whatever the input size.
- They SHOULD skip compression for inputs under 256 bytes. They SHOULD emit the
  envelope only when it saves at least 5%, that is, when `20 × envelope_size ≤ 19 ×
  input_size`, where the envelope includes its 16-byte header. Otherwise they emit the
  uncompressed document.
- Compressed bytes are not canonical. Different zlib builds produce different, equally
  valid streams.

**Decoders**

- Recognize the envelope when the input is at least 16 bytes long and starts with the
  magic. This is unambiguous, because a bare BTOON document starting with `42` is the
  integer 66 and can't have trailing bytes (§7).
- Check the header in this order and report the first failure:
  1. `version ≠ 1` → `invalid_envelope`
  2. `reserved ≠ 0` → `invalid_envelope`
  3. `compressed_size ≠ input length - 16` → `invalid_envelope`
  4. `uncompressed_size` above their limit (default 256 MiB) → `size_limit`. This check
     comes **before** decompressing.
  5. an unknown or unsupported algorithm → `unsupported_compression`
- Stop decompressing at `uncompressed_size`. If the payload produces more output or less
  output, the decoder MUST raise `invalid_envelope`.
- The compressed stream MUST end exactly at the end of the payload. Bytes after it, such
  as a second zlib stream or padding, are `invalid_envelope`.
- MUST NOT fall back to interpreting envelope bytes as a bare document.
- SHOULD bound decoder memory for codecs with large windows: reject Zstd frames whose
  window exceeds max(8 MiB, min(`uncompressed_size`, 128 MiB)) with `invalid_envelope`.
  A decoder that can only set a power-of-two window limit MAY round the bound up to the
  next power of two.

Envelopes don't nest. The payload of a compression envelope is a bare BTOON document.

---

## 7. Decoding rules

A conforming decoder MUST accept every valid encoding of a value, canonical or not
(for example an int16 holding `5`, float32, or str32 for a short string). It MUST
produce the same logical value regardless of which encoding was used.

In **strict mode**, which MUST be the default, a decoder MUST reject:

| Condition | Error code |
|---|---|
| Input ends before a value is complete, or a length runs past the input | `truncated` |
| Marker `0xc1` | `invalid_marker` |
| Bytes remain after the top-level value | `trailing_data` |
| A string, map key, or column name is not well-formed UTF-8 | `invalid_utf8` |
| A map key is not a string | `non_string_key` |
| A map contains the same key twice | `duplicate_key` |
| Nesting deeper than `max_depth` (default **128**) | `max_depth` |
| A malformed BTOON extension payload (§5) | `invalid_extension` |
| A malformed tabular payload (§5.6.3) | `invalid_tabular` |
| A malformed envelope (§6, §8) | `invalid_envelope` |
| A size above a configured limit | `size_limit` |
| An unsupported compression algorithm | `unsupported_compression` |
| HMAC verification failed or a key is required (§8) | `authentication_failed` |

**Depth.** The top-level value is at depth 0. Each array or map increases the depth of
its children by one. A tabular array increases it by two, one for the array and one for
the row maps, even when it has no rows. `max_depth = 128` therefore allows 128 nested containers and rejects the
129th. Encoders MUST apply the same limit and raise `max_depth`, which turns cyclic
object graphs into errors.

**Allocation.** Decoders MUST NOT pre-allocate more than the remaining input could
encode. An array or map announcing N elements needs at least N (or 2N) more bytes, and
decoders MUST check that before reserving space.

**Non-strict mode** MAY allow trailing data and duplicate map keys (last wins) and MAY skip
UTF-8 validation. It MUST NOT relax bounds, depth, or allocation checks, or any other
structural rule, including the ban on duplicate tabular column names.

**Precedence.** Decoders parse front to back and report the first error they encounter.
For example, a map key whose marker is `c1` is `non_string_key`, because the key is
read as a key before its marker is examined as a value. A duplicate key is detected
only once its entry is complete, that is, after the value has been decoded. So in
`82 a1 61 01 a1 61 c1` the error is `invalid_marker` at the value, not `duplicate_key`. For envelopes, the header order
in §6 applies. The conformance vectors avoid cases where reasonable implementations
could disagree.

Implementations SHOULD expose the error code as a machine-readable field on their error
type.

---

## 8. Signed envelope (HMAC)

A document MAY be authenticated with HMAC-SHA256 (RFC 2104 / FIPS 180-4):

```
offset size field
0      4    magic        "BTSG" = 42 54 53 47
4      1    version      = 1
5      1    algorithm    = 1 (HMAC-SHA256)
6      2    reserved     = 0
8      32   mac          HMAC-SHA256(key, bytes[0..8) ‖ payload)
40     …    payload      a bare BTOON document or a compression envelope
```

The MAC covers the first 8 header bytes and the payload, which prevents algorithm or
version confusion. Layering is always `sign(compress(encode(value)))`.

Decoders configured with a key MUST require the envelope and verify the MAC in constant
time **before** parsing the payload:

- input that doesn't start with `"BTSG"` → `authentication_failed`;
- `"BTSG"` followed by fewer than 36 more bytes, or an unsupported version or algorithm
  byte, or non-zero reserved bytes → `invalid_envelope`;
- a mismatched MAC → `authentication_failed`.

Decoders without a key MUST treat input that starts with `"BTSG"` and is at least 40
bytes long as a signed envelope, and reject it with `authentication_failed` rather than
ignore the signature. Shorter inputs are parsed as bare documents, which fail because
`42` is the integer 66 followed by trailing data. An *empty* key is a configuration
error, not "no key": APIs SHOULD reject it with `invalid_argument`, or make "no key"
expressible only as an absent value. The key is caller-supplied and SHOULD be at least
32 random bytes.

---

## 9. Security considerations

- **Hostile input.** §7 is designed so that a decoder's memory and time are bounded by
  input size × a small constant, plus `max_depth` stack frames.
- **Decompression bombs.** Zlib can expand about 1000×, and Zstd and Brotli far more.
  The `uncompressed_size` limit (§6) is the defense, and it must be enforced while
  decompressing, not only by trusting the header.
- **Parser differentials.** Strict mode rejects duplicate keys and invalid UTF-8, so
  two conforming decoders can't disagree about what a document means.
- **Canonical form and MACs.** Canonical encoding makes "sign the encoding of this
  value" reproducible across languages. Verifiers should still verify the bytes they
  received, not a re-encoding.

---

## 10. Conformance vectors

`tests/vectors/btoon-vectors.json` lists test cases:

```json
{ "id": "int/uint8-min", "hex": "cc80", "value": {"int": "128"}, "canonical": true }
{ "id": "invalid/trailing", "hex": "0102", "error": "trailing_data" }
```

- `value` uses a typed JSON form: `{"nil": null}`, `{"bool": b}`,
  `{"int": "<decimal>"}` (any size, including bigint), `{"float": "<decimal> | NaN |
  Infinity | -Infinity"}`, `{"string": s}`, `{"binary": "<hex>"}`, `{"array": [v…]}`,
  `{"map": {"key": v, …}}`, `{"timestamp": {"seconds": "<decimal>", "nanoseconds": n,
  "offset_minutes": m?}}`, `{"date": "<ms>"}`, `{"datetime": "<ns>"}`,
  `{"float32_vector": ["<decimal>", …]}`, `{"float64_vector": […]}`,
  `{"ext": {"type": t, "data": "<hex>"}}`.
- `options` holds decoder options for that vector. Today the only one is `hmac_key`,
  the key as hex. Vectors without `options` use the file's `defaults` (strict mode,
  `max_depth` 128, `max_decompressed_size` 256 MiB). `note` is a comment for humans.
- **Every** implementation MUST decode every non-error vector to `value`.
- Vectors with `"canonical": true` MUST also be re-encoded byte-for-byte from `value`.
- Error vectors MUST be rejected. Matching the `error` code is RECOMMENDED.
- Vectors with `"requires": "<feature>"` (for example `"zstd"`) MAY be skipped by
  implementations without that feature.

---

## 11. Changes from 0.0.x

Version 0.0.x had two contradictory spec documents, and an encoder that didn't match
its decoder. 0.1 is the first version where the spec, the reference implementation,
and the vectors agree.

- Extension ids are fixed: date -2, datetime -3, bigint -4, vectors -5/-6. The 0.0.x
  *encoder* wrote -1, -2, 0, -3, and -4, which its own decoder couldn't read back.
- Timestamps follow MessagePack. The 0.0.x 12-byte layout (`i64 sec, u32 nsec`) is no
  longer produced.
- Integers are canonical. Non-negative values use the uint family.
- Tabular encoding is used only when it is smaller (§5.6.2). The byte layout is
  unchanged, so 0.0.x tabular data still decodes.
- The compression envelope layout is unchanged. `compress=true` now defaults to zlib.
- The signed envelope (§8) is new. The 0.0.x `[u8 len][mac]` prefix was never
  verifiable and is not supported.

#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2025 Alvar Laigna <https://alvarlaigna.com>
# SPDX-License-Identifier: MIT
"""
Generates tests/vectors/btoon-vectors.json — the BTOON conformance vectors.

This script is an *independent* implementation of docs/SPEC.md: the MessagePack
layer comes from msgpack-python, and the BTOON extensions are written here from
the spec, so the C++ reference implementation is checked against something
other than itself.

    uv run --with msgpack --with zstandard --with lz4 --with brotli \
        python tests/vectors/generate.py            # rewrite the file
    ... python tests/vectors/generate.py --check    # CI: fail if the file is stale

The zstandard/lz4/brotli modules are optional; without them the corresponding
"requires" vectors are omitted.
"""
from __future__ import annotations

import hashlib
import hmac
import json
import math
import struct
import sys
import zlib
from pathlib import Path

import msgpack

SPEC_VERSION = "0.3"
HMAC_KEY = b"btoon-conformance-key-0123456789"  # 32 bytes


# --- BTOON value types that Python lacks -----------------------------------------
class TS:
    def __init__(self, seconds: int, nanoseconds: int = 0, offset: int | None = None):
        self.seconds, self.nanoseconds, self.offset = seconds, nanoseconds, offset


class Date:
    def __init__(self, ms: int):
        self.ms = ms


class DateTime:
    def __init__(self, ns: int):
        self.ns = ns


class F32Vec:
    def __init__(self, values):
        self.values = list(values)


class F64Vec:
    def __init__(self, values):
        self.values = list(values)


class Ext:
    def __init__(self, type_: int, data: bytes):
        self.type, self.data = type_, data


class Bits32:
    """A float32 vector element given by its exact bit pattern (NaN payloads, §5.5)."""
    def __init__(self, bits: int):
        self.bits = bits


class Bits64:
    """A float64 vector element given by its exact bit pattern (NaN payloads, §5.5)."""
    def __init__(self, bits: int):
        self.bits = bits


MAX_DEPTH = 128
FLT_MAX = struct.unpack("<f", struct.pack("<I", 0x7F7FFFFF))[0]
# A float64 at or above this rounds to infinity as binary32 (round to nearest, ties to even).
F32_OVERFLOW = FLT_MAX + 2.0 ** 103


def f32_bytes(x) -> bytes:
    """Little-endian binary32 for a vector element, as every library writes it: values
    beyond the binary32 range round to ±Infinity (struct.pack would raise OverflowError)."""
    if isinstance(x, Bits32):
        return struct.pack("<I", x.bits)
    if math.isfinite(x) and abs(x) > FLT_MAX:
        if abs(x) >= F32_OVERFLOW:
            return struct.pack("<f", math.copysign(math.inf, x))
        return struct.pack("<f", math.copysign(FLT_MAX, x))
    return struct.pack("<f", x)


def f64_bytes(x) -> bytes:
    if isinstance(x, Bits64):
        return struct.pack("<Q", x.bits)
    return struct.pack("<d", x)


INT_MIN, UINT_MAX = -(2**63), 2**64 - 1


# --- Canonical encoder (docs/SPEC.md §4–§5) ----------------------------------------
def ukey(k: str) -> bytes:
    return k.encode("utf-8")


def str_hdr(n: int) -> int:
    return 1 if n <= 31 else 2 if n <= 0xFF else 3 if n <= 0xFFFF else 5


def map_hdr(n: int) -> int:
    return 1 if n <= 15 else 3 if n <= 0xFFFF else 5


def ext(type_: int, payload: bytes) -> bytes:
    """Smallest ext header (§4.3). msgpack-python's ExtType only allows codes 0..127,
    so negative ids are framed here; positive ids are cross-checked against msgpack."""
    n, t = len(payload), struct.pack("b", type_)
    fixed = {1: 0xD4, 2: 0xD5, 4: 0xD6, 8: 0xD7, 16: 0xD8}
    if n in fixed:
        out = bytes([fixed[n]]) + t + payload
    elif n <= 0xFF:
        out = b"\xc7" + bytes([n]) + t + payload
    elif n <= 0xFFFF:
        out = b"\xc8" + struct.pack(">H", n) + t + payload
    else:
        out = b"\xc9" + struct.pack(">I", n) + t + payload
    if type_ >= 0:
        assert out == msgpack.packb(msgpack.ExtType(type_, payload)), "ext framing disagrees with msgpack"
    return out


def bigint_bytes(v: int) -> bytes:
    n = (v.bit_length() + 8) // 8  # minimal two's complement length incl. sign bit
    b = v.to_bytes(n, "big", signed=True)
    # strip redundant sign bytes (to_bytes with this length is already minimal or one over)
    while len(b) > 1 and ((b[0] == 0x00 and b[1] < 0x80) or (b[0] == 0xFF and b[1] >= 0x80)):
        b = b[1:]
    return b


def type_code(v) -> int:
    if v is None:
        return 0
    if isinstance(v, bool):
        return 1
    if isinstance(v, int):
        return 2 if INT_MIN <= v <= 2**63 - 1 else 3 if v <= UINT_MAX and v > 0 else 9
    if isinstance(v, float):
        return 4
    if isinstance(v, str):
        return 5
    if isinstance(v, bytes):
        return 6
    if isinstance(v, list):
        return 7
    if isinstance(v, dict):
        return 8
    return 9


def tabular_keys(arr: list):
    if len(arr) < 2 or not all(isinstance(r, dict) for r in arr):
        return None
    keys = sorted(arr[0].keys(), key=ukey)
    if not keys or any(set(r.keys()) != set(keys) for r in arr[1:]):
        return None
    per_row = map_hdr(len(keys)) + sum(str_hdr(len(ukey(k))) + len(ukey(k)) for k in keys)
    overhead = 18 + sum(9 + len(ukey(k)) for k in keys)
    return keys if len(arr) * per_row > overhead else None


def enc(v, tabular=True, depth=0) -> bytes:
    """Canonical encoding. Like the libraries, it refuses values the spec forbids: nesting
    deeper than max_depth (§7) and out-of-range timestamps (§5.1)."""
    if isinstance(v, (list, dict)) and depth >= MAX_DEPTH:
        raise ValueError(f"max_depth: nesting deeper than {MAX_DEPTH}")
    if v is None or isinstance(v, bool):
        return msgpack.packb(v)
    if isinstance(v, int):
        if INT_MIN <= v <= UINT_MAX:
            return msgpack.packb(v)
        return ext(-4, bigint_bytes(v))
    if isinstance(v, float):
        if math.isnan(v):
            return b"\xcb\x7f\xf8\x00\x00\x00\x00\x00\x00"
        return struct.pack(">Bd", 0xCB, v)
    if isinstance(v, str):
        return msgpack.packb(v)
    if isinstance(v, bytes):
        return msgpack.packb(v, use_bin_type=True)
    if isinstance(v, list):
        keys = tabular_keys(v) if tabular else None
        if keys:
            if depth + 1 >= MAX_DEPTH:
                raise ValueError(f"max_depth: a tabular array needs two levels (§7)")
            return enc_tabular(v, keys, tabular, depth)
        p = msgpack.Packer()
        return p.pack_array_header(len(v)) + b"".join(enc(x, tabular, depth + 1) for x in v)
    if isinstance(v, dict):
        p = msgpack.Packer()
        out = p.pack_map_header(len(v))
        for k in sorted(v.keys(), key=ukey):
            out += msgpack.packb(k) + enc(v[k], tabular, depth + 1)
        return out
    if isinstance(v, TS):
        if not 0 <= v.nanoseconds < 10**9:
            raise ValueError("timestamp nanoseconds out of range (§5.1)")
        if v.offset is not None and not -1080 <= v.offset <= 1080:
            raise ValueError("timestamp offset beyond ±1080 minutes (§5.1)")
        if v.offset is not None:
            return ext(-1, struct.pack(">Iqh", v.nanoseconds, v.seconds, v.offset))
        return msgpack.packb(msgpack.Timestamp(v.seconds, v.nanoseconds))
    if isinstance(v, Date):
        return ext(-2, struct.pack(">q", v.ms))
    if isinstance(v, DateTime):
        return ext(-3, struct.pack(">q", v.ns))
    if isinstance(v, F32Vec):
        return ext(-5, b"".join(f32_bytes(x) for x in v.values))
    if isinstance(v, F64Vec):
        return ext(-6, b"".join(f64_bytes(x) for x in v.values))
    if isinstance(v, Ext):
        return ext(v.type, v.data)
    raise TypeError(type(v))


_TABULAR_EMITTED = [0]


def enc_tabular(rows: list, keys: list, tabular=True, depth=0) -> bytes:
    _TABULAR_EMITTED[0] += 1
    payload = struct.pack(">III", 1, len(keys), len(rows))
    for k in keys:
        payload += struct.pack(">I", len(ukey(k))) + ukey(k)
    payload += bytes(type_code(rows[0][k]) for k in keys)
    for k in keys:
        col = b"".join(enc(r[k], tabular, depth + 2) for r in rows)
        payload += struct.pack(">I", len(col)) + col
    return ext(-10, payload)


def tabular_payload(names, codes, columns, nrows, version=1) -> bytes:
    """Hand-assembled tabular payload (for decode-only / invalid vectors)."""
    payload = struct.pack(">III", version, len(names), nrows)
    for n in names:
        b = n if isinstance(n, bytes) else ukey(n)
        payload += struct.pack(">I", len(b)) + b
    payload += bytes(codes)
    for col in columns:
        payload += struct.pack(">I", len(col)) + col
    return payload


# --- Typed JSON form (docs/SPEC.md §10) ----------------------------------------------
def fstr(x: float) -> str:
    if math.isnan(x):
        return "NaN"
    if math.isinf(x):
        return "Infinity" if x > 0 else "-Infinity"
    return repr(x)


def vec_elem(raw: bytes, int_fmt: str, canonical_nan: int, float_fmt: str) -> str:
    """A vector element in the §10 notation: "NaN" only for the canonical quiet NaN, and
    any other NaN as its bit pattern in hex."""
    bits = struct.unpack(int_fmt, raw)[0]
    x = struct.unpack(float_fmt, raw)[0]
    if math.isnan(x) and bits != canonical_nan:
        return "0x%0*x" % (len(raw) * 2, bits)
    return fstr(x)


def typed(v):
    if v is None:
        return {"nil": None}
    if isinstance(v, bool):
        return {"bool": v}
    if isinstance(v, int):
        return {"int": str(v)}
    if isinstance(v, float):
        return {"float": fstr(v)}
    if isinstance(v, str):
        return {"string": v}
    if isinstance(v, bytes):
        return {"binary": v.hex()}
    if isinstance(v, list):
        return {"array": [typed(x) for x in v]}
    if isinstance(v, dict):
        return {"map": {k: typed(x) for k, x in v.items()}}
    if isinstance(v, TS):
        t = {"seconds": str(v.seconds), "nanoseconds": v.nanoseconds}
        if v.offset is not None:
            t["offset_minutes"] = v.offset
        return {"timestamp": t}
    if isinstance(v, Date):
        return {"date": str(v.ms)}
    if isinstance(v, DateTime):
        return {"datetime": str(v.ns)}
    if isinstance(v, F32Vec):
        return {"float32_vector": [vec_elem(f32_bytes(x), "<I", 0x7FC00000, "<f") for x in v.values]}
    if isinstance(v, F64Vec):
        return {"float64_vector": [vec_elem(f64_bytes(x), "<Q", 0x7FF8000000000000, "<d") for x in v.values]}
    if isinstance(v, Ext):
        return {"ext": {"type": v.type, "data": v.data.hex()}}
    raise TypeError(type(v))


# --- Vector collection -----------------------------------------------------------------
VECTORS: list[dict] = []
_ids: set[str] = set()


def _add(entry: dict):
    assert entry["id"] not in _ids, entry["id"]
    _ids.add(entry["id"])
    VECTORS.append(entry)


def canonical(id_: str, value, expect_hex: str | None = None, note: str | None = None, tabular: int | None = None):
    """Value whose canonical encoding every encoder must reproduce byte-for-byte.

    `tabular` (if given) asserts how many tabular sections the encoding contains, so a
    vector meant to exercise the columnar form can't silently fall back to plain arrays."""
    _TABULAR_EMITTED[0] = 0
    h = enc(value).hex()
    if tabular is not None:
        assert _TABULAR_EMITTED[0] == tabular, f"{id_}: expected {tabular} tabular sections, got {_TABULAR_EMITTED[0]}"
    if expect_hex is not None:
        exp = expect_hex.replace(" ", "")
        assert h == exp, f"{id_}: generator produced {h}, spec example says {exp}"
    e = {"id": id_, "hex": h, "value": typed(value), "canonical": True}
    if note:
        e["note"] = note
    _add(e)


def decode_only(id_: str, hex_: str, value, note: str | None = None, **extra):
    """Valid but non-canonical bytes: decoders must accept them and produce `value`."""
    e = {"id": id_, "hex": hex_.replace(" ", ""), "value": typed(value), "canonical": False}
    if note:
        e["note"] = note
    e.update(extra)
    _add(e)


def invalid(id_: str, hex_: str, error: str, note: str | None = None, **extra):
    """Bytes that decoders (strict mode, default options) must reject."""
    e = {"id": id_, "hex": hex_.replace(" ", ""), "error": error}
    if note:
        e["note"] = note
    e.update(extra)
    _add(e)


def encode_error(id_: str, value, error: str, note: str | None = None):
    """A value that encoders must refuse (§10)."""
    e = {"id": id_, "value": typed(value), "encode_error": error}
    if note:
        e["note"] = note
    _add(e)


def build():
    # nil / bool
    canonical("nil", None, "c0")
    canonical("bool/false", False, "c2")
    canonical("bool/true", True, "c3")

    # integers: every boundary of every format
    ints = {
        "0": (0, "00"), "1": (1, "01"), "127": (127, "7f"), "128": (128, "cc80"),
        "255": (255, "ccff"), "256": (256, "cd0100"), "65535": (65535, "cdffff"),
        "65536": (65536, "ce00010000"), "4294967295": (2**32 - 1, "ceffffffff"),
        "4294967296": (2**32, "cf0000000100000000"),
        "int64-max": (2**63 - 1, "cf7fffffffffffffff"), "2^63": (2**63, "cf8000000000000000"),
        "uint64-max": (2**64 - 1, "cfffffffffffffffff"),
        "-1": (-1, "ff"), "-32": (-32, "e0"), "-33": (-33, "d0df"), "-128": (-128, "d080"),
        "-129": (-129, "d1ff7f"), "-32768": (-32768, "d18000"), "-32769": (-32769, "d2ffff7fff"),
        "-2^31": (-(2**31), "d280000000"), "-2^31-1": (-(2**31) - 1, "d3ffffffff7fffffff"),
        "int64-min": (-(2**63), "d38000000000000000"),
    }
    for name, (v, h) in ints.items():
        canonical(f"int/{name}", v, h)

    # bigint (§5.4)
    canonical("bigint/2^64", 2**64, "c709fc010000000000000000")
    canonical("bigint/-2^63-1", -(2**63) - 1, "c709fcff7fffffffffffffff")
    canonical("bigint/2^100", 2**100)
    canonical("bigint/-2^100", -(2**100))
    canonical("bigint/10^30", 10**30)

    # floats (always float64; NaN canonicalized)
    for name, v in {"0": 0.0, "-0": -0.0, "1.5": 1.5, "-2.5": -2.5, "pi": math.pi, "1e300": 1e300,
                    "min-subnormal": 5e-324, "inf": math.inf, "-inf": -math.inf, "nan": math.nan}.items():
        canonical(f"float/{name}", v)
    canonical("float/1.5-hex", 1.5, "cb3ff8000000000000")

    # strings
    canonical("str/empty", "", "a0")
    canonical("str/a", "a", "a161")
    canonical("str/31", "x" * 31)
    canonical("str/32-str8", "x" * 32)
    canonical("str/255", "x" * 255)
    canonical("str/256-str16", "x" * 256)
    canonical("str/utf8-2byte", "é", "a2c3a9")
    canonical("str/utf8-3byte", "日本語")
    canonical("str/utf8-4byte", "🎉", "a4f09f8e89")
    canonical("str/mixed", "Grüße, 世界! 🚀")

    # binary
    canonical("bin/empty", b"", "c400")
    canonical("bin/1", b"\x01", "c40101")
    canonical("bin/255", bytes(range(255)))
    canonical("bin/256-bin16", bytes(256))

    # arrays
    canonical("array/empty", [], "90")
    canonical("array/one", [1], "9101")
    canonical("array/15", list(range(15)))
    canonical("array/16-array16", list(range(16)))
    canonical("array/nested", [[1, [2, [3]]], []], "9292019202910390")
    canonical("array/mixed", [None, True, -1, 1.5, "s", b"\x00"])
    canonical("array/spec-example", [1, 2, 3, 100, 1000], "950102036 4cd03e8".replace(" ", ""))

    # maps: canonical key order is bytewise UTF-8
    canonical("map/empty", {}, "80")
    canonical("map/one", {"a": 1}, "81a16101")
    canonical("map/spec-example", {"name": "Alice", "age": 30, "active": True},
              "83a6616374697665c3a36167651ea46e616d65a5416c696365")
    canonical("map/sorted-prefix", {"b": 1, "aa": 2, "a": 3}, "83a16103a2616102a16201")
    canonical("map/sorted-utf8", {"é": 1, "z": 2, "Z": 3}, "83a15a03a17a02a2c3a901")
    canonical("map/15", {f"k{i:02d}": i for i in range(15)})
    canonical("map/16-map16", {f"k{i:02d}": i for i in range(16)})
    canonical("map/nested", {"outer": {"inner": {"deep": [1, {"x": None}]}}})

    # timestamps (§5.1)
    canonical("timestamp/32", TS(1704067200), "d6ff65920080")
    canonical("timestamp/64", TS(1704067200, 500000000))
    canonical("timestamp/64-max-nanos", TS(2**34 - 1, 999999999))
    canonical("timestamp/96-negative", TS(-1), "c70cff00000000ffffffffffffffff")
    canonical("timestamp/96-large", TS(2**34, 1))
    canonical("timestamp/96-negative-nanos", TS(-62135596800, 123456789), note="0001-01-01T00:00:00.123456789Z")
    canonical("timestamp/epoch", TS(0), "d6ff00000000")
    canonical("timestamp/offset+120", TS(1704067200, 0, 120))
    canonical("timestamp/offset-330", TS(1704067200, 999, -330))

    # date / datetime
    canonical("date/epoch", Date(0), "d7fe0000000000000000")
    canonical("date/2023", Date(1700000000123))
    canonical("date/negative", Date(-1))
    canonical("datetime/2023", DateTime(1700000000123456789))
    canonical("datetime/negative", DateTime(-1))

    # vectors (little-endian, §5.5)
    canonical("vector-f32/two", F32Vec([1.0, 2.0]), "d7fb0000803f00000040")
    canonical("vector-f32/empty", F32Vec([]), "c700fb")
    canonical("vector-f32/values", F32Vec([-0.5, 3.25, 1024.0, -0.0]))
    canonical("vector-f64/one", F64Vec([1.0]), "d7fa000000000000f03f")
    canonical("vector-f64/values", F64Vec([0.1, -1e300, 2.5]))

    # generic extensions
    canonical("ext/fixext1", Ext(1, b"\xaa"), "d401aa")
    canonical("ext/ext8-3", Ext(42, b"\x01\x02\x03"), "c7032a010203")
    canonical("ext/fixext16", Ext(127, bytes(range(16))))
    canonical("ext/empty", Ext(0, b""), "c70000")
    canonical("ext/reserved-negative", Ext(-100, b"\x01\x02"), note="reserved ids decode as generic extensions")

    # tabular (§5.6)
    people = [{"id": i, "name": f"user{i}", "active": i % 2 == 0} for i in range(8)]
    canonical("tabular/people", people, tabular=1)
    canonical("tabular/two-rows-plain", [{"id": 1, "name": "a"}, {"id": 2, "name": "b"}],
              "9282a2696401a46e616d65a16182a2696402a46e616d65a162",
              note="qualifies but the size rule picks a plain array", tabular=0)
    # One 2-byte key: per-row cost 4, tabular overhead 29 -> 7 rows (28) plain, 8 rows (32) tabular.
    canonical("tabular/size-rule-7-plain", [{"ab": 1}] * 7, "97" + "81a2616201" * 7, tabular=0)
    canonical("tabular/size-rule-8-tabular", [{"ab": 1}] * 8, tabular=1)
    canonical("tabular/nonuniform-plain", [{"a": 1, "b": 2}, {"a": 1}] * 5, tabular=0)
    canonical("tabular/nested-cells", [{"k": [i, {"x": i}], "v": None if i % 2 else "s"} for i in range(8)],
              tabular=1)
    canonical("tabular/nested-tabular",
              [{"group": g, "rows": [{"n": i, "sq": i * i} for i in range(7)]} for g in range(4)],
              note="an outer table whose 'rows' cells are tables themselves", tabular=5)
    canonical("tabular/in-map", {"data": [{"x": i, "y": -i} for i in range(10)]}, tabular=1)
    canonical("tabular/utf8-columns", [{"é": i, "z": i, "Z": i} for i in range(7)],
              note="columns in UTF-8 byte order: Z, z, é", tabular=1)
    canonical("tabular/type-codes", [{"n": None, "b": True, "i": -5, "u": 2**63, "f": 1.5, "s": "x",
                                      "bin": b"\x01", "arr": [1], "m": {"a": 1}, "ts": TS(0)}] * 5,
              note="one column per type code 0-9", tabular=1)
    canonical("tabular/cell-tabular-type-code",
              [{"t": [{"q": j} for j in range(10)]} for _ in range(10)],
              note="a cell holding a tabular array has type code 7 (array); 10 rows of one 1-byte key "
                   "is the smallest tabular size", tabular=11)

    # nesting depth: 128 levels allowed (§7)
    _add({"id": "depth/128-ok", "hex": "91" * 128 + "c0", "value": _nested(128), "canonical": True})
    invalid("depth/129", "91" * 129 + "c0", "max_depth")
    invalid("depth/129-maps", "81a161" * 129 + "c0", "max_depth")

    # --- decode-only: valid, non-canonical encodings -----------------------------
    decode_only("noncanonical/int16-5", "d10005", 5)
    decode_only("noncanonical/int64-5", "d30000000000000005", 5)
    decode_only("noncanonical/uint64-1", "cf0000000000000001", 1)
    decode_only("noncanonical/uint8-5", "cc05", 5)
    decode_only("noncanonical/int8-positive", "d07f", 127)
    decode_only("noncanonical/float32", "ca3fc00000", 1.5)
    decode_only("noncanonical/float32-nan", "ca7fc00000", math.nan)
    decode_only("noncanonical/str8-short", "d90161", "a")
    decode_only("noncanonical/str32-short", "db0000000161", "a")
    decode_only("noncanonical/bin32", "c60000000101", b"\x01")
    decode_only("noncanonical/array32", "dd0000000101", [1])
    decode_only("noncanonical/map16", "de0001a16101", {"a": 1})
    decode_only("noncanonical/unsorted-map", "82a16201a16102", {"b": 1, "a": 2})
    decode_only("noncanonical/ext8-for-fixext4", "c704050102030 4".replace(" ", ""), Ext(5, b"\x01\x02\x03\x04"))
    decode_only("noncanonical/timestamp96-small", "c70cff000000000000000065920080", TS(1704067200))
    decode_only("noncanonical/timestamp64-no-nanos", "d7ff0000000065920080", TS(1704067200))
    decode_only("noncanonical/bigint-small", "c701fc05", 5)
    decode_only("noncanonical/bigint-nonminimal", "c703fc000005", 5)
    decode_only("noncanonical/bigint-negative-nonminimal", "c703fcffff80", -128)
    decode_only("noncanonical/nan-payload", "cb7ff0000000000001", math.nan)
    rows2 = [{"id": 1, "name": "a"}, {"id": 2, "name": "b"}]
    tp = tabular_payload(["id", "name"], [2, 5], [enc(1) + enc(2), enc("a") + enc("b")], 2)
    decode_only("noncanonical/tabular-small", ext(-10, tp).hex(), rows2,
                note="tabular although the size rule prefers plain; 0.0.x encoders did this")
    tp = tabular_payload(["name", "id"], [0, 0], [enc("a") + enc("b"), enc(1) + enc(2)], 2)
    decode_only("noncanonical/tabular-unsorted-columns", ext(-10, tp).hex(), rows2,
                note="columns out of order and 0.0.x-style type codes")
    tp = tabular_payload([], [], [], 0)
    decode_only("noncanonical/tabular-empty", ext(-10, tp).hex(), [])
    tp = tabular_payload(["a"], [0], [enc(1) + enc(2)], 2)
    decode_only("noncanonical/tabular-ext32-header", (b"\xc9" + struct.pack(">I", len(tp)) + b"\xf6" + tp).hex(),
                [{"a": 1}, {"a": 2}])

    # --- invalid: strict decoders must reject ---------------------------------------
    for id_, h in {"empty": "", "fixmap-no-key": "81", "fixstr-short": "a561", "array16-header": "dc00",
                   "float64": "cb0000", "array-missing-element": "9201", "bin8-short": "c40501",
                   "fixext-short": "d7ff00", "map-missing-value": "81a161", "ext8-type-missing": "c701",
                   "str32-header": "db0000", "uint64": "cf00"}.items():
        invalid(f"truncated/{id_}", h, "truncated")
    invalid("truncated/array32-huge-claim", "ddffffffff", "truncated")
    invalid("truncated/map32-huge-claim", "dfffffffff", "truncated")
    invalid("truncated/str32-huge-claim", "dbffffffff61", "truncated")
    invalid("truncated/bin32-huge-claim", "c6ffffffff", "truncated")
    invalid("marker/c1", "c1", "invalid_marker")
    invalid("marker/c1-nested", "91c1", "invalid_marker")
    invalid("trailing/two-ints", "0102", "trailing_data")
    invalid("trailing/after-map", "80c0", "trailing_data")
    invalid("utf8/lone-continuation", "a1ff", "invalid_utf8")
    invalid("utf8/bad-continuation", "a2c328", "invalid_utf8")
    invalid("utf8/overlong-2", "a2c080", "invalid_utf8")
    invalid("utf8/overlong-3", "a3e08080", "invalid_utf8")
    invalid("utf8/surrogate", "a3eda080", "invalid_utf8")
    invalid("utf8/above-max", "a4f4908080", "invalid_utf8")
    invalid("utf8/truncated-sequence", "a2e282", "invalid_utf8")
    invalid("utf8/key", "81a1ff01", "invalid_utf8")
    invalid("key/int", "810101", "non_string_key")
    invalid("key/nil", "81c001", "non_string_key")
    invalid("key/binary", "81c4016101", "non_string_key")
    invalid("key/duplicate", "82a16101a16102", "duplicate_key")
    invalid("ext/timestamp-len3", "c703ff000000", "invalid_extension")
    invalid("ext/timestamp64-nanos", "d7fffffffffc00000000", "invalid_extension")
    invalid("ext/timestamp96-nanos", "c70cff3b9aca000000000000000000", "invalid_extension")
    invalid("ext/timestamp-offset-range", "c70eff0000000000000000000000000439", "invalid_extension")
    invalid("ext/date-len4", "d6fe00000000", "invalid_extension")
    invalid("ext/datetime-len4", "d6fd00000000", "invalid_extension")
    invalid("ext/bigint-empty", "c700fc", "invalid_extension")
    invalid("ext/vector-f32-len3", "c703fb000000", "invalid_extension")
    invalid("ext/vector-f64-len4", "d6fa00000000", "invalid_extension")
    good_cols = [enc(1) + enc(2)]
    invalid("tabular/version-2", ext(-10, tabular_payload(["a"], [2], good_cols, 2, version=2)).hex(), "invalid_tabular")
    invalid("tabular/rows-without-columns", ext(-10, tabular_payload([], [], [], 5)).hex(), "invalid_tabular")
    invalid("tabular/duplicate-columns",
            ext(-10, tabular_payload(["a", "a"], [2, 2], [enc(1) + enc(2)] * 2, 2)).hex(), "invalid_tabular")
    invalid("tabular/column-too-short", ext(-10, tabular_payload(["a"], [2], [enc(1)], 2)).hex(), "invalid_tabular")
    invalid("tabular/column-too-long", ext(-10, tabular_payload(["a"], [2], [enc(1) + enc(2) + enc(3)], 2)).hex(),
            "invalid_tabular")
    invalid("tabular/trailing-payload", ext(-10, tabular_payload(["a"], [2], good_cols, 2) + b"\x00").hex(),
            "invalid_tabular")
    invalid("tabular/truncated-header", ext(-10, struct.pack(">II", 1, 1)).hex(), "invalid_tabular")
    invalid("tabular/huge-row-count", ext(-10, tabular_payload(["a"], [2], good_cols, 0xFFFFFFFF)).hex(),
            "invalid_tabular")
    invalid("tabular/name-past-payload", ext(-10, struct.pack(">IIII", 1, 1, 1, 100) + b"ab").hex(),
            "invalid_tabular")
    invalid("tabular/invalid-utf8-column",
            ext(-10, tabular_payload([b"\xff"], [2], good_cols, 2)).hex(), "invalid_utf8")

    # --- boundary cases (first found by the PHP port's reference suite) ---------------
    canonical("tabular/type-code-9-variants",
              [{"big": 2**64, "neg": -(2**63) - 1, "d": Date(1), "dt": DateTime(1), "v32": F32Vec([1.0]),
                "v64": F64Vec([2.0]), "e": Ext(7, b"\x01"), "tso": TS(1, 2, 60)}] * 6, tabular=1,
              note="every column is type code 9")
    canonical("tabular/type-code-from-first-row",
              [{"x": None if i == 0 else i, "y": "s" if i == 0 else 1.5} for i in range(10)], tabular=1,
              note="codes follow row 0 (nil, string) even though later rows differ")
    canonical("tabular/type-code-int64-edges", [{"lo": -(2**63), "hi": 2**63 - 1, "u": 2**64 - 1}] * 6, tabular=1,
              note="codes 2, 2, 3")
    canonical("tabular/key-31", [{"k" * 31: i} for i in range(2)], tabular=1)
    canonical("tabular/key-32", [{"k" * 32: i} for i in range(2)], tabular=1)
    canonical("tabular/16-columns", [{f"c{c:02d}": r for c in range(16)} for r in range(4)], tabular=1,
              note="row maps would need a map16 header")
    canonical("tabular/ext16-header", [{"id": i, "name": f"user{i}"} for i in range(40)], tabular=1)
    canonical("tabular/empty-container-cells", [{"a": {}, "b": []} for _ in range(12)], tabular=1)
    canonical("map/utf8-order-non-bmp", {"\U0001F600": 1, "\uffff": 2, "": 3, "z": 4},
              note="U+FFFF (ef bf bf) sorts before U+1F600 (f0 9f 98 80)")
    canonical("map/integer-like-keys", {"10": 1, "9": 2, "a": 3, "-1": 4, "01": 5, "": 6, "1": 7},
              note="keys are strings, sorted by bytes, never numerically")
    canonical("map/key-31-32", {"k" * 31: 1, "k" * 32: 2}, note="fixstr and str8 keys")
    canonical("map/nested-empty", {"a": {}, "b": [], "c": [{}], "d": {"e": {}}})
    for name, (sec, ns) in {"u32-max": (2**32 - 1, 0), "2^32": (2**32, 0), "2^34-1": (2**34 - 1, 0),
                            "2^34": (2**34, 0), "int64-min": (-(2**63), 0), "int64-max": (2**63 - 1, 999999999),
                            "1ns": (0, 1)}.items():
        canonical(f"timestamp/{name}", TS(sec, ns))
    canonical("timestamp/offset+1080", TS(-5, 7, 1080))
    canonical("timestamp/offset-1080", TS(5, 0, -1080))
    canonical("timestamp/offset-0", TS(0, 0, 0), note="a zero offset is still an offset: 14-byte form")
    for n in (0, 1, 2, 3, 4, 5, 8, 15, 16, 17, 255, 256):
        canonical(f"ext/length-{n}", Ext(9, bytes(range(256))[:n]))
    canonical("ext/type-minus-128", Ext(-128, b"\x00"))
    canonical("ext/type-minus-7", Ext(-7, b""), note="the first id below the BTOON range")
    for name, v in {"2^53": 2.0**53, "-2^53": -(2.0**53), "subnormal": 1e-310, "max": 1.7976931348623157e308,
                    "-1e-300": -1e-300, "0.1": 0.1}.items():
        canonical(f"float/{name}", v)
    for name, v in {"-2^64": -(2**64), "2^64+1": 2**64 + 1, "-2^64-1": -(2**64) - 1, "2^127": 2**127,
                    "-2^127": -(2**127), "2^1000": 2**1000, "-10^50+1": -(10**50) + 1, "2^71-1": 2**71 - 1}.items():
        canonical(f"bigint/{name}", v)

    # --- compression envelope (§6) ------------------------------------------------
    doc = {"rows": [{"id": i, "name": f"user{i}", "email": f"user{i}@example.com"} for i in range(20)]}
    raw = enc(doc)
    decode_only("compression/zlib", envelope(0, zlib.compress(raw, 6), len(raw)).hex(), doc)
    decode_only("compression/zlib-level9", envelope(0, zlib.compress(raw, 9), len(raw)).hex(), doc)
    tiny = enc("hello")
    decode_only("compression/zlib-tiny", envelope(0, zlib.compress(tiny), len(tiny)).hex(), "hello",
                note="encoders wouldn't compress this, but decoders must accept it")
    try:
        import zstandard

        decode_only("compression/zstd", envelope(2, zstandard.ZstdCompressor(level=3).compress(raw), len(raw)).hex(),
                    doc, requires="zstd")
    except ImportError:
        print("note: zstandard not installed; skipping zstd vectors", file=sys.stderr)
    try:
        import lz4.block

        decode_only("compression/lz4", envelope(1, lz4.block.compress(raw, store_size=False), len(raw)).hex(),
                    doc, requires="lz4")
    except ImportError:
        print("note: lz4 not installed; skipping lz4 vectors", file=sys.stderr)
    try:
        import brotli

        decode_only("compression/brotli", envelope(3, brotli.compress(raw), len(raw)).hex(), doc, requires="brotli")
    except ImportError:
        print("note: brotli not installed; skipping brotli vectors", file=sys.stderr)

    z = zlib.compress(raw)
    invalid("envelope/version-2", envelope(0, z, len(raw), version=2).hex(), "invalid_envelope")
    invalid("envelope/reserved", envelope(0, z, len(raw), reserved=1).hex(), "invalid_envelope")
    invalid("envelope/compressed-size-mismatch", envelope(0, z, len(raw), csize=len(z) + 1).hex(), "invalid_envelope")
    invalid("envelope/uncompressed-size-mismatch", envelope(0, z, len(raw) + 1).hex(), "invalid_envelope")
    invalid("envelope/corrupt-zlib", envelope(0, b"\x78\x9c" + bytes(10), len(raw)).hex(), "invalid_envelope")
    invalid("envelope/size-limit", envelope(0, z, 256 * 1024 * 1024 + 1).hex(), "size_limit",
            note="checked before decompressing")
    invalid("envelope/size-limit-default", envelope(0, z, 64 * 1024 * 1024 + 1).hex(), "size_limit",
            note="the default max_decompressed_size is 64 MiB (§7.1)")
    invalid("envelope/zlib-fdict", envelope(0, zlib_with_fdict(enc("hello")), len(enc("hello"))).hex(),
            "invalid_envelope", note="FDICT set, here with DICTID = adler32 of an empty dictionary (§6)")
    invalid("envelope/max-values", envelope(0, zlib.compress(enc([1, 2, 3])), len(enc([1, 2, 3]))).hex(),
            "size_limit", options={"max_values": 3}, note="values inside an envelope count (§7.1)")
    invalid("envelope/unknown-algorithm", envelope(9, z, len(raw)).hex(), "unsupported_compression")
    invalid("envelope/nested", envelope(0, zlib.compress(envelope(0, z, len(raw))),
                                        len(envelope(0, z, len(raw)))).hex(), "trailing_data",
            note="envelopes don't nest: the inner 'BTON' parses as the integer 66 with trailing bytes")
    invalid("envelope/zlib-trailing-bytes", envelope(0, z + b"\x00", len(raw)).hex(), "invalid_envelope",
            note="the compressed stream must end exactly at the end of the payload")
    half = len(raw) // 2
    invalid("envelope/zlib-two-streams",
            envelope(0, zlib.compress(raw[:half]) + zlib.compress(raw[half:]), len(raw)).hex(), "invalid_envelope",
            note="one zlib stream per payload")

    # --- signed envelope (§8); HMAC is deterministic, so these are canonical -----------
    key_hex = HMAC_KEY.hex()
    for id_, v in {"signed/map": {"a": 1, "b": [True, None]}, "signed/string": "hello"}.items():
        body = enc(v)
        _add({"id": id_, "hex": signed(body).hex(), "value": typed(v), "canonical": True,
              "options": {"hmac_key": key_hex}})
    decode_only("signed/compressed", signed(envelope(0, zlib.compress(raw), len(raw))).hex(), doc,
                options={"hmac_key": key_hex})
    bad = bytearray(signed(enc({"a": 1})))
    bad[-1] ^= 0x01
    invalid("signed/tampered", bytes(bad).hex(), "authentication_failed", options={"hmac_key": key_hex})
    invalid("signed/wrong-key", signed(enc(1), key=b"x" * 32).hex(), "authentication_failed",
            options={"hmac_key": key_hex})
    invalid("signed/missing-key", signed(enc(1)).hex(), "authentication_failed",
            note="decoders without a key must not ignore a signature")
    invalid("signed/unsigned-input", enc(1).hex(), "authentication_failed", options={"hmac_key": key_hex})
    invalid("signed/truncated-header", (b"BTSG" + bytes([1, 1, 0, 0]) + bytes(10)).hex(), "invalid_envelope",
            options={"hmac_key": key_hex}, note="with a key: BTSG + fewer than 36 bytes")
    invalid("signed/keyless-short", b"BTSG".hex(), "trailing_data",
            note="without a key, inputs shorter than 40 bytes are bare documents: 0x42 = 66, then trailing bytes")
    invalid("signed/bad-version", signed(enc(1))[:4].hex() + "02" + signed(enc(1))[5:].hex(), "invalid_envelope",
            options={"hmac_key": key_hex})
    invalid("signed/empty-key", signed(enc(1)).hex(), "invalid_argument", options={"hmac_key": ""},
            note="an empty key is a configuration error, not \"no key\" (§8)")
    invalid("signed/empty-key-unsigned-input", enc(1).hex(), "invalid_argument", options={"hmac_key": ""})

    build_02()
    build_03()


def build_02():
    """Vectors added in spec 0.2 (§11.2)."""
    # --- NaN payloads in float vectors (§5.5, §10) -------------------------------------
    canonical("vector-f32/nan", F32Vec([math.nan, 1.0]), "d7fb0000c07f0000803f")
    canonical("vector-f32/nan-payloads", F32Vec([Bits32(0x7FA00001), Bits32(0xFFC00000)]),
              note="NaN payloads inside vectors are kept bit for bit (§5.5)")
    canonical("vector-f32/infinities", F32Vec([math.inf, -math.inf]))
    canonical("vector-f64/nan", F64Vec([math.nan]))
    canonical("vector-f64/nan-payloads", F64Vec([Bits64(0x7FF0000000000001), Bits64(0xFFF8000000000000)]))

    # --- tabular names, front to back (§5.6.3) --------------------------------------
    def tab(names, nrows=1, cells=b"\xc0"):
        return ext(-10, tabular_payload(names, [0] * len(names), [cells * nrows] * len(names), nrows)).hex()

    invalid("tabular/duplicate-before-invalid-utf8", tab(["a", "a", b"\xff"]), "invalid_tabular",
            note="names are checked in order: the duplicate comes first")
    invalid("tabular/invalid-utf8-before-duplicate", tab([b"\xff", "a", "a"]), "invalid_utf8")

    # --- keys must be UTF-8 in non-strict mode too (§7) -----------------------------
    nonstrict = {"strict": False}
    invalid("nonstrict/invalid-utf8-key", "81a1ff01", "invalid_utf8", options=nonstrict)
    invalid("nonstrict/invalid-utf8-keys-would-merge", "82a1ff01a1fe02", "invalid_utf8", options=nonstrict,
            note="replacing both keys with U+FFFD would merge them and drop a value")
    invalid("nonstrict/invalid-utf8-column-name", tab([b"\xff"]), "invalid_utf8", options=nonstrict)
    invalid("nonstrict/duplicate-column-name", tab(["a", "a"]), "invalid_tabular", options=nonstrict)

    # --- max_values (§7.1) -----------------------------------------------------------
    def limits(id_, hex_, value, n, note=None):
        decode_only(f"limits/{id_}-at-limit", hex_, value, note=note, options={"max_values": n})
        invalid(f"limits/{id_}-over-limit", hex_, "size_limit", options={"max_values": n - 1})

    limits("array", "93010203", [1, 2, 3], 4, note="the array and its three elements")
    limits("map", "81a1619101", {"a": [1]}, 3, note="map keys don't count")
    limits("nested", "9291019102", [[1], [2]], 5)
    two_rows = ext(-10, tabular_payload(["a", "b"], [2, 2], [b"\x01\x02", b"\x03\x04"], 2)).hex()
    limits("tabular", two_rows, [{"a": 1, "b": 3}, {"a": 2, "b": 4}], 7,
           note="1 array + 2 row maps + 4 cells; column names don't count")
    invalid("limits/tabular-rows-counted-at-header",
            ext(-10, tabular_payload(["a"], [0], [b"\xc1\xc0"], 2)).hex(), "size_limit",
            options={"max_values": 2}, note="the row maps count once the header is checked, before any cell")
    invalid("limits/counted-when-value-starts", "9201c1", "size_limit", options={"max_values": 2},
            note="the third value exceeds the limit before its marker is examined")
    invalid("limits/counted-before-truncation", "81a161", "size_limit", options={"max_values": 1},
            note="the map's value counts when it would start, even though the input has ended")
    invalid("limits/announced-length-checked-first", "9201", "truncated", options={"max_values": 2},
            note="the array's announced length is checked against the input before any element starts")

    # --- tabular key bytes: N × Σ len(name) ≤ max_decompressed_size (§7.1) -----------
    keyed = ext(-10, tabular_payload(["abcd", "efgh"], [0, 0], [b"\xc0" * 4, b"\xc0" * 4], 4)).hex()
    decode_only("limits/tabular-key-bytes-at-limit", keyed, [{"abcd": None, "efgh": None}] * 4,
                options={"max_decompressed_size": 32}, note="4 rows × 8 bytes of names")
    invalid("limits/tabular-key-bytes-over-limit", keyed, "size_limit", options={"max_decompressed_size": 31})

    # --- raw extensions with ids BTOON defines (§5.7) --------------------------------
    for t, payload in [(-1, bytes(4)), (-2, bytes(8)), (-3, bytes(8)), (-4, b"\x05"), (-5, bytes(4)),
                       (-6, bytes(8)), (-10, struct.pack(">III", 1, 0, 0))]:
        encode_error(f"encode/raw-ext{t}", Ext(t, payload), "invalid_argument",
                     note="written verbatim, this could be malformed or non-canonical")
    canonical("ext/future-id-written-as-given", Ext(-11, b"\x01"), "d4f501",
              note="ids reserved for future versions pass through (§5.7)")


def build_03():
    """Vectors added in spec 0.3 (§11.1)."""
    # --- date and datetime at the 64-bit extremes (§5.2, §5.3) ------------------------
    canonical("date/max", Date(2**63 - 1), "d7fe7fffffffffffffff")
    canonical("date/min", Date(-(2**63)), "d7fe8000000000000000")
    canonical("datetime/max", DateTime(2**63 - 1), "d7fd7fffffffffffffff")
    canonical("datetime/min", DateTime(-(2**63)), "d7fd8000000000000000")

    # --- a table with date and datetime columns (§5.6) --------------------------------
    rows = [{"at": DateTime(1700000000123456789 + i), "day": Date(1700000000000 + 86400000 * i), "id": i}
            for i in range(6)]
    canonical("tabular/date-and-datetime-columns", rows, tabular=1,
              note="every library must write these back, through its typed date and datetime values (§10)")

    # --- LZ4 end-of-block conditions (§6) ---------------------------------------------
    try:
        import lz4.block
    except ImportError:
        print("note: lz4 not installed; skipping lz4 end-of-block vectors", file=sys.stderr)
        return

    def lz4_block(id_, block: bytes, text: str, valid: bool, note: str, liblz4_accepts: bool | None = None):
        doc = enc(text)
        try:
            out = lz4.block.decompress(block, uncompressed_size=len(doc))
        except Exception:
            out = None
        # Each vector records liblz4's verdict. It matches the spec except on liblz4's fast
        # path, which skips the end-of-block checks (§6); those vectors say so.
        if valid:
            assert out == doc, f"{id_}: liblz4 verdict changed"
        else:
            assert (out is not None) == bool(liblz4_accepts), f"{id_}: liblz4 verdict changed"
        hex_ = envelope(1, block, len(doc)).hex()
        if valid:
            decode_only(id_, hex_, text, note=note, requires="lz4")
        else:
            invalid(id_, hex_, "invalid_envelope", note=note, requires="lz4")

    lz4_block("lz4-block/ends-with-match", bytes.fromhex("20a561" "0100"), "aaaaa", False,
              "the block ends with a match instead of a literals-only sequence")
    lz4_block("lz4-block/last-literals-too-short", bytes.fromhex("2ab361" "0100" "40") + b"aaaa", "a" * 19, False,
              "only 4 literal bytes follow the last match; the last 5 must be literals")
    lz4_block("lz4-block/last-match-too-close", bytes.fromhex("20aa61" "0100" "50") + b"aaaaa", "a" * 10, False,
              "the last match starts 9 bytes before the end; it must start at least 12 before")
    lz4_block("lz4-block/last-match-at-the-limit", bytes.fromhex("23ad61" "0100" "50") + b"aaaaa", "a" * 13, True,
              "the last match starts exactly 12 bytes before the end, followed by 5 literals")
    lz4_block("lz4-block/final-token-match-bits-ignored", bytes.fromhex("6fa5") + b"aaaaa", "aaaaa", True,
              "the final token's match-length bits are ignored")
    # Longer outputs. A match at offset 12 after "\xbf" + 13 letters repeats "bcdefghijklm".
    letters = "abcdefghijklm"
    copied = letters[1:] * 3
    lz4_block("lz4-block/long-output-ends-with-match",
              bytes([0xEE, 0xBF]) + letters.encode() + bytes.fromhex("0c00" "00"), letters + copied[:18], False,
              "a 32-byte output whose last sequence is a match; liblz4's fast path accepts it", liblz4_accepts=True)
    # 37-byte outputs (str8 header d9 23, then 35 characters) where only the offset is wrong:
    # 14 literals, an 18-byte match, then 5 literals, so the end-of-block conditions hold.
    first = "abcdefghijkl"
    lz4_block("lz4-block/offset-zero", bytes([0xEE, 0xD9, 0x23]) + first.encode() + bytes.fromhex("0000" "50") + b"vwxyz",
              first + "x" * 18 + "vwxyz", False,
              "offset 0 is invalid in the LZ4 block format; liblz4 accepts it and writes zeros", liblz4_accepts=True)
    lz4_block("lz4-block/offset-before-start",
              bytes([0xEE, 0xD9, 0x23]) + first.encode() + bytes.fromhex("1400" "50") + b"vwxyz",
              first + "x" * 18 + "vwxyz", False, "the match reaches 20 bytes back when only 14 have been written")


def zlib_with_fdict(data: bytes) -> bytes:
    """A zlib stream (RFC 1950) whose header sets FDICT with the empty dictionary's DICTID."""
    cmf, flg = 0x78, 0x20
    flg += (31 - ((cmf << 8) | flg) % 31) % 31
    c = zlib.compressobj(wbits=-15)
    raw = c.compress(data) + c.flush()
    return bytes([cmf, flg]) + struct.pack(">I", zlib.adler32(b"")) + raw + struct.pack(">I", zlib.adler32(data))


def _nested(depth: int):
    v = {"nil": None}
    for _ in range(depth):
        v = {"array": [v]}
    return v


def envelope(algo: int, payload: bytes, usize: int, version=1, reserved=0, csize=None) -> bytes:
    csize = len(payload) if csize is None else csize
    return b"BTON" + struct.pack(">BBHII", version, algo, reserved, csize, usize) + payload


def signed(payload: bytes, key: bytes = HMAC_KEY) -> bytes:
    header = b"BTSG" + bytes([1, 1, 0, 0])
    mac = hmac.new(key, header + payload, hashlib.sha256).digest()
    return header + mac + payload


# Vectors whose bytes embed compressor output can differ between zlib/zstd/brotli
# versions; --check skips them (they are decode-only and stay valid regardless).
_COMPRESSOR_DEPENDENT = ("compression/", "envelope/", "signed/compressed")


def main():
    build()
    out = {
        "spec": SPEC_VERSION,
        "description": "BTOON conformance vectors. See docs/SPEC.md §10. Generated by tests/vectors/generate.py.",
        "defaults": {"max_depth": 128, "max_decompressed_size": 67108864, "max_values": 1048576, "strict": True},
        "vectors": VECTORS,
    }
    path = Path(__file__).with_name("btoon-vectors.json")
    if "--check" in sys.argv:
        current = {v["id"]: v for v in json.loads(path.read_text(encoding="utf-8"))["vectors"]}
        fresh = {v["id"]: v for v in VECTORS}
        stable = lambda ids: {i for i in ids if not i.startswith(_COMPRESSOR_DEPENDENT)}
        problems = sorted(stable(current) ^ stable(fresh))
        problems += sorted(i for i in stable(current) & stable(fresh) if current[i] != fresh[i])
        if problems:
            print("btoon-vectors.json is out of date for:", *problems[:20], sep="\n  ", file=sys.stderr)
            sys.exit(1)
        print(f"btoon-vectors.json is up to date ({len(stable(fresh))} stable vectors compared)")
        return
    path.write_text(json.dumps(out, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
    kinds = {"canonical": 0, "decode-only": 0, "invalid": 0, "encode-error": 0}
    for v in VECTORS:
        if "encode_error" in v:
            kinds["encode-error"] += 1
        else:
            kinds["invalid" if "error" in v else "canonical" if v["canonical"] else "decode-only"] += 1
    print(f"wrote {path} ({len(VECTORS)} vectors: {kinds})")


if __name__ == "__main__":
    main()

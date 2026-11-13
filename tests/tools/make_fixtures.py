#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2025 Alvar Laigna <https://alvarlaigna.com>
# SPDX-License-Identifier: MIT
"""
Writes the fixtures for the CLI round-trip test (tests/tools/run_tools_test.cmake):

  all-types.btoon   every BTOON type, with edge cases, as one canonical document
  all-types.json    what `btoon decode` must print for it (tools/json_mapping.h)
  deep-128.btoon    a document nested exactly 128 containers deep, with tags at the bottom

The bytes come from the independent encoder in tests/vectors/generate.py
(msgpack-python plus the spec), and the JSON from a Python reimplementation of the
mapping, so the C++ tools are checked against something other than themselves.

    uv run --with msgpack python tests/tools/make_fixtures.py [--check]
"""
from __future__ import annotations

import base64
import datetime
import json
import math
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "vectors"))
import generate as g  # noqa: E402

INT64_MIN, INT64_MAX, UINT64_MAX = -(2**63), 2**63 - 1, 2**64 - 1
BIGINT_DECIMAL_MAX_BYTES = 40000


class RawVec:
    """A vector given as its little-endian payload (for NaN payloads)."""

    def __init__(self, ext_type: int, payload: bytes):
        self.ext_type, self.payload = ext_type, payload


def f32_bits(bits: int) -> bytes:
    return struct.pack("<I", bits)


def f64_bits(bits: int) -> bytes:
    return struct.pack("<Q", bits)


# --- the reference JSON mapping ------------------------------------------------------
def civil_from_days(z: int):
    z += 719468
    era = z // 146097
    doe = z - era * 146097
    yoe = (doe - doe // 1460 + doe // 36524 - doe // 146096) // 365
    y = yoe + era * 400
    doy = doe - (365 * yoe + yoe // 4 - yoe // 100)
    mp = (5 * doy + 2) // 153
    d = doy - (153 * mp + 2) // 5 + 1
    m = mp + 3 if mp < 10 else mp - 9
    return y + (m <= 2), m, d


def rfc3339(seconds: int, nanos: int, offset: int | None) -> str:
    days, sod = divmod(seconds + (offset or 0) * 60, 86400)
    y, m, d = civil_from_days(days)
    if 1 <= y <= 9999:  # cross-check against the standard library where it can
        dt = datetime.datetime(1970, 1, 1) + datetime.timedelta(days=days, seconds=sod)
        assert (dt.year, dt.month, dt.day) == (y, m, d)
    ystr = f"{y:04d}" if 0 <= y <= 9999 else f"{'-' if y < 0 else '+'}{abs(y):06d}"
    s = f"{ystr}-{m:02d}-{d:02d}T{sod // 3600:02d}:{sod // 60 % 60:02d}:{sod % 60:02d}"
    if nanos:
        s += "." + f"{nanos:09d}".rstrip("0")
    if offset is None:
        return s + "Z"
    return s + f"{'-' if offset < 0 else '+'}{abs(offset) // 60:02d}:{abs(offset) % 60:02d}"


def b64(b: bytes) -> str:
    return base64.b64encode(b).decode("ascii")


def float_json(x: float):
    if math.isnan(x):
        return {"$float": "NaN"}
    if math.isinf(x):
        return {"$float": "Infinity" if x > 0 else "-Infinity"}
    return x


def to_json(v):
    if v is None or isinstance(v, (bool, str)):
        return v
    if isinstance(v, int):
        if INT64_MIN <= v <= UINT64_MAX:
            return v
        if len(g.bigint_bytes(v)) <= BIGINT_DECIMAL_MAX_BYTES:
            return {"$bigint": str(v)}
        return {"$bigint": f"-0x{-v:x}" if v < 0 else f"0x{v:x}"}
    if isinstance(v, float):
        return float_json(v)
    if isinstance(v, bytes):
        return {"$binary": b64(v)}
    if isinstance(v, list):
        return [to_json(x) for x in v]
    if isinstance(v, dict):
        o = {k: to_json(x) for k, x in v.items()}
        return {"$map": o} if len(v) == 1 and next(iter(v)).startswith("$") else o
    if isinstance(v, g.TS):
        return {"$timestamp": rfc3339(v.seconds, v.nanoseconds, v.offset)}
    if isinstance(v, g.Date):
        return {"$date": v.ms}
    if isinstance(v, g.DateTime):
        return {"$datetime": str(v.ns)}
    if isinstance(v, g.F32Vec):  # elements as the exact double value of each float32
        return {"$f32": [float_json(struct.unpack("<f", struct.pack("<f", x))[0]) for x in v.values]}
    if isinstance(v, g.F64Vec):
        return {"$f64": [float_json(x) for x in v.values]}
    if isinstance(v, RawVec):
        return {"$f32" if v.ext_type == -5 else "$f64": b64(v.payload)}
    if isinstance(v, g.Ext):
        return {"$ext": [v.type, b64(v.data)]}
    raise TypeError(type(v))


def encode(v) -> bytes:
    if isinstance(v, RawVec):
        return g.ext(v.ext_type, v.payload)
    if isinstance(v, list) and any(isinstance(x, RawVec) for x in v):
        return g.msgpack.Packer().pack_array_header(len(v)) + b"".join(encode(x) for x in v)
    if isinstance(v, dict) and any(isinstance(x, (RawVec, list)) for x in v.values()):
        out = g.msgpack.Packer().pack_map_header(len(v))
        for k in sorted(v, key=g.ukey):
            out += g.msgpack.packb(k) + encode(v[k])
        return out
    return g.enc(v)


# --- the fixtures ----------------------------------------------------------------------
NAN, INF = float("nan"), float("inf")

ALL_TYPES = {
    "nil": None,
    "bool": [True, False],
    "int": [0, 1, -1, 127, 128, -32, -33, 255, 65535, 2**32, INT64_MAX, INT64_MIN],
    "uint64": [2**63, UINT64_MAX],
    "bigint": [2**64, -(2**63) - 1, 10**40, -(10**40), 2**1000],
    "float": [1.5, -0.0, 0.1, 2.0, 1e300, 5e-324, 1.7976931348623157e308, NAN, INF, -INF],
    "string": ["", "plain", "unicode é ✓ 🎉", 'quote " backslash \\ newline \n tab \t', "\u0000\u001f\u007f",
               "$binary", '{"$date": 1}'],
    "binary": [b"", b"\x00\x01\xfe\xff", bytes(range(48))],
    "timestamp": [
        g.TS(0), g.TS(1704067200), g.TS(1704067200, 500000000), g.TS(2**34, 1), g.TS(-1, 999999999),
        g.TS(-62135596800), g.TS(-62167219201), g.TS(253402300800),
        g.TS(INT64_MIN), g.TS(INT64_MAX, 999999999),
        g.TS(1704067200, 0, 120), g.TS(1704067200, 123456789, -330), g.TS(0, 0, 0),
        g.TS(INT64_MAX - 4, 0, 1080), g.TS(INT64_MIN + 5, 0, -1080),
    ],
    "date": [g.Date(0), g.Date(1704067200000), g.Date(-1), g.Date(INT64_MAX), g.Date(INT64_MIN)],
    "datetime": [g.DateTime(0), g.DateTime(1704067200000000001), g.DateTime(INT64_MIN), g.DateTime(INT64_MAX)],
    "f32": [
        g.F32Vec([]),
        g.F32Vec([1.5, -0.0, NAN, INF, -INF, 3.4028234663852886e38, 1e-45, 0.1]),
        RawVec(-5, f32_bits(0x7FC00001) + f32_bits(0xFFC00000) + f32_bits(0x3F800000)),
    ],
    "f64": [
        g.F64Vec([]),
        g.F64Vec([2.5, -0.0, NAN, INF, -INF, 5e-324]),
        RawVec(-6, f64_bits(0x7FF8000000000001) + f64_bits(0xFFF8000000000000)),
    ],
    "ext": [g.Ext(5, b"\x01\x02\x03"), g.Ext(-7, b""), g.Ext(127, b"x" * 16), g.Ext(-128, b"\xff"),
            g.Ext(0, b"a" * 20)],
    "tag-like maps": [{"$binary": "AAAA"}, {"$map": 1}, {"$foo": {"$bar": 2}}, {"$float": "NaN"},
                      {"$timestamp": "not a timestamp"}, {"$a": 1, "$b": 2}],
    "table": [{"id": i, "name": f"row{i}", "score": i * 1.5} for i in range(5)],
    "nested": [[[]], {}, [{}]],
}


def deep_128():
    """Containers at depths 0..127: the top-level map, 126 arrays, then a tag-shaped map
    holding a float32 vector with NaN (so the JSON nests 132 levels)."""
    inner = {"$k": g.F32Vec([NAN])}
    for _ in range(126):
        inner = [inner]
    return {"deep": inner}


def main():
    files = {
        "all-types.btoon": encode(ALL_TYPES),
        "all-types.json": (json.dumps(to_json(ALL_TYPES), indent=2, ensure_ascii=False, sort_keys=True) + "\n")
        .encode("utf-8"),
        "deep-128.btoon": encode(deep_128()),
    }
    stale = []
    for name, data in files.items():
        path = HERE / name
        if "--check" in sys.argv:
            if not path.exists() or path.read_bytes() != data:
                stale.append(name)
        else:
            path.write_bytes(data)
            print(f"wrote {path} ({len(data)} bytes)")
    if stale:
        print("out of date:", ", ".join(stale), file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()

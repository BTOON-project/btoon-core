#!/usr/bin/env python3
"""Writes a libFuzzer seed corpus (one file per conformance vector) to fuzz/corpus/."""
import json
from pathlib import Path

here = Path(__file__).resolve().parent
out = here / "corpus"
out.mkdir(exist_ok=True)
vectors = json.loads((here.parent / "tests/vectors/btoon-vectors.json").read_text(encoding="utf-8"))["vectors"]
count = 0
for v in vectors:
    if "hex" not in v:  # encode_error vectors (docs/SPEC.md §10) carry a value, not bytes
        continue
    data = bytes.fromhex(v["hex"])
    if 0 < len(data) <= 4096:
        (out / (v["id"].replace("/", "_") + ".bin")).write_bytes(data)
        count += 1
print(f"wrote {count} seeds to {out}")

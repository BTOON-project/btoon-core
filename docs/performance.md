# Performance

## Measured

`btoon_bench` (`tests/bench.cpp`), 10,000 rows, Release build, Apple M4 Max, best of 7
runs, 2026-11-07:

| Case | Size | Encode | Decode |
|---|---:|---:|---:|
| records, tabular | 447 KB | 0.60 ms (741 MB/s) | 1.54 ms (291 MB/s) |
| records, plain (`auto_tabular=false`) | 767 KB | 0.69 ms (1.1 GB/s) | 2.05 ms (374 MB/s) |
| nested, mixed (not tabular) | 814 KB | 0.86 ms (943 MB/s) | 2.16 ms (378 MB/s) |

The same benchmark on 0.0.1, before the encoder rewrite: encode 3.39 / 9.52 / 14.39 ms,
decode 1.59 / 2.15 / 2.44 ms.

```bash
cmake -S . -B build-rel -DCMAKE_BUILD_TYPE=Release -DBUILD_BENCHMARKS=ON
cmake --build build-rel --target btoon_bench && build-rel/btoon_bench 10000
```

## Where the time goes

- **Encode** writes into one growing buffer. The costs are mostly memory copies of
  strings and UTF-8 validation, which has an 8-bytes-at-a-time ASCII fast path. Turn
  validation off with `validate_utf8=false` only when the strings are known to be good.
- **Decode** is dominated by building the `Value` tree: one `std::map` node per key and
  one `std::string` per string. Canonical (sorted) input inserts in O(1) through
  `try_emplace(end(), …)`. `Validator` decodes too (it checks the decoded value), so it
  costs at least as much as `decode()`.
- **Tabular** encodes slightly slower than plain (it walks the table column by column),
  but it writes far fewer bytes, which usually wins once I/O or compression is involved.

## Tuning

1. **Keep records uniform.** Tabular encoding needs every row to have exactly the same
   keys. Fill in missing fields with `nil` rather than omitting them.
2. **Short keys still matter** in non-tabular data, because every map repeats them.
3. **Compression:** zlib is the portable default. Zstd at level 3 is usually smaller and
   faster. LZ4 is the fastest when latency matters. Compressing tabular BTOON beats
   compressing JSON (11.3% vs 13.6% of the JSON size in the README sample).
4. **Reuse an `Encoder`**: `clear()` keeps its buffer, which avoids reallocating for each
   message.
5. **Vectors:** store embeddings as `VectorFloat`/`VectorDouble`. That's a single memcpy
   each way, and 4 bytes per float instead of 9 for float64-in-an-array.
6. **Never use `BTOON_NATIVE_ARCH` for binaries you distribute.** It is fine for a local
   benchmark.

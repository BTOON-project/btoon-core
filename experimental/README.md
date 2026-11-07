# Experimental headers

These headers were written ahead of an implementation. As of 0.1.0 they don't compile,
or they declare functions that are never defined. They're kept here as design sketches.
They are **not** built, installed, or covered by the stability promise of the library.

| Header | State |
|---|---|
| `batch_processor.h` | Parallel batch processing. Doesn't compile. |
| `decimal.h` | Decimal/currency type. Declarations only; doesn't compile. |
| `graph.h` | Graph structures and algorithms. Template members never defined. |
| `graphql_schema.h` | GraphQL schema bridge. Doesn't compile. |
| `json_schema.h` | JSON Schema bridge. Doesn't compile. |
| `timeseries.h` | Time-series helpers. Mostly declarations. |

To promote one of them: implement it in `src/` and add tests to `tests/`. Then move the
header back to `include/btoon/` and list it in `BTOON_PUBLIC_HEADERS` in `CMakeLists.txt`.
If it changes anything on the wire, update `docs/SPEC.md` first.

## Other experimental material

- `docs/columnar-compression-spec.md` describes per-column codecs (delta, RLE,
  dictionary) inside the tabular extension. The format doesn't implement it. The codecs
  exist as standalone utilities in `src/*_codec.cpp`.
- `bindings/` holds early in-tree Python and JavaScript binding prototypes. They're
  superseded by the [btoon-python](https://github.com/BTOON-project/btoon-python) and
  [btoon-nodejs](https://github.com/BTOON-project/btoon-nodejs) repos and target the
  0.0.x API.

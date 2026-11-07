# btoon-core

Reference C++20 implementation of BTOON, plus **the spec** (`docs/SPEC.md`) and **the
conformance vectors** (`tests/vectors/btoon-vectors.json`) that every other BTOON
library follows.

## Commands

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON   # ASan+UBSan for lib + tests
cmake --build build -j && ctest --test-dir build --output-on-failure
build/btoon_tests --gtest_filter='Vectors.*'                        # conformance only
build/btoon_fuzz_smoke 500000                                       # mutation fuzzer
uv run --with msgpack --with zstandard --with lz4 --with brotli python tests/vectors/generate.py [--check]
cmake -B build-rel -DCMAKE_BUILD_TYPE=Release -DBUILD_BENCHMARKS=ON && cmake --build build-rel && build-rel/btoon_bench
```

On macOS:
- Optional codecs need `-DBTOON_WITH_LZ4=ON -DBTOON_WITH_ZSTD=ON -DBTOON_WITH_BROTLI=ON`
  (from Homebrew).
- libFuzzer needs Homebrew LLVM; see `fuzz/README.md`.
- With Homebrew LLVM or GCC, pass `-DZLIB_ROOT=$(brew --prefix zlib)`. Otherwise CMake
  picks the SDK's zlib, whose include dir breaks libc++ header ordering.
- `/usr/local` holds a stale 0.0.x install (`include/btoon`, `lib/libbtoon_core.a`, a
  GTest built with libc++). With GCC, add `-DCMAKE_DISABLE_FIND_PACKAGE_GTest=ON` so the
  pinned GoogleTest gets fetched.

## Invariants (don't break these)

- The wire format is defined by `docs/SPEC.md`. To change the format, edit the spec,
  then `tests/vectors/generate.py`, regenerate the vectors, then the C++ code. Record
  the reason in the workspace's `../docs/DECISIONS.md`.
- `generate.py` is an *independent* oracle built on msgpack-python. Never generate the
  vectors from the C++ code.
- The encoder is canonical: smallest ints by value, float64 with canonical NaN, keys in
  UTF-8 byte order (std::map gives this), and tabular only when the §5.6.2 size rule
  says so.
- Every decoder read goes through `Reader::need()`. Check counts against the remaining
  input before allocating. Recursion is depth-limited.
- `add_compile_options` for sanitizers and fuzzing must stay *before*
  `add_library(btoon_core)` in `CMakeLists.txt`. Otherwise the library isn't
  instrumented.
- The version lives only in `include/btoon/btoon.h` (`BTOON_VERSION_*`). CMake parses it.

## Layout

- `src/internal.h`: private helpers (endianness, UTF-8, bigint, tabular rule, `fail()`).
- `src/encoder.cpp`, `src/decoder.cpp`, `src/btoon.cpp`: the codec and the envelope
  layering.
- `src/compression.cpp`: bounded decompression. `src/sha256.cpp`: in-tree SHA-256 and HMAC.
- `tools/json_mapping.h`: the lossless JSON ↔ BTOON mapping used by the CLI tools.
- `experimental/`: headers and prototypes that aren't built or installed.

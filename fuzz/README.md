# Fuzzing

Two fuzzers exercise the decoder with hostile input:

- **`btoon_fuzz_smoke`** (`tests/fuzz_smoke.cpp`): a dependency-free mutation fuzzer
  that works with any compiler. It's part of `ctest` (20k iterations). Run it longer by
  hand with `build/btoon_fuzz_smoke 1000000`.
- **`fuzz_decoder`** (`fuzz/fuzz_decoder.cpp`): a coverage-guided libFuzzer target. It
  needs Clang with libFuzzer. Apple's clang lacks libFuzzer, so on macOS use Homebrew
  LLVM.

Both check the same invariants:
- decoding succeeds or throws `BtoonException`, never anything else;
- every decoded value re-encodes and decodes back to itself;
- the validator never throws anything else either.

Any crash or sanitizer report is a bug.

## libFuzzer

```bash
python3 fuzz/make_corpus.py                    # seeds from the conformance vectors
LLVM=$(brew --prefix llvm)                     # macOS; on Linux just use clang++
cmake -S . -B build-fuzz -DCMAKE_CXX_COMPILER=$LLVM/bin/clang++ \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_FUZZ_TESTS=ON \
      -DBUILD_TESTS=OFF -DBUILD_TOOLS=OFF -DBUILD_EXAMPLES=OFF \
      -DZLIB_ROOT=$(brew --prefix zlib) \
      -DCMAKE_EXE_LINKER_FLAGS="-L$LLVM/lib/c++ -Wl,-rpath,$LLVM/lib/c++"
cmake --build build-fuzz --target fuzz_decoder
mkdir -p fuzz/work && cp fuzz/corpus/* fuzz/work/
./build-fuzz/fuzz_decoder -max_total_time=300 -rss_limit_mb=2048 fuzz/work
```

With `BUILD_FUZZ_TESTS=ON` the library itself is built with
`-fsanitize=fuzzer-no-link,address,undefined`, so libFuzzer gets coverage from the
decoder and not just from the harness.

Results on an Apple M4 Max on 2026-11-07: 788,707 executions in 4 minutes reached 2,613
edges, with no crashes.

`fuzz/corpus/` and `fuzz/work/` are generated directories. Don't commit them.

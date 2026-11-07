# Contributing to BTOON Core

Thanks for helping. This repository is the reference for every BTOON implementation,
so correctness and compatibility come first.

## Ground rules

- **Spec first.** A change that affects bytes on the wire starts in `docs/SPEC.md`, then
  `tests/vectors/generate.py`. Regenerate the vectors, then change the C++ code. Open an
  issue labelled `spec-change` before writing code.
- **Canonical output is a contract.** Every library must produce identical bytes for
  the same value. If you change what the encoder emits, you change the spec.
- **The decoder treats input as hostile.** New parsing code reads through the
  bounds-checked `Reader` and checks sizes before allocating. Add a hardening test for
  any new failure mode.
- **No unmeasured claims.** Performance numbers in docs come from `btoon_bench` or a
  similar reproducible run, with the machine named.

## Build and test

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure      # unit tests, vectors, fuzz smoke test
```

Before you open a PR:

```bash
# vectors still match the independent generator
uv run --with msgpack --with zstandard --with lz4 --with brotli python tests/vectors/generate.py --check
# optional codecs
cmake -S . -B build-codecs -DBTOON_WITH_LZ4=ON -DBTOON_WITH_ZSTD=ON -DBTOON_WITH_BROTLI=ON && cmake --build build-codecs -j && ctest --test-dir build-codecs
```

For libFuzzer instructions see `fuzz/README.md`. CI also runs Linux GCC/Clang, macOS,
Windows MSVC, ASan/UBSan with every codec, and a short libFuzzer job.

## Style

- C++20. Formatting follows the existing code: 4-space indent, braces on the same line.
- Public headers in `include/btoon/` are part of the installed API. Keep them small and
  documented, and put helpers in `src/internal.h`.
- Errors are `BtoonException` with an `ErrorCode`. Use `detail::fail(code, message)`.
- Every bug fix comes with a test that fails without it.

## Commits and PRs

Keep PRs focused. Describe *why*. If a format change is involved, list the vector IDs you
added or changed. Update `docs/CHANGELOG.md` under "Unreleased".

## Reporting security issues

Please don't open public issues for vulnerabilities. Email security@btoon.net with a
reproducer: the input bytes plus the build configuration.

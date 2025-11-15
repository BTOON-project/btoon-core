# BTOON Fuzzing

This directory contains fuzzing infrastructure for testing BTOON robustness against malformed and malicious inputs.

## Building Fuzz Tests

### With LibFuzzer (Clang)

```bash
mkdir build-fuzz
cd build-fuzz
CC=clang CXX=clang++ cmake .. -DBUILD_FUZZ_TESTS=ON -DENABLE_SANITIZERS=ON
make fuzz_decoder
```

### Running Fuzz Tests

```bash
# Create corpus directory
mkdir corpus

# Run fuzzer
./fuzz_decoder corpus/ -max_total_time=60 -max_len=1024

# Run with a specific crash/hang
./fuzz_decoder crash-file
```

### With AFL++

```bash
# Build with AFL instrumentation
CC=afl-clang-fast CXX=afl-clang-fast++ cmake ..
make

# Prepare corpus
mkdir input output
echo "test" > input/seed

# Run fuzzing
afl-fuzz -i input -o output ./fuzz_decoder
```

## Corpus Files

The `corpus/` directory should contain seed inputs for fuzzing:
- Valid BTOON files of various types
- Edge cases (empty, minimal, maximal)
- Previously found crashes (for regression testing)

## Security Testing

The fuzzer tests for:
- Buffer overflows/underflows
- Integer overflows
- Excessive memory allocation
- Stack overflow via deep nesting
- Invalid type markers
- Malformed size claims
- UTF-8 validation bypass attempts

## Integration with CI

Add fuzzing to your CI pipeline:

```yaml
fuzz-test:
  runs-on: ubuntu-latest
  steps:
    - uses: actions/checkout@v2
    - name: Build Fuzzer
      run: |
        CC=clang CXX=clang++ cmake -B build -DBUILD_FUZZ_TESTS=ON
        cmake --build build --target fuzz_decoder
    - name: Run Fuzzing
      run: |
        ./build/fuzz_decoder corpus/ -max_total_time=300
```

## Analyzing Crashes

When a crash is found:

1. Minimize the test case:
   ```bash
   ./fuzz_decoder crash-file -minimize_crash=1
   ```

2. Debug with sanitizers:
   ```bash
   ASAN_OPTIONS=symbolize=1 ./fuzz_decoder crash-file
   ```

3. Add to regression corpus:
   ```bash
   cp crash-file corpus/regression/
   ```

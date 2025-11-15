#include <benchmark/benchmark.h>
#include "btoon/encoder.h"
#include "btoon/decoder.h"
#include "btoon/btoon.h"
#include <vector>
#include <map>
#include <string>
#include <chrono>

using namespace btoon;

// Test data
static const std::string test_string = "Hello, BTOON! This is a test string for benchmarking.";
static const std::vector<int64_t> test_int_array = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
static const DateTime test_datetime = {std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count()};
static const VectorFloat test_vector_float = {{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f}};
static const VectorDouble test_vector_double = {{1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0}};

static Value createTestMapValue() {
    return Map{
        {"name", String("Alice")},
        {"age", Int(30)},
        {"active", Bool(true)},
        {"timestamp", Timestamp{std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count()}},
        {"datetime", test_datetime},
        {"float_vec", test_vector_float},
        {"double_vec", test_vector_double}
    };
}

static const Value test_map_value = createTestMapValue();

// Encoding benchmarks
static void BM_BtoonEncodeString(benchmark::State& state) {
    for (auto _ : state) {
        benchmark::DoNotOptimize(encode(String(test_string)));
    }
    state.SetBytesProcessed(state.iterations() * test_string.size());
}
BENCHMARK(BM_BtoonEncodeString);

static void BM_BtoonEncodeInt(benchmark::State& state) {
    for (auto _ : state) {
        benchmark::DoNotOptimize(encode(Int(42)));
    }
    state.SetBytesProcessed(state.iterations() * sizeof(int64_t));
}
BENCHMARK(BM_BtoonEncodeInt);

static void BM_BtoonEncodeArray(benchmark::State& state) {
    Array arr;
    arr.reserve(test_int_array.size());
    for (int64_t val : test_int_array) {
        arr.push_back(Int(val));
    }
    
    for (auto _ : state) {
        benchmark::DoNotOptimize(encode(arr));
    }
    state.SetBytesProcessed(state.iterations() * test_int_array.size() * sizeof(int64_t));
}
BENCHMARK(BM_BtoonEncodeArray);

static void BM_BtoonEncodeMap(benchmark::State& state) {
    for (auto _ : state) {
        benchmark::DoNotOptimize(encode(test_map_value));
    }
    // Approximate size, actual encoded size varies slightly
    state.SetBytesProcessed(state.iterations() * 100); 
}
BENCHMARK(BM_BtoonEncodeMap);

static void BM_BtoonEncodeDateTime(benchmark::State& state) {
    for (auto _ : state) {
        benchmark::DoNotOptimize(encode(test_datetime));
    }
    state.SetBytesProcessed(state.iterations() * sizeof(int64_t));
}
BENCHMARK(BM_BtoonEncodeDateTime);

static void BM_BtoonEncodeVectorFloat(benchmark::State& state) {
    for (auto _ : state) {
        benchmark::DoNotOptimize(encode(test_vector_float));
    }
    state.SetBytesProcessed(state.iterations() * test_vector_float.data.size() * sizeof(float));
}
BENCHMARK(BM_BtoonEncodeVectorFloat);

static void BM_BtoonEncodeVectorDouble(benchmark::State& state) {
    for (auto _ : state) {
        benchmark::DoNotOptimize(encode(test_vector_double));
    }
    state.SetBytesProcessed(state.iterations() * test_vector_double.data.size() * sizeof(double));
}
BENCHMARK(BM_BtoonEncodeVectorDouble);

// Decoding benchmarks
static void BM_BtoonDecodeString(benchmark::State& state) {
    auto encoded = encode(String(test_string));
    for (auto _ : state) {
        benchmark::DoNotOptimize(decode(encoded));
    }
    state.SetBytesProcessed(state.iterations() * test_string.size());
}
BENCHMARK(BM_BtoonDecodeString);

static void BM_BtoonDecodeInt(benchmark::State& state) {
    auto encoded = encode(Int(42));
    for (auto _ : state) {
        benchmark::DoNotOptimize(decode(encoded));
    }
    state.SetBytesProcessed(state.iterations() * sizeof(int64_t));
}
BENCHMARK(BM_BtoonDecodeInt);

static void BM_BtoonDecodeArray(benchmark::State& state) {
    Array arr;
    arr.reserve(test_int_array.size());
    for (int64_t val : test_int_array) {
        arr.push_back(Int(val));
    }
    auto encoded = encode(arr);
    for (auto _ : state) {
        benchmark::DoNotOptimize(decode(encoded));
    }
    state.SetBytesProcessed(state.iterations() * test_int_array.size() * sizeof(int64_t));
}
BENCHMARK(BM_BtoonDecodeArray);

static void BM_BtoonDecodeMap(benchmark::State& state) {
    auto encoded = encode(test_map_value);
    for (auto _ : state) {
        benchmark::DoNotOptimize(decode(encoded));
    }
    state.SetBytesProcessed(state.iterations() * 100); // Approximate size
}
BENCHMARK(BM_BtoonDecodeMap);

static void BM_BtoonDecodeDateTime(benchmark::State& state) {
    auto encoded = encode(test_datetime);
    for (auto _ : state) {
        benchmark::DoNotOptimize(decode(encoded));
    }
    state.SetBytesProcessed(state.iterations() * sizeof(int64_t));
}
BENCHMARK(BM_BtoonDecodeDateTime);

static void BM_BtoonDecodeVectorFloat(benchmark::State& state) {
    auto encoded = encode(test_vector_float);
    for (auto _ : state) {
        benchmark::DoNotOptimize(decode(encoded));
    }
    state.SetBytesProcessed(state.iterations() * test_vector_float.data.size() * sizeof(float));
}
BENCHMARK(BM_BtoonDecodeVectorFloat);

static void BM_BtoonDecodeVectorDouble(benchmark::State& state) {
    auto encoded = encode(test_vector_double);
    for (auto _ : state) {
        benchmark::DoNotOptimize(decode(encoded));
    }
    state.SetBytesProcessed(state.iterations() * test_vector_double.data.size() * sizeof(double));
}
BENCHMARK(BM_BtoonDecodeVectorDouble);

// Round-trip benchmarks
static void BM_BtoonRoundTripString(benchmark::State& state) {
    for (auto _ : state) {
        auto encoded = encode(String(test_string));
        auto decoded = decode(encoded);
        benchmark::DoNotOptimize(decoded);
    }
}
BENCHMARK(BM_BtoonRoundTripString);

static void BM_BtoonRoundTripInt(benchmark::State& state) {
    for (auto _ : state) {
        auto encoded = encode(Int(42));
        auto decoded = decode(encoded);
        benchmark::DoNotOptimize(decoded);
    }
}
BENCHMARK(BM_BtoonRoundTripInt);

static void BM_BtoonRoundTripArray(benchmark::State& state) {
    Array arr;
    arr.reserve(test_int_array.size());
    for (int64_t val : test_int_array) {
        arr.push_back(Int(val));
    }
    for (auto _ : state) {
        auto encoded = encode(arr);
        auto decoded = decode(encoded);
        benchmark::DoNotOptimize(decoded);
    }
}
BENCHMARK(BM_BtoonRoundTripArray);

static void BM_BtoonRoundTripMap(benchmark::State& state) {
    for (auto _ : state) {
        auto encoded = encode(test_map_value);
        auto decoded = decode(encoded);
        benchmark::DoNotOptimize(decoded);
    }
}
BENCHMARK(BM_BtoonRoundTripMap);

static void BM_BtoonRoundTripDateTime(benchmark::State& state) {
    for (auto _ : state) {
        auto encoded = encode(test_datetime);
        auto decoded = decode(encoded);
        benchmark::DoNotOptimize(decoded);
    }
}
BENCHMARK(BM_BtoonRoundTripDateTime);

static void BM_BtoonRoundTripVectorFloat(benchmark::State& state) {
    for (auto _ : state) {
        auto encoded = encode(test_vector_float);
        auto decoded = decode(encoded);
        benchmark::DoNotOptimize(decoded);
    }
}
BENCHMARK(BM_BtoonRoundTripVectorFloat);

static void BM_BtoonRoundTripVectorDouble(benchmark::State& state) {
    for (auto _ : state) {
        auto encoded = encode(test_vector_double);
        auto decoded = decode(encoded);
        benchmark::DoNotOptimize(decoded);
    }
}
BENCHMARK(BM_BtoonRoundTripVectorDouble);

BENCHMARK_MAIN();
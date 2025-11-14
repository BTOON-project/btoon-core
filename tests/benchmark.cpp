#include <benchmark/benchmark.h>
#include "btoon/encoder.h"
#include "btoon/decoder.h"
#include <vector>
#include <map>
#include <string>

using namespace btoon;

// Test data
static const std::string test_string = "Hello, BTOON! This is a test string for benchmarking.";
static const std::vector<int64_t> test_array = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

static std::map<std::string, std::vector<uint8_t>> createTestMap() {
    Encoder enc;
    std::map<std::string, std::vector<uint8_t>> map;
    map["name"] = enc.encodeString("Alice");
    map["age"] = enc.encodeInt(30);
    map["active"] = enc.encodeBool(true);
    return map;
}

static const auto test_map = createTestMap();

// Encoding benchmarks
static void BM_BtoonEncodeString(benchmark::State& state) {
    Encoder encoder;
    for (auto _ : state) {
        benchmark::DoNotOptimize(encoder.encodeString(test_string));
    }
    state.SetBytesProcessed(state.iterations() * test_string.size());
}
BENCHMARK(BM_BtoonEncodeString);

static void BM_BtoonEncodeInt(benchmark::State& state) {
    Encoder encoder;
    for (auto _ : state) {
        benchmark::DoNotOptimize(encoder.encodeInt(42));
    }
    state.SetBytesProcessed(state.iterations() * sizeof(int64_t));
}
BENCHMARK(BM_BtoonEncodeInt);

static void BM_BtoonEncodeArray(benchmark::State& state) {
    Encoder encoder;
    std::vector<std::vector<uint8_t>> elements;
    for (int64_t val : test_array) {
        elements.push_back(encoder.encodeInt(val));
    }
    
    for (auto _ : state) {
        benchmark::DoNotOptimize(encoder.encodeArray(elements));
    }
    state.SetBytesProcessed(state.iterations() * test_array.size() * sizeof(int64_t));
}
BENCHMARK(BM_BtoonEncodeArray);

static void BM_BtoonEncodeMap(benchmark::State& state) {
    Encoder encoder;
    for (auto _ : state) {
        benchmark::DoNotOptimize(encoder.encodeMap(test_map));
    }
    state.SetBytesProcessed(state.iterations() * test_map.size());
}
BENCHMARK(BM_BtoonEncodeMap);

// Decoding benchmarks
static void BM_BtoonDecodeString(benchmark::State& state) {
    Encoder encoder;
    Decoder decoder;
    auto encoded = encoder.encodeString(test_string);
    
    for (auto _ : state) {
        benchmark::DoNotOptimize(decoder.decode(encoded));
    }
    state.SetBytesProcessed(state.iterations() * test_string.size());
}
BENCHMARK(BM_BtoonDecodeString);

static void BM_BtoonDecodeInt(benchmark::State& state) {
    Encoder encoder;
    Decoder decoder;
    auto encoded = encoder.encodeInt(42);
    
    for (auto _ : state) {
        benchmark::DoNotOptimize(decoder.decode(encoded));
    }
    state.SetBytesProcessed(state.iterations() * sizeof(int64_t));
}
BENCHMARK(BM_BtoonDecodeInt);

static void BM_BtoonDecodeArray(benchmark::State& state) {
    Encoder encoder;
    Decoder decoder;
    std::vector<std::vector<uint8_t>> elements;
    for (int64_t val : test_array) {
        elements.push_back(encoder.encodeInt(val));
    }
    auto encoded = encoder.encodeArray(elements);
    
    for (auto _ : state) {
        benchmark::DoNotOptimize(decoder.decode(encoded));
    }
    state.SetBytesProcessed(state.iterations() * test_array.size() * sizeof(int64_t));
}
BENCHMARK(BM_BtoonDecodeArray);

static void BM_BtoonDecodeMap(benchmark::State& state) {
    Encoder encoder;
    Decoder decoder;
    auto encoded = encoder.encodeMap(test_map);
    
    for (auto _ : state) {
        benchmark::DoNotOptimize(decoder.decode(encoded));
    }
    state.SetBytesProcessed(state.iterations() * test_map.size());
}
BENCHMARK(BM_BtoonDecodeMap);

// Round-trip benchmark
static void BM_BtoonRoundTrip(benchmark::State& state) {
    Encoder encoder;
    Decoder decoder;
    std::map<std::string, std::vector<uint8_t>> map = test_map;
    
    for (auto _ : state) {
        auto encoded = encoder.encodeMap(map);
        auto decoded = decoder.decode(encoded);
        benchmark::DoNotOptimize(decoded);
    }
}
BENCHMARK(BM_BtoonRoundTrip);

BENCHMARK_MAIN();

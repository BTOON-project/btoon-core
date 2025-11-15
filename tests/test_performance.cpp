#include <gtest/gtest.h>
#include "btoon/btoon.h"
#include "btoon/encoder.h"
#include "btoon/decoder.h"
#include "btoon/memory_pool.h"
#include <chrono>
#include <random>
#include <iomanip>

using namespace btoon;
using namespace std::chrono;

class PerformanceTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Generate test data
        small_string = "Hello, World!";
        medium_string = std::string(1024, 'X');  // 1KB string
        large_string = std::string(65536, 'Y');  // 64KB string
        
        // Generate binary data
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);
        
        small_binary.resize(128);
        medium_binary.resize(4096);
        large_binary.resize(65536);
        
        for (auto& vec : {&small_binary, &medium_binary, &large_binary}) {
            for (auto& byte : *vec) {
                byte = static_cast<uint8_t>(dis(gen));
            }
        }
        
        // Generate array data
        for (int i = 0; i < 1000; ++i) {
            int_array.push_back(Int(i * 12345));
            float_array.push_back(Float(i * 3.14159));
            string_array.push_back(String("Item " + std::to_string(i)));
        }
        
        // Generate tabular data
        for (int i = 0; i < 100; ++i) {
            tabular_data.push_back(Map{
                {"id", Int(i)},
                {"name", String("User" + std::to_string(i))},
                {"score", Float(i * 10.5)},
                {"active", Bool(i % 2 == 0)}
            });
        }
    }
    
    template<typename F>
    double measure_time_ms(F func, int iterations = 1000) {
        auto start = high_resolution_clock::now();
        for (int i = 0; i < iterations; ++i) {
            func();
        }
        auto end = high_resolution_clock::now();
        return duration_cast<microseconds>(end - start).count() / 1000.0;
    }
    
    std::string small_string, medium_string, large_string;
    std::vector<uint8_t> small_binary, medium_binary, large_binary;
    Array int_array, float_array, string_array, tabular_data;
};

TEST_F(PerformanceTest, StringEncoding) {
    std::cout << "\n=== String Encoding Performance ===\n";
    
    double small_time = measure_time_ms([&]() {
        auto encoded = encode(String(small_string));
    });
    std::cout << "Small string (13B): " << std::fixed << std::setprecision(3) 
              << small_time << " ms for 1000 iterations\n";
    
    double medium_time = measure_time_ms([&]() {
        auto encoded = encode(String(medium_string));
    });
    std::cout << "Medium string (1KB): " << medium_time << " ms for 1000 iterations\n";
    
    double large_time = measure_time_ms([&]() {
        auto encoded = encode(String(large_string));
    });
    std::cout << "Large string (64KB): " << large_time << " ms for 1000 iterations\n";
}

TEST_F(PerformanceTest, BinaryEncoding) {
    std::cout << "\n=== Binary Encoding Performance ===\n";
    
    double small_time = measure_time_ms([&]() {
        auto encoded = encode(Binary(small_binary));
    });
    std::cout << "Small binary (128B): " << std::fixed << std::setprecision(3)
              << small_time << " ms for 1000 iterations\n";
    
    double medium_time = measure_time_ms([&]() {
        auto encoded = encode(Binary(medium_binary));
    });
    std::cout << "Medium binary (4KB): " << medium_time << " ms for 1000 iterations\n";
    
    double large_time = measure_time_ms([&]() {
        auto encoded = encode(Binary(large_binary));
    });
    std::cout << "Large binary (64KB): " << large_time << " ms for 1000 iterations\n";
}

TEST_F(PerformanceTest, ArrayEncoding) {
    std::cout << "\n=== Array Encoding Performance ===\n";
    
    double int_time = measure_time_ms([&]() {
        auto encoded = encode(int_array);
    }, 100);
    std::cout << "Int array (1000 elements): " << std::fixed << std::setprecision(3)
              << int_time << " ms for 100 iterations\n";
    
    double float_time = measure_time_ms([&]() {
        auto encoded = encode(float_array);
    }, 100);
    std::cout << "Float array (1000 elements): " << float_time << " ms for 100 iterations\n";
    
    double string_time = measure_time_ms([&]() {
        auto encoded = encode(string_array);
    }, 100);
    std::cout << "String array (1000 elements): " << string_time << " ms for 100 iterations\n";
}

TEST_F(PerformanceTest, TabularEncoding) {
    std::cout << "\n=== Tabular Encoding Performance ===\n";
    
    EncodeOptions opts;
    opts.auto_tabular = true;
    
    double tabular_time = measure_time_ms([&]() {
        auto encoded = encode(tabular_data, opts);
    }, 100);
    std::cout << "Tabular data (100 rows x 4 columns): " << std::fixed << std::setprecision(3)
              << tabular_time << " ms for 100 iterations\n";
}

TEST_F(PerformanceTest, RoundTrip) {
    std::cout << "\n=== Round-trip Performance (Encode + Decode) ===\n";
    
    // Prepare a complex value
    Value complex_value = Map{
        {"users", tabular_data},
        {"metadata", Map{
            {"version", String("1.0.0")},
            {"timestamp", Int(1234567890)},
            {"large_data", Binary(large_binary)}
        }},
        {"scores", float_array}
    };
    
    double roundtrip_time = measure_time_ms([&]() {
        auto encoded = encode(complex_value);
        auto decoded = decode(encoded);
    }, 100);
    
    std::cout << "Complex structure round-trip: " << std::fixed << std::setprecision(3)
              << roundtrip_time << " ms for 100 iterations\n";
}

TEST_F(PerformanceTest, MemoryPoolEfficiency) {
    std::cout << "\n=== Memory Pool Performance ===\n";
    
    auto pool = std::make_unique<MemoryPool>();
    
    // Test allocation performance
    double alloc_time = measure_time_ms([&]() {
        for (int i = 0; i < 100; ++i) {
            void* ptr = pool->allocate(1024);
            pool->deallocate(ptr, 1024);
        }
    }, 100);
    
    std::cout << "100 allocations/deallocations (1KB each): " 
              << std::fixed << std::setprecision(3)
              << alloc_time << " ms for 100 iterations\n";
    
    // Prepare a complex value for encoder test
    Value test_value = Map{
        {"data", string_array},
        {"binary", Binary(large_binary)}
    };
    
    // Test with encoder using pool
    Encoder encoder(pool.get());
    double encode_with_pool = measure_time_ms([&]() {
        encoder.encode(test_value);
    }, 100);
    
    std::cout << "Encoding with custom pool: " << encode_with_pool << " ms for 100 iterations\n";
    
    std::cout << "Total memory allocated by pool: " << pool->total_allocated() << " bytes\n";
    std::cout << "Current memory usage: " << pool->current_usage() << " bytes\n";
}

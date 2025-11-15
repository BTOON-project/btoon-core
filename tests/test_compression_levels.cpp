#include <gtest/gtest.h>
#include "btoon/btoon.h"
#include "btoon/compression.h"
#include <random>
#include <chrono>
#include <iomanip>

using namespace btoon;
using namespace std::chrono;

class CompressionLevelsTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Generate different types of test data
        
        // Highly compressible data (repeated patterns)
        highly_compressible.resize(10000, 'A');
        for (int i = 0; i < 10000; i += 100) {
            highly_compressible[i] = 'B';
        }
        
        // Random data (poorly compressible)
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);
        random_data.resize(10000);
        for (auto& byte : random_data) {
            byte = static_cast<uint8_t>(dis(gen));
        }
        
        // Real-world JSON-like data
        json_like_data = Map{
            {"id", Int(12345)},
            {"name", String("John Doe")},
            {"email", String("john.doe@example.com")},
            {"scores", Array{Float(95.5), Float(87.3), Float(92.1)}},
            {"metadata", Map{
                {"created", Int(1234567890)},
                {"updated", Int(1234567900)},
                {"version", String("0.0.1")}
            }}
        };
        
        // Tabular data
        for (int i = 0; i < 50; ++i) {
            tabular_data.push_back(Map{
                {"id", Int(i)},
                {"value", Float(i * 1.5)},
                {"status", Bool(i % 2 == 0)}
            });
        }
    }
    
    std::vector<uint8_t> highly_compressible;
    std::vector<uint8_t> random_data;
    Value json_like_data;
    Array tabular_data;
};

TEST_F(CompressionLevelsTest, PresetLevels) {
    std::cout << "\n=== Testing Compression Level Presets ===\n";
    
    Value test_value = Binary(highly_compressible);
    auto uncompressed = encode(test_value);
    std::cout << "Uncompressed size: " << uncompressed.size() << " bytes\n\n";
    
    // Test each preset level
    std::vector<CompressionLevel> levels = {
        CompressionLevel::FASTEST,
        CompressionLevel::FAST,
        CompressionLevel::BALANCED,
        CompressionLevel::HIGH,
        CompressionLevel::MAXIMUM
    };
    
    for (auto level : levels) {
        EncodeOptions opts;
        opts.compress = true;
        opts.compression_algorithm = CompressionAlgorithm::ZLIB;
        opts.compression_preset = level;
        
        auto start = high_resolution_clock::now();
        auto compressed = encode(test_value, opts);
        auto end = high_resolution_clock::now();
        
        auto duration = duration_cast<microseconds>(end - start).count();
        float ratio = (float)compressed.size() / uncompressed.size();
        
        std::cout << "Level ";
        switch(level) {
            case CompressionLevel::FASTEST: std::cout << "FASTEST"; break;
            case CompressionLevel::FAST: std::cout << "FAST   "; break;
            case CompressionLevel::BALANCED: std::cout << "BALANCED"; break;
            case CompressionLevel::HIGH: std::cout << "HIGH   "; break;
            case CompressionLevel::MAXIMUM: std::cout << "MAXIMUM"; break;
            default: std::cout << "UNKNOWN"; break;
        }
        std::cout << ": size=" << std::setw(6) << compressed.size() 
                  << " bytes, ratio=" << std::fixed << std::setprecision(2) << ratio
                  << ", time=" << std::setw(6) << duration << " μs\n";
                  
        // Verify decompression works
        auto decoded = decode(compressed);
        EXPECT_EQ(std::get<Binary>(test_value), std::get<Binary>(decoded));
    }
}

TEST_F(CompressionLevelsTest, CompressionProfiles) {
    std::cout << "\n=== Testing Compression Profiles ===\n";
    
    struct ProfileTest {
        std::string name;
        CompressionProfile profile;
    };
    
    std::vector<ProfileTest> profiles = {
        {"Realtime", CompressionProfile::realtime()},
        {"Network", CompressionProfile::network()},
        {"Storage", CompressionProfile::storage()},
        {"Streaming", CompressionProfile::streaming()}
    };
    
    Value test_value = json_like_data;
    auto uncompressed = encode(test_value);
    
    for (const auto& pt : profiles) {
        EncodeOptions opts;
        opts.compress = true;
        opts.use_profile = true;
        opts.compression_profile = pt.profile;
        
        auto start = high_resolution_clock::now();
        auto compressed = encode(test_value, opts);
        auto end = high_resolution_clock::now();
        
        auto duration = duration_cast<microseconds>(end - start).count();
        float ratio = (float)compressed.size() / uncompressed.size();
        
        std::cout << std::setw(10) << pt.name << " profile: "
                  << "algo=" << static_cast<int>(pt.profile.algorithm)
                  << ", size=" << compressed.size() 
                  << ", ratio=" << std::fixed << std::setprecision(2) << ratio
                  << ", time=" << duration << " μs\n";
                  
        // Verify decompression
        auto decoded = decode(compressed);
        EXPECT_EQ(test_value, decoded);
    }
}

TEST_F(CompressionLevelsTest, AdaptiveCompression) {
    std::cout << "\n=== Testing Adaptive Compression ===\n";
    
    struct TestCase {
        std::string name;
        Value data;
    };
    
    std::vector<TestCase> test_cases = {
        {"Highly compressible", Binary(highly_compressible)},
        {"Random data", Binary(random_data)},
        {"JSON-like", json_like_data},
        {"Tabular", tabular_data}
    };
    
    for (const auto& tc : test_cases) {
        EncodeOptions opts;
        opts.compress = true;
        opts.adaptive_compression = true;
        
        auto uncompressed = encode(tc.data);
        auto compressed = encode(tc.data, opts);
        
        float ratio = (float)compressed.size() / uncompressed.size();
        
        std::cout << std::setw(20) << tc.name << ": "
                  << "original=" << std::setw(6) << uncompressed.size() << " bytes, "
                  << "compressed=" << std::setw(6) << compressed.size() << " bytes, "
                  << "ratio=" << std::fixed << std::setprecision(2) << ratio << "\n";
                  
        // Verify decompression
        auto decoded = decode(compressed);
        EXPECT_EQ(tc.data, decoded);
    }
}

TEST_F(CompressionLevelsTest, MinimumSizeThreshold) {
    std::cout << "\n=== Testing Minimum Size Threshold ===\n";
    
    // Small data that shouldn't be compressed
    Value small_value = String("Hello");
    
    EncodeOptions opts;
    opts.compress = true;
    opts.compression_algorithm = CompressionAlgorithm::ZLIB;
    opts.min_compression_size = 100;  // Set threshold above data size
    
    auto encoded = encode(small_value, opts);
    
    // Should not have compression header (16 bytes header + some data)
    auto uncompressed_small = encode(small_value);
    EXPECT_LE(encoded.size(), uncompressed_small.size() + 4);  // Allow small overhead
    std::cout << "Small data (below threshold) size: " << encoded.size() << " bytes\n";
    
    // Large data that should be compressed
    Value large_value = Binary(highly_compressible);
    opts.min_compression_size = 100;  // Set threshold below data size
    
    auto encoded_large = encode(large_value, opts);
    auto uncompressed_large = encode(large_value);
    
    // Should be smaller than uncompressed (compression worked)
    EXPECT_LT(encoded_large.size(), uncompressed_large.size());
    std::cout << "Large data (above threshold) size: " << encoded_large.size() 
              << " bytes (original: " << uncompressed_large.size() << " bytes)\n";
    
    // Verify both decode correctly
    EXPECT_EQ(small_value, decode(encoded));
    EXPECT_EQ(large_value, decode(encoded_large));
}

#ifdef BTOON_WITH_LZ4
TEST_F(CompressionLevelsTest, LZ4Levels) {
    std::cout << "\n=== Testing LZ4 Compression Levels ===\n";
    
    std::vector<uint8_t> test_data = highly_compressible;
    
    // Test LZ4 fast mode
    auto fast_compressed = compress_lz4(test_data, 0);
    std::cout << "LZ4 Fast: " << fast_compressed.size() << " bytes\n";
    
    // Test LZ4HC with different levels
    for (int level = 4; level <= 12; level += 4) {
        auto compressed = compress_lz4(test_data, level);
        std::cout << "LZ4HC Level " << level << ": " << compressed.size() << " bytes\n";
        
        // Verify decompression
        auto decompressed = decompress_lz4(compressed);
        EXPECT_EQ(test_data.size(), decompressed.size());
        EXPECT_EQ(test_data, decompressed);
    }
}
#endif

#ifdef BTOON_WITH_ZSTD
TEST_F(CompressionLevelsTest, ZSTDLevels) {
    std::cout << "\n=== Testing ZSTD Compression Levels ===\n";
    
    std::vector<uint8_t> test_data = highly_compressible;
    
    // Test various ZSTD levels
    std::vector<int> levels = {1, 3, 5, 9, 15, 19};
    
    for (int level : levels) {
        auto compressed = compress_zstd(test_data, level);
        std::cout << "ZSTD Level " << std::setw(2) << level << ": " 
                  << compressed.size() << " bytes\n";
        
        // Verify decompression
        auto decompressed = decompress_zstd(compressed);
        EXPECT_EQ(test_data.size(), decompressed.size());
        EXPECT_EQ(test_data, decompressed);
    }
}
#endif

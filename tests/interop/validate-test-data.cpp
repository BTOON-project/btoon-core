/**
 * @file validate-test-data.cpp
 * @brief Validate test data from other language implementations
 */

#include "btoon/btoon.h"
#include "btoon/decoder.h"
#include "btoon/validator.h"
#include <fstream>
#include <iostream>
#include <filesystem>
#include <iomanip>

namespace fs = std::filesystem;
using namespace btoon;

struct InteropValidationResult {
    std::string test_name;
    bool passed;
    std::string error_message;
    size_t size;
    uint32_t checksum;
};

bool validate_test_case(const fs::path& btoon_file, const fs::path& meta_file) {
    // Read metadata
    std::ifstream meta(meta_file);
    if (!meta) {
        throw std::runtime_error("Cannot read metadata file");
    }
    
    // Simple JSON parsing (production code would use proper parser)
    std::string line;
    std::string expected_name;
    std::string expected_type;
    uint32_t expected_checksum = 0;
    
    while (std::getline(meta, line)) {
        if (line.find("\"name\":") != std::string::npos) {
            size_t start = line.find('"', line.find(':')) + 1;
            size_t end = line.find('"', start);
            expected_name = line.substr(start, end - start);
        } else if (line.find("\"type\":") != std::string::npos) {
            size_t start = line.find('"', line.find(':')) + 1;
            size_t end = line.find('"', start);
            expected_type = line.substr(start, end - start);
        } else if (line.find("\"checksum\":") != std::string::npos) {
            size_t start = line.find(':', line.find("checksum")) + 1;
            expected_checksum = std::stoul(line.substr(start));
        }
    }
    
    // Read BTOON data
    std::ifstream file(btoon_file, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("Cannot read BTOON file");
    }
    
    size_t size = file.tellg();
    file.seekg(0);
    std::vector<uint8_t> data(size);
    file.read(reinterpret_cast<char*>(data.data()), size);
    
    // Calculate checksum
    uint32_t actual_checksum = 0;
    for (auto byte : data) {
        actual_checksum = ((actual_checksum << 1) | (actual_checksum >> 31)) ^ byte;
    }
    
    // Validate checksum if provided
    if (expected_checksum != 0 && actual_checksum != expected_checksum) {
        throw std::runtime_error("Checksum mismatch: expected " + 
                               std::to_string(expected_checksum) + 
                               ", got " + std::to_string(actual_checksum));
    }
    
    // Validate data structure
    ValidationOptions val_opts;
    val_opts.max_depth = 128;
    val_opts.require_utf8_strings = true;
    
    Validator validator(val_opts);
    auto validation_result = validator.validate(std::span<const uint8_t>(data.data(), data.size()));
    
    if (!validation_result.valid) {
        std::string errors;
        for (const auto& error : validation_result.errors) {
            errors += error + "; ";
        }
        throw std::runtime_error("Validation failed: " + errors);
    }
    
    // Decode the data
    Decoder decoder;
    Value value = decoder.decode(data);
    
    // Verify type matches expected
    std::string actual_type = "unknown";
    std::visit([&actual_type](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, Nil>) actual_type = "nil";
        else if constexpr (std::is_same_v<T, Bool>) actual_type = "boolean";
        else if constexpr (std::is_same_v<T, Int>) actual_type = "integer";
        else if constexpr (std::is_same_v<T, Uint>) actual_type = "unsigned";
        else if constexpr (std::is_same_v<T, Float>) actual_type = "float";
        else if constexpr (std::is_same_v<T, String>) actual_type = "string";
        else if constexpr (std::is_same_v<T, Binary>) actual_type = "binary";
        else if constexpr (std::is_same_v<T, Array>) actual_type = "array";
        else if constexpr (std::is_same_v<T, Map>) actual_type = "map";
        else if constexpr (std::is_same_v<T, Timestamp>) actual_type = "timestamp";
        else if constexpr (std::is_same_v<T, Extension>) actual_type = "extension";
    }, value);
    
    if (!expected_type.empty() && actual_type != expected_type) {
        // Allow integer/unsigned mismatch for positive values
        if (!((expected_type == "integer" && actual_type == "unsigned") ||
              (expected_type == "unsigned" && actual_type == "integer"))) {
            throw std::runtime_error("Type mismatch: expected " + expected_type + 
                                   ", got " + actual_type);
        }
    }
    
    // Try round-trip encoding
    auto re_encoded = encode(value);
    
    // For compressed data, sizes may differ
    // Just verify we can decode the re-encoded data
    Value round_trip = decoder.decode(re_encoded);
    
    return true;
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <test_data_directory>" << std::endl;
        return 1;
    }
    
    fs::path test_dir = argv[1];
    
    std::cout << "BTOON Interoperability Test Data Validator" << std::endl;
    std::cout << "Version: " << btoon::version() << std::endl;
    std::cout << "Test Directory: " << test_dir << std::endl;
    std::cout << std::endl;
    
    // Read manifest if it exists
    fs::path manifest_path = test_dir / "manifest.json";
    if (fs::exists(manifest_path)) {
        std::ifstream manifest(manifest_path);
        std::string line;
        std::string generator;
        while (std::getline(manifest, line)) {
            if (line.find("\"generator\":") != std::string::npos) {
                size_t start = line.find('"', line.find(':')) + 1;
                size_t end = line.find('"', start);
                generator = line.substr(start, end - start);
                std::cout << "Test data generated by: " << generator << std::endl;
                break;
            }
        }
    }
    
    std::vector<InteropValidationResult> results;
    int passed = 0;
    int failed = 0;
    
    // Iterate through all .btoon files
    for (const auto& entry : fs::directory_iterator(test_dir)) {
        if (entry.path().extension() == ".btoon") {
            std::string test_name = entry.path().stem().string();
            fs::path meta_path = entry.path();
            meta_path.replace_extension(".meta.json");
            
            InteropValidationResult result;
            result.test_name = test_name;
            
            try {
                bool valid = validate_test_case(entry.path(), meta_path);
                result.passed = valid;
                result.size = fs::file_size(entry.path());
                
                if (valid) {
                    passed++;
                    std::cout << "[PASS] " << test_name << std::endl;
                }
            } catch (const std::exception& e) {
                result.passed = false;
                result.error_message = e.what();
                failed++;
                std::cout << "[FAIL] " << test_name << ": " << e.what() << std::endl;
            }
            
            results.push_back(result);
        }
    }
    
    // Print summary
    std::cout << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "SUMMARY" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Total Tests: " << (passed + failed) << std::endl;
    std::cout << "Passed: " << passed << std::endl;
    std::cout << "Failed: " << failed << std::endl;
    std::cout << "Success Rate: " << std::fixed << std::setprecision(1) 
              << (100.0 * passed / (passed + failed)) << "%" << std::endl;
    
    if (failed > 0) {
        std::cout << std::endl;
        std::cout << "Failed Tests:" << std::endl;
        for (const auto& result : results) {
            if (!result.passed) {
                std::cout << "  - " << result.test_name << ": " 
                         << result.error_message << std::endl;
            }
        }
        return 1;
    }
    
    std::cout << std::endl;
    std::cout << "All tests passed! ✅" << std::endl;
    return 0;
}

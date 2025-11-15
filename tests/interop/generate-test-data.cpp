/**
 * @file generate-test-data.cpp
 * @brief Generate comprehensive test data for cross-language interoperability
 */

#include "btoon/btoon.h"
#include "btoon/schema.h"
#include "btoon/compression.h"
#include <fstream>
#include <iostream>
#include <filesystem>
#include <vector>
#include <limits>
#include <cmath>

namespace fs = std::filesystem;
using namespace btoon;

struct InteropTestCase {
    std::string name;
    std::string description;
    Value value;
    EncodeOptions options;
    std::string expected_schema;
};

std::vector<InteropTestCase> generate_test_cases() {
    std::vector<InteropTestCase> cases;
    
    // Basic types
    cases.push_back({
        "nil", "Nil value",
        Nil{}, {}, ""
    });
    
    cases.push_back({
        "bool_true", "Boolean true",
        Bool(true), {}, ""
    });
    
    cases.push_back({
        "bool_false", "Boolean false",
        Bool(false), {}, ""
    });
    
    // Numbers - edge cases
    cases.push_back({
        "int_zero", "Integer zero",
        Int(0), {}, ""
    });
    
    cases.push_back({
        "int_positive_fixint", "Positive fixint",
        Int(42), {}, ""
    });
    
    cases.push_back({
        "int_negative_fixint", "Negative fixint",
        Int(-17), {}, ""
    });
    
    cases.push_back({
        "int_max", "Maximum int64",
        Int(std::numeric_limits<int64_t>::max()), {}, ""
    });
    
    cases.push_back({
        "int_min", "Minimum int64",
        Int(std::numeric_limits<int64_t>::min()), {}, ""
    });
    
    cases.push_back({
        "uint_max", "Maximum uint64",
        Uint(std::numeric_limits<uint64_t>::max()), {}, ""
    });
    
    cases.push_back({
        "float_pi", "Float PI",
        Float(3.141592653589793), {}, ""
    });
    
    cases.push_back({
        "float_nan", "Float NaN",
        Float(std::nan("")), {}, ""
    });
    
    cases.push_back({
        "float_inf", "Float infinity",
        Float(std::numeric_limits<double>::infinity()), {}, ""
    });
    
    cases.push_back({
        "float_neg_inf", "Float negative infinity",
        Float(-std::numeric_limits<double>::infinity()), {}, ""
    });
    
    // Strings
    cases.push_back({
        "string_empty", "Empty string",
        String(""), {}, ""
    });
    
    cases.push_back({
        "string_ascii", "ASCII string",
        String("Hello, BTOON!"), {}, ""
    });
    
    cases.push_back({
        "string_unicode", "Unicode string",
        String("Hello 世界 🌍 مرحبا"), {}, ""
    });
    
    cases.push_back({
        "string_long", "Long string (1KB)",
        String(std::string(1024, 'a')), {}, ""
    });
    
    // Binary
    cases.push_back({
        "binary_empty", "Empty binary",
        Binary{}, {}, ""
    });
    
    cases.push_back({
        "binary_small", "Small binary",
        Binary{0x00, 0xFF, 0x42, 0x13, 0x37}, {}, ""
    });
    
    // Arrays
    cases.push_back({
        "array_empty", "Empty array",
        Array{}, {}, ""
    });
    
    cases.push_back({
        "array_homogeneous", "Homogeneous array",
        Array{Int(1), Int(2), Int(3), Int(4), Int(5)}, {}, ""
    });
    
    cases.push_back({
        "array_heterogeneous", "Heterogeneous array",
        Array{
            Nil{},
            Bool(true),
            Int(42),
            Float(3.14),
            String("test"),
            Binary{0x01, 0x02}
        }, {}, ""
    });
    
    cases.push_back({
        "array_nested", "Nested arrays",
        Array{
            Array{Int(1), Int(2)},
            Array{Int(3), Int(4)},
            Array{Array{Int(5), Int(6)}}
        }, {}, ""
    });
    
    // Maps
    cases.push_back({
        "map_empty", "Empty map",
        Map{}, {}, ""
    });
    
    cases.push_back({
        "map_simple", "Simple map",
        Map{
            {"name", String("Alice")},
            {"age", Int(30)},
            {"active", Bool(true)}
        }, {}, ""
    });
    
    cases.push_back({
        "map_nested", "Nested map",
        Map{
            {"user", Map{
                {"id", Int(1)},
                {"profile", Map{
                    {"bio", String("Developer")},
                    {"tags", Array{String("cpp"), String("btoon")}}
                }}
            }}
        }, {}, ""
    });
    
    // Special keys in maps
    cases.push_back({
        "map_special_keys", "Map with special keys",
        Map{
            {"", String("empty key")},
            {" ", String("space key")},
            {"with spaces", String("spaces in key")},
            {"unicode_κλειδί", String("unicode key")},
            {"emoji_🔑", String("emoji key")}
        }, {}, ""
    });
    
    // Large structures
    Map large_map;
    for (int i = 0; i < 1000; i++) {
        large_map["key_" + std::to_string(i)] = Int(i);
    }
    cases.push_back({
        "map_large", "Large map (1000 entries)",
        large_map, {}, ""
    });
    
    Array large_array;
    for (int i = 0; i < 1000; i++) {
        large_array.push_back(Map{
            {"id", Int(i)},
            {"value", Float(i * 1.5)},
            {"name", String("item_" + std::to_string(i))}
        });
    }
    cases.push_back({
        "array_large", "Large array (1000 items)",
        large_array, {}, ""
    });
    
    // Deeply nested structure
    Value deep = Int(42);
    for (int i = 0; i < 50; i++) {
        deep = Array{deep};
    }
    cases.push_back({
        "deep_nesting", "Deeply nested (50 levels)",
        deep, {}, ""
    });
    
    // Timestamp
    cases.push_back({
        "timestamp", "Timestamp",
        Timestamp{1234567890}, {}, ""
    });
    
    // Extension
    cases.push_back({
        "extension", "Custom extension",
        Extension{42, {0x01, 0x02, 0x03}}, {}, ""
    });
    
    // Compression variants
    EncodeOptions compressed_opts;
    compressed_opts.compress = true;
    compressed_opts.compression_algorithm = CompressionAlgorithm::ZLIB;
    
    cases.push_back({
        "compressed_zlib", "ZLIB compressed data",
        Map{
            {"data", String(std::string(10000, 'a'))},
            {"compressed", Bool(true)}
        },
        compressed_opts, ""
    });
    
    compressed_opts.compression_algorithm = CompressionAlgorithm::LZ4;
    cases.push_back({
        "compressed_lz4", "LZ4 compressed data",
        Map{
            {"data", String(std::string(10000, 'b'))},
            {"compressed", Bool(true)}
        },
        compressed_opts, ""
    });
    
    compressed_opts.compression_algorithm = CompressionAlgorithm::ZSTD;
    cases.push_back({
        "compressed_zstd", "ZSTD compressed data",
        Map{
            {"data", String(std::string(10000, 'c'))},
            {"compressed", Bool(true)}
        },
        compressed_opts, ""
    });
    
    // Tabular data
    Array tabular_data;
    for (int i = 0; i < 100; i++) {
        tabular_data.push_back(Map{
            {"id", Int(i)},
            {"name", String("row_" + std::to_string(i))},
            {"value", Float(i * 2.5)},
            {"active", Bool(i % 2 == 0)}
        });
    }
    
    EncodeOptions tabular_opts;
    tabular_opts.auto_tabular = true;
    
    cases.push_back({
        "tabular_data", "Tabular optimized data",
        tabular_data,
        tabular_opts, ""
    });
    
    return cases;
}

void write_test_case(const fs::path& output_dir, const InteropTestCase& test_case) {
    // Create output directory if it doesn't exist
    fs::create_directories(output_dir);
    
    // Encode the data
    auto encoded = encode(test_case.value, test_case.options);
    
    // Write binary file
    fs::path binary_path = output_dir / (test_case.name + ".btoon");
    std::ofstream binary_file(binary_path, std::ios::binary);
    binary_file.write(reinterpret_cast<const char*>(encoded.data()), encoded.size());
    binary_file.close();
    
    // Write metadata file
    fs::path meta_path = output_dir / (test_case.name + ".meta.json");
    std::ofstream meta_file(meta_path);
    meta_file << "{\n";
    meta_file << "  \"name\": \"" << test_case.name << "\",\n";
    meta_file << "  \"description\": \"" << test_case.description << "\",\n";
    meta_file << "  \"size\": " << encoded.size() << ",\n";
    meta_file << "  \"compressed\": " << (test_case.options.compress ? "true" : "false") << ",\n";
    
    // Add type information
    std::string type_name = "unknown";
    std::visit([&type_name](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, Nil>) type_name = "nil";
        else if constexpr (std::is_same_v<T, Bool>) type_name = "boolean";
        else if constexpr (std::is_same_v<T, Int>) type_name = "integer";
        else if constexpr (std::is_same_v<T, Uint>) type_name = "unsigned";
        else if constexpr (std::is_same_v<T, Float>) type_name = "float";
        else if constexpr (std::is_same_v<T, String>) type_name = "string";
        else if constexpr (std::is_same_v<T, Binary>) type_name = "binary";
        else if constexpr (std::is_same_v<T, Array>) type_name = "array";
        else if constexpr (std::is_same_v<T, Map>) type_name = "map";
        else if constexpr (std::is_same_v<T, Timestamp>) type_name = "timestamp";
        else if constexpr (std::is_same_v<T, Extension>) type_name = "extension";
    }, test_case.value);
    
    meta_file << "  \"type\": \"" << type_name << "\",\n";
    
    // Add checksum
    uint32_t checksum = 0;
    for (auto byte : encoded) {
        checksum = ((checksum << 1) | (checksum >> 31)) ^ byte;
    }
    meta_file << "  \"checksum\": " << checksum << "\n";
    meta_file << "}\n";
    meta_file.close();
    
    std::cout << "Generated: " << test_case.name << " (" << encoded.size() << " bytes)" << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <output_directory>" << std::endl;
        return 1;
    }
    
    fs::path output_dir = argv[1];
    
    std::cout << "BTOON Interoperability Test Data Generator" << std::endl;
    std::cout << "Version: " << btoon::version() << std::endl;
    std::cout << "Output: " << output_dir << std::endl;
    std::cout << std::endl;
    
    auto test_cases = generate_test_cases();
    std::cout << "Generating " << test_cases.size() << " test cases..." << std::endl;
    
    for (const auto& test_case : test_cases) {
        try {
            write_test_case(output_dir, test_case);
        } catch (const std::exception& e) {
            std::cerr << "Error generating " << test_case.name << ": " << e.what() << std::endl;
            return 1;
        }
    }
    
    // Generate manifest file
    fs::path manifest_path = output_dir / "manifest.json";
    std::ofstream manifest(manifest_path);
    manifest << "{\n";
    manifest << "  \"version\": \"" << btoon::version() << "\",\n";
    manifest << "  \"generator\": \"cpp\",\n";
    manifest << "  \"timestamp\": " << std::time(nullptr) << ",\n";
    manifest << "  \"test_cases\": [\n";
    
    for (size_t i = 0; i < test_cases.size(); i++) {
        manifest << "    \"" << test_cases[i].name << "\"";
        if (i < test_cases.size() - 1) {
            manifest << ",";
        }
        manifest << "\n";
    }
    
    manifest << "  ]\n";
    manifest << "}\n";
    manifest.close();
    
    std::cout << "\nSuccessfully generated " << test_cases.size() << " test cases" << std::endl;
    return 0;
}

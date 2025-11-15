#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <arpa/inet.h>
#endif

#include "btoon/btoon.h"
#include "btoon/encoder.h"
#include "btoon/decoder.h"
#include "btoon/compression.h"
#include <set>
#include <vector>
#include <algorithm>
#include <stdexcept>
#include <cstring>
#include <chrono>

namespace btoon {

namespace {
struct CompressionHeader {
    uint32_t magic;
    uint8_t version;
    uint8_t algorithm;
    uint16_t reserved;
    uint32_t compressed_size;
    uint32_t uncompressed_size;
};
const uint32_t BTOON_MAGIC = 0x42544F4E; // "BTON"
} // namespace

const char* Value::type_name() const {
    return std::visit([](auto&& arg) -> const char* {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, Nil>) return "nil";
        else if constexpr (std::is_same_v<T, Bool>) return "bool";
        else if constexpr (std::is_same_v<T, Int>) return "int";
        else if constexpr (std::is_same_v<T, Uint>) return "uint";
        else if constexpr (std::is_same_v<T, Float>) return "float";
        else if constexpr (std::is_same_v<T, String>) return "string";
        else if constexpr (std::is_same_v<T, Binary>) return "binary";
        else if constexpr (std::is_same_v<T, Array>) return "array";
        else if constexpr (std::is_same_v<T, Map>) return "map";
        else if constexpr (std::is_same_v<T, Extension>) return "extension";
        else if constexpr (std::is_same_v<T, Timestamp>) return "timestamp";
        else if constexpr (std::is_same_v<T, Date>) return "date";
        else if constexpr (std::is_same_v<T, DateTime>) return "datetime";
        else if constexpr (std::is_same_v<T, BigInt>) return "bigint";
        else if constexpr (std::is_same_v<T, VectorFloat>) return "vector_float";
        else if constexpr (std::is_same_v<T, VectorDouble>) return "vector_double";
        else return "unknown";
    }, *this);
}

std::vector<uint8_t> encode(const Value& value, const btoon::EncodeOptions& options) {
    Encoder encoder;
    encoder.setOptions(options);
    encoder.encode(value);
    auto encoded_data = encoder.getBuffer();
    std::vector<uint8_t> result(encoded_data.begin(), encoded_data.end());

    if (options.compress) {
        // Skip compression for small data
        if (result.size() < options.min_compression_size) {
            return result;
        }
        
        std::vector<uint8_t> compressed;
        
        if (options.use_profile) {
            // Use compression profile
            compressed = compress(options.compression_profile, result);
        } else if (options.adaptive_compression) {
            // Auto-select best algorithm
            CompressionAlgorithm algo = select_best_algorithm(result, 
                options.compression_level <= 2 || options.compression_preset == CompressionLevel::FAST);
            
            if (algo != CompressionAlgorithm::NONE) {
                int level = options.compression_level;
                if (level == 0 && options.compression_preset != CompressionLevel::CUSTOM) {
                    level = get_numeric_level(algo, options.compression_preset);
                }
                compressed = compress(algo, result, level);
            } else {
                return result; // No compression beneficial
            }
        } else {
            // Use specified algorithm and level
            CompressionAlgorithm algo = options.compression_algorithm;
            if (algo == CompressionAlgorithm::NONE) {
                return result;
            }
            
            int level = options.compression_level;
            if (level == 0 && options.compression_preset != CompressionLevel::CUSTOM) {
                level = get_numeric_level(algo, options.compression_preset);
            }
            
            compressed = compress(algo, result, level);
        }
        
        // Only use compressed if it's actually smaller
        if (compressed.size() < static_cast<size_t>(result.size() * 0.95)) { // 5% threshold
            // Add compression header
            CompressionHeader header;
            header.magic = htonl(BTOON_MAGIC);
            header.version = 1;
            
            // Store the actual algorithm used (not the original which might be AUTO)
            if (options.adaptive_compression) {
                CompressionAlgorithm actual_algo = select_best_algorithm(result, 
                    options.compression_level <= 2 || options.compression_preset == CompressionLevel::FAST);
                header.algorithm = static_cast<uint8_t>(actual_algo);
            } else if (options.use_profile && options.compression_profile.algorithm == CompressionAlgorithm::AUTO) {
                CompressionAlgorithm actual_algo = select_best_algorithm(result, options.compression_profile.numeric_level <= 3);
                header.algorithm = static_cast<uint8_t>(actual_algo);
            } else if (options.use_profile) {
                header.algorithm = static_cast<uint8_t>(options.compression_profile.algorithm);
            } else {
                header.algorithm = static_cast<uint8_t>(options.compression_algorithm);
            }
            
            header.reserved = 0;
            header.compressed_size = htonl(compressed.size());
            header.uncompressed_size = htonl(result.size());
            
            std::vector<uint8_t> final_result;
            final_result.reserve(sizeof(header) + compressed.size());
            final_result.insert(final_result.end(), 
                reinterpret_cast<uint8_t*>(&header),
                reinterpret_cast<uint8_t*>(&header) + sizeof(header));
            final_result.insert(final_result.end(), compressed.begin(), compressed.end());
            
            return final_result;
        }
    }

    return result;
}

Value decode(std::span<const uint8_t> data, const btoon::DecodeOptions& options) {
    std::span<const uint8_t> actual_data = data;
    std::vector<uint8_t> decompressed;
    
    if (options.auto_decompress && data.size() >= sizeof(CompressionHeader)) {
        // Check for compression header
        CompressionHeader header;
        std::memcpy(&header, data.data(), sizeof(header));
        header.magic = ntohl(header.magic);
        
        if (header.magic == BTOON_MAGIC) {
            header.compressed_size = ntohl(header.compressed_size);
            header.uncompressed_size = ntohl(header.uncompressed_size);
            
            // Validate header
            if (header.version == 1 && 
                header.compressed_size + sizeof(header) <= data.size()) {
                
                auto compressed_data = data.subspan(sizeof(header), header.compressed_size);
                CompressionAlgorithm algo = static_cast<CompressionAlgorithm>(header.algorithm);
                
                try {
                    decompressed = decompress(algo, compressed_data);
                    
                    // Validate decompressed size
                    if (decompressed.size() != header.uncompressed_size) {
                        throw BtoonException("Decompressed size mismatch");
                    }
                    
                    actual_data = decompressed;
                } catch (const std::exception& e) {
                    if (options.strict) {
                        throw;
                    }
                    // Fall back to treating as uncompressed if not strict
                }
            }
        }
    }

    Decoder decoder;
    return decoder.decode(actual_data);
}

bool is_tabular(const Array& arr) {
    if (arr.size() < 2) {
        return false;
    }

    const auto* first_row = std::get_if<Map>(&arr[0]);
    if (!first_row) {
        return false;
    }

    std::vector<std::string> column_names;
    for (const auto& [key, _] : *first_row) {
        column_names.push_back(key);
    }
    std::sort(column_names.begin(), column_names.end());

    for (size_t i = 1; i < arr.size(); ++i) {
        const auto* row = std::get_if<Map>(&arr[i]);
        if (!row || row->size() != column_names.size()) {
            return false;
        }
        std::vector<std::string> row_keys;
        for (const auto& [key, _] : *row) {
            row_keys.push_back(key);
        }
        std::sort(row_keys.begin(), row_keys.end());
        if (row_keys != column_names) {
            return false;
        }
    }
    return true;
}

const char* version() {
    return "0.0.1";
}

// Timestamp implementations
Timestamp Timestamp::now() {
    using namespace std::chrono;
    
    auto now = system_clock::now();
    auto duration = now.time_since_epoch();
    
    auto seconds = duration_cast<std::chrono::seconds>(duration);
    auto nanoseconds = duration_cast<std::chrono::nanoseconds>(duration - seconds);
    
    return Timestamp(seconds.count(), static_cast<uint32_t>(nanoseconds.count()));
}

Timestamp Timestamp::from_microseconds(int64_t micros) {
    int64_t sec = micros / 1000000;
    uint32_t nano = static_cast<uint32_t>((micros % 1000000) * 1000);
    return Timestamp(sec, nano);
}

Timestamp Timestamp::from_milliseconds(int64_t millis) {
    int64_t sec = millis / 1000;
    uint32_t nano = static_cast<uint32_t>((millis % 1000) * 1000000);
    return Timestamp(sec, nano);
}

} // namespace btoon
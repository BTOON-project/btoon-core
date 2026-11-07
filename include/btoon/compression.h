//  BTOON Core
//  https://btoon.net & https://github.com/BTOON-project/btoon-core
//
// SPDX-FileCopyrightText: 2025 Alvar Laigna <https://alvarlaigna.com>
// SPDX-License-Identifier: MIT
/**
 * @file compression.h
 * @brief Header file for BTOON data compression.
 */
#ifndef BTOON_COMPRESSION_H
#define BTOON_COMPRESSION_H

#include <cstdint>
#include <vector>
#include <span>

namespace btoon {

/**
 * @brief Enumeration of supported compression algorithms.
 */
enum class CompressionAlgorithm : uint8_t {
    NONE = 255,
    ZLIB = 0,
    LZ4 = 1,
    ZSTD = 2,
    BROTLI = 3,
    SNAPPY = 4,
    AUTO = 254  // Automatically select best algorithm
};

/**
 * @brief Compression level presets for easy configuration
 */
enum class CompressionLevel : int8_t {
    // Predefined levels
    FASTEST = 1,      // Maximum speed, minimal compression
    FAST = 2,         // Fast compression with reasonable ratio
    BALANCED = 3,     // Balance between speed and compression
    HIGH = 4,         // Higher compression, slower speed
    MAXIMUM = 5,      // Maximum compression, slowest speed
    
    // Special values
    DEFAULT = BALANCED,
    CUSTOM = -1       // Use custom numeric level
};

/**
 * @brief Compression profile for different use cases
 */
struct CompressionProfile {
    CompressionAlgorithm algorithm;
    int numeric_level;  // Algorithm-specific level
    size_t min_size;    // Minimum size to compress (bytes)
    bool adaptive;      // Enable adaptive compression
    
    CompressionProfile(CompressionAlgorithm algo = CompressionAlgorithm::ZLIB,
                      int level = 6,
                      size_t min = 256,
                      bool adapt = false)
        : algorithm(algo), numeric_level(level), min_size(min), adaptive(adapt) {}
    
    // Predefined profiles
    static CompressionProfile realtime();
    static CompressionProfile network();
    static CompressionProfile storage();
    static CompressionProfile streaming();
};

/**
 * @brief Compresses data using the specified algorithm.
 *
 * @param algorithm The compression algorithm to use.
 * @param data The binary data to compress.
 * @param level The compression level (algorithm-specific).
 * @return A vector of bytes representing the compressed data.
 */
std::vector<uint8_t> compress(CompressionAlgorithm algorithm, std::span<const uint8_t> data, int level = 0);

/**
 * @brief Compresses data using a compression level preset.
 *
 * @param algorithm The compression algorithm to use.
 * @param data The binary data to compress.
 * @param level The compression level preset.
 * @return A vector of bytes representing the compressed data.
 */
std::vector<uint8_t> compress(CompressionAlgorithm algorithm, std::span<const uint8_t> data, CompressionLevel level);

/**
 * @brief Compresses data using a compression profile.
 *
 * @param profile The compression profile to use.
 * @param data The binary data to compress.
 * @return A vector of bytes representing the compressed data.
 */
std::vector<uint8_t> compress(const CompressionProfile& profile, std::span<const uint8_t> data);

/**
 * @brief Decompresses a payload whose decompressed size is known exactly.
 *
 * This is what the compression envelope uses (docs/SPEC.md §6). Output never
 * exceeds `expected_size`; a payload that would produce more or less fails
 * with ErrorCode::invalid_envelope.
 */
std::vector<uint8_t> decompress(CompressionAlgorithm algorithm, std::span<const uint8_t> compressed_data,
                                size_t expected_size);

/**
 * @brief Decompresses data of unknown decompressed size, up to `max_size` bytes.
 *
 * LZ4 block payloads don't record their size and require the overload above.
 * @throws BtoonException with ErrorCode::size_limit if output exceeds max_size.
 */
std::vector<uint8_t> decompress(CompressionAlgorithm algorithm, std::span<const uint8_t> compressed_data);
std::vector<uint8_t> decompress_bounded(CompressionAlgorithm algorithm, std::span<const uint8_t> compressed_data,
                                        size_t max_size);

/// True if this build of the library supports `algorithm` (ZLIB always is).
bool compression_available(CompressionAlgorithm algorithm) noexcept;

/// Spec name of an algorithm ("zlib", "lz4", "zstd", "brotli", "snappy").
const char* compression_algorithm_name(CompressionAlgorithm algorithm) noexcept;

// --- Helper functions ---

/**
 * @brief Converts compression level preset to algorithm-specific numeric level.
 */
int get_numeric_level(CompressionAlgorithm algo, CompressionLevel level);

/**
 * @brief Selects the best compression algorithm based on data characteristics.
 */
CompressionAlgorithm select_best_algorithm(std::span<const uint8_t> data, bool prefer_speed = false);

/**
 * @brief Estimates compression ratio for given data and algorithm.
 */
float estimate_compression_ratio(CompressionAlgorithm algo, std::span<const uint8_t> data);


} // namespace btoon

#endif // BTOON_COMPRESSION_H

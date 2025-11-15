//  ██████╗ ████████╗ ██████╗  ██████╗ ███╗   ██╗
//  ██╔══██╗╚══██╔══╝██╔═══██╗██╔═══██╗████╗  ██║
//  ██████╔╝   ██║   ██║   ██║██║   ██║██╔██╗ ██║
//  ██╔══██╗   ██║   ██║   ██║██║   ██║██║╚██╗██║
//  ██████╔╝   ██║   ╚██████╔╝╚██████╔╝██║ ╚████║
//  ╚═════╝    ╚═╝    ╚═════╝  ╚═════╝ ╚═╝  ╚═══╝
//
//  BTOON Core
//  Version 0.0.1
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
    ZSTD = 2
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
 * @brief Decompresses data using the specified algorithm.
 *
 * @param algorithm The compression algorithm to use.
 * @param compressed_data The compressed binary data.
 * @return A vector of bytes representing the decompressed data.
 */
std::vector<uint8_t> decompress(CompressionAlgorithm algorithm, std::span<const uint8_t> compressed_data);

// --- Algorithm-specific implementations ---

std::vector<uint8_t> compress_zlib(std::span<const uint8_t> data, int level);
std::vector<uint8_t> decompress_zlib(std::span<const uint8_t> compressed_data);

#ifdef BTOON_WITH_LZ4
std::vector<uint8_t> compress_lz4(std::span<const uint8_t> data, int level);
std::vector<uint8_t> decompress_lz4(std::span<const uint8_t> compressed_data);
#endif

#ifdef BTOON_WITH_ZSTD
std::vector<uint8_t> compress_zstd(std::span<const uint8_t> data, int level);
std::vector<uint8_t> decompress_zstd(std::span<const uint8_t> compressed_data);
#endif

} // namespace btoon

#endif // BTOON_COMPRESSION_H

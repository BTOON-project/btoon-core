#include "btoon/compression.h"
#include "btoon/btoon.h"
#include <zlib.h>
#include <stdexcept>
#include <array>

#ifdef BTOON_WITH_LZ4
#include <lz4.h>
#include <lz4hc.h>
#endif

#ifdef BTOON_WITH_ZSTD
#include <zstd.h>
#endif

#ifdef BTOON_WITH_BROTLI
#include <brotli/encode.h>
#include <brotli/decode.h>
#endif

#ifdef BTOON_WITH_SNAPPY
#include <snappy.h>
#endif

#include <algorithm>
#include <cstring>

namespace btoon {

// --- Compression Profile Implementations ---

CompressionProfile CompressionProfile::realtime() {
    // For real-time applications: prioritize speed
#ifdef BTOON_WITH_LZ4
    return CompressionProfile(CompressionAlgorithm::LZ4, 1, 128, true);
#else
    return CompressionProfile(CompressionAlgorithm::ZLIB, 1, 128, true);
#endif
}

CompressionProfile CompressionProfile::network() {
    // For network transmission: balance speed and size
#ifdef BTOON_WITH_ZSTD
    return CompressionProfile(CompressionAlgorithm::ZSTD, 3, 256, true);
#else
    return CompressionProfile(CompressionAlgorithm::ZLIB, 6, 256, true);
#endif
}

CompressionProfile CompressionProfile::storage() {
    // For storage: prioritize compression ratio
#ifdef BTOON_WITH_ZSTD
    return CompressionProfile(CompressionAlgorithm::ZSTD, 9, 64, false);
#else
    return CompressionProfile(CompressionAlgorithm::ZLIB, 9, 64, false);
#endif
}

CompressionProfile CompressionProfile::streaming() {
    // For streaming: low latency with decent compression
#ifdef BTOON_WITH_LZ4
    return CompressionProfile(CompressionAlgorithm::LZ4, 0, 512, true);
#elif defined(BTOON_WITH_ZSTD)
    return CompressionProfile(CompressionAlgorithm::ZSTD, 1, 512, true);
#else
    return CompressionProfile(CompressionAlgorithm::ZLIB, 3, 512, true);
#endif
}

// --- Helper Functions ---

int get_numeric_level(CompressionAlgorithm algo, CompressionLevel level) {
    if (level == CompressionLevel::CUSTOM) {
        return 0; // Will use default
    }
    
    switch (algo) {
        case CompressionAlgorithm::ZLIB:
            switch (level) {
                case CompressionLevel::FASTEST: return 1;
                case CompressionLevel::FAST: return 3;
                case CompressionLevel::BALANCED: return 6;
                case CompressionLevel::HIGH: return 7;
                case CompressionLevel::MAXIMUM: return 9;
                default: return 6;
            }
            
        case CompressionAlgorithm::LZ4:
            switch (level) {
                case CompressionLevel::FASTEST: return 0;  // LZ4 fast
                case CompressionLevel::FAST: return 1;     // LZ4 default
                case CompressionLevel::BALANCED: return 4; // LZ4HC level 4
                case CompressionLevel::HIGH: return 9;     // LZ4HC level 9
                case CompressionLevel::MAXIMUM: return 12; // LZ4HC level 12
                default: return 1;
            }
            
#ifdef BTOON_WITH_ZSTD
        case CompressionAlgorithm::ZSTD:
            switch (level) {
                case CompressionLevel::FASTEST: return 1;
                case CompressionLevel::FAST: return 3;
                case CompressionLevel::BALANCED: return 5;
                case CompressionLevel::HIGH: return 9;
                case CompressionLevel::MAXIMUM: return 19; // ZSTD can go up to 22
                default: return 3;
            }
#endif

#ifdef BTOON_WITH_BROTLI
        case CompressionAlgorithm::BROTLI:
            switch (level) {
                case CompressionLevel::FASTEST: return 0;
                case CompressionLevel::FAST: return 2;
                case CompressionLevel::BALANCED: return 6;
                case CompressionLevel::HIGH: return 9;
                case CompressionLevel::MAXIMUM: return 11; // Brotli max
                default: return 6;
            }
#endif

        case CompressionAlgorithm::SNAPPY:
            // Snappy doesn't have compression levels
            return 0;
            
        default:
            return 0;
    }
}

CompressionAlgorithm select_best_algorithm(std::span<const uint8_t> data, bool prefer_speed) {
    // Simple heuristic-based selection
    size_t size = data.size();
    
    // For very small data, prefer no compression
    if (size < 128) {
        return CompressionAlgorithm::NONE;
    }
    
    // Check data entropy (simple check for compressibility)
    bool highly_compressible = false;
    if (size >= 256) {
        // Sample first 256 bytes for patterns
        std::array<int, 256> freq = {0};
        size_t sample_size = std::min(size_t(1024), size);
        for (size_t i = 0; i < sample_size; ++i) {
            freq[data[i]]++;
        }
        
        // Count unique bytes
        int unique_bytes = 0;
        for (int f : freq) {
            if (f > 0) unique_bytes++;
        }
        
        // If less than 50% unique bytes, data is likely highly compressible
        highly_compressible = (unique_bytes < 128);
    }
    
    if (prefer_speed) {
#ifdef BTOON_WITH_LZ4
        return CompressionAlgorithm::LZ4;
#elif defined(BTOON_WITH_ZSTD)
        return CompressionAlgorithm::ZSTD;
#else
        return CompressionAlgorithm::ZLIB;
#endif
    }
    
    if (highly_compressible) {
#ifdef BTOON_WITH_ZSTD
        return CompressionAlgorithm::ZSTD;
#else
        return CompressionAlgorithm::ZLIB;
#endif
    }
    
    // For moderately compressible data
#ifdef BTOON_WITH_LZ4
    if (size < 65536) {
        return CompressionAlgorithm::LZ4;
    }
#endif
    
#ifdef BTOON_WITH_ZSTD
    return CompressionAlgorithm::ZSTD;
#else
    return CompressionAlgorithm::ZLIB;
#endif
}

float estimate_compression_ratio(CompressionAlgorithm algo, std::span<const uint8_t> data) {
    // Quick estimation based on data characteristics
    // This is a simplified heuristic
    size_t size = data.size();
    if (size < 64) return 1.0f; // No compression for tiny data
    
    // Sample entropy
    std::array<int, 256> freq = {0};
    size_t sample_size = std::min(size_t(1024), size);
    for (size_t i = 0; i < sample_size; ++i) {
        freq[data[i]]++;
    }
    
    int unique_bytes = 0;
    for (int f : freq) {
        if (f > 0) unique_bytes++;
    }
    
    float entropy_factor = unique_bytes / 256.0f;
    
    // Rough estimates based on algorithm and entropy
    switch (algo) {
        case CompressionAlgorithm::NONE:
            return 1.0f;
            
        case CompressionAlgorithm::LZ4:
            return 0.7f + (0.25f * entropy_factor);
            
        case CompressionAlgorithm::ZLIB:
            return 0.5f + (0.4f * entropy_factor);
            
        case CompressionAlgorithm::ZSTD:
            return 0.45f + (0.45f * entropy_factor);
            
        case CompressionAlgorithm::BROTLI:
            return 0.4f + (0.5f * entropy_factor);  // Brotli typically achieves best compression
            
        case CompressionAlgorithm::SNAPPY:
            return 0.75f + (0.2f * entropy_factor);  // Snappy prioritizes speed over ratio
            
        default:
            return 1.0f;
    }
}

// --- Generic Dispatch Functions ---

std::vector<uint8_t> compress(CompressionAlgorithm algorithm, std::span<const uint8_t> data, CompressionLevel level) {
    return compress(algorithm, data, get_numeric_level(algorithm, level));
}

std::vector<uint8_t> compress(const CompressionProfile& profile, std::span<const uint8_t> data) {
    // Check minimum size threshold
    if (data.size() < profile.min_size) {
        return std::vector<uint8_t>(data.begin(), data.end());
    }
    
    CompressionAlgorithm algo = profile.algorithm;
    
    // Auto-select algorithm if requested
    if (algo == CompressionAlgorithm::AUTO || profile.adaptive) {
        algo = select_best_algorithm(data, profile.numeric_level <= 3);
    }
    
    if (algo == CompressionAlgorithm::NONE) {
        return std::vector<uint8_t>(data.begin(), data.end());
    }
    
    return compress(algo, data, profile.numeric_level);
}

std::vector<uint8_t> compress(CompressionAlgorithm algorithm, std::span<const uint8_t> data, int level) {
    switch (algorithm) {
        case CompressionAlgorithm::NONE:
            return std::vector<uint8_t>(data.begin(), data.end());
            
        case CompressionAlgorithm::ZLIB:
            return compress_zlib(data, level == 0 ? 6 : level);
            
#ifdef BTOON_WITH_LZ4
        case CompressionAlgorithm::LZ4:
            return compress_lz4(data, level);
#endif

#ifdef BTOON_WITH_ZSTD
        case CompressionAlgorithm::ZSTD:
            return compress_zstd(data, level);
#endif

#ifdef BTOON_WITH_BROTLI
        case CompressionAlgorithm::BROTLI:
            return compress_brotli(data, level == 0 ? 6 : level);
#endif

#ifdef BTOON_WITH_SNAPPY
        case CompressionAlgorithm::SNAPPY:
            return compress_snappy(data);
#endif
        
        case CompressionAlgorithm::AUTO:
            // AUTO should have been resolved to a specific algorithm before calling this
            return compress(select_best_algorithm(data, level <= 3), data, level);
            
        default:
            throw BtoonException("Unsupported compression algorithm");
    }
}

std::vector<uint8_t> decompress(CompressionAlgorithm algorithm, std::span<const uint8_t> compressed_data) {
    switch (algorithm) {
        case CompressionAlgorithm::NONE:
            return std::vector<uint8_t>(compressed_data.begin(), compressed_data.end());
            
        case CompressionAlgorithm::ZLIB:
            return decompress_zlib(compressed_data);
            
#ifdef BTOON_WITH_LZ4
        case CompressionAlgorithm::LZ4:
            return decompress_lz4(compressed_data);
#endif

#ifdef BTOON_WITH_ZSTD
        case CompressionAlgorithm::ZSTD:
            return decompress_zstd(compressed_data);
#endif

#ifdef BTOON_WITH_BROTLI
        case CompressionAlgorithm::BROTLI:
            return decompress_brotli(compressed_data);
#endif

#ifdef BTOON_WITH_SNAPPY
        case CompressionAlgorithm::SNAPPY:
            return decompress_snappy(compressed_data);
#endif
        
        case CompressionAlgorithm::AUTO:
            // AUTO should never appear in decompression (header should specify actual algorithm)
            throw BtoonException("AUTO compression algorithm cannot be used for decompression");
            
        default:
            throw BtoonException("Unsupported compression algorithm");
    }
}

// --- Zlib Implementation ---

std::vector<uint8_t> compress_zlib(std::span<const uint8_t> data, int level) {
    if (data.empty()) return {};

    z_stream strm;
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;

    if (deflateInit(&strm, level) != Z_OK) {
        throw BtoonException("Failed to initialize zlib compression");
    }

    std::vector<uint8_t> compressed;
    const size_t CHUNK = 16384;
    unsigned char out[CHUNK];

    strm.avail_in = data.size();
    strm.next_in = (Bytef*)data.data();

    do {
        strm.avail_out = CHUNK;
        strm.next_out = out;
        if (deflate(&strm, Z_FINISH) == Z_STREAM_ERROR) {
            deflateEnd(&strm);
            throw BtoonException("Zlib compression error");
        }
        size_t have = CHUNK - strm.avail_out;
        compressed.insert(compressed.end(), out, out + have);
    } while (strm.avail_out == 0);

    deflateEnd(&strm);
    return compressed;
}

std::vector<uint8_t> decompress_zlib(std::span<const uint8_t> compressed_data) {
    if (compressed_data.empty()) return {};

    z_stream strm;
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = 0;
    strm.next_in = Z_NULL;

    if (inflateInit(&strm) != Z_OK) {
        throw BtoonException("Failed to initialize zlib decompression");
    }

    std::vector<uint8_t> decompressed;
    const size_t CHUNK = 16384;
    unsigned char out[CHUNK];

    strm.avail_in = compressed_data.size();
    strm.next_in = (Bytef*)compressed_data.data();

    do {
        strm.avail_out = CHUNK;
        strm.next_out = out;
        int ret = inflate(&strm, Z_NO_FLUSH);
        if (ret == Z_STREAM_ERROR || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR) {
            inflateEnd(&strm);
            throw BtoonException("Zlib decompression error");
        }
        size_t have = CHUNK - strm.avail_out;
        decompressed.insert(decompressed.end(), out, out + have);
    } while (strm.avail_out == 0);

    inflateEnd(&strm);
    return decompressed;
}

// --- LZ4 Implementation ---

#ifdef BTOON_WITH_LZ4
std::vector<uint8_t> compress_lz4(std::span<const uint8_t> data, int level) {
    if (data.empty()) return {};
    
    size_t max_dst_size = LZ4_compressBound(data.size());
    std::vector<uint8_t> compressed(max_dst_size + sizeof(uint32_t)); // Extra space for original size
    
    // Store original size at the beginning for decompression
    uint32_t original_size = static_cast<uint32_t>(data.size());
    std::memcpy(compressed.data(), &original_size, sizeof(uint32_t));
    
    int compressed_size;
    
    if (level <= 1) {
        // Use fast compression
        compressed_size = LZ4_compress_default(
            (const char*)data.data(),
            (char*)(compressed.data() + sizeof(uint32_t)),
            data.size(),
            max_dst_size
        );
    } else {
        // Use high compression (LZ4HC)
        compressed_size = LZ4_compress_HC(
            (const char*)data.data(),
            (char*)(compressed.data() + sizeof(uint32_t)),
            data.size(),
            max_dst_size,
            std::min(level, 12)  // LZ4HC max level is 12
        );
    }

    if (compressed_size <= 0) {
        throw BtoonException("LZ4 compression failed");
    }
    compressed.resize(compressed_size + sizeof(uint32_t));
    return compressed;
}

std::vector<uint8_t> decompress_lz4(std::span<const uint8_t> compressed_data) {
    if (compressed_data.size() < sizeof(uint32_t)) {
        throw BtoonException("LZ4 compressed data too small");
    }
    
    // Read original size from the beginning
    uint32_t original_size;
    std::memcpy(&original_size, compressed_data.data(), sizeof(uint32_t));
    
    std::vector<uint8_t> decompressed(original_size);
    
    int decompressed_size = LZ4_decompress_safe(
        (const char*)(compressed_data.data() + sizeof(uint32_t)),
        (char*)decompressed.data(),
        compressed_data.size() - sizeof(uint32_t),
        original_size
    );
    
    if (decompressed_size != static_cast<int>(original_size)) {
        throw BtoonException("LZ4 decompression failed");
    }
    
    return decompressed;
}
#endif

// --- Zstd Implementation ---

#ifdef BTOON_WITH_ZSTD
std::vector<uint8_t> compress_zstd(std::span<const uint8_t> data, int level) {
    if (data.empty()) return {};

    size_t max_dst_size = ZSTD_compressBound(data.size());
    std::vector<uint8_t> compressed(max_dst_size);

    size_t compressed_size = ZSTD_compress(
        compressed.data(),
        max_dst_size,
        data.data(),
        data.size(),
        level == 0 ? 1 : level // ZSTD level 0 is invalid
    );

    if (ZSTD_isError(compressed_size)) {
        throw BtoonException("ZSTD compression failed: " + std::string(ZSTD_getErrorName(compressed_size)));
    }
    compressed.resize(compressed_size);
    return compressed;
}

std::vector<uint8_t> decompress_zstd(std::span<const uint8_t> compressed_data) {
    unsigned long long const decompressed_size = ZSTD_getFrameContentSize(compressed_data.data(), compressed_data.size());
    if (decompressed_size == ZSTD_CONTENTSIZE_ERROR || decompressed_size == ZSTD_CONTENTSIZE_UNKNOWN) {
        throw BtoonException("ZSTD decompression failed: unable to determine decompressed size.");
    }

    std::vector<uint8_t> decompressed(decompressed_size);
    
    size_t actual_size = ZSTD_decompress(
        decompressed.data(),
        decompressed_size,
        compressed_data.data(),
        compressed_data.size()
    );

    if (ZSTD_isError(actual_size) || actual_size != decompressed_size) {
        throw BtoonException("ZSTD decompression failed: " + std::string(ZSTD_getErrorName(actual_size)));
    }
    return decompressed;
}
#endif

#ifdef BTOON_WITH_BROTLI

std::vector<uint8_t> compress_brotli(std::span<const uint8_t> data, int level) {
    if (data.empty()) return {};
    
    // Clamp level to Brotli's valid range (0-11)
    level = std::max(0, std::min(level, 11));
    
    size_t encoded_size = BrotliEncoderMaxCompressedSize(data.size());
    std::vector<uint8_t> compressed(encoded_size);
    
    if (BrotliEncoderCompress(
            level,
            BROTLI_DEFAULT_WINDOW,
            BROTLI_DEFAULT_MODE,
            data.size(),
            data.data(),
            &encoded_size,
            compressed.data()) == BROTLI_FALSE) {
        throw BtoonException("Brotli compression failed");
    }
    
    compressed.resize(encoded_size);
    return compressed;
}

std::vector<uint8_t> decompress_brotli(std::span<const uint8_t> compressed_data) {
    if (compressed_data.empty()) return {};
    
    // Estimate decompressed size (Brotli doesn't provide an exact size function)
    // Start with 4x compressed size as initial guess
    size_t estimated_size = compressed_data.size() * 4;
    std::vector<uint8_t> decompressed;
    
    BrotliDecoderState* state = BrotliDecoderCreateInstance(nullptr, nullptr, nullptr);
    if (!state) {
        throw BtoonException("Failed to create Brotli decoder");
    }
    
    const uint8_t* next_in = compressed_data.data();
    size_t available_in = compressed_data.size();
    
    BrotliDecoderResult result;
    do {
        size_t old_size = decompressed.size();
        decompressed.resize(old_size + estimated_size);
        
        uint8_t* next_out = decompressed.data() + old_size;
        size_t available_out = estimated_size;
        
        result = BrotliDecoderDecompressStream(
            state,
            &available_in,
            &next_in,
            &available_out,
            &next_out,
            nullptr
        );
        
        // Resize to actual output size
        decompressed.resize(old_size + (estimated_size - available_out));
        
    } while (result == BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT);
    
    BrotliDecoderDestroyInstance(state);
    
    if (result != BROTLI_DECODER_RESULT_SUCCESS) {
        throw BtoonException("Brotli decompression failed");
    }
    
    return decompressed;
}

#endif // BTOON_WITH_BROTLI

#ifdef BTOON_WITH_SNAPPY

std::vector<uint8_t> compress_snappy(std::span<const uint8_t> data) {
    if (data.empty()) return {};
    
    std::string compressed;
    snappy::Compress(reinterpret_cast<const char*>(data.data()), 
                     data.size(), &compressed);
    
    return std::vector<uint8_t>(compressed.begin(), compressed.end());
}

std::vector<uint8_t> decompress_snappy(std::span<const uint8_t> compressed_data) {
    if (compressed_data.empty()) return {};
    
    size_t uncompressed_length;
    if (!snappy::GetUncompressedLength(
            reinterpret_cast<const char*>(compressed_data.data()),
            compressed_data.size(),
            &uncompressed_length)) {
        throw BtoonException("Invalid Snappy compressed data");
    }
    
    std::vector<uint8_t> decompressed(uncompressed_length);
    
    if (!snappy::RawUncompress(
            reinterpret_cast<const char*>(compressed_data.data()),
            compressed_data.size(),
            reinterpret_cast<char*>(decompressed.data()))) {
        throw BtoonException("Snappy decompression failed");
    }
    
    return decompressed;
}

#endif // BTOON_WITH_SNAPPY

} // namespace btoon

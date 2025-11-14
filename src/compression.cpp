#include "btoon/compression.h"
#include "btoon/btoon.h"
#include <zlib.h>
#include <stdexcept>

#ifdef BTOON_WITH_LZ4
#include <lz4.h>
#endif

#ifdef BTOON_WITH_ZSTD
#include <zstd.h>
#endif

namespace btoon {

// --- Generic Dispatch Functions ---

std::vector<uint8_t> compress(CompressionAlgorithm algorithm, std::span<const uint8_t> data, int level) {
    switch (algorithm) {
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
        default:
            throw BtoonException("Unsupported compression algorithm");
    }
}

std::vector<uint8_t> decompress(CompressionAlgorithm algorithm, std::span<const uint8_t> compressed_data) {
    switch (algorithm) {
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
    std::vector<uint8_t> compressed(max_dst_size);

    int compressed_size = LZ4_compress_default(
        (const char*)data.data(),
        (char*)compressed.data(),
        data.size(),
        max_dst_size
    );

    if (compressed_size <= 0) {
        throw BtoonException("LZ4 compression failed");
    }
    compressed.resize(compressed_size);
    return compressed;
}

std::vector<uint8_t> decompress_lz4(std::span<const uint8_t> compressed_data) {
    // LZ4 requires the original size to be known. This needs to be handled
    // by the BTOON compression header format. This is a placeholder.
    throw BtoonException("LZ4 decompression requires original size; not yet implemented in BTOON header.");
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

} // namespace btoon

//  ██████╗ ████████╗ ██████╗  ██████╗ ███╗   ██╗
//  ██╔══██╗╚══██╔══╝██╔═══██╗██╔═══██╗████╗  ██║
//  ██████╔╝   ██║   ██║   ██║██║   ██║██╔██╗ ██║
//  ██╔══██╗   ██║   ██║   ██║██║   ██║██║╚██╗██║
//  ██████╔╝   ██║   ╚██████╔╝╚██████╔╝██║ ╚████║
//  ╚═════╝    ╚═╝    ╚═════╝  ╚═════╝ ╚═╝  ╚═══╝
//
//  BTOON Core
//  https://btoon.net //  https://btoon.net //  https://btoon.net //  https://btoon.net //  https://btoon.net & https://github.com/BTOON-project/btoon-coren// https://github.com/BTOON-project/btoon-core https://github.com/BTOON-project/btoon-core https://github.com/BTOON-project/btoon-core https://github.com/BTOON-project/btoon-core
//
// SPDX-FileCopyrightText: 2025 Alvar Laigna <https://alvarlaigna.com>
// SPDX-License-Identifier: MIT

/**
 * @file utils.h
 * @brief Utility functions for BTOON: endianness, varint encoding, checksums
 */

 #ifndef BTOON_UTILS_H
 #define BTOON_UTILS_H
 
 #include "btoon.h"
 #include <cstdint>
 #include <cstring>
 #include <vector>
 #include <array>
#include <span>
 
 // Platform-specific includes for endianness
 #if defined(_WIN32) || defined(_WIN64)
     #include <intrin.h>
 #elif defined(__APPLE__)
     #include <libkern/OSByteOrder.h>
 #elif defined(__linux__) || defined(__unix__)
     #include <endian.h>
 #endif
 
 namespace btoon {
 namespace utils {
 
 /**
  * @brief Convert host byte order to big-endian (network byte order)
  */
 inline uint16_t hton16(uint16_t value) {
 #if defined(_WIN32) || defined(_WIN64)
     return _byteswap_ushort(value);
 #elif defined(__APPLE__)
     return OSSwapInt16(value);
 #elif defined(__linux__) || defined(__unix__)
     return htobe16(value);
 #else
     // Fallback: manual byte swap
     return ((value & 0xFF00) >> 8) | ((value & 0x00FF) << 8);
 #endif
 }
 
 inline uint32_t hton32(uint32_t value) {
 #if defined(_WIN32) || defined(_WIN64)
     return _byteswap_ulong(value);
 #elif defined(__APPLE__)
     return OSSwapInt32(value);
 #elif defined(__linux__) || defined(__unix__)
     return htobe32(value);
 #else
     // Fallback: manual byte swap
     return ((value & 0xFF000000) >> 24) |
           ((value & 0x00FF0000) >> 8) |
           ((value & 0x0000FF00) << 8) |
           ((value & 0x000000FF) << 24);
#endif
}

inline uint64_t hton64(uint64_t value) {
#if defined(_WIN32) || defined(_WIN64)
    return _byteswap_uint64(value);
#elif defined(__APPLE__)
    return OSSwapInt64(value);
#elif defined(__linux__) || defined(__unix__)
    return htobe64(value);
#else
    // Fallback: manual byte swap
    return ((value & 0xFF00000000000000ULL) >> 56) |
           ((value & 0x00FF000000000000ULL) >> 40) |
           ((value & 0x0000FF0000000000ULL) >> 24) |
           ((value & 0x000000FF00000000ULL) >> 8) |
           ((value & 0x00000000FF000000ULL) << 8) |
           ((value & 0x0000000000FF0000ULL) << 24) |
           ((value & 0x000000000000FF00ULL) << 40) |
           ((value & 0x00000000000000FFULL) << 56);
#endif
}

/**
 * @brief Convert big-endian (network byte order) to host byte order
 */
inline uint16_t ntoh16(uint16_t value) {
    return hton16(value); // Same operation
}

inline uint32_t ntoh32(uint32_t value) {
    return hton32(value); // Same operation
}

inline uint64_t ntoh64(uint64_t value) {
    return hton64(value); // Same operation
}

/**
 * @brief Encode a variable-length integer (varint) into a buffer
 * @param value The integer value to encode
 * @param buffer Output buffer to write encoded bytes
 * @return Number of bytes written
 */
inline size_t encodeVarint(uint64_t value, std::vector<uint8_t>& buffer) {
    size_t start = buffer.size();
    
    while (value >= 0x80) {
        buffer.push_back(static_cast<uint8_t>(value | 0x80));
        value >>= 7;
    }
    buffer.push_back(static_cast<uint8_t>(value));
    
    return buffer.size() - start;
}

/**
 * @brief Decode a variable-length integer (varint) from a buffer
 * @param buffer Input buffer to read from
 * @param pos Current position (updated after reading)
 * @return Decoded integer value
 * @throws BtoonException if buffer is exhausted
 */
inline uint64_t decodeVarint(std::span<const uint8_t> buffer, size_t& pos) {
    uint64_t result = 0;
    int shift = 0;
    
    while (pos < buffer.size()) {
        uint8_t byte = buffer[pos++];
        // The tenth byte holds only bit 63. Anything above it would be silently lost.
        if (shift == 63 && (byte & 0x7E) != 0) {
            throw BtoonException(ErrorCode::invalid_argument, "Varint overflow");
        }
        result |= static_cast<uint64_t>(byte & 0x7F) << shift;

        if ((byte & 0x80) == 0) {
            return result;
        }

        shift += 7;
        if (shift > 63) {
            throw BtoonException(ErrorCode::invalid_argument, "Varint overflow");
        }
    }

    throw BtoonException(ErrorCode::truncated, "Unexpected end of buffer while decoding varint");
}

/**
 * @brief Simple CRC32 checksum (for basic integrity checking)
 */
inline constexpr std::array<uint32_t, 256> make_crc32_table() {
    // The reflected CRC-32 (IEEE 802.3) table, as zlib uses. Generated, not typed in:
    // the old literal table had two wrong entries.
    std::array<uint32_t, 256> table{};
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int k = 0; k < 8; ++k) c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        table[i] = c;
    }
    return table;
}

inline uint32_t crc32(std::span<const uint8_t> data) {
    static constexpr std::array<uint32_t, 256> crc_table = make_crc32_table();

    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < data.size(); ++i) {
        crc = crc_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

/**
 * @brief Bounds-checked buffer reader
 */
class BufferReader {
public:
    explicit BufferReader(std::span<const uint8_t> buffer) : buffer_(buffer), pos_(0) {}
    
    void skip(size_t n) {
        if (n > buffer_.size() - pos_) {
            throw BtoonException("Buffer overflow");
        }
        pos_ += n;
    }
    
    uint8_t readUint8() {
        if (pos_ >= buffer_.size()) {
            throw BtoonException("Buffer overflow");
        }
        return buffer_[pos_++];
    }
    
    uint16_t readUint16() {
        if (2 > buffer_.size() - pos_) {
            throw BtoonException("Buffer overflow");
        }
        uint16_t value;
        std::memcpy(&value, buffer_.data() + pos_, 2);
        pos_ += 2;
        return ntoh16(value);
    }
    
    uint32_t readUint32() {
        if (4 > buffer_.size() - pos_) {
            throw BtoonException("Buffer overflow");
        }
        uint32_t value;
        std::memcpy(&value, buffer_.data() + pos_, 4);
        pos_ += 4;
        return ntoh32(value);
    }
    
    uint64_t readUint64() {
        if (8 > buffer_.size() - pos_) {
            throw BtoonException("Buffer overflow");
        }
        uint64_t value;
        std::memcpy(&value, buffer_.data() + pos_, 8);
        pos_ += 8;
        return ntoh64(value);
    }
    
    std::span<const uint8_t> readBytes(size_t n) {
        if (n > buffer_.size() - pos_) {
            throw BtoonException("Buffer overflow");
        }
        auto result = buffer_.subspan(pos_, n);
        pos_ += n;
        return result;
    }
    
    size_t position() const { return pos_; }
    size_t remaining() const { return buffer_.size() - pos_; }
    bool eof() const { return pos_ >= buffer_.size(); }
    
private:
    std::span<const uint8_t> buffer_;
    size_t pos_;
};

} // namespace utils
} // namespace btoon

#endif // BTOON_UTILS_H


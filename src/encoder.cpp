#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <arpa/inet.h>
#endif

#include "btoon/encoder.h"
#include <algorithm>
#include <stdexcept>
#include <cstring>
#include <iostream>

#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#elif defined(__ARM_NEON)
#include <arm_neon.h>
#endif

namespace btoon {

// Define htonll if not available on this platform
#ifndef htonll
inline uint64_t htonll(uint64_t value) {
    // Check endianness
    static const int num = 42;
    if (*reinterpret_cast<const char*>(&num) == num) {
        // Little endian - swap bytes
        const uint32_t high_part = htonl(static_cast<uint32_t>(value >> 32));
        const uint32_t low_part = htonl(static_cast<uint32_t>(value & 0xFFFFFFFFLL));
        return (static_cast<uint64_t>(low_part) << 32) | high_part;
    } else {
        // Big endian - no swap needed
        return value;
    }
}
#endif

void Encoder::grow_buffer(size_t needed) {
    if (size_ + needed > capacity_) {
        size_t new_capacity = capacity_ == 0 ? 1024 : capacity_ * 2;
        if (new_capacity < size_ + needed) {
            new_capacity = size_ + needed;
        }
        uint8_t* new_buffer = static_cast<uint8_t*>(pool_->allocate(new_capacity));
        if (buffer_) {
            // Use SIMD-optimized copy for better performance
            simd_copy(new_buffer, buffer_, size_);
            pool_->deallocate(buffer_, capacity_);
        }
        buffer_ = new_buffer;
        capacity_ = new_capacity;
    }
}

Encoder::Encoder() : pool_(new MemoryPool()), owns_pool_(true) {}

Encoder::Encoder(MemoryPool* pool) : pool_(pool), owns_pool_(false) {}

Encoder::Encoder(const Security& security) : security_(&security), useSecurity_(true), pool_(new MemoryPool()), owns_pool_(true) {}

Encoder::Encoder(const Security& security, MemoryPool* pool) : security_(&security), useSecurity_(true), pool_(pool), owns_pool_(false) {}

Encoder::~Encoder() {
    if (owns_pool_) {
        delete pool_;
    }
}

std::span<const uint8_t> Encoder::getBuffer() {
    addSignatureIfEnabled();
    return {buffer_, size_};
}

void Encoder::encodeNil() {
    grow_buffer(1);
    buffer_[size_++] = 0xc0;
}

void Encoder::encodeBool(bool value) {
    grow_buffer(1);
    buffer_[size_++] = static_cast<uint8_t>(value ? 0xc3 : 0xc2);
}

void Encoder::encodeInt(int64_t value) {
    if (value >= -32 && value <= 127) {
        grow_buffer(1);
        buffer_[size_++] = static_cast<uint8_t>(value);
    } else if (value >= -128 && value <= 127) {
        grow_buffer(2);
        buffer_[size_++] = 0xd0;
        buffer_[size_++] = static_cast<uint8_t>(value);
    } else if (value >= -32768 && value <= 32767) {
        grow_buffer(3);
        buffer_[size_++] = 0xd1;
        int16_t val16 = static_cast<int16_t>(value);
        uint16_t be_val = htons(static_cast<uint16_t>(val16));
        std::memcpy(buffer_ + size_, &be_val, 2);
        size_ += 2;
    } else if (value >= -2147483648LL && value <= 2147483647LL) {
        grow_buffer(5);
        buffer_[size_++] = 0xd2;
        int32_t val32 = static_cast<int32_t>(value);
        uint32_t be_val = htonl(static_cast<uint32_t>(val32));
        std::memcpy(buffer_ + size_, &be_val, 4);
        size_ += 4;
    } else {
        grow_buffer(9);
        buffer_[size_++] = 0xd3;
        uint64_t be_val = htonll(static_cast<uint64_t>(value));
        std::memcpy(buffer_ + size_, &be_val, 8);
        size_ += 8;
    }
}

void Encoder::encodeUint(uint64_t value) {
    if (value <= 127) {
        grow_buffer(1);
        buffer_[size_++] = static_cast<uint8_t>(value);
    } else if (value <= 255) {
        grow_buffer(2);
        buffer_[size_++] = 0xcc;
        buffer_[size_++] = static_cast<uint8_t>(value);
    } else if (value <= 65535) {
        grow_buffer(3);
        buffer_[size_++] = 0xcd;
        uint16_t be_val = htons(static_cast<uint16_t>(value));
        std::memcpy(buffer_ + size_, &be_val, 2);
        size_ += 2;
    } else if (value <= 4294967295ULL) {
        grow_buffer(5);
        buffer_[size_++] = 0xce;
        uint32_t be_val = htonl(static_cast<uint32_t>(value));
        std::memcpy(buffer_ + size_, &be_val, 4);
        size_ += 4;
    } else {
        grow_buffer(9);
        buffer_[size_++] = 0xcf;
        uint64_t be_val = htonll(value);
        std::memcpy(buffer_ + size_, &be_val, 8);
        size_ += 8;
    }
}

void Encoder::encodeFloat(double value) {
    grow_buffer(9);
    buffer_[size_++] = 0xcb;
    // Convert double to uint64_t preserving bit pattern, then convert to big-endian
    uint64_t bits;
    std::memcpy(&bits, &value, 8);
    uint64_t be_bits = htonll(bits);
    std::memcpy(buffer_ + size_, &be_bits, 8);
    size_ += 8;
}

void Encoder::encodeString(const std::string& value) {
    size_t len = value.size();
    if (len <= 31) {
        grow_buffer(1 + len);
        buffer_[size_++] = static_cast<uint8_t>(0xa0 | len);
    } else if (len <= 255) {
        grow_buffer(2 + len);
        buffer_[size_++] = 0xd9;
        buffer_[size_++] = static_cast<uint8_t>(len);
    } else if (len <= 65535) {
        grow_buffer(3 + len);
        buffer_[size_++] = 0xda;
        uint16_t be_len = htons(static_cast<uint16_t>(len));
        std::memcpy(buffer_ + size_, &be_len, 2);
        size_ += 2;
    } else {
        grow_buffer(5 + len);
        buffer_[size_++] = 0xdb;
        uint32_t be_len = htonl(static_cast<uint32_t>(len));
        std::memcpy(buffer_ + size_, &be_len, 4);
        size_ += 4;
    }
    // Use SIMD copy for strings longer than 32 bytes
    if (len > 32) {
        simd_copy(buffer_ + size_, reinterpret_cast<const uint8_t*>(value.data()), len);
    } else {
        std::memcpy(buffer_ + size_, value.data(), len);
    }
    size_ += len;
}

void Encoder::encodeBinary(std::span<const uint8_t> value) {
    size_t len = value.size();
    if (len <= 255) {
        grow_buffer(2 + len);
        buffer_[size_++] = 0xc4;
        buffer_[size_++] = static_cast<uint8_t>(len);
    } else if (len <= 65535) {
        grow_buffer(3 + len);
        buffer_[size_++] = 0xc5;
        uint16_t be_len = htons(static_cast<uint16_t>(len));
        std::memcpy(buffer_ + size_, &be_len, 2);
        size_ += 2;
    } else {
        grow_buffer(5 + len);
        buffer_[size_++] = 0xc6;
        uint32_t be_len = htonl(static_cast<uint32_t>(len));
        std::memcpy(buffer_ + size_, &be_len, 4);
        size_ += 4;
    }
    // Use SIMD copy for binary data longer than 32 bytes
    if (len > 32) {
        simd_copy(buffer_ + size_, value.data(), len);
    } else {
        std::memcpy(buffer_ + size_, value.data(), len);
    }
    size_ += len;
}

void Encoder::encodeArray(const std::vector<std::vector<uint8_t>>& elements) {
    size_t len = elements.size();
    if (len <= 15) {
        grow_buffer(1);
        buffer_[size_++] = static_cast<uint8_t>(0x90 | len);
    } else if (len <= 65535) {
        grow_buffer(3);
        buffer_[size_++] = 0xdc;
        uint16_t be_len = htons(static_cast<uint16_t>(len));
        std::memcpy(buffer_ + size_, &be_len, 2);
        size_ += 2;
    } else {
        grow_buffer(5);
        buffer_[size_++] = 0xdd;
        uint32_t be_len = htonl(static_cast<uint32_t>(len));
        std::memcpy(buffer_ + size_, &be_len, 4);
        size_ += 4;
    }
    for (const auto& elem : elements) {
        grow_buffer(elem.size());
        std::memcpy(buffer_ + size_, elem.data(), elem.size());
        size_ += elem.size();
    }
}

void Encoder::encodeMap(const std::map<std::string, std::vector<uint8_t>>& pairs) {
    size_t len = pairs.size();
    if (len <= 15) {
        grow_buffer(1);
        buffer_[size_++] = static_cast<uint8_t>(0x80 | len);
    } else if (len <= 65535) {
        grow_buffer(3);
        buffer_[size_++] = 0xde;
        uint16_t be_len = htons(static_cast<uint16_t>(len));
        std::memcpy(buffer_ + size_, &be_len, 2);
        size_ += 2;
    } else {
        grow_buffer(5);
        buffer_[size_++] = 0xdf;
        uint32_t be_len = htonl(static_cast<uint32_t>(len));
        std::memcpy(buffer_ + size_, &be_len, 4);
        size_ += 4;
    }
    for (const auto& pair : pairs) {
        encodeString(pair.first);
        grow_buffer(pair.second.size());
        std::memcpy(buffer_ + size_, pair.second.data(), pair.second.size());
        size_ += pair.second.size();
    }
}

void Encoder::encodeTimestamp(const Timestamp& timestamp) {
    // Encode as extension type -1 (timestamp)
    // Format: 8 bytes seconds + 4 bytes nanoseconds + optional 2 bytes timezone
    
    size_t data_size = 8 + 4;  // seconds + nanoseconds
    if (timestamp.has_timezone) {
        data_size += 2;  // timezone offset
    }
    
    std::vector<uint8_t> data(data_size);
    
    // Encode seconds (big-endian)
    uint64_t sec_be = htonll(static_cast<uint64_t>(timestamp.seconds));
    std::memcpy(data.data(), &sec_be, 8);
    
    // Encode nanoseconds (big-endian)
    uint32_t nano_be = htonl(timestamp.nanoseconds);
    std::memcpy(data.data() + 8, &nano_be, 4);
    
    // Encode timezone if present (big-endian)
    if (timestamp.has_timezone) {
        uint16_t tz_be = htons(static_cast<uint16_t>(timestamp.timezone_offset));
        std::memcpy(data.data() + 12, &tz_be, 2);
    }
    
    encodeExtension(-1, data);
}

void Encoder::encodeDate(int64_t milliseconds) {
    encodeExtension(-1, std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&milliseconds), 8));
}

void Encoder::encodeDateTime(int64_t nanoseconds) {
    encodeExtension(-2, std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&nanoseconds), 8));
}

void Encoder::encodeBigInt(std::span<const uint8_t> bytes) {
    encodeExtension(0, bytes);
}

void Encoder::encodeVectorFloat(const VectorFloat& value) {
    encodeExtension(-3, std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(value.data.data()), value.data.size() * sizeof(float)));
}

void Encoder::encodeVectorDouble(const VectorDouble& value) {
    encodeExtension(-4, std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(value.data.data()), value.data.size() * sizeof(double)));
}

void Encoder::encodeExtension(int8_t type, std::span<const uint8_t> data) {
    size_t len = data.size();
    if (len == 1) {
        grow_buffer(2);
        buffer_[size_++] = 0xd4;
    } else if (len == 2) {
        grow_buffer(3);
        buffer_[size_++] = 0xd5;
    } else if (len == 4) {
        grow_buffer(5);
        buffer_[size_++] = 0xd6;
    } else if (len == 8) {
        grow_buffer(9);
        buffer_[size_++] = 0xd7;
    } else if (len == 16) {
        grow_buffer(17);
        buffer_[size_++] = 0xd8;
    } else if (len <= 255) {
        grow_buffer(2 + len);
        buffer_[size_++] = 0xc7;
        buffer_[size_++] = static_cast<uint8_t>(len);
    } else if (len <= 65535) {
        grow_buffer(3 + len);
        buffer_[size_++] = 0xc8;
        uint16_t be_len = htons(static_cast<uint16_t>(len));
        std::memcpy(buffer_ + size_, &be_len, 2);
        size_ += 2;
    } else {
        grow_buffer(5 + len);
        buffer_[size_++] = 0xc9;
        uint32_t be_len = htonl(static_cast<uint32_t>(len));
        std::memcpy(buffer_ + size_, &be_len, 4);
        size_ += 4;
    }
    buffer_[size_++] = static_cast<uint8_t>(type);
    std::memcpy(buffer_ + size_, data.data(), len);
    size_ += len;
}

void Encoder::encodeColumnar(const Array& data) {
    if (!is_tabular(data)) {
        std::vector<std::vector<uint8_t>> elements;
        for(const auto& v : data) {
            Encoder temp_encoder;
            temp_encoder.encode(v);
            auto buf = temp_encoder.getBuffer();
            elements.emplace_back(buf.begin(), buf.end());
        }
        encodeArray(elements);
        return;
    }

    const auto* first_row = std::get_if<Map>(&data[0]);
    std::vector<std::string> column_names;
    for (const auto& [key, _] : *first_row) {
        column_names.push_back(key);
    }
    std::sort(column_names.begin(), column_names.end());

    std::vector<uint8_t> schema_bytes;

    // version
    schema_bytes.push_back(0); schema_bytes.push_back(0); schema_bytes.push_back(0); schema_bytes.push_back(1);

    // num_columns
    uint32_t num_columns_val = htonl(column_names.size());
    schema_bytes.insert(schema_bytes.end(), reinterpret_cast<uint8_t*>(&num_columns_val), reinterpret_cast<uint8_t*>(&num_columns_val) + 4);

    // num_rows
    uint32_t num_rows_val = htonl(data.size());
    schema_bytes.insert(schema_bytes.end(), reinterpret_cast<uint8_t*>(&num_rows_val), reinterpret_cast<uint8_t*>(&num_rows_val) + 4);

    for (const auto& name : column_names) {
        uint32_t name_len = htonl(name.length());
        schema_bytes.insert(schema_bytes.end(), reinterpret_cast<uint8_t*>(&name_len), reinterpret_cast<uint8_t*>(&name_len) + 4);
        schema_bytes.insert(schema_bytes.end(), name.begin(), name.end());
    }

    for (const auto& name : column_names) {
        const auto& val = (*first_row).at(name);
        std::visit([&](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, Nil>) schema_bytes.push_back(0);
            else if constexpr (std::is_same_v<T, Bool>) schema_bytes.push_back(1);
            else if constexpr (std::is_same_v<T, Int>) schema_bytes.push_back(2);
            else if constexpr (std::is_same_v<T, Uint>) schema_bytes.push_back(3);
            else if constexpr (std::is_same_v<T, Float>) schema_bytes.push_back(4);
            else if constexpr (std::is_same_v<T, String>) schema_bytes.push_back(5);
            else schema_bytes.push_back(0); // Default to unknown
        }, val);
    }

    std::vector<uint8_t> columns_data_bytes;
    for (const auto& name : column_names) {
        std::vector<uint8_t> column_data;
        for (const auto& row_value : data) {
            const auto* row = std::get_if<Map>(&row_value);
            Encoder temp_encoder(pool_); // Uses the main pool, no security
            temp_encoder.encode((*row).at(name));
            auto buf = temp_encoder.getBuffer();
            column_data.insert(column_data.end(), buf.begin(), buf.end());
        }

        uint32_t column_data_size_val = htonl(column_data.size());
        columns_data_bytes.insert(columns_data_bytes.end(), reinterpret_cast<uint8_t*>(&column_data_size_val), reinterpret_cast<uint8_t*>(&column_data_size_val) + 4);
        columns_data_bytes.insert(columns_data_bytes.end(), column_data.begin(), column_data.end());
    }
    
    std::vector<uint8_t> combined_bytes;
    combined_bytes.insert(combined_bytes.end(), schema_bytes.begin(), schema_bytes.end());
    combined_bytes.insert(combined_bytes.end(), columns_data_bytes.begin(), columns_data_bytes.end());

    encodeExtension(-10, combined_bytes);
}

void Encoder::encode(const Value& value) {
    std::visit([this](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, Nil>) encodeNil();
        else if constexpr (std::is_same_v<T, Bool>) encodeBool(arg);
        else if constexpr (std::is_same_v<T, Int>) encodeInt(arg);
        else if constexpr (std::is_same_v<T, Uint>) encodeUint(arg);
        else if constexpr (std::is_same_v<T, Float>) encodeFloat(arg);
        else if constexpr (std::is_same_v<T, String>) encodeString(arg);
        else if constexpr (std::is_same_v<T, Binary>) encodeBinary(arg);
        else if constexpr (std::is_same_v<T, std::vector<Value>>) {
            if (options_.auto_tabular && is_tabular(arg)) {
                encodeColumnar(arg);
            } else {
                std::vector<std::vector<uint8_t>> elements;
                for (const auto& v : arg) {
                    Encoder temp_encoder;
                    temp_encoder.encode(v);
                    auto buf = temp_encoder.getBuffer();
                    elements.emplace_back(buf.begin(), buf.end());
                }
                encodeArray(elements);
            }
        }
        else if constexpr (std::is_same_v<T, std::map<String, Value>>) {
            std::map<std::string, std::vector<uint8_t>> pairs;
            for (const auto& [key, val] : arg) {
                Encoder temp_encoder;
                temp_encoder.encode(val);
                auto buf = temp_encoder.getBuffer();
                pairs[key] = std::vector<uint8_t>(buf.begin(), buf.end());
            }
            encodeMap(pairs);
        }
        else if constexpr (std::is_same_v<T, Extension>) encodeExtension(arg.type, arg.data);
        else if constexpr (std::is_same_v<T, Timestamp>) encodeTimestamp(arg);
        else if constexpr (std::is_same_v<T, Date>) encodeDate(arg.milliseconds);
        else if constexpr (std::is_same_v<T, DateTime>) encodeDateTime(arg.nanoseconds);
        else if constexpr (std::is_same_v<T, BigInt>) encodeBigInt(arg.bytes);
        else if constexpr (std::is_same_v<T, VectorFloat>) encodeVectorFloat(arg);
        else if constexpr (std::is_same_v<T, VectorDouble>) encodeVectorDouble(arg);
        else { throw BtoonException("Unsupported type for encoding"); }
    }, value);
}

void Encoder::addSignatureIfEnabled() {
    if (useSecurity_ && security_ != nullptr) {
        auto signature = security_->sign({buffer_, size_});
        uint8_t sigLen = static_cast<uint8_t>(signature.size());
        size_t new_size = 1 + sigLen + size_;
        uint8_t* new_buffer = static_cast<uint8_t*>(pool_->allocate(new_size));
        new_buffer[0] = sigLen;
        std::memcpy(new_buffer + 1, signature.data(), sigLen);
        std::memcpy(new_buffer + 1 + sigLen, buffer_, size_);
        pool_->deallocate(buffer_, capacity_);
        buffer_ = new_buffer;
        size_ = new_size;
        capacity_ = new_size;
    }
}

void Encoder::simd_copy(uint8_t* dst, const uint8_t* src, size_t size) const {
    size_t i = 0;
    
#if defined(__AVX2__)
    // AVX2 can process 32 bytes at a time
    const size_t simd_width = 32;
    size_t simd_iterations = size / simd_width;
    
    for (; i < simd_iterations * simd_width; i += simd_width) {
        __m256i data = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src + i));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + i), data);
    }
#elif defined(__SSE2__)
    // SSE2 can process 16 bytes at a time
    const size_t simd_width = 16;
    size_t simd_iterations = size / simd_width;
    
    for (; i < simd_iterations * simd_width; i += simd_width) {
        __m128i data = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i), data);
    }
#elif defined(__ARM_NEON)
    // NEON can process 16 bytes at a time
    const size_t simd_width = 16;
    size_t simd_iterations = size / simd_width;
    
    for (; i < simd_iterations * simd_width; i += simd_width) {
        uint8x16_t data = vld1q_u8(src + i);
        vst1q_u8(dst + i, data);
    }
#endif
    
    // Handle remaining bytes with regular memcpy
    if (i < size) {
        std::memcpy(dst + i, src + i, size - i);
    }
}

} // namespace btoon

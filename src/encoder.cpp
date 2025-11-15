#include "btoon/encoder.h"
#include <algorithm>
#include <cstring>
#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#elif defined(__ARM_NEON)
#include <arm_neon.h>
#endif

namespace btoon {



std::span<const uint8_t> Encoder::getBuffer() {
    addSignatureIfEnabled();
    return buffer_;
}

void Encoder::encodeNil() {
    buffer_.push_back(0xc0);
}

void Encoder::encodeBool(bool value) {
    buffer_.push_back(static_cast<uint8_t>(value ? 0xc3 : 0xc2));
}

void Encoder::encodeInt(int64_t value) {
    if (value >= -32 && value <= 127) {
        buffer_.push_back(static_cast<uint8_t>(value));
    } else if (value >= -128 && value <= 127) {
        buffer_.push_back(0xd0);
        buffer_.push_back(static_cast<uint8_t>(value));
    } else if (value >= -32768 && value <= 32767) {
        buffer_.push_back(0xd1);
        buffer_.insert(buffer_.end(), reinterpret_cast<uint8_t*>(&value), reinterpret_cast<uint8_t*>(&value) + 2);
    } else if (value >= -2147483648LL && value <= 2147483647LL) {
        buffer_.push_back(0xd2);
        buffer_.insert(buffer_.end(), reinterpret_cast<uint8_t*>(&value), reinterpret_cast<uint8_t*>(&value) + 4);
    } else {
        buffer_.push_back(0xd3);
        buffer_.insert(buffer_.end(), reinterpret_cast<uint8_t*>(&value), reinterpret_cast<uint8_t*>(&value) + 8);
    }
}

void Encoder::encodeUint(uint64_t value) {
    if (value <= 127) {
        buffer_.push_back(static_cast<uint8_t>(value));
    } else if (value <= 255) {
        buffer_.push_back(0xcc);
        buffer_.push_back(static_cast<uint8_t>(value));
    } else if (value <= 65535) {
        buffer_.push_back(0xcd);
        buffer_.insert(buffer_.end(), reinterpret_cast<uint8_t*>(&value), reinterpret_cast<uint8_t*>(&value) + 2);
    } else if (value <= 4294967295ULL) {
        buffer_.push_back(0xce);
        buffer_.insert(buffer_.end(), reinterpret_cast<uint8_t*>(&value), reinterpret_cast<uint8_t*>(&value) + 4);
    } else {
        buffer_.push_back(0xcf);
        buffer_.insert(buffer_.end(), reinterpret_cast<uint8_t*>(&value), reinterpret_cast<uint8_t*>(&value) + 8);
    }
}

void Encoder::encodeFloat(double value) {
    buffer_.push_back(0xcb);
    buffer_.insert(buffer_.end(), reinterpret_cast<uint8_t*>(&value), reinterpret_cast<uint8_t*>(&value) + 8);
}

void Encoder::encodeString(const std::string& value) {
    size_t len = value.size();
    if (len <= 31) {
        buffer_.push_back(static_cast<uint8_t>(0xa0 | len));
    } else if (len <= 255) {
        buffer_.push_back(0xd9);
        buffer_.push_back(static_cast<uint8_t>(len));
    } else if (len <= 65535) {
        buffer_.push_back(0xda);
        buffer_.insert(buffer_.end(), reinterpret_cast<const uint8_t*>(&len), reinterpret_cast<const uint8_t*>(&len) + 2);
    } else {
        buffer_.push_back(0xdb);
        buffer_.insert(buffer_.end(), reinterpret_cast<const uint8_t*>(&len), reinterpret_cast<const uint8_t*>(&len) + 4);
    }
    buffer_.insert(buffer_.end(), value.begin(), value.end());
}

void Encoder::encodeBinary(std::span<const uint8_t> value) {
    size_t len = value.size();
    if (len <= 255) {
        buffer_.push_back(0xc4);
        buffer_.push_back(static_cast<uint8_t>(len));
    } else if (len <= 65535) {
        buffer_.push_back(0xc5);
        buffer_.insert(buffer_.end(), reinterpret_cast<const uint8_t*>(&len), reinterpret_cast<const uint8_t*>(&len) + 2);
    } else {
        buffer_.push_back(0xc6);
        buffer_.insert(buffer_.end(), reinterpret_cast<const uint8_t*>(&len), reinterpret_cast<const uint8_t*>(&len) + 4);
    }
    buffer_.insert(buffer_.end(), value.begin(), value.end());
}

void Encoder::encodeArray(const std::vector<std::vector<uint8_t>>& elements) {
    size_t len = elements.size();
    if (len <= 15) {
        buffer_.push_back(static_cast<uint8_t>(0x90 | len));
    } else if (len <= 65535) {
        buffer_.push_back(0xdc);
        buffer_.insert(buffer_.end(), reinterpret_cast<const uint8_t*>(&len), reinterpret_cast<const uint8_t*>(&len) + 2);
    } else {
        buffer_.push_back(0xdd);
        buffer_.insert(buffer_.end(), reinterpret_cast<const uint8_t*>(&len), reinterpret_cast<const uint8_t*>(&len) + 4);
    }
    for (const auto& elem : elements) {
        buffer_.insert(buffer_.end(), elem.begin(), elem.end());
    }
}

void Encoder::encodeMap(const std::map<std::string, std::vector<uint8_t>>& pairs) {
    size_t len = pairs.size();
    if (len <= 15) {
        buffer_.push_back(static_cast<uint8_t>(0x80 | len));
    } else if (len <= 65535) {
        buffer_.push_back(0xde);
        buffer_.insert(buffer_.end(), reinterpret_cast<const uint8_t*>(&len), reinterpret_cast<const uint8_t*>(&len) + 2);
    } else {
        buffer_.push_back(0xdf);
        buffer_.insert(buffer_.end(), reinterpret_cast<const uint8_t*>(&len), reinterpret_cast<const uint8_t*>(&len) + 4);
    }
    for (const auto& pair : pairs) {
        encodeString(pair.first);
        buffer_.insert(buffer_.end(), pair.second.begin(), pair.second.end());
    }
}

void Encoder::encodeTimestamp(int64_t timestamp) {
    encodeExtension(-1, std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&timestamp), 8));
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
    if (len == 1) buffer_.push_back(0xd4);
    else if (len == 2) buffer_.push_back(0xd5);
    else if (len == 4) buffer_.push_back(0xd6);
    else if (len == 8) buffer_.push_back(0xd7);
    else if (len == 16) buffer_.push_back(0xd8);
    else if (len <= 255) {
        buffer_.push_back(0xc7);
        buffer_.push_back(static_cast<uint8_t>(len));
    } else if (len <= 65535) {
        buffer_.push_back(0xc8);
        uint16_t be_len = htons(static_cast<uint16_t>(len));
        buffer_.insert(buffer_.end(), reinterpret_cast<uint8_t*>(&be_len), reinterpret_cast<uint8_t*>(&be_len) + 2);
    } else {
        buffer_.push_back(0xc9);
        uint32_t be_len = htonl(static_cast<uint32_t>(len));
        buffer_.insert(buffer_.end(), reinterpret_cast<uint8_t*>(&be_len), reinterpret_cast<uint8_t*>(&be_len) + 4);
    }
    buffer_.push_back(static_cast<uint8_t>(type));
    buffer_.insert(buffer_.end(), data.begin(), data.end());
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
    uint32_t num_columns = htonl(column_names.size());
    schema_bytes.insert(schema_bytes.end(), reinterpret_cast<uint8_t*>(&num_columns), reinterpret_cast<uint8_t*>(&num_columns) + 4);

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
            else if constexpr (std::is_same_v<T, String> || std::is_same_v<T, StringView>) schema_bytes.push_back(5);
            else schema_bytes.push_back(0); // Default to unknown
        }, val);
    }

    std::vector<uint8_t> rows_bytes;
    uint32_t num_rows = htonl(data.size());
    rows_bytes.insert(rows_bytes.end(), reinterpret_cast<uint8_t*>(&num_rows), reinterpret_cast<uint8_t*>(&num_rows) + 4);

    for (const auto& row_value : data) {
        const auto* row = std::get_if<Map>(&row_value);
        for (const auto& name : column_names) {
            Encoder temp_encoder;
            temp_encoder.encode((*row).at(name));
            auto buf = temp_encoder.getBuffer();
            rows_bytes.insert(rows_bytes.end(), buf.begin(), buf.end());
        }
    }
    
    std::vector<uint8_t> combined_bytes;
    combined_bytes.insert(combined_bytes.end(), schema_bytes.begin(), schema_bytes.end());
    combined_bytes.insert(combined_bytes.end(), rows_bytes.begin(), rows_bytes.end());

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
        else if constexpr (std::is_same_v<T, StringView>) encodeString({arg.data(), arg.size()});
        else if constexpr (std::is_same_v<T, Binary>) encodeBinary(arg);
        else if constexpr (std::is_same_v<T, Extension>) encodeExtension(arg.type, arg.data);
        else if constexpr (std::is_same_v<T, Timestamp>) encodeTimestamp(arg.seconds);
        else if constexpr (std::is_same_v<T, Date>) encodeDate(arg.milliseconds);
        else if constexpr (std::is_same_v<T, DateTime>) encodeDateTime(arg.nanoseconds);
        else if constexpr (std::is_same_v<T, BigInt>) encodeBigInt(arg.bytes);
        else if constexpr (std::is_same_v<T, VectorFloat>) encodeVectorFloat(arg);
        else if constexpr (std::is_same_v<T, VectorDouble>) encodeVectorDouble(arg);
        else if constexpr (std::is_same_v<T, Array>) {
            if (is_tabular(arg)) {
                encodeColumnar(arg);
            } else {
                std::vector<std::vector<uint8_t>> elements;
                for(const auto& v : arg) {
                    Encoder temp_encoder;
                    temp_encoder.encode(v);
                    auto buf = temp_encoder.getBuffer();
                    elements.emplace_back(buf.begin(), buf.end());
                }
                encodeArray(elements);
            }
        }
        else if constexpr (std::is_same_v<T, Map>) {
            // This is inefficient and will be replaced
            std::map<std::string, std::vector<uint8_t>> pairs;
            for(const auto& [k, v] : arg) {
                Encoder temp_encoder;
                temp_encoder.encode(v);
                auto buf = temp_encoder.getBuffer();
                pairs[k] = {buf.begin(), buf.end()};
            }
            encodeMap(pairs);
        }
    }, value);
}

void Encoder::addSignatureIfEnabled() {
    if (useSecurity_ && security_ != nullptr) {
        auto signature = security_->sign(buffer_);
        std::vector<uint8_t> signedData;
        uint8_t sigLen = static_cast<uint8_t>(signature.size());
        signedData.push_back(sigLen);
        signedData.insert(signedData.end(), signature.begin(), signature.end());
        signedData.insert(signedData.end(), buffer_.begin(), buffer_.end());
        buffer_ = signedData;
    }
}

void Encoder::simd_copy(uint8_t* dst, const uint8_t* src, size_t size) const {
#if defined(__AVX2__)
    // AVX implementation
#elif defined(__ARM_NEON)
    // NEON implementation
#else
    std::memcpy(dst, src, size);
#endif
}

} // namespace btoon

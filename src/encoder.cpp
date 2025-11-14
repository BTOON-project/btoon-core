#include "btoon/encoder.h"
#include <algorithm>
#include <cstring>

namespace btoon {

// TODO: Implement memory pool for buffer allocation to reduce dynamic memory overhead
// TODO: Use SIMD instructions (e.g., SSE/AVX) for bulk encoding of arrays and binary data

std::vector<uint8_t> Encoder::encodeNil() const {
    std::vector<uint8_t> result = {0xc0};
    return addSignatureIfEnabled(result);
}

std::vector<uint8_t> Encoder::encodeBool(bool value) const {
    std::vector<uint8_t> result = {static_cast<uint8_t>(value ? 0xc3 : 0xc2)};
    return addSignatureIfEnabled(result);
}

std::vector<uint8_t> Encoder::encodeInt(int64_t value) const {
    std::vector<uint8_t> buf;
    if (value >= -32 && value <= 127) {
        buf = {static_cast<uint8_t>(value)};
    } else if (value >= -128 && value <= 127) {
        buf = {0xd0, static_cast<uint8_t>(value)};
    } else if (value >= -32768 && value <= 32767) {
        buf.resize(3);
        buf[0] = 0xd1;
        std::memcpy(&buf[1], &value, 2);
        std::reverse(buf.begin() + 1, buf.end());
    } else if (value >= -2147483648LL && value <= 2147483647LL) {
        buf.resize(5);
        buf[0] = 0xd2;
        std::memcpy(&buf[1], &value, 4);
        std::reverse(buf.begin() + 1, buf.end());
    } else {
        buf.resize(9);
        buf[0] = 0xd3;
        std::memcpy(&buf[1], &value, 8);
        std::reverse(buf.begin() + 1, buf.end());
    }
    return addSignatureIfEnabled(buf);
}

std::vector<uint8_t> Encoder::encodeUint(uint64_t value) const {
    std::vector<uint8_t> buf;
    if (value <= 127) {
        buf = {static_cast<uint8_t>(value)};
    } else if (value <= 255) {
        buf = {0xcc, static_cast<uint8_t>(value)};
    } else if (value <= 65535) {
        buf.resize(3);
        buf[0] = 0xcd;
        std::memcpy(&buf[1], &value, 2);
        std::reverse(buf.begin() + 1, buf.end());
    } else if (value <= 4294967295ULL) {
        buf.resize(5);
        buf[0] = 0xce;
        std::memcpy(&buf[1], &value, 4);
        std::reverse(buf.begin() + 1, buf.end());
    } else {
        buf.resize(9);
        buf[0] = 0xcf;
        std::memcpy(&buf[1], &value, 8);
        std::reverse(buf.begin() + 1, buf.end());
    }
    return addSignatureIfEnabled(buf);
}

std::vector<uint8_t> Encoder::encodeFloat(double value) const {
    std::vector<uint8_t> buf(9);
    buf[0] = 0xcb;
    std::memcpy(&buf[1], &value, 8);
    std::reverse(buf.begin() + 1, buf.end());
    return addSignatureIfEnabled(buf);
}

std::vector<uint8_t> Encoder::encodeString(const std::string& value) const {
    size_t len = value.size();
    std::vector<uint8_t> buf;
    if (len <= 31) {
        buf.push_back(static_cast<uint8_t>(0xa0 | len));
    } else if (len <= 255) {
        buf.push_back(0xd9);
        buf.push_back(static_cast<uint8_t>(len));
    } else if (len <= 65535) {
        buf.push_back(0xda);
        buf.push_back(static_cast<uint8_t>(len >> 8));
        buf.push_back(static_cast<uint8_t>(len));
    } else {
        buf.push_back(0xdb);
        buf.push_back(static_cast<uint8_t>(len >> 24));
        buf.push_back(static_cast<uint8_t>(len >> 16));
        buf.push_back(static_cast<uint8_t>(len >> 8));
        buf.push_back(static_cast<uint8_t>(len));
    }
    buf.insert(buf.end(), value.begin(), value.end());
    // TODO: Optimize string copying with SIMD for large strings
    return addSignatureIfEnabled(buf);
}

std::vector<uint8_t> Encoder::encodeBinary(const std::vector<uint8_t>& value) const {
    size_t len = value.size();
    std::vector<uint8_t> buf;
    if (len <= 255) {
        buf.push_back(0xc4);
        buf.push_back(static_cast<uint8_t>(len));
    } else if (len <= 65535) {
        buf.push_back(0xc5);
        buf.push_back(static_cast<uint8_t>(len >> 8));
        buf.push_back(static_cast<uint8_t>(len));
    } else {
        buf.push_back(0xc6);
        buf.push_back(static_cast<uint8_t>(len >> 24));
        buf.push_back(static_cast<uint8_t>(len >> 16));
        buf.push_back(static_cast<uint8_t>(len >> 8));
        buf.push_back(static_cast<uint8_t>(len));
    }
    buf.insert(buf.end(), value.begin(), value.end());
    // TODO: Optimize binary data copying with SIMD for large buffers
    return addSignatureIfEnabled(buf);
}

std::vector<uint8_t> Encoder::encodeArray(const std::vector<std::vector<uint8_t>>& elements) const {
    size_t len = elements.size();
    std::vector<uint8_t> buf;
    if (len <= 15) {
        buf.push_back(static_cast<uint8_t>(0x90 | len));
    } else if (len <= 65535) {
        buf.push_back(0xdc);
        buf.push_back(static_cast<uint8_t>(len >> 8));
        buf.push_back(static_cast<uint8_t>(len));
    } else {
        buf.push_back(0xdd);
        buf.push_back(static_cast<uint8_t>(len >> 24));
        buf.push_back(static_cast<uint8_t>(len >> 16));
        buf.push_back(static_cast<uint8_t>(len >> 8));
        buf.push_back(static_cast<uint8_t>(len));
    }
    for (const auto& elem : elements) {
        buf.insert(buf.end(), elem.begin(), elem.end());
    }
    // TODO: Optimize array concatenation with pre-allocated buffers from memory pool
    return addSignatureIfEnabled(buf);
}

std::vector<uint8_t> Encoder::encodeMap(const std::map<std::string, std::vector<uint8_t>>& pairs) const {
    size_t len = pairs.size();
    std::vector<uint8_t> buf;
    if (len <= 15) {
        buf.push_back(static_cast<uint8_t>(0x80 | len));
    } else if (len <= 65535) {
        buf.push_back(0xde);
        buf.push_back(static_cast<uint8_t>(len >> 8));
        buf.push_back(static_cast<uint8_t>(len));
    } else {
        buf.push_back(0xdf);
        buf.push_back(static_cast<uint8_t>(len >> 24));
        buf.push_back(static_cast<uint8_t>(len >> 16));
        buf.push_back(static_cast<uint8_t>(len >> 8));
        buf.push_back(static_cast<uint8_t>(len));
    }
    for (const auto& pair : pairs) {
        auto keyBuf = encodeString(pair.first);
        buf.insert(buf.end(), keyBuf.begin(), keyBuf.end());
        buf.insert(buf.end(), pair.second.begin(), pair.second.end());
    }
    // TODO: Optimize map encoding with pre-allocated buffers and parallel processing for large maps
    return addSignatureIfEnabled(buf);
}

std::vector<uint8_t> Encoder::encodeTimestamp(int64_t timestamp) const {
    std::vector<uint8_t> buf(10);
    buf[0] = 0xd7; // fixext 8
    buf[1] = -1;   // Timestamp type
    uint64_t ts = htonll(timestamp);
    std::memcpy(&buf[2], &ts, 8);
    return addSignatureIfEnabled(buf);
}

std::vector<uint8_t> Encoder::encodeDate(int64_t milliseconds) const {
    return encodeExtension(0, {(uint8_t*)&milliseconds, (uint8_t*)&milliseconds + 8});
}

std::vector<uint8_t> Encoder::encodeBigInt(const std::vector<uint8_t>& bytes) const {
    return encodeExtension(1, bytes);
}

std::vector<uint8_t> Encoder::encodeExtension(int8_t type, const std::vector<uint8_t>& data) const {
    size_t len = data.size();
    std::vector<uint8_t> buf;
    if (len == 1) {
        buf.push_back(0xd4);
    } else if (len == 2) {
        buf.push_back(0xd5);
    } else if (len == 4) {
        buf.push_back(0xd6);
    } else if (len == 8) {
        buf.push_back(0xd7);
    } else if (len == 16) {
        buf.push_back(0xd8);
    } else if (len <= 255) {
        buf.push_back(0xc7);
        buf.push_back(static_cast<uint8_t>(len));
    } else if (len <= 65535) {
        buf.push_back(0xc8);
        buf.push_back(static_cast<uint8_t>(len >> 8));
        buf.push_back(static_cast<uint8_t>(len));
    } else {
        buf.push_back(0xc9);
        buf.push_back(static_cast<uint8_t>(len >> 24));
        buf.push_back(static_cast<uint8_t>(len >> 16));
        buf.push_back(static_cast<uint8_t>(len >> 8));
        buf.push_back(static_cast<uint8_t>(len));
    }
    buf.push_back(static_cast<uint8_t>(type));
    buf.insert(buf.end(), data.begin(), data.end());
    return addSignatureIfEnabled(buf);
}

std::vector<uint8_t> Encoder::encodeVarInt(uint64_t value, uint8_t prefix, uint8_t bits) const {
    std::vector<uint8_t> buf;
    buf.push_back(prefix);
    for (int i = bits / 8 - 1; i >= 0; --i) {
        buf.push_back(static_cast<uint8_t>(value >> (i * 8)));
    }
    return buf;
}

std::vector<uint8_t> Encoder::addSignatureIfEnabled(const std::vector<uint8_t>& data) const {
    if (useSecurity_ && security_ != nullptr) {
        // Generate HMAC signature
        auto signature = security_->sign(data);
        // Prepend signature to data (signature length + signature + data)
        std::vector<uint8_t> signedData;
        uint8_t sigLen = static_cast<uint8_t>(signature.size());
        signedData.push_back(sigLen);
        signedData.insert(signedData.end(), signature.begin(), signature.end());
        signedData.insert(signedData.end(), data.begin(), data.end());
        return signedData;
    }
    return data;
}

} // namespace btoon

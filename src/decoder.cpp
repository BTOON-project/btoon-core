#include "btoon/decoder.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <arpa/inet.h> // For ntoh functions

namespace btoon {

// --- Helper Functions ---

// Checks if the next read of `count` bytes would overflow the buffer.
static void check_overflow(size_t pos, size_t count, size_t buffer_size) {
    if (pos + count > buffer_size) {
        throw BtoonException("Decoder overflow: attempted to read past buffer end.");
    }
}

// --- Decoder Public Methods ---

Value Decoder::decode(std::span<const uint8_t> buffer) const {
    size_t pos = 0;
    auto data_span = useSecurity_ ? verifyAndExtractData(buffer) : buffer;
    Value result = decode(data_span, pos);
    if (pos != data_span.size()) {
        // This could be a strictness error in the future. For now, we allow trailing bytes.
    }
    return result;
}

// --- Decoder Private Methods ---

Value Decoder::decode(std::span<const uint8_t> buffer, size_t& pos) const {
    check_overflow(pos, 1, buffer.size());
    uint8_t marker = buffer[pos];

    // Positive fixint
    if (marker <= 0x7f) {
        pos++;
        return Value(static_cast<uint64_t>(marker));
    }
    // Negative fixint
    if (marker >= 0xe0) {
        pos++;
        return Value(static_cast<int64_t>(static_cast<int8_t>(marker)));
    }
    // Fixmap
    if (marker >= 0x80 && marker <= 0x8f) {
        return Value(decodeMap(buffer, pos));
    }
    // Fixarray
    if (marker >= 0x90 && marker <= 0x9f) {
        return Value(decodeArray(buffer, pos));
    }
    // Fixstr
    if (marker >= 0xa0 && marker <= 0xbf) {
        return Value(decodeString(buffer, pos));
    }

    switch (marker) {
        case 0xc0: return Value(decodeNil(pos));
        case 0xc2:
        case 0xc3: return Value(decodeBool(buffer, pos));
        case 0xc4:
        case 0xc5:
        case 0xc6: return Value(decodeBinary(buffer, pos));
        case 0xc7:
        case 0xc8:
        case 0xc9: return Value(decodeExtension(buffer, pos));
        case 0xca:
        case 0xcb: return Value(decodeFloat(buffer, pos));
        case 0xcc:
        case 0xcd:
        case 0xce:
        case 0xcf: return Value(decodeUint(buffer, pos));
        case 0xd0:
        case 0xd1:
        case 0xd2:
        case 0xd3: return Value(decodeInt(buffer, pos));
        case 0xd4:
        case 0xd5:
        case 0xd6:
        case 0xd7:
        case 0xd8: return Value(decodeExtension(buffer, pos));
        case 0xd9:
        case 0xda:
        case 0xdb: return Value(decodeString(buffer, pos));
        case 0xdc:
        case 0xdd: return Value(decodeArray(buffer, pos));
        case 0xde:
        case 0xdf: return Value(decodeMap(buffer, pos));
        default:
            throw BtoonException("Unknown or unsupported marker: " + std::to_string(marker));
    }
}

Nil Decoder::decodeNil(size_t& pos) const {
    pos++;
    return nullptr;
}

Bool Decoder::decodeBool(std::span<const uint8_t> buffer, size_t& pos) const {
    bool value = buffer[pos] == 0xc3;
    pos++;
    return value;
}

Int Decoder::decodeInt(std::span<const uint8_t> buffer, size_t& pos) const {
    uint8_t marker = buffer[pos];
    pos++;
    switch (marker) {
        case 0xd0: {
            check_overflow(pos, 1, buffer.size());
            int8_t val;
            std::memcpy(&val, &buffer[pos], 1);
            pos += 1;
            return static_cast<Int>(val);
        }
        case 0xd1: {
            check_overflow(pos, 2, buffer.size());
            int16_t val;
            std::memcpy(&val, &buffer[pos], 2);
            pos += 2;
            return static_cast<Int>(ntohs(val));
        }
        case 0xd2: {
            check_overflow(pos, 4, buffer.size());
            int32_t val;
            std::memcpy(&val, &buffer[pos], 4);
            pos += 4;
            return static_cast<Int>(ntohl(val));
        }
        case 0xd3: {
            check_overflow(pos, 8, buffer.size());
            int64_t val;
            std::memcpy(&val, &buffer[pos], 8);
            pos += 8;
            return static_cast<Int>(ntohll(val));
        }
        default:
            throw BtoonException("Invalid signed integer marker.");
    }
}

Uint Decoder::decodeUint(std::span<const uint8_t> buffer, size_t& pos) const {
    uint8_t marker = buffer[pos];
    pos++;
    switch (marker) {
        case 0xcc: {
            check_overflow(pos, 1, buffer.size());
            uint8_t val;
            std::memcpy(&val, &buffer[pos], 1);
            pos += 1;
            return static_cast<Uint>(val);
        }
        case 0xcd: {
            check_overflow(pos, 2, buffer.size());
            uint16_t val;
            std::memcpy(&val, &buffer[pos], 2);
            pos += 2;
            return static_cast<Uint>(ntohs(val));
        }
        case 0xce: {
            check_overflow(pos, 4, buffer.size());
            uint32_t val;
            std::memcpy(&val, &buffer[pos], 4);
            pos += 4;
            return static_cast<Uint>(ntohl(val));
        }
        case 0xcf: {
            check_overflow(pos, 8, buffer.size());
            uint64_t val;
            std::memcpy(&val, &buffer[pos], 8);
            pos += 8;
            return static_cast<Uint>(ntohll(val));
        }
        default:
            throw BtoonException("Invalid unsigned integer marker.");
    }
}

Float Decoder::decodeFloat(std::span<const uint8_t> buffer, size_t& pos) const {
    uint8_t marker = buffer[pos];
    pos++;
    switch (marker) {
        case 0xca: {
            check_overflow(pos, 4, buffer.size());
            uint32_t val;
            std::memcpy(&val, &buffer[pos], 4);
            val = ntohl(val);
            float f;
            std::memcpy(&f, &val, 4);
            pos += 4;
            return static_cast<Float>(f);
        }
        case 0xcb: {
            check_overflow(pos, 8, buffer.size());
            uint64_t val;
            std::memcpy(&val, &buffer[pos], 8);
            val = ntohll(val);
            double d;
            std::memcpy(&d, &val, 8);
            pos += 8;
            return d;
        }
        default:
            throw BtoonException("Invalid float marker.");
    }
}

String Decoder::decodeString(std::span<const uint8_t> buffer, size_t& pos) const {
    uint8_t marker = buffer[pos];
    pos++;
    size_t len = 0;
    if (marker >= 0xa0 && marker <= 0xbf) {
        len = marker & 0x1f;
    } else if (marker == 0xd9) {
        check_overflow(pos, 1, buffer.size());
        len = buffer[pos];
        pos += 1;
    } else if (marker == 0xda) {
        check_overflow(pos, 2, buffer.size());
        uint16_t l;
        std::memcpy(&l, &buffer[pos], 2);
        len = ntohs(l);
        pos += 2;
    } else if (marker == 0xdb) {
        check_overflow(pos, 4, buffer.size());
        uint32_t l;
        std::memcpy(&l, &buffer[pos], 4);
        len = ntohl(l);
        pos += 4;
    } else {
        throw BtoonException("Invalid string marker.");
    }
    check_overflow(pos, len, buffer.size());
    String str(reinterpret_cast<const char*>(&buffer[pos]), len);
    pos += len;
    return str;
}

Binary Decoder::decodeBinary(std::span<const uint8_t> buffer, size_t& pos) const {
    uint8_t marker = buffer[pos];
    pos++;
    size_t len = 0;
    if (marker == 0xc4) {
        check_overflow(pos, 1, buffer.size());
        len = buffer[pos];
        pos += 1;
    } else if (marker == 0xc5) {
        check_overflow(pos, 2, buffer.size());
        uint16_t l;
        std::memcpy(&l, &buffer[pos], 2);
        len = ntohs(l);
        pos += 2;
    } else if (marker == 0xc6) {
        check_overflow(pos, 4, buffer.size());
        uint32_t l;
        std::memcpy(&l, &buffer[pos], 4);
        len = ntohl(l);
        pos += 4;
    } else {
        throw BtoonException("Invalid binary marker.");
    }
    check_overflow(pos, len, buffer.size());
    Binary bin(buffer.begin() + pos, buffer.begin() + pos + len);
    pos += len;
    return bin;
}

Array Decoder::decodeArray(std::span<const uint8_t> buffer, size_t& pos) const {
    uint8_t marker = buffer[pos];
    pos++;
    size_t len = 0;
    if (marker >= 0x90 && marker <= 0x9f) {
        len = marker & 0x0f;
    } else if (marker == 0xdc) {
        check_overflow(pos, 2, buffer.size());
        uint16_t l;
        std::memcpy(&l, &buffer[pos], 2);
        len = ntohs(l);
        pos += 2;
    } else if (marker == 0xdd) {
        check_overflow(pos, 4, buffer.size());
        uint32_t l;
        std::memcpy(&l, &buffer[pos], 4);
        len = ntohl(l);
        pos += 4;
    } else {
        throw BtoonException("Invalid array marker.");
    }
    Array arr;
    arr.reserve(len);
    for (size_t i = 0; i < len; ++i) {
        arr.push_back(decode(buffer, pos));
    }
    return arr;
}

Map Decoder::decodeMap(std::span<const uint8_t> buffer, size_t& pos) const {
    uint8_t marker = buffer[pos];
    pos++;
    size_t len = 0;
    if (marker >= 0x80 && marker <= 0x8f) {
        len = marker & 0x0f;
    } else if (marker == 0xde) {
        check_overflow(pos, 2, buffer.size());
        uint16_t l;
        std::memcpy(&l, &buffer[pos], 2);
        len = ntohs(l);
        pos += 2;
    } else if (marker == 0xdf) {
        check_overflow(pos, 4, buffer.size());
        uint32_t l;
        std::memcpy(&l, &buffer[pos], 4);
        len = ntohl(l);
        pos += 4;
    } else {
        throw BtoonException("Invalid map marker.");
    }
    Map map;
    for (size_t i = 0; i < len; ++i) {
        String key = decodeString(buffer, pos);
        map[key] = decode(buffer, pos);
    }
    return map;
}

Value Decoder::decodeExtension(std::span<const uint8_t> buffer, size_t& pos) const {
    uint8_t marker = buffer[pos];
    pos++;
    size_t len = 0;
    if (marker >= 0xd4 && marker <= 0xd8) {
        len = 1 << (marker - 0xd4);
    } else if (marker == 0xc7) {
        check_overflow(pos, 1, buffer.size());
        len = buffer[pos];
        pos++;
    } else if (marker == 0xc8) {
        check_overflow(pos, 2, buffer.size());
        uint16_t l;
        std::memcpy(&l, &buffer[pos], 2);
        len = ntohs(l);
        pos += 2;
    } else if (marker == 0xc9) {
        check_overflow(pos, 4, buffer.size());
        uint32_t l;
        std::memcpy(&l, &buffer[pos], 4);
        len = ntohl(l);
        pos += 4;
    } else {
        throw BtoonException("Invalid extension marker.");
    }

    check_overflow(pos, 1, buffer.size());
    int8_t type = static_cast<int8_t>(buffer[pos]);
    pos++;

    check_overflow(pos, len, buffer.size());
    
    if (type == 0) { // Date
        return Value(decodeDate(buffer, pos, len));
    } else if (type == 1) { // BigInt
        return Value(decodeBigInt(buffer, pos, len));
    }

    Binary data(buffer.begin() + pos, buffer.begin() + pos + len);
    pos += len;
    return Value(Extension{type, data});
}

Date Decoder::decodeDate(std::span<const uint8_t> buffer, size_t& pos, size_t len) const {
    if (len != 8) {
        throw BtoonException("Invalid data length for Date extension.");
    }
    int64_t ms;
    std::memcpy(&ms, &buffer[pos], 8);
    pos += 8;
    return Date{static_cast<int64_t>(ntohll(ms))};
}

BigInt Decoder::decodeBigInt(std::span<const uint8_t> buffer, size_t& pos, size_t len) const {
    BigInt bi;
    bi.bytes.assign(buffer.begin() + pos, buffer.begin() + pos + len);
    pos += len;
    return bi;
}

Timestamp Decoder::decodeTimestamp(std::span<const uint8_t> buffer, size_t& pos) const {
    // This is a simplified implementation. Timestamps are a type of extension.
    throw BtoonException("Timestamp decoding is not fully implemented yet.");
}

std::span<const uint8_t> Decoder::verifyAndExtractData(std::span<const uint8_t> buffer) const {
    // Security features are not implemented in this refactoring pass.
    if (useSecurity_) {
        throw BtoonException("Security features are not implemented yet.");
    }
    return buffer;
}

} // namespace btoon

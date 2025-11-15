#include "btoon/decoder.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <arpa/inet.h>

namespace btoon {

static void check_overflow(size_t pos, size_t count, size_t buffer_size) {
    if (pos + count > buffer_size) {
        throw BtoonException("Decoder overflow");
    }
}

Value Decoder::decode(std::span<const uint8_t> buffer) const {
    size_t pos = 0;
    auto data_span = useSecurity_ ? verifyAndExtractData(buffer) : buffer;
    Value result = decode(data_span, pos);
    return result;
}

std::pair<Value, size_t> Decoder::decode_and_get_pos(std::span<const uint8_t> buffer) const {
    size_t pos = 0;
    auto data_span = useSecurity_ ? verifyAndExtractData(buffer) : buffer;
    Value result = decode(data_span, pos);
    return {result, pos};
}

Value Decoder::decode(std::span<const uint8_t> buffer, size_t& pos) const {
    check_overflow(pos, 1, buffer.size());
    uint8_t marker = buffer[pos];

    if (marker <= 0x7f) { pos++; return Value(static_cast<uint64_t>(marker)); }
    if (marker >= 0xe0) { pos++; return Value(static_cast<int64_t>(static_cast<int8_t>(marker))); }
    if (marker >= 0x80 && marker <= 0x8f) { return Value(decodeMap(buffer, pos)); }
    if (marker >= 0x90 && marker <= 0x9f) { return Value(decodeArray(buffer, pos)); }
    if (marker >= 0xa0 && marker <= 0xbf) { return Value(decodeString(buffer, pos)); }

    switch (marker) {
        case 0xc0: return Value(decodeNil(pos));
        case 0xc2: case 0xc3: return Value(decodeBool(buffer, pos));
        case 0xc4: case 0xc5: case 0xc6: return Value(decodeBinary(buffer, pos));
        case 0xca: case 0xcb: return Value(decodeFloat(buffer, pos));
        case 0xcc: case 0xcd: case 0xce: case 0xcf: return Value(decodeUint(buffer, pos));
        case 0xd0: case 0xd1: case 0xd2: case 0xd3: return Value(decodeInt(buffer, pos));
        case 0xd4: case 0xd5: case 0xd6: case 0xd7: case 0xd8: // Fixed extensions
        case 0xc7: case 0xc8: case 0xc9: { // Variable extensions
            return decodeExtension(buffer, pos);
        }
        default: throw BtoonException("Unknown marker");
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
    uint8_t marker = buffer[pos++];
    switch (marker) {
        case 0xd0: { check_overflow(pos, 1, buffer.size()); int8_t val; std::memcpy(&val, &buffer[pos], 1); pos += 1; return val; }
        case 0xd1: { check_overflow(pos, 2, buffer.size()); int16_t val; std::memcpy(&val, &buffer[pos], 2); pos += 2; return ntohs(val); }
        case 0xd2: { check_overflow(pos, 4, buffer.size()); int32_t val; std::memcpy(&val, &buffer[pos], 4); pos += 4; return ntohl(val); }
        case 0xd3: { check_overflow(pos, 8, buffer.size()); int64_t val; std::memcpy(&val, &buffer[pos], 8); pos += 8; return ntohll(val); }
        default: throw BtoonException("Invalid signed integer marker");
    }
}

Uint Decoder::decodeUint(std::span<const uint8_t> buffer, size_t& pos) const {
    uint8_t marker = buffer[pos++];
    switch (marker) {
        case 0xcc: { check_overflow(pos, 1, buffer.size()); uint8_t val; std::memcpy(&val, &buffer[pos], 1); pos += 1; return val; }
        case 0xcd: { check_overflow(pos, 2, buffer.size()); uint16_t val; std::memcpy(&val, &buffer[pos], 2); pos += 2; return ntohs(val); }
        case 0xce: { check_overflow(pos, 4, buffer.size()); uint32_t val; std::memcpy(&val, &buffer[pos], 4); pos += 4; return ntohl(val); }
        case 0xcf: { check_overflow(pos, 8, buffer.size()); uint64_t val; std::memcpy(&val, &buffer[pos], 8); pos += 8; return ntohll(val); }
        default: throw BtoonException("Invalid unsigned integer marker");
    }
}

Float Decoder::decodeFloat(std::span<const uint8_t> buffer, size_t& pos) const {
    uint8_t marker = buffer[pos++];
    switch (marker) {
        case 0xca: {
            check_overflow(pos, 4, buffer.size());
            uint32_t val; std::memcpy(&val, &buffer[pos], 4); val = ntohl(val);
            float f; std::memcpy(&f, &val, 4);
            pos += 4;
            return f;
        }
        case 0xcb: {
            check_overflow(pos, 8, buffer.size());
            uint64_t val; std::memcpy(&val, &buffer[pos], 8); val = ntohll(val);
            double d; std::memcpy(&d, &val, 8);
            pos += 8;
            return d;
        }
        default: throw BtoonException("Invalid float marker");
    }
}

String Decoder::decodeString(std::span<const uint8_t> buffer, size_t& pos) const {
    uint8_t marker = buffer[pos++];
    size_t len = 0;
    if (marker >= 0xa0 && marker <= 0xbf) { len = marker & 0x1f; }
    else if (marker == 0xd9) { check_overflow(pos, 1, buffer.size()); len = buffer[pos]; pos += 1; }
    else if (marker == 0xda) { check_overflow(pos, 2, buffer.size()); uint16_t l; std::memcpy(&l, &buffer[pos], 2); len = ntohs(l); pos += 2; }
    else if (marker == 0xdb) { check_overflow(pos, 4, buffer.size()); uint32_t l; std::memcpy(&l, &buffer[pos], 4); len = ntohl(l); pos += 4; }
    else { throw BtoonException("Invalid string marker"); }
    check_overflow(pos, len, buffer.size());
    String str(reinterpret_cast<const char*>(&buffer[pos]), len);
    pos += len;
    return str;
}

Binary Decoder::decodeBinary(std::span<const uint8_t> buffer, size_t& pos) const {
    uint8_t marker = buffer[pos++];
    size_t len = 0;
    if (marker == 0xc4) { check_overflow(pos, 1, buffer.size()); len = buffer[pos]; pos += 1; }
    else if (marker == 0xc5) { check_overflow(pos, 2, buffer.size()); uint16_t l; std::memcpy(&l, &buffer[pos], 2); len = ntohs(l); pos += 2; }
    else if (marker == 0xc6) { check_overflow(pos, 4, buffer.size()); uint32_t l; std::memcpy(&l, &buffer[pos], 4); len = ntohl(l); pos += 4; }
    else { throw BtoonException("Invalid binary marker"); }
    check_overflow(pos, len, buffer.size());
    auto sub = buffer.subspan(pos, len);
    Binary bin(sub.begin(), sub.end());
    pos += len;
    return bin;
}

Array Decoder::decodeArray(std::span<const uint8_t> buffer, size_t& pos) const {
    uint8_t marker = buffer[pos++];
    size_t len = 0;
    if (marker >= 0x90 && marker <= 0x9f) { len = marker & 0x0f; }
    else if (marker == 0xdc) { check_overflow(pos, 2, buffer.size()); uint16_t l; std::memcpy(&l, &buffer[pos], 2); len = ntohs(l); pos += 2; }
    else if (marker == 0xdd) { check_overflow(pos, 4, buffer.size()); uint32_t l; std::memcpy(&l, &buffer[pos], 4); len = ntohl(l); pos += 4; }
    else { throw BtoonException("Invalid array marker"); }
    Array arr;
    arr.reserve(len);
    for (size_t i = 0; i < len; ++i) {
        arr.push_back(decode(buffer, pos));
    }
    return arr;
}

Map Decoder::decodeMap(std::span<const uint8_t> buffer, size_t& pos) const {
    uint8_t marker = buffer[pos++];
    size_t len = 0;
    if (marker >= 0x80 && marker <= 0x8f) { len = marker & 0x0f; }
    else if (marker == 0xde) { check_overflow(pos, 2, buffer.size()); uint16_t l; std::memcpy(&l, &buffer[pos], 2); len = ntohs(l); pos += 2; }
    else if (marker == 0xdf) { check_overflow(pos, 4, buffer.size()); uint32_t l; std::memcpy(&l, &buffer[pos], 4); len = ntohl(l); pos += 4; }
    else { throw BtoonException("Invalid map marker"); }
    Map map;
    for (size_t i = 0; i < len; ++i) {
        String key(decodeString(buffer, pos));
        map[key] = decode(buffer, pos);
    }
    return map;
}

Value Decoder::decodeExtension(std::span<const uint8_t> buffer, size_t& pos) const {
    uint8_t marker = buffer[pos++];
    size_t len = 0;
    if (marker == 0xd4) { len = 1; }
    else if (marker == 0xd5) { len = 2; }
    else if (marker == 0xd6) { len = 4; }
    else if (marker == 0xd7) { len = 8; }
    else if (marker == 0xd8) { len = 16; }
    else if (marker == 0xc7) { check_overflow(pos, 1, buffer.size()); len = buffer[pos]; pos += 1; }
    else if (marker == 0xc8) { check_overflow(pos, 2, buffer.size()); uint16_t l; std::memcpy(&l, &buffer[pos], 2); len = ntohs(l); pos += 2; }
    else if (marker == 0xc9) { check_overflow(pos, 4, buffer.size()); uint32_t l; std::memcpy(&l, &buffer[pos], 4); len = ntohl(l); pos += 4; }
    else { throw BtoonException("Invalid extension marker"); }

    check_overflow(pos, 1, buffer.size());
    int8_t ext_type = buffer[pos++];

    switch (ext_type) {
        case -10: { // Tabular data
            // --- Header ---
            uint32_t version;
            std::memcpy(&version, &buffer[pos], 4);
            version = ntohl(version);
            pos += 4;

            if (version != 1) {
                throw BtoonException("Unsupported tabular version");
            }

            uint32_t num_columns;
            std::memcpy(&num_columns, &buffer[pos], 4);
            num_columns = ntohl(num_columns);
            pos += 4;

            uint32_t num_rows;
            std::memcpy(&num_rows, &buffer[pos], 4);
            num_rows = ntohl(num_rows);
            pos += 4;

            // --- Schema Section ---
            std::vector<std::string> column_names;
            std::vector<uint8_t> column_types;
            for (uint32_t i = 0; i < num_columns; ++i) {
                uint32_t name_len;
                std::memcpy(&name_len, &buffer[pos], 4);
                name_len = ntohl(name_len);
                pos += 4;

                column_names.emplace_back(reinterpret_cast<const char*>(&buffer[pos]), name_len);
                pos += name_len;

                column_types.push_back(buffer[pos]);
                pos += 1;
            }

            // --- Data Section ---
            Array arr(num_rows);
            for (uint32_t i = 0; i < num_rows; ++i) {
                arr[i] = Map{};
            }

            for (uint32_t i = 0; i < num_columns; ++i) {
                uint32_t column_len;
                std::memcpy(&column_len, &buffer[pos], 4);
                column_len = ntohl(column_len);
                pos += 4;

                size_t column_data_start = pos;
                for (uint32_t j = 0; j < num_rows; ++j) {
                    auto& row_map = std::get<Map>(arr[j]);
                    Value decoded_value = decode(buffer, pos);
                    // TODO: Validate type against schema (column_types[i])
                    row_map[column_names[i]] = decoded_value;
                }
                pos = column_data_start + column_len;
            }
            return arr;
        }
        case -1: { // Timestamp
            if (len != 4 && len != 8) throw BtoonException("Invalid timestamp length");
            int64_t seconds = 0;
            if (len == 4) {
                uint32_t val;
                std::memcpy(&val, &buffer[pos], 4);
                seconds = ntohl(val);
            } else if (len == 8) {
                uint64_t val;
                std::memcpy(&val, &buffer[pos], 8);
                seconds = ntohll(val);
            }
            pos += len;
            return Timestamp{seconds};
        }
        case -2: return decodeDate(buffer, pos, len - 1);
        case -3: return decodeDateTime(buffer, pos, len - 1);
        case -4: return decodeBigInt(buffer, pos, len - 1);
        case -5: return decodeVectorFloat(buffer, pos, len - 1);
        case -6: return decodeVectorDouble(buffer, pos, len - 1);
        default: { // Generic extension
            Extension ext;
            ext.type = ext_type;
            ext.data.assign(buffer.begin() + pos, buffer.begin() + pos + len - 1); // len - 1 because ext_type is already read
            pos += (len - 1);
            return ext;
        }
    }
}

Date Decoder::decodeDate(std::span<const uint8_t> buffer, size_t& pos, size_t len) const {
    if (len != 8) throw BtoonException("Invalid date length");
    int64_t val;
    std::memcpy(&val, &buffer[pos], 8);
    pos += 8;
    return {static_cast<int64_t>(ntohll(val))};
}

DateTime Decoder::decodeDateTime(std::span<const uint8_t> buffer, size_t& pos, size_t len) const {
    if (len != 8) throw BtoonException("Invalid datetime length");
    int64_t val;
    std::memcpy(&val, &buffer[pos], 8);
    pos += 8;
    return {static_cast<int64_t>(ntohll(val))};
}

BigInt Decoder::decodeBigInt(std::span<const uint8_t> buffer, size_t& pos, size_t len) const {
    check_overflow(pos, len, buffer.size());
    BigInt bi;
    bi.bytes.assign(buffer.begin() + pos, buffer.begin() + pos + len);
    pos += len;
    return bi;
}

VectorFloat Decoder::decodeVectorFloat(std::span<const uint8_t> buffer, size_t& pos, size_t len) const {
    if (len % sizeof(float) != 0) throw BtoonException("Invalid vector_float length");
    size_t num_elements = len / sizeof(float);
    const float* data_ptr = reinterpret_cast<const float*>(&buffer[pos]);
    VectorFloat vec;
    vec.data.assign(data_ptr, data_ptr + num_elements);
    pos += len;
    return vec;
}

VectorDouble Decoder::decodeVectorDouble(std::span<const uint8_t> buffer, size_t& pos, size_t len) const {
    if (len % sizeof(double) != 0) throw BtoonException("Invalid vector_double length");
    size_t num_elements = len / sizeof(double);
    const double* data_ptr = reinterpret_cast<const double*>(&buffer[pos]);
    VectorDouble vec;
    vec.data.assign(data_ptr, data_ptr + num_elements);
    pos += len;
    return vec;
}


std::span<const uint8_t> Decoder::verifyAndExtractData(std::span<const uint8_t> buffer) const {
    if (useSecurity_) {
        throw BtoonException("Security not implemented");
    }
    return buffer;
}

} // namespace btoon
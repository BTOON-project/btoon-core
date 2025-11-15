#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <arpa/inet.h>
#endif

#include "btoon/decoder.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace btoon {

// Define ntohll if not available on this platform
#ifndef ntohll
inline uint64_t ntohll(uint64_t value) {
    // Check endianness
    static const int num = 42;
    if (*reinterpret_cast<const char*>(&num) == num) {
        // Little endian - swap bytes
        const uint32_t high_part = ntohl(static_cast<uint32_t>(value >> 32));
        const uint32_t low_part = ntohl(static_cast<uint32_t>(value & 0xFFFFFFFFLL));
        return (static_cast<uint64_t>(low_part) << 32) | high_part;
    } else {
        // Big endian - no swap needed
        return value;
    }
}
#endif

static void check_overflow(size_t pos, size_t count, size_t buffer_size) {
    // Check for integer overflow first
    if (count > buffer_size || pos > buffer_size - count) {
        throw BtoonException("Decoder overflow");
    }
}

Decoder::Decoder() : pool_(new MemoryPool()), owns_pool_(true) {}

Decoder::Decoder(MemoryPool* pool) : pool_(pool), owns_pool_(false) {}

Decoder::Decoder(const Security& security) : security_(&security), useSecurity_(true), pool_(new MemoryPool()), owns_pool_(true) {}

Decoder::Decoder(const Security& security, MemoryPool* pool) : security_(&security), useSecurity_(true), pool_(pool), owns_pool_(false) {}

Decoder::~Decoder() {
    if (owns_pool_) {
        delete pool_;
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
        case 0xd9: case 0xda: case 0xdb: return Value(decodeString(buffer, pos)); // str8, str16, str32
        case 0xdc: case 0xdd: return Value(decodeArray(buffer, pos)); // array16, array32
        case 0xde: case 0xdf: return Value(decodeMap(buffer, pos)); // map16, map32
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
        case 0xd1: { check_overflow(pos, 2, buffer.size()); uint16_t val_be; std::memcpy(&val_be, &buffer[pos], 2); pos += 2; return static_cast<int16_t>(ntohs(val_be)); }
        case 0xd2: { check_overflow(pos, 4, buffer.size()); uint32_t val_be; std::memcpy(&val_be, &buffer[pos], 4); pos += 4; return static_cast<int32_t>(ntohl(val_be)); }
        case 0xd3: { check_overflow(pos, 8, buffer.size()); uint64_t val_be; std::memcpy(&val_be, &buffer[pos], 8); pos += 8; return static_cast<int64_t>(ntohll(val_be)); }
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
    char* str_data = static_cast<char*>(pool_->allocate(len));
    std::memcpy(str_data, &buffer[pos], len);
    pos += len;
    return String(str_data, len);
}

Binary Decoder::decodeBinary(std::span<const uint8_t> buffer, size_t& pos) const {
    uint8_t marker = buffer[pos++];
    size_t len = 0;
    if (marker == 0xc4) { check_overflow(pos, 1, buffer.size()); len = buffer[pos]; pos += 1; }
    else if (marker == 0xc5) { check_overflow(pos, 2, buffer.size()); uint16_t l; std::memcpy(&l, &buffer[pos], 2); len = ntohs(l); pos += 2; }
    else if (marker == 0xc6) { check_overflow(pos, 4, buffer.size()); uint32_t l; std::memcpy(&l, &buffer[pos], 4); len = ntohl(l); pos += 4; }
    else { throw BtoonException("Invalid binary marker"); }
    check_overflow(pos, len, buffer.size());
    uint8_t* bin_data = static_cast<uint8_t*>(pool_->allocate(len));
    std::memcpy(bin_data, &buffer[pos], len);
    pos += len;
    return Binary(bin_data, bin_data + len);
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

    // Validate that the entire extension payload is within buffer bounds
    check_overflow(pos, len, buffer.size());

    switch (ext_type) {
        case -10: { // Tabular data
            size_t current_ext_data_pos = 0;

            // --- Header ---
            // Note: len is already validated against buffer at pos
            if (current_ext_data_pos + 4 > len) {
                throw BtoonException("Decoder overflow in tabular header");
            }
            uint32_t version = (static_cast<uint32_t>(buffer[pos + current_ext_data_pos]) << 24) |
                               (static_cast<uint32_t>(buffer[pos + current_ext_data_pos + 1]) << 16) |
                               (static_cast<uint32_t>(buffer[pos + current_ext_data_pos + 2]) << 8) |
                               (static_cast<uint32_t>(buffer[pos + current_ext_data_pos + 3]));
            current_ext_data_pos += 4;

            if (version != 1) {
                throw BtoonException("Unsupported tabular version");
            }

            if (current_ext_data_pos + 4 > len) {
                throw BtoonException("Decoder overflow in tabular num_columns");
            }
            uint32_t num_columns = (static_cast<uint32_t>(buffer[pos + current_ext_data_pos]) << 24) |
                                   (static_cast<uint32_t>(buffer[pos + current_ext_data_pos + 1]) << 16) |
                                   (static_cast<uint32_t>(buffer[pos + current_ext_data_pos + 2]) << 8) |
                                   (static_cast<uint32_t>(buffer[pos + current_ext_data_pos + 3]));
            current_ext_data_pos += 4;

            if (current_ext_data_pos + 4 > len) {
                throw BtoonException("Decoder overflow in tabular num_rows");
            }
            uint32_t num_rows = (static_cast<uint32_t>(buffer[pos + current_ext_data_pos]) << 24) |
                                (static_cast<uint32_t>(buffer[pos + current_ext_data_pos + 1]) << 16) |
                                (static_cast<uint32_t>(buffer[pos + current_ext_data_pos + 2]) << 8) |
                                (static_cast<uint32_t>(buffer[pos + current_ext_data_pos + 3]));
            current_ext_data_pos += 4;

            // --- Schema Section ---
            std::vector<std::string> column_names;
            std::vector<uint8_t> column_types;

            // Read all column names first
            for (uint32_t i = 0; i < num_columns; ++i) {
                if (current_ext_data_pos + 4 > len) {
                    throw BtoonException("Decoder overflow in tabular column name length");
                }
                uint32_t name_len = (static_cast<uint32_t>(buffer[pos + current_ext_data_pos]) << 24) |
                                    (static_cast<uint32_t>(buffer[pos + current_ext_data_pos + 1]) << 16) |
                                    (static_cast<uint32_t>(buffer[pos + current_ext_data_pos + 2]) << 8) |
                                    (static_cast<uint32_t>(buffer[pos + current_ext_data_pos + 3]));
                current_ext_data_pos += 4;

                if (name_len > len || current_ext_data_pos > len - name_len) {
                    throw BtoonException("Decoder overflow in tabular column name data");
                }
                column_names.emplace_back(reinterpret_cast<const char*>(&buffer[pos + current_ext_data_pos]), name_len);
                current_ext_data_pos += name_len;
            }

            // Then read all column types
            for (uint32_t i = 0; i < num_columns; ++i) {
                if (current_ext_data_pos + 1 > len) {
                    throw BtoonException("Decoder overflow in tabular column type");
                }
                column_types.push_back(buffer[pos + current_ext_data_pos]);
                current_ext_data_pos += 1;
            }

            // --- Data Section ---
            Array arr(num_rows);
            for (uint32_t i = 0; i < num_rows; ++i) {
                arr[i] = Map{};
            }

            for (uint32_t i = 0; i < num_columns; ++i) {
                if (current_ext_data_pos + 4 > len) {
                    throw BtoonException("Decoder overflow in tabular column data size");
                }
                uint32_t column_data_size = (static_cast<uint32_t>(buffer[pos + current_ext_data_pos]) << 24) |
                                            (static_cast<uint32_t>(buffer[pos + current_ext_data_pos + 1]) << 16) |
                                            (static_cast<uint32_t>(buffer[pos + current_ext_data_pos + 2]) << 8) |
                                            (static_cast<uint32_t>(buffer[pos + current_ext_data_pos + 3]));
                current_ext_data_pos += 4;

                // Validate column_data_size against remaining extension payload
                if (column_data_size > len || current_ext_data_pos > len - column_data_size) {
                    throw BtoonException("Decoder overflow in tabular column data");
                }

                size_t column_data_start_in_ext = current_ext_data_pos;
                // Create a sub-span for the current column's data to decode from
                // We've already validated that pos + column_data_start_in_ext + column_data_size <= buffer.size()
                std::span<const uint8_t> column_buffer = buffer.subspan(pos + column_data_start_in_ext, column_data_size);

                size_t sub_pos = 0;
                for (uint32_t j = 0; j < num_rows; ++j) {
                    auto& row_map = std::get<Map>(arr[j]);
                    // Use the generic decode function which handles all encoding formats
                    Value decoded_value = decode(column_buffer, sub_pos);
                    row_map[column_names[i]] = decoded_value;
                }
                current_ext_data_pos += column_data_size; // Advance current_ext_data_pos by the total column data size

                // Ensure we consumed exactly column_data_size bytes for this column
                if (sub_pos != column_data_size) {
                    throw BtoonException("Column data size mismatch during decoding");
                }
            }
            pos += len; // Advance the main position by the total length of the extension
            return arr;
        }
        case -1: { // Timestamp
            // New format: 8 bytes seconds + 4 bytes nanoseconds + optional 2 bytes timezone
            // Old format (backward compat): 4 or 8 bytes seconds only
            
            if (len == 4 || len == 8) {
                // Old format - seconds only
                int64_t seconds = 0;
                if (len == 4) {
                    uint32_t val;
                    std::memcpy(&val, &buffer[pos], 4);
                    seconds = ntohl(val);
                } else {
                    uint64_t val;
                    std::memcpy(&val, &buffer[pos], 8);
                    seconds = ntohll(val);
                }
                pos += len;
                return Timestamp(seconds);
            } else if (len == 12) {
                // New format without timezone
                uint64_t sec_be;
                std::memcpy(&sec_be, &buffer[pos], 8);
                int64_t seconds = static_cast<int64_t>(ntohll(sec_be));
                
                uint32_t nano_be;
                std::memcpy(&nano_be, &buffer[pos + 8], 4);
                uint32_t nanoseconds = ntohl(nano_be);
                
                pos += len;
                return Timestamp(seconds, nanoseconds);
            } else if (len == 14) {
                // New format with timezone
                uint64_t sec_be;
                std::memcpy(&sec_be, &buffer[pos], 8);
                int64_t seconds = static_cast<int64_t>(ntohll(sec_be));
                
                uint32_t nano_be;
                std::memcpy(&nano_be, &buffer[pos + 8], 4);
                uint32_t nanoseconds = ntohl(nano_be);
                
                uint16_t tz_be;
                std::memcpy(&tz_be, &buffer[pos + 12], 2);
                int16_t timezone_offset = static_cast<int16_t>(ntohs(tz_be));
                
                pos += len;
                return Timestamp(seconds, nanoseconds, timezone_offset);
            } else {
                throw BtoonException("Invalid timestamp length");
            }
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
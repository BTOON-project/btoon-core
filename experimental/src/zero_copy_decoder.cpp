// SPDX-FileCopyrightText: 2025 Alvar Laigna <https://alvarlaigna.com>
// SPDX-License-Identifier: MIT
//
// EXPERIMENTAL, not built: the view-returning decoder that was in src/zero_copy.cpp until
// 0.3.0. See experimental/include/btoon/zero_copy_decoder.h for what it lacks.
#include "btoon/zero_copy_decoder.h"

namespace btoon {

// ===== ZeroCopyDecoder Implementation =====

ValueView ZeroCopyDecoder::decode(const MemoryView& view) {
    return decode(view.data(), view.size());
}

ValueView ZeroCopyDecoder::decode(const uint8_t* data, size_t size) {
    if (!data || size == 0) {
        throw BtoonException("Invalid input data");
    }
    
    size_t pos = 0;
    return decodeValue(data, size, pos, 0);
}

ValueView ZeroCopyDecoder::decodeValue(const uint8_t* data, size_t size, 
                                      size_t& pos, size_t depth) {
    if (depth > options_.max_depth) {
        throw BtoonException("Maximum nesting depth exceeded");
    }
    
    if (pos >= size) {
        throw BtoonException("Buffer underflow");
    }
    
    uint8_t type = data[pos++];
    
    // Handle fixed types
    if (type <= 0x7f) {
        return static_cast<Uint>(type);  // Positive fixint
    } else if (type >= 0xe0) {
        return static_cast<Int>(static_cast<int8_t>(type));  // Negative fixint
    }
    
    // Handle other types
    switch (type) {
        case 0xc0: return Nil{};
        case 0xc2: return Bool(false);
        case 0xc3: return Bool(true);
        
        // Strings - return string_view instead of copying
        default:
            break;
    }
    
    // fixstr (0xa0 - 0xbf)
    if (type >= 0xa0 && type <= 0xbf) {
        size_t len = type & 0x1f;
        if (pos + len > size) throw BtoonException("Buffer underflow");
        std::string_view str(reinterpret_cast<const char*>(data + pos), len);
        pos += len;
        return str;
    }
    
    // Other string types
    switch (type) {
        
        case 0xd9: {  // str8
            if (pos + 1 > size) throw BtoonException("Buffer underflow");
            size_t len = data[pos++];
            if (pos + len > size) throw BtoonException("Buffer underflow");
            std::string_view str(reinterpret_cast<const char*>(data + pos), len);
            pos += len;
            return str;
        }
        
        case 0xda: {  // str16
            if (pos + 2 > size) throw BtoonException("Buffer underflow");
            size_t len = (data[pos] << 8) | data[pos + 1];
            pos += 2;
            if (pos + len > size) throw BtoonException("Buffer underflow");
            std::string_view str(reinterpret_cast<const char*>(data + pos), len);
            pos += len;
            return str;
        }
        
        // Binary - return span instead of copying
        case 0xc4: {  // bin8
            if (pos + 1 > size) throw BtoonException("Buffer underflow");
            size_t len = data[pos++];
            if (pos + len > size) throw BtoonException("Buffer underflow");
            std::span<const uint8_t> bin(data + pos, len);
            pos += len;
            return bin;
        }
        
        case 0xc5: {  // bin16
            if (pos + 2 > size) throw BtoonException("Buffer underflow");
            size_t len = (data[pos] << 8) | data[pos + 1];
            pos += 2;
            if (pos + len > size) throw BtoonException("Buffer underflow");
            std::span<const uint8_t> bin(data + pos, len);
            pos += len;
            return bin;
        }
        
        // Simplified - full implementation would handle all other types
        default:
            break;
    }
    
    // fixarray (0x90 - 0x9f)
    if (type >= 0x90 && type <= 0x9f) {
        size_t count = type & 0x0f;
        std::vector<ValueView> arr;
        arr.reserve(count);
        for (size_t i = 0; i < count; i++) {
            arr.push_back(decodeValue(data, size, pos, depth + 1));
        }
        return arr;
    }
    
    // fixmap (0x80 - 0x8f)
    if (type >= 0x80 && type <= 0x8f) {
        size_t count = type & 0x0f;
        std::unordered_map<std::string_view, ValueView> map;
        for (size_t i = 0; i < count; i++) {
            auto key = decodeValue(data, size, pos, depth + 1);
            auto value = decodeValue(data, size, pos, depth + 1);
            if (auto* str_key = std::get_if<std::string_view>(&key)) {
                map[*str_key] = std::move(value);
            } else {
                throw BtoonException("Map key must be string");
            }
        }
        return map;
    }
    
    throw BtoonException("Unsupported type in zero-copy decoder");
}

} // namespace btoon

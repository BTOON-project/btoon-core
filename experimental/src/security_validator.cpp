// SPDX-FileCopyrightText: 2025 Alvar Laigna <https://alvarlaigna.com>
// SPDX-License-Identifier: MIT
//
// EXPERIMENTAL, not built: the heuristic SecurityValidator that was in src/validator.cpp
// until 0.3.0. See experimental/include/btoon/security_validator.h for why it was moved.
#include "btoon/security_validator.h"
#include "btoon/validator.h"
#include "internal.h"  // src/internal.h

#include <stack>

namespace btoon {

// ===== SecurityValidator Implementation =====

std::vector<std::string> SecurityValidator::checkSecurity(std::span<const uint8_t> data) {
    std::vector<std::string> issues;
    
    if (checkForZipBomb(data)) {
        issues.push_back("Potential zip bomb detected (excessive compression ratio)");
    }
    
    if (!checkNestingDepth(data)) {
        issues.push_back("Excessive nesting depth detected");
    }
    
    if (!checkSizeClaims(data)) {
        issues.push_back("Unreasonable size claims detected");
    }
    
    return issues;
}

bool SecurityValidator::checkForZipBomb(std::span<const uint8_t> data) {
    // Check if data claims to expand to unreasonable size
    // Look for compression headers
    if (data.size() >= 16) {
        // Check for BTOON compression header
        uint32_t magic = detail::load_be32(data.data());
        if (magic == 0x42544F4E) {  // "BTON"
            uint32_t uncompressed = detail::load_be32(data.data() + 12);
            uint32_t compressed = detail::load_be32(data.data() + 8);
            
            // Check for extreme compression ratio (>1000:1)
            if (compressed > 0 && uncompressed / compressed > 1000) {
                return true;
            }
            
            // Check for unreasonable uncompressed size (>1GB)
            if (uncompressed > 1024 * 1024 * 1024) {
                return true;
            }
        }
    }
    
    return false;
}

bool SecurityValidator::checkNestingDepth(std::span<const uint8_t> data, size_t maxDepth) {
    size_t pos = 0;
    size_t depth = 0;
    std::stack<uint8_t> typeStack;
    
    while (pos < data.size()) {
        uint8_t type = data[pos];
        
        // Check for container types
        if ((type >= 0x90 && type <= 0x9f) ||  // fixarray
            type == 0xdc || type == 0xdd ||     // array16/32
            (type >= 0x80 && type <= 0x8f) ||   // fixmap
            type == 0xde || type == 0xdf) {     // map16/32
            
            depth++;
            if (depth > maxDepth) {
                return false;
            }
            typeStack.push(type);
        }
        
        // Skip to next element (simplified - doesn't fully parse)
        pos++;
        
        // This is a simplified check - a full implementation would
        // properly parse the format to track nesting
    }
    
    return true;
}

bool SecurityValidator::checkSizeClaims(std::span<const uint8_t> data) {
    size_t pos = 0;
    BoundsChecker checker(data);
    
    while (pos < data.size()) {
        auto byte = checker.readByte(pos);
        if (!byte) break;
        
        uint8_t type = *byte;
        
        // Check string/binary size claims
        if (type == 0xd9) {  // str8
            auto size_byte = checker.readByte(pos);
            if (!size_byte) return false;
            size_t size = *size_byte;
            if (!checker.canRead(pos, size)) return false;
            pos += size;
        } else if (type == 0xda) {  // str16
            if (!checker.canRead(pos, 2)) return false;
            uint16_t size = detail::load_be16(&data[pos]);
            pos += 2;
            if (!checker.canRead(pos, size)) return false;
            pos += size;
        } else if (type == 0xdb) {  // str32
            if (!checker.canRead(pos, 4)) return false;
            uint32_t size = detail::load_be32(&data[pos]);
            pos += 4;
            if (size > 1024 * 1024 * 100) return false;  // 100MB limit
            if (!checker.canRead(pos, size)) return false;
            pos += size;
        } else if (type >= 0xa0 && type <= 0xbf) {  // fixstr
            size_t size = type & 0x1f;
            if (!checker.canRead(pos, size)) return false;
            pos += size;
        } else {
            // Skip other types (simplified)
            if (type < 0x80) {
                // Positive fixint - single byte
            } else if (type >= 0xe0) {
                // Negative fixint - single byte
            } else {
                // Other types would need proper parsing
                break;  // Simplified for now
            }
        }
    }
    
    return true;
}

} // namespace btoon

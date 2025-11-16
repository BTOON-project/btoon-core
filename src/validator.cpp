#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <arpa/inet.h>
#endif

#include "btoon/validator.h"
#include "btoon/decoder.h"
#include <cmath>
#include <stack>
#include <unordered_set>

namespace btoon {

// ===== BoundsChecker Implementation =====

BoundsChecker::BoundsChecker(std::span<const uint8_t> data) : data_(data) {}

bool BoundsChecker::canRead(size_t position, size_t n) const {
    if (position > data_.size()) return false;
    return (data_.size() - position) >= n;
}

std::optional<uint8_t> BoundsChecker::readByte(size_t& position) const {
    if (!canRead(position, 1)) return std::nullopt;
    return data_[position++];
}

std::optional<std::span<const uint8_t>> BoundsChecker::readBytes(size_t& position, size_t n) const {
    if (!canRead(position, n)) return std::nullopt;
    auto result = data_.subspan(position, n);
    position += n;
    return result;
}

size_t BoundsChecker::remaining(size_t position) const {
    if (position >= data_.size()) return 0;
    return data_.size() - position;
}

bool BoundsChecker::inBounds(size_t position) const {
    return position <= data_.size();
}

// ===== UTF8Validator Implementation =====

bool UTF8Validator::isValidCodePoint(uint32_t cp) {
    // Check for valid Unicode ranges
    if (cp > 0x10FFFF) return false;  // Beyond Unicode range
    if (cp >= 0xD800 && cp <= 0xDFFF) return false;  // Surrogate pairs
    return true;
}

int UTF8Validator::getUTF8SequenceLength(uint8_t lead) {
    if ((lead & 0x80) == 0) return 1;  // ASCII
    if ((lead & 0xE0) == 0xC0) return 2;
    if ((lead & 0xF0) == 0xE0) return 3;
    if ((lead & 0xF8) == 0xF0) return 4;
    return 0;  // Invalid
}

bool UTF8Validator::isValid(std::string_view str) {
    size_t i = 0;
    while (i < str.length()) {
        uint8_t lead = static_cast<uint8_t>(str[i]);
        int len = getUTF8SequenceLength(lead);
        
        if (len == 0) return false;
        if (i + len > str.length()) return false;
        
        // Check continuation bytes
        for (int j = 1; j < len; j++) {
            uint8_t byte = static_cast<uint8_t>(str[i + j]);
            if ((byte & 0xC0) != 0x80) return false;
        }
        
        // Decode and validate code point
        uint32_t cp = 0;
        if (len == 1) {
            cp = lead;
        } else if (len == 2) {
            cp = ((lead & 0x1F) << 6) | (str[i + 1] & 0x3F);
            if (cp < 0x80) return false;  // Overlong encoding
        } else if (len == 3) {
            cp = ((lead & 0x0F) << 12) | ((str[i + 1] & 0x3F) << 6) | (str[i + 2] & 0x3F);
            if (cp < 0x800) return false;  // Overlong encoding
        } else if (len == 4) {
            cp = ((lead & 0x07) << 18) | ((str[i + 1] & 0x3F) << 12) |
                 ((str[i + 2] & 0x3F) << 6) | (str[i + 3] & 0x3F);
            if (cp < 0x10000) return false;  // Overlong encoding
        }
        
        if (!isValidCodePoint(cp)) return false;
        
        i += static_cast<size_t>(len);
    }
    
    return true;
}

std::string UTF8Validator::sanitize(std::string_view str) {
    std::string result;
    result.reserve(str.length());
    
    size_t i = 0;
    while (i < str.length()) {
        uint8_t lead = static_cast<uint8_t>(str[i]);
        int len = getUTF8SequenceLength(lead);
        
        if (len == 0 || i + len > str.length()) {
            // Invalid sequence, replace with replacement character
            result += "\xEF\xBF\xBD";  // U+FFFD
            i++;
            continue;
        }
        
        // Check if valid sequence
        bool valid = true;
        for (int j = 1; j < len && valid; j++) {
            uint8_t byte = static_cast<uint8_t>(str[i + j]);
            if ((byte & 0xC0) != 0x80) valid = false;
        }
        
        if (valid) {
            result.append(str.data() + i, static_cast<size_t>(len));
        } else {
            result += "\xEF\xBF\xBD";  // U+FFFD
        }
        
        i += static_cast<size_t>(len);
    }
    
    return result;
}

// ===== TypeValidator Implementation =====

bool TypeValidator::validateInt(int64_t value, int64_t min, int64_t max) {
    return value >= min && value <= max;
}

bool TypeValidator::validateUint(uint64_t value, uint64_t min, uint64_t max) {
    return value >= min && value <= max;
}

bool TypeValidator::validateFloat(double value, bool allowNaN, bool allowInf) {
    if (!allowNaN && std::isnan(value)) return false;
    if (!allowInf && std::isinf(value)) return false;
    return true;
}

bool TypeValidator::validateTimestamp(const Timestamp& ts) {
    // Check reasonable timestamp bounds (1970-2100)
    const int64_t MIN_TIMESTAMP = 0;
    const int64_t MAX_TIMESTAMP = 4102444800;  // Jan 1, 2100
    
    return ts.seconds >= MIN_TIMESTAMP && ts.seconds <= MAX_TIMESTAMP;
}

bool TypeValidator::validateExtension(const Extension& ext) {
    // Validate known extension types
    switch (ext.type) {
        case -1:   // Timestamp (legacy)
        case -2:   // Date
        case -3:   // DateTime
        case -4:   // BigInt
        case -5:   // VectorFloat
        case -6:   // VectorDouble
        case -10:  // Tabular data
            return true;
        default:
            // Unknown extension types in range -128 to -1 are reserved
            if (ext.type >= -128 && ext.type <= -1) {
                return false;  // Reserved but undefined
            }
            // Application-specific types (0-127) are allowed
            return ext.type >= 0 && ext.type <= 127;
    }
}

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
        uint32_t magic = ntohl(*reinterpret_cast<const uint32_t*>(data.data()));
        if (magic == 0x42544F4E) {  // "BTON"
            uint32_t uncompressed = ntohl(*reinterpret_cast<const uint32_t*>(data.data() + 12));
            uint32_t compressed = ntohl(*reinterpret_cast<const uint32_t*>(data.data() + 8));
            
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
            uint16_t size = ntohs(*reinterpret_cast<const uint16_t*>(&data[pos]));
            pos += 2;
            if (!checker.canRead(pos, size)) return false;
            pos += size;
        } else if (type == 0xdb) {  // str32
            if (!checker.canRead(pos, 4)) return false;
            uint32_t size = ntohl(*reinterpret_cast<const uint32_t*>(&data[pos]));
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

// ===== Validator Implementation =====

class Validator::ValidatorImpl {
public:
    ValidationOptions options;
    
    explicit ValidatorImpl(const ValidationOptions& opts) : options(opts) {}
    
    ValidationResult validateData(std::span<const uint8_t> data) const {
        ValidationResult result;
        
        if (options.collect_stats) {
            result.stats = ValidationResult::Stats{};
        }
        
        // Check total size
        if (data.size() > options.max_total_size) {
            result.addError("Data exceeds maximum allowed size");
            return result;
        }
        
        // Quick format check
        if (data.empty()) {
            result.addError("Empty data");
            return result;
        }
        
        // Check for compression header
        [[maybe_unused]] bool isCompressed = false;
        if (data.size() >= 16) {
            uint32_t magic = ntohl(*reinterpret_cast<const uint32_t*>(data.data()));
            if (magic == 0x42544F4E) {  // "BTON"
                isCompressed = true;
                
                // Validate compression header
                uint32_t compressed_size = ntohl(*reinterpret_cast<const uint32_t*>(data.data() + 8));
                uint32_t uncompressed_size = ntohl(*reinterpret_cast<const uint32_t*>(data.data() + 12));
                
                if (compressed_size + 16 != data.size()) {
                    result.addError("Compression header size mismatch");
                    return result;
                }
                
                if (uncompressed_size > options.max_total_size) {
                    result.addError("Uncompressed size exceeds limit");
                    return result;
                }
                
                // Check compression ratio for zip bomb
                if (compressed_size > 0 && uncompressed_size / compressed_size > 1000) {
                    result.addWarning("Extremely high compression ratio detected");
                }
            }
        }
        
        // Security checks
        if (!options.fast_mode) {
            auto security_issues = SecurityValidator::checkSecurity(data);
            for (const auto& issue : security_issues) {
                result.addWarning(issue);
            }
        }
        
        // Try to decode and validate structure
        try {
            Decoder decoder;
            Value value = decoder.decode(data);
            
            // Validate the decoded value
            ValidationResult valueResult = validateValue(value, 0);
            result.errors.insert(result.errors.end(), 
                               valueResult.errors.begin(), 
                               valueResult.errors.end());
            result.warnings.insert(result.warnings.end(), 
                                 valueResult.warnings.begin(), 
                                 valueResult.warnings.end());
            result.valid = result.valid && valueResult.valid;
            
            if (options.collect_stats) {
                if (!result.stats) {
                    result.stats = ValidationResult::Stats{};
                }
                if (valueResult.stats) {
                    // Merge stats from value validation
                    result.stats->string_count += valueResult.stats->string_count;
                    result.stats->binary_count += valueResult.stats->binary_count;
                    result.stats->array_count += valueResult.stats->array_count;
                    result.stats->map_count += valueResult.stats->map_count;
                    result.stats->max_depth_reached = std::max(result.stats->max_depth_reached, valueResult.stats->max_depth_reached);
                    result.stats->largest_string = std::max(result.stats->largest_string, valueResult.stats->largest_string);
                    result.stats->largest_binary = std::max(result.stats->largest_binary, valueResult.stats->largest_binary);
                    result.stats->largest_array = std::max(result.stats->largest_array, valueResult.stats->largest_array);
                    result.stats->largest_map = std::max(result.stats->largest_map, valueResult.stats->largest_map);
                }
            }
            
        } catch (const std::exception& e) {
            result.addError(std::string("Decode error: ") + e.what());
        }
        
        return result;
    }
    
    // Helper template to validate specific value types (MSVC-compatible)
    template<typename T>
    void validateTypedValue(const T& arg, ValidationResult& result, size_t depth) const {
            if constexpr (std::is_same_v<T, String>) {
                if (arg.length() > options.max_string_length) {
                    result.addError("String exceeds maximum length");
                }
                if (options.require_utf8_strings && !UTF8Validator::isValid(arg)) {
                    result.addError("Invalid UTF-8 string");
                }
                if (options.collect_stats && result.stats) {
                    result.stats->string_count++;
                    result.stats->largest_string = std::max(result.stats->largest_string, arg.length());
                }
        } else if constexpr (std::is_same_v<T, Binary>) {
                if (arg.size() > options.max_binary_length) {
                    result.addError("Binary data exceeds maximum length");
                }
                if (options.collect_stats && result.stats) {
                    result.stats->binary_count++;
                    result.stats->largest_binary = std::max(result.stats->largest_binary, arg.size());
                }
        } else if constexpr (std::is_same_v<T, Array>) {
                if (arg.size() > options.max_array_size) {
                    result.addError("Array exceeds maximum size");
                }
                if (options.collect_stats && result.stats) {
                    result.stats->array_count++;
                    result.stats->largest_array = std::max(result.stats->largest_array, arg.size());
                    result.stats->max_depth_reached = std::max(result.stats->max_depth_reached, depth);
                }
                
                // Recursively validate elements
                for (const auto& elem : arg) {
                    auto elemResult = validateValue(elem, depth + 1);
                    result.errors.insert(result.errors.end(), 
                                       elemResult.errors.begin(), 
                                       elemResult.errors.end());
                    result.valid = result.valid && elemResult.valid;
                    
                    // Merge stats from element validation
                    if (options.collect_stats && result.stats && elemResult.stats) {
                        result.stats->string_count += elemResult.stats->string_count;
                        result.stats->binary_count += elemResult.stats->binary_count;
                        result.stats->array_count += elemResult.stats->array_count;
                        result.stats->map_count += elemResult.stats->map_count;
                        result.stats->max_depth_reached = std::max(result.stats->max_depth_reached, elemResult.stats->max_depth_reached);
                        result.stats->largest_string = std::max(result.stats->largest_string, elemResult.stats->largest_string);
                        result.stats->largest_binary = std::max(result.stats->largest_binary, elemResult.stats->largest_binary);
                        result.stats->largest_array = std::max(result.stats->largest_array, elemResult.stats->largest_array);
                        result.stats->largest_map = std::max(result.stats->largest_map, elemResult.stats->largest_map);
                    }
                }
        } else if constexpr (std::is_same_v<T, Map>) {
                if (arg.size() > options.max_map_size) {
                    result.addError("Map exceeds maximum size");
                }
                if (options.collect_stats && result.stats) {
                    result.stats->map_count++;
                    result.stats->largest_map = std::max(result.stats->largest_map, arg.size());
                    result.stats->max_depth_reached = std::max(result.stats->max_depth_reached, depth);
                }
                
                // Check for duplicate keys (if not allowed)
                if (!options.allow_duplicate_map_keys) {
                    std::unordered_set<std::string> keys;
                    for (const auto& [key, _] : arg) {
                        if (!keys.insert(key).second) {
                            result.addError("Duplicate map key: " + key);
                        }
                    }
                }
                
                // Recursively validate values
                for (const auto& [_, val] : arg) {
                    auto valResult = validateValue(val, depth + 1);
                    result.errors.insert(result.errors.end(), 
                                       valResult.errors.begin(), 
                                       valResult.errors.end());
                    result.valid = result.valid && valResult.valid;
                    
                    // Merge stats from value validation
                    if (options.collect_stats && result.stats && valResult.stats) {
                        result.stats->string_count += valResult.stats->string_count;
                        result.stats->binary_count += valResult.stats->binary_count;
                        result.stats->array_count += valResult.stats->array_count;
                        result.stats->map_count += valResult.stats->map_count;
                        result.stats->max_depth_reached = std::max(result.stats->max_depth_reached, valResult.stats->max_depth_reached);
                        result.stats->largest_string = std::max(result.stats->largest_string, valResult.stats->largest_string);
                        result.stats->largest_binary = std::max(result.stats->largest_binary, valResult.stats->largest_binary);
                        result.stats->largest_array = std::max(result.stats->largest_array, valResult.stats->largest_array);
                        result.stats->largest_map = std::max(result.stats->largest_map, valResult.stats->largest_map);
                    }
                }
        } else if constexpr (std::is_same_v<T, Float>) {
                if (!TypeValidator::validateFloat(arg, options.allow_nan_float, options.allow_inf_float)) {
                    result.addError("Invalid float value");
            }
        } else if constexpr (std::is_same_v<T, Extension>) {
                if (options.validate_extension_types && !TypeValidator::validateExtension(arg)) {
                    result.addError("Invalid extension type: " + std::to_string(arg.type));
            }
        } else if constexpr (std::is_same_v<T, Timestamp>) {
                if (!TypeValidator::validateTimestamp(arg)) {
                    result.addError("Invalid timestamp value");
                }
            }
    }
    
    ValidationResult validateValue(const Value& value, size_t depth) const {
        ValidationResult result;
        
        if (options.collect_stats) {
            result.stats = ValidationResult::Stats{};
        }
        
        // Check depth
        if (depth > options.max_depth) {
            result.addError("Maximum nesting depth exceeded");
            return result;
        }
        
        std::visit([&](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            validateTypedValue(arg, result, depth);
        }, value);
        
        return result;
    }
    
    bool quickCheckData(std::span<const uint8_t> data) const {
        if (data.empty() || data.size() > options.max_total_size) {
            return false;
        }
        
        // Quick MessagePack format check
        uint8_t first = data[0];
        
        // Check for valid MessagePack type
        if (first <= 0x7f) return true;  // Positive fixint
        if (first >= 0x80 && first <= 0x8f) return true;  // Fixmap
        if (first >= 0x90 && first <= 0x9f) return true;  // Fixarray
        if (first >= 0xa0 && first <= 0xbf) return true;  // Fixstr
        if (first >= 0xc0 && first <= 0xdf) return true;  // Various types
        if (first >= 0xe0 && first <= 0xff) return true;  // Negative fixint
        
        return false;
    }
};

Validator::Validator(const ValidationOptions& options)
    : pimpl_(std::make_unique<ValidatorImpl>(options)) {}

Validator::~Validator() = default;

ValidationResult Validator::validate(std::span<const uint8_t> data) const {
    return pimpl_->validateData(data);
}

ValidationResult Validator::validate(const Value& value) const {
    return pimpl_->validateValue(value, 0);
}

bool Validator::quickCheck(std::span<const uint8_t> data) const {
    return pimpl_->quickCheckData(data);
}

std::vector<uint8_t> Validator::sanitize(std::span<const uint8_t> data) const {
    // Try to decode and re-encode with validation
    try {
        Decoder decoder;
        Value value = decoder.decode(data);
        
        // Validate
        auto result = validate(value);
        if (!result.valid) {
            return {};  // Return empty if invalid
        }
        
        // Re-encode
        return encode(value);
    } catch (...) {
        return {};  // Return empty on any error
    }
}

} // namespace btoon

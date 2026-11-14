// SPDX-FileCopyrightText: 2025 Alvar Laigna <https://alvarlaigna.com>
// SPDX-License-Identifier: MIT
#include "btoon/validator.h"
#include "internal.h"
#include "btoon/decoder.h"
#include <cmath>
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
    return detail::is_valid_utf8(str);
}

std::string UTF8Validator::sanitize(std::string_view str) {
    // Each maximal subpart of an ill-formed sequence becomes one U+FFFD (Unicode §3.9,
    // "U+FFFD Substitution of Maximal Subparts"), as browsers and ICU do. Before 0.3.0
    // overlong forms, surrogates and code points above U+10FFFF were copied through, and
    // a bad continuation byte swallowed the valid characters after it.
    static constexpr char kReplacement[] = "\xEF\xBF\xBD";
    std::string out;
    out.reserve(str.size());
    const auto* s = reinterpret_cast<const uint8_t*>(str.data());
    const size_t n = str.size();
    size_t i = 0;
    while (i < n) {
        const uint8_t c = s[i];
        if (c < 0x80) {
            out += static_cast<char>(c);
            ++i;
            continue;
        }
        size_t need = 0;
        uint8_t lo = 0x80, hi = 0xBF;  // allowed range of the second byte
        if (c >= 0xC2 && c <= 0xDF) {
            need = 1;
        } else if (c >= 0xE0 && c <= 0xEF) {
            need = 2;
            if (c == 0xE0) lo = 0xA0;  // no overlongs
            if (c == 0xED) hi = 0x9F;  // no surrogates
        } else if (c >= 0xF0 && c <= 0xF4) {
            need = 3;
            if (c == 0xF0) lo = 0x90;  // no overlongs
            if (c == 0xF4) hi = 0x8F;  // nothing above U+10FFFF
        }
        size_t good = 1;  // bytes of a valid prefix, the lead included
        if (need != 0) {
            while (good <= need && i + good < n) {
                const uint8_t b = s[i + good];
                const uint8_t low = good == 1 ? lo : 0x80;
                const uint8_t high = good == 1 ? hi : 0xBF;
                if (b < low || b > high) break;
                ++good;
            }
        }
        if (need != 0 && good == need + 1) {
            out.append(str.data() + i, good);
        } else {
            out.append(kReplacement, 3);
        }
        i += good;
    }
    return out;
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
    // docs/SPEC.md §5.1: seconds may be any int64, so dates before 1970 or after 2100 are
    // valid. Only the nanoseconds and the offset have limits.
    if (ts.nanoseconds >= 1000000000u) return false;
    return !ts.has_timezone || (ts.timezone_offset >= -1080 && ts.timezone_offset <= 1080);
}

bool TypeValidator::validateExtension(const Extension& ext) {
    // Every int8 id is acceptable (docs/SPEC.md §5): BTOON's own ids are decoded
    // into dedicated types, reserved ids surface as generic extensions, and
    // 0..127 belong to applications.
    (void)ext;
    return true;
}

// ===== Validator Implementation =====

class Validator::ValidatorImpl {
public:
    ValidationOptions options;
    
    explicit ValidatorImpl(const ValidationOptions& opts) : options(opts) {}

    // The decoder settings for encoded input. Returns an error message if the options
    // can't be used (an empty key).
    std::optional<std::string> decodeOptions(DecodeOptions& out) const {
        out.strict = true;
        out.auto_decompress = true;
        out.max_depth = options.max_depth;
        out.max_decompressed_size = std::min(options.max_total_size, kDefaultMaxDecompressedSize);
        if (options.hmac_key) {
            if (options.hmac_key->empty()) return std::string("Invalid options: hmac_key is empty");
            out.hmac_key = *options.hmac_key;
        }
        return std::nullopt;
    }
    
    ValidationResult validateData(std::span<const uint8_t> data) const {
        ValidationResult result;
        
        if (options.collect_stats) {
            result.stats = ValidationResult::Stats{};
            result.stats->total_size = data.size();
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
            uint32_t magic = detail::load_be32(data.data());
            if (magic == 0x42544F4E) {  // "BTON"
                isCompressed = true;
                
                // Validate compression header
                uint32_t compressed_size = detail::load_be32(data.data() + 8);
                uint32_t uncompressed_size = detail::load_be32(data.data() + 12);
                
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
        
        // Decode with the caller's limits and key (see ValidationOptions).
        DecodeOptions decode_options;
        if (auto error = decodeOptions(decode_options)) {
            result.addError(*error);
            return result;
        }
        try {
            Value value = decode(data, decode_options);
            
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
        
        std::visit([&](auto&& arg) { validateTypedValue(arg, result, depth); }, value);
        
        return result;
    }
    
    // Whether the bytes decode with the settings validate() uses.
    bool quickCheckData(std::span<const uint8_t> data) const {
        if (data.empty() || data.size() > options.max_total_size) {
            return false;
        }
        DecodeOptions decode_options;
        if (decodeOptions(decode_options)) return false;
        try {
            (void)decode(data, decode_options);
            return true;
        } catch (...) {
            return false;
        }
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
    // Decode with the same settings as validate(), then re-encode canonically.
    DecodeOptions decode_options;
    if (pimpl_->decodeOptions(decode_options)) return {};
    try {
        Value value = decode(data, decode_options);
        
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

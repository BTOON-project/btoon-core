// SPDX-FileCopyrightText: 2025 Alvar Laigna <https://alvarlaigna.com>
// SPDX-License-Identifier: MIT
/**
 * @file validator.h
 * @brief Validation of BTOON data against size and type rules.
 *
 * Validator decodes encoded input with the hardened decoder (which enforces the
 * docs/SPEC.md §7 limits) and then checks the decoded value against ValidationOptions.
 * Validating encoded input allocates the decoded value, like btoon::decode().
 */

#ifndef BTOON_VALIDATOR_H
#define BTOON_VALIDATOR_H

#include "btoon.h"
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <limits>
#include <optional>
#include <memory>

namespace btoon {

/**
 * @brief Validation options for controlling strictness
 */
// How the options apply to encoded input (Validator::validate(span), sanitize()): the
// bytes are decoded in strict mode with max_depth, a decompression limit of
// min(max_total_size, kDefaultMaxDecompressedSize), and hmac_key. Strict decoding always
// rejects duplicate map keys and invalid UTF-8, so allow_duplicate_map_keys and
// require_utf8_strings only change the result for a decoded Value.
struct ValidationOptions {
    // Size limits
    size_t max_depth = 128;              // Maximum nesting depth for containers
    size_t max_string_length = 1024 * 1024 * 10;  // 10MB max string
    size_t max_binary_length = 1024 * 1024 * 100; // 100MB max binary
    size_t max_array_size = 1000000;     // Maximum array elements
    size_t max_map_size = 100000;        // Maximum map entries
    size_t max_total_size = 1024 * 1024 * 1024;   // 1GB max total size
    
    // Type validation
    bool allow_nan_float = false;        // Allow NaN values in floats
    bool allow_inf_float = false;        // Allow infinity values in floats
    bool require_utf8_strings = true;    // Validate UTF-8 encoding
    bool allow_duplicate_map_keys = false; // Allow duplicate keys in maps
    
    // Security options
    bool check_circular_references = true; // No effect: a Value can't hold a cycle
    bool validate_extension_types = true;  // Check extensions with TypeValidator::validateExtension (accepts every id)
    bool limit_recursion = true;          // No effect: decoding and validation are always depth-limited
    // The key for signed documents (docs/SPEC.md §8). Without it, a signed document fails
    // validation with authentication_failed. An empty key is an error, not "no key".
    std::optional<std::string> hmac_key;
    
    // Performance options
    bool fast_mode = false;               // No effect since 0.3.0 (it skipped the heuristic SecurityValidator scan)
    bool collect_stats = false;           // Collect validation statistics
};

/**
 * @brief Validation result with detailed error information
 */
struct ValidationResult {
    bool valid = true;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    
    // Optional statistics
    struct Stats {
        size_t total_size = 0;
        size_t max_depth_reached = 0;
        size_t string_count = 0;
        size_t binary_count = 0;
        size_t array_count = 0;
        size_t map_count = 0;
        size_t largest_string = 0;
        size_t largest_binary = 0;
        size_t largest_array = 0;
        size_t largest_map = 0;
    };
    
    std::optional<Stats> stats;
    
    void addError(const std::string& error) {
        valid = false;
        errors.push_back(error);
    }
    
    void addWarning(const std::string& warning) {
        warnings.push_back(warning);
    }
};

/**
 * @brief BTOON data validator
 */
class Validator {
public:
    explicit Validator(const ValidationOptions& options = ValidationOptions{});
    ~Validator();
    
    /**
     * @brief Validate encoded BTOON data
     * @param data Raw BTOON data to validate
     * @return Validation result with errors/warnings
     */
    ValidationResult validate(std::span<const uint8_t> data) const;

    /**
     * @brief Validate encoded BTOON data held in a vector
     *
     * A byte vector converts both to a span and to a Value (as Binary), so without this
     * overload `validate(bytes)` would be ambiguous.
     */
    ValidationResult validate(const std::vector<uint8_t>& data) const {
        return validate(std::span<const uint8_t>(data));
    }

    /**
     * @brief Validate a decoded Value
     * @param value Decoded value to validate
     * @return Validation result with errors/warnings
     */
    ValidationResult validate(const Value& value) const;
    
    /**
     * @brief Whether the data decodes, with the same decoder settings as validate().
     *
     * Skips validate()'s checks on the decoded value (lengths, sizes, floats, timestamps).
     * Before 0.3.0 it only looked at the first byte, so truncated input passed.
     */
    bool quickCheck(std::span<const uint8_t> data) const;
    
    /**
     * @brief Decodes, validates and re-encodes untrusted input canonically.
     * @return The canonical encoding (bare, without envelopes), or empty if the data
     *         doesn't decode or doesn't validate.
     */
    std::vector<uint8_t> sanitize(std::span<const uint8_t> data) const;
    
private:
    class ValidatorImpl;
    std::unique_ptr<ValidatorImpl> pimpl_;
};

/**
 * @brief Input bounds checker for safe parsing
 */
class BoundsChecker {
public:
    explicit BoundsChecker(std::span<const uint8_t> data);
    
    /**
     * @brief Check if reading n bytes at position is safe
     */
    bool canRead(size_t position, size_t n) const;
    
    /**
     * @brief Safely read a byte
     */
    std::optional<uint8_t> readByte(size_t& position) const;
    
    /**
     * @brief Safely read multiple bytes
     */
    std::optional<std::span<const uint8_t>> readBytes(size_t& position, size_t n) const;
    
    /**
     * @brief Get remaining bytes from position
     */
    size_t remaining(size_t position) const;
    
    /**
     * @brief Check if position is within bounds
     */
    bool inBounds(size_t position) const;
    
private:
    std::span<const uint8_t> data_;
};

/**
 * @brief UTF-8 validator
 */
class UTF8Validator {
public:
    /**
     * @brief Check if string is valid UTF-8
     * @param str String to validate
     * @return True if valid UTF-8
     */
    static bool isValid(std::string_view str);
    
    /**
     * @brief Sanitize string to valid UTF-8
     * @param str String to sanitize
     * @return Valid UTF-8: each maximal invalid subsequence (Unicode §3.9) is replaced
     *         with U+FFFD, and everything valid is kept
     */
    static std::string sanitize(std::string_view str);
    
private:
    static bool isValidCodePoint(uint32_t cp);
    static int getUTF8SequenceLength(uint8_t lead);
};

/**
 * @brief Type validator for specific BTOON types
 */
class TypeValidator {
public:
    /**
     * @brief Validate integer range
     */
    // (std::numeric_limits<T>::min)() and (…max)(): the parentheses keep <windows.h>'s min
    // and max macros from expanding here when NOMINMAX isn't defined.
    static bool validateInt(int64_t value, int64_t min = (std::numeric_limits<int64_t>::min)(),
                           int64_t max = (std::numeric_limits<int64_t>::max)());
    
    /**
     * @brief Validate unsigned integer range
     */
    static bool validateUint(uint64_t value, uint64_t min = 0,
                            uint64_t max = (std::numeric_limits<uint64_t>::max)());
    
    /**
     * @brief Validate float value
     */
    static bool validateFloat(double value, bool allowNaN = false, bool allowInf = false);
    
    /**
     * @brief Validate timestamp: nanoseconds below 10^9, and an offset within ±1080
     * minutes when it has one (docs/SPEC.md §5.1). Any seconds value is valid.
     */
    static bool validateTimestamp(const Timestamp& ts);
    
    /**
     * @brief Validate extension type
     */
    static bool validateExtension(const Extension& ext);
};

/**
 * Convenience functions
 */

/**
 * @brief Validate BTOON data with default options
 */
inline ValidationResult validate(std::span<const uint8_t> data) {
    Validator validator;
    return validator.validate(data);
}

/**
 * @brief Whether the data decodes (Validator::quickCheck with default options)
 */
inline bool isValid(std::span<const uint8_t> data) {
    Validator validator;
    return validator.quickCheck(data);
}

/**
 * @brief Strict validation with security checks
 */
inline ValidationResult validateStrict(std::span<const uint8_t> data) {
    ValidationOptions opts;
    opts.max_depth = 32;  // More restrictive
    opts.max_string_length = 1024 * 1024;  // 1MB
    opts.max_total_size = 100 * 1024 * 1024;  // 100MB
    opts.allow_duplicate_map_keys = false;
    opts.require_utf8_strings = true;
    
    Validator validator(opts);
    return validator.validate(data);
}

} // namespace btoon

#endif // BTOON_VALIDATOR_H

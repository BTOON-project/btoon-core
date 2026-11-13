//  ██████╗ ████████╗ ██████╗  ██████╗ ███╗   ██╗
//  ██╔══██╗╚══██╔══╝██╔═══██╗██╔═══██╗████╗  ██║
//  ██████╔╝   ██║   ██║   ██║██║   ██║██╔██╗ ██║
//  ██╔══██╗   ██║   ██║   ██║██║   ██║██║╚██╗██║
//  ██████╔╝   ██║   ╚██████╔╝╚██████╔╝██║ ╚████║
//  ╚═════╝    ╚═╝    ╚═════╝  ╚═════╝ ╚═╝  ╚═══╝
//
//  BTOON Core
//  https://btoon.net & https://github.com/BTOON-project/btoon-core
//
// SPDX-FileCopyrightText: 2025 Alvar Laigna <https://alvarlaigna.com>
// SPDX-License-Identifier: MIT
/**
 * @file btoon.h
 * @brief Main header file for the BTOON library.
 *
 * BTOON is MessagePack plus a columnar encoding for arrays of uniform maps,
 * a few extension types and optional compression/HMAC envelopes. The wire
 * format is specified in docs/SPEC.md.
 */
#ifndef BTOON_H
#define BTOON_H

// Single source of truth for the library version (CMake parses these lines).
#define BTOON_VERSION_MAJOR 0
#define BTOON_VERSION_MINOR 1
#define BTOON_VERSION_PATCH 5
#define BTOON_VERSION_STRING "0.1.5"
/// Version of docs/SPEC.md implemented by this library.
#define BTOON_SPEC_VERSION "0.1"

#include "btoon/compression.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace btoon {

// --- Errors ------------------------------------------------------------------

/// Machine-readable error categories (docs/SPEC.md §7).
enum class ErrorCode : uint8_t {
    generic = 0,
    truncated,
    invalid_marker,
    trailing_data,
    invalid_utf8,
    non_string_key,
    duplicate_key,
    max_depth,
    invalid_extension,
    invalid_tabular,
    invalid_envelope,
    size_limit,
    unsupported_compression,
    authentication_failed,
    invalid_argument,
};

/// Returns the spec name of an error code, e.g. "trailing_data".
const char* error_code_name(ErrorCode code) noexcept;

class BtoonException : public std::runtime_error {
public:
    explicit BtoonException(const std::string& message)
        : std::runtime_error(message), code_(ErrorCode::generic) {}
    explicit BtoonException(const char* message)
        : std::runtime_error(message), code_(ErrorCode::generic) {}
    BtoonException(ErrorCode code, const std::string& message)
        : std::runtime_error(message), code_(code) {}

    ErrorCode code() const noexcept { return code_; }
    const char* code_name() const noexcept { return error_code_name(code_); }

private:
    ErrorCode code_;
};

// --- Value types -------------------------------------------------------------

struct Value;

using Nil = std::nullptr_t;
using Bool = bool;
using Int = int64_t;
using Uint = uint64_t;
using Float = double;
using String = std::string;
using Binary = std::vector<uint8_t>;

/// Packed float32 array (extension -5, little-endian on the wire).
struct VectorFloat {
    std::vector<float> data;
    bool operator==(const VectorFloat& other) const { return data == other.data; }
    bool operator<(const VectorFloat& other) const { return data < other.data; }
};

/// Packed float64 array (extension -6, little-endian on the wire).
struct VectorDouble {
    std::vector<double> data;
    bool operator==(const VectorDouble& other) const { return data == other.data; }
    bool operator<(const VectorDouble& other) const { return data < other.data; }
};

/// Any extension type BTOON does not interpret itself.
struct Extension {
    int8_t type = 0;
    std::vector<uint8_t> data;
    bool operator==(const Extension& other) const { return type == other.type && data == other.data; }
    bool operator<(const Extension& other) const {
        return type != other.type ? type < other.type : data < other.data;
    }
};

/**
 * @brief An instant in UTC (extension -1, MessagePack-compatible).
 *
 * `seconds`/`nanoseconds` always describe the instant in UTC. A timezone
 * offset, if present, is presentation metadata (local = UTC + offset) and is
 * written using BTOON's 14-byte timestamp form (docs/SPEC.md §5.1).
 */
struct Timestamp {
    int64_t seconds = 0;          ///< Seconds since the Unix epoch (UTC).
    uint32_t nanoseconds = 0;     ///< 0 … 999'999'999.
    int16_t timezone_offset = 0;  ///< Minutes east of UTC, -1080 … 1080.
    bool has_timezone = false;    ///< Whether timezone_offset is meaningful.

    Timestamp() = default;
    explicit Timestamp(int64_t sec) : seconds(sec) {}
    Timestamp(int64_t sec, uint32_t nano) : seconds(sec), nanoseconds(nano) {}
    Timestamp(int64_t sec, uint32_t nano, int16_t tz_offset)
        : seconds(sec), nanoseconds(nano), timezone_offset(tz_offset), has_timezone(true) {}

    static Timestamp now();
    static Timestamp from_microseconds(int64_t micros);
    static Timestamp from_milliseconds(int64_t millis);

    int64_t to_microseconds() const { return seconds * 1000000LL + nanoseconds / 1000; }
    int64_t to_milliseconds() const { return seconds * 1000LL + nanoseconds / 1000000; }
    double to_seconds_double() const {
        return static_cast<double>(seconds) + static_cast<double>(nanoseconds) / 1e9;
    }

    bool operator==(const Timestamp& other) const {
        return seconds == other.seconds && nanoseconds == other.nanoseconds &&
               has_timezone == other.has_timezone &&
               (!has_timezone || timezone_offset == other.timezone_offset);
    }
    bool operator<(const Timestamp& other) const {
        return seconds != other.seconds ? seconds < other.seconds : nanoseconds < other.nanoseconds;
    }
};

/// Milliseconds since the Unix epoch, UTC (extension -2).
struct Date {
    int64_t milliseconds = 0;
    bool operator==(const Date& other) const { return milliseconds == other.milliseconds; }
    bool operator<(const Date& other) const { return milliseconds < other.milliseconds; }
};

/// Nanoseconds since the Unix epoch, UTC (extension -3).
struct DateTime {
    int64_t nanoseconds = 0;
    bool operator==(const DateTime& other) const { return nanoseconds == other.nanoseconds; }
    bool operator<(const DateTime& other) const { return nanoseconds < other.nanoseconds; }
};

/**
 * @brief Arbitrary-precision integer (extension -4).
 *
 * `bytes` holds the value as signed two's complement, big-endian. Equality
 * compares numeric values, so redundant sign bytes don't matter.
 */
struct BigInt {
    std::vector<uint8_t> bytes;

    /// Strips redundant leading sign bytes (0x00 / 0xff). Empty stays empty.
    std::vector<uint8_t> minimal_bytes() const;
    bool operator==(const BigInt& other) const { return minimal_bytes() == other.minimal_bytes(); }
    bool operator<(const BigInt& other) const { return bytes < other.bytes; }
};

struct Value : std::variant<
    Nil,
    Bool,
    Int,
    Uint,
    Float,
    String,
    Binary,
    std::vector<struct Value>,
    std::map<String, struct Value>,
    Extension,
    Timestamp,
    Date,
    DateTime,
    BigInt,
    VectorFloat,
    VectorDouble
> {
    using variant::variant;

    /// Spec name of the held type ("nil", "int", "map", …).
    const char* type_name() const;

    bool is_nil() const noexcept { return std::holds_alternative<Nil>(*this); }
    /// True for Int and Uint (not BigInt).
    bool is_integer() const noexcept {
        return std::holds_alternative<Int>(*this) || std::holds_alternative<Uint>(*this);
    }
    /// The integer value if it is an Int, or a Uint ≤ INT64_MAX.
    std::optional<int64_t> as_int64() const noexcept;
    /// The integer value if it is a Uint, or a non-negative Int.
    std::optional<uint64_t> as_uint64() const noexcept;
    /// Int, Uint or Float converted to double.
    std::optional<double> as_double() const noexcept;

    /**
     * Value equality. Integers compare numerically across Int/Uint (MessagePack
     * does not distinguish them on the wire); floats use IEEE `==`.
     */
    bool operator==(const Value& other) const {
        if (index() != other.index()) {
            if (is_integer() && other.is_integer()) {
                auto a = as_uint64();
                auto b = other.as_uint64();
                if (a && b) return *a == *b;
                auto c = as_int64();
                auto d = other.as_int64();
                return c && d && *c == *d;
            }
            return false;
        }
        return std::visit([&](const auto& arg) -> bool {
            using T = std::decay_t<decltype(arg)>;
            return arg == *std::get_if<T>(&other);
        }, static_cast<const variant&>(*this));
    }
    // Declared so that `a != b` uses the semantic comparison above instead of
    // std::variant's operator!=, which doesn't compile for these alternatives.
    bool operator!=(const Value& other) const { return !(*this == other); }
};

using Array = std::vector<Value>;
using Map = std::map<String, Value>;

// --- Options -----------------------------------------------------------------

inline constexpr size_t kDefaultMaxDepth = 128;
inline constexpr size_t kDefaultMaxDecompressedSize = size_t(256) * 1024 * 1024;

struct EncodeOptions {
    /// Wrap the output in a compression envelope (docs/SPEC.md §6).
    bool compress = false;
    /// Algorithm-specific level; 0 = derive from compression_preset.
    int compression_level = 0;
    /// Algorithm used when compress=true. NONE means "default" (zlib).
    CompressionAlgorithm compression_algorithm = CompressionAlgorithm::NONE;
    /// Use the columnar encoding for arrays of uniform maps when smaller.
    bool auto_tabular = true;

    CompressionLevel compression_preset = CompressionLevel::DEFAULT;
    CompressionProfile compression_profile;  ///< Used when use_profile=true.
    bool use_profile = false;
    bool adaptive_compression = false;       ///< Pick the algorithm from the data.
    size_t min_compression_size = 256;       ///< Smaller payloads are never compressed.

    /// Maximum container nesting; deeper values (or cycles) fail with max_depth.
    size_t max_depth = kDefaultMaxDepth;
    /// Reject strings/keys that are not well-formed UTF-8.
    bool validate_utf8 = true;
    /// If non-empty, wrap the result in an HMAC-SHA256 signed envelope (§8).
    std::string hmac_key;
};

struct DecodeOptions {
    /// Transparently decompress compression envelopes. If false, they are rejected.
    bool auto_decompress = true;
    /// Strict mode (docs/SPEC.md §7). Non-strict allows trailing data and
    /// duplicate keys and skips UTF-8 validation; it never relaxes bounds checks.
    bool strict = true;
    /// Maximum container nesting depth.
    size_t max_depth = kDefaultMaxDepth;
    /// Upper bound for a compression envelope's uncompressed size.
    size_t max_decompressed_size = kDefaultMaxDecompressedSize;
    /// If non-empty, require and verify an HMAC-SHA256 signed envelope (§8).
    std::string hmac_key;
};

// --- Core functions ----------------------------------------------------------

/// Encodes a value (canonical form), optionally compressed and/or signed.
std::vector<uint8_t> encode(const Value& value, const EncodeOptions& options = {});

/// Decodes a complete BTOON document (bare, compressed and/or signed).
Value decode(std::span<const uint8_t> data, const DecodeOptions& options = {});

// --- Utilities ---------------------------------------------------------------

/// True if `arr` qualifies for tabular encoding *and* tabular is smaller
/// (docs/SPEC.md §5.6.2). This is exactly the rule the encoder applies.
bool is_tabular(const Array& arr);

/// The size rule of SPEC §5.6.2 alone: is the tabular layout smaller for
/// `num_rows` rows (≥ 2) with keys of these UTF-8 byte lengths (≥ 1 key)?
bool tabular_beneficial(size_t num_rows, std::span<const size_t> key_utf8_lengths);

/// Applies the compression and signed envelopes requested by `options` to an
/// already-encoded bare document (what encode() does after encoding).
std::vector<uint8_t> apply_envelopes(std::vector<uint8_t> bare, const EncodeOptions& options);

/// Library version, e.g. "0.1.0".
const char* version();

/// Spec version implemented, e.g. "0.1".
const char* spec_version();

} // namespace btoon

#endif // BTOON_H

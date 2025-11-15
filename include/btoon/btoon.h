//  ██████╗ ████████╗ ██████╗  ██████╗ ███╗   ██╗
//  ██╔══██╗╚══██╔══╝██╔═══██╗██╔═══██╗████╗  ██║
//  ██████╔╝   ██║   ██║   ██║██║   ██║██╔██╗ ██║
//  ██╔══██╗   ██║   ██║   ██║██║   ██║██║╚██╗██║
//  ██████╔╝   ██║   ╚██████╔╝╚██████╔╝██║ ╚████║
//  ╚═════╝    ╚═╝    ╚═════╝  ╚═════╝ ╚═╝  ╚═══╝
//
//  BTOON Core
//  Version 0.0.1
//  https://btoon.net & https://github.com/BTOON-project/btoon-core
//
// SPDX-FileCopyrightText: 2025 Alvar Laigna <https://alvarlaigna.com>
// SPDX-License-Identifier: MIT
/**
 * @file btoon.h
 * @brief Main header file for the BTOON library.
 *
 * This file provides the primary, user-friendly interface for BTOON,
 * a high-performance binary serialization format that extends MessagePack.
 */
#ifndef BTOON_H
#define BTOON_H

#include "btoon/compression.h"
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <variant>
#include <span>
#include <stdexcept>

namespace btoon {

// Forward declaration for the recursive Value variant
struct Value;

// Core Types
using Nil = std::nullptr_t;
using Bool = bool;
using Int = int64_t;
using Uint = uint64_t;
using Float = double;
using String = std::string;
using Binary = std::vector<uint8_t>;

struct VectorFloat {
    std::vector<float> data;

    bool operator==(const VectorFloat& other) const {
        return data == other.data;
    }

    bool operator<(const VectorFloat& other) const {
        return std::lexicographical_compare(data.begin(), data.end(), other.data.begin(), other.data.end());
    }
};

struct VectorDouble {
    std::vector<double> data;

    bool operator==(const VectorDouble& other) const {
        return data == other.data;
    }

    bool operator<(const VectorDouble& other) const {
        return std::lexicographical_compare(data.begin(), data.end(), other.data.begin(), other.data.end());
    }
};

struct Extension {
    int8_t type;
    std::vector<uint8_t> data;

    bool operator==(const Extension& other) const {
        return type == other.type && data == other.data;
    }

    bool operator<(const Extension& other) const {
        if (type != other.type) {
            return type < other.type;
        }
        return std::lexicographical_compare(data.begin(), data.end(), other.data.begin(), other.data.end());
    }
};

struct Timestamp {
    int64_t seconds;

    bool operator==(const Timestamp& other) const {
        return seconds == other.seconds;
    }

    bool operator<(const Timestamp& other) const {
        return seconds < other.seconds;
    }
};

struct Date {
    int64_t milliseconds;

    bool operator==(const Date& other) const {
        return milliseconds == other.milliseconds;
    }

    bool operator<(const Date& other) const {
        return milliseconds < other.milliseconds;
    }
};

struct DateTime {
    int64_t nanoseconds;

    bool operator==(const DateTime& other) const {
        return nanoseconds == other.nanoseconds;
    }

    bool operator<(const DateTime& other) const {
        return nanoseconds < other.nanoseconds;
    }
};

struct BigInt {
    std::vector<uint8_t> bytes;

    bool operator==(const BigInt& other) const {
        return bytes == other.bytes;
    }

    bool operator<(const BigInt& other) const {
        return std::lexicographical_compare(bytes.begin(), bytes.end(), other.bytes.begin(), other.bytes.end());
    }
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
    const char* type_name() const;

    bool operator==(const Value& other) const {
        if (index() != other.index()) {
            return false;
        }
        return std::visit([&](auto&& arg) -> bool {
            using T = std::decay_t<decltype(arg)>;
            auto other_arg = std::get_if<T>(&other);
            if (!other_arg) {
                return false;
            }
            return arg == *other_arg;
        }, *this);
    }
};

using Array = std::vector<Value>;
using Map = std::map<String, Value>;

struct EncodeOptions {
    bool compress = false;
    int compression_level = 0;  // Numeric level for backward compatibility
    CompressionAlgorithm compression_algorithm = CompressionAlgorithm::NONE;
    bool auto_tabular = true;
    
    // New compression level system
    CompressionLevel compression_preset = CompressionLevel::DEFAULT;
    CompressionProfile compression_profile;  // Optional: use profile instead
    bool use_profile = false;  // If true, use compression_profile instead of algorithm/level
    
    // Adaptive compression
    bool adaptive_compression = false;  // Auto-select best algorithm based on data
    size_t min_compression_size = 256;  // Minimum size to compress
    
    // Potentially add security options here in the future
};

struct DecodeOptions {
    bool auto_decompress = true;
    bool strict = true;
    // Potentially add security options here in the future
};

// --- Core Functions ---

std::vector<uint8_t> encode(const Value& value, const EncodeOptions& options = {});
Value decode(std::span<const uint8_t> data, const DecodeOptions& options = {});

// --- Utility Functions ---

bool is_tabular(const Array& arr);
const char* version();

// --- Exceptions ---

class BtoonException : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

} // namespace btoon

#endif // BTOON_BTOON_H} // namespace btoon

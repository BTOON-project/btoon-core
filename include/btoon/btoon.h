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
using StringView = std::string_view;
using Binary = std::vector<uint8_t>;
using BinaryView = std::span<const uint8_t>;
using Array = std::vector<Value>;
using Map = std::map<String, Value>;

struct VectorFloat {
    std::vector<float> data;

    bool operator==(const VectorFloat& other) const {
        return data == other.data;
    }

    bool operator<(const VectorFloat& other) const {
        return std::lexicographical_compare(data.begin(), data.end(), other.data.begin(), other.data.end());
    }
};

using VectorFloatView = std::span<const float>;

struct VectorDouble {
    std::vector<double> data;

    bool operator==(const VectorDouble& other) const {
        return data == other.data;
    }

    bool operator<(const VectorDouble& other) const {
        return std::lexicographical_compare(data.begin(), data.end(), other.data.begin(), other.data.end());
    }
};

using VectorDoubleView = std::span<const double>;

inline bool operator==(const BinaryView& lhs, const BinaryView& rhs) {
    return std::equal(lhs.begin(), lhs.end(), rhs.begin(), rhs.end());
}

inline bool operator==(const VectorFloatView& lhs, const VectorFloatView& rhs) {
    return std::equal(lhs.begin(), lhs.end(), rhs.begin(), rhs.end());
}

inline bool operator==(const VectorDoubleView& lhs, const VectorDoubleView& rhs) {
    return std::equal(lhs.begin(), lhs.end(), rhs.begin(), rhs.end());
}

struct Extension {
    int8_t type;
    std::span<const uint8_t> data;

    bool operator==(const Extension& other) const {
        return type == other.type && std::equal(data.begin(), data.end(), other.data.begin(), other.data.end());
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
    std::span<const uint8_t> bytes;

    bool operator==(const BigInt& other) const {
        return std::equal(bytes.begin(), bytes.end(), other.bytes.begin(), other.bytes.end());
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
    StringView,
    Binary,
    BinaryView,
    Array,
    Map,
    Extension,
    Timestamp,
    Date,
    DateTime,
    BigInt,
    VectorFloat,
    VectorFloatView,
    VectorDouble,
    VectorDoubleView
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
            if constexpr (std::is_same_v<T, StringView> || std::is_same_v<T, BinaryView> || std::is_same_v<T, VectorFloatView> || std::is_same_v<T, VectorDoubleView>) {
                return std::equal(arg.begin(), arg.end(), other_arg->begin(), other_arg->end());
            } else {
                return arg == *other_arg;
            }
        }, *this);
    }
};

struct EncodeOptions {
    bool compress = false;
    int compression_level = 0;
    CompressionAlgorithm compression_algorithm = CompressionAlgorithm::NONE;
    bool auto_tabular = true;
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

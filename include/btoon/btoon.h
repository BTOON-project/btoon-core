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
using Array = std::vector<Value>;
using Map = std::map<String, Value>;

/**
 * @brief Represents a MessagePack extension type.
 */
struct Extension {
    int8_t type;
    std::vector<uint8_t> data;
};

/**
 * @brief Represents a MessagePack timestamp.
 */
struct Timestamp {
    int64_t seconds;
};

/**
 * @brief Represents a Date.
 */
struct Date {
    int64_t milliseconds;
};

/**
 * @brief Represents a BigInt.
 */
struct BigInt {
    std::vector<uint8_t> bytes;
};

/**
 * @brief A variant type representing any serializable BTOON value.
 *
 * This is the central type used for interacting with the high-level
 * encode and decode functions.
 */
struct Value : std::variant<
    Nil,
    Bool,
    Int,
    Uint,
    Float,
    String,
    StringView,
    Binary,
    Array,
    Map,
    Extension,
    Timestamp,
    Date,
    BigInt
> {
    // Inherit constructors from std::variant
    using variant::variant;
};

/**
 * @brief Options for configuring the encoding process.
 */
struct EncodeOptions {
    bool compress = false;
    bool auto_tabular = true;
    CompressionAlgorithm compression_algorithm = CompressionAlgorithm::ZLIB;
    int compression_level = 0; // 0 lets the library choose a default
    // std::string hmac_key; // Future security feature
};

/**
 * @brief Options for configuring the decoding process.
 */
struct DecodeOptions {
    bool decompress = false;
    bool strict = true; // Enforce strict MessagePack compliance
    // std::string hmac_key; // Future security feature
};

/**
 * @brief Exception class for BTOON library errors.
 */
class BtoonException : public std::runtime_error {
public:
    explicit BtoonException(const std::string& message) : std::runtime_error(message) {}
};

/**
 * @brief Encodes a btoon::Value into a binary byte vector.
 *
 * @param value The btoon::Value to encode.
 * @param options Configuration for the encoding process.
 * @return A std::vector<uint8_t> containing the serialized data.
 * @throws BtoonException on encoding failure.
 */
std::vector<uint8_t> encode(const Value& value, const EncodeOptions& options = {});

/**
 * @brief Decodes a binary byte span into a btoon::Value.
 *
 * @param data A std::span<const uint8_t> pointing to the binary data.
 * @param options Configuration for the decoding process.
 * @return The decoded btoon::Value.
 * @throws BtoonException on decoding failure or data corruption.
 */
Value decode(std::span<const uint8_t> data, const DecodeOptions& options = {});

/**
 * @brief Checks if a btoon::Array is eligible for tabular optimization.
 *
 * An array is tabular if it's not empty and all its elements are maps
 * with the exact same set of keys.
 *
 * @param arr The btoon::Array to check.
 * @return True if the array is tabular, false otherwise.
 */
bool is_tabular(const Array& arr);

/**
 * @brief Returns the version of the BTOON library.
 * @return A const char* to the version string.
 */
const char* version();

} // namespace btoon

#endif // BTOON_H

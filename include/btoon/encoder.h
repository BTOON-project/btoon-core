/**
 * @file encoder.h
 * @brief Header file for the BTOON Encoder class.
 * 
 * This file defines the Encoder class, which provides methods to serialize data
 * into the BTOON binary format. It supports a variety of data types including
 * primitive types, arrays, maps, timestamps, and custom extensions.
 */
#ifndef BTOON_ENCODER_H
#define BTOON_ENCODER_H

#include <cstdint>
#include <string>
#include <vector>
#include <map>

#include "btoon.h"
#include "security.h"

namespace btoon {

/**
 * @brief Class responsible for encoding data into BTOON binary format.
 * 
 * The Encoder class provides a set of methods to convert various data types into
 * a compact binary representation following the BTOON specification. It is designed
 * for efficiency and minimal memory footprint.
 */
class Encoder {
public:
    /**
     * @brief Default constructor for Encoder.
     */
    Encoder() = default;
    
    /**
     * @brief Constructor with security settings for HMAC signing.
     * @param security Reference to a Security object for signing encoded data.
     */
    explicit Encoder(const Security& security) : security_(&security), useSecurity_(true) {}
    
    /**
     * @brief Default destructor for Encoder.
     */
    ~Encoder() = default;

    /**
     * @brief Enables or disables security signing for encoded data.
     * @param enable Boolean to enable (true) or disable (false) security.
     */
    void setSecurityEnabled(bool enable) { useSecurity_ = enable; }

    // Encode basic types
    /**
     * @brief Encodes a null value.
     * @return A vector of bytes representing the encoded null value.
     */
    std::vector<uint8_t> encodeNil() const;
    
    /**
     * @brief Encodes a boolean value.
     * @param value The boolean value to encode.
     * @return A vector of bytes representing the encoded boolean.
     */
    std::vector<uint8_t> encodeBool(bool value) const;
    
    /**
     * @brief Encodes a signed integer value.
     * @param value The 64-bit signed integer to encode.
     * @return A vector of bytes representing the encoded integer.
     */
    std::vector<uint8_t> encodeInt(int64_t value) const;
    
    /**
     * @brief Encodes an unsigned integer value.
     * @param value The 64-bit unsigned integer to encode.
     * @return A vector of bytes representing the encoded unsigned integer.
     */
    std::vector<uint8_t> encodeUint(uint64_t value) const;
    
    /**
     * @brief Encodes a floating-point value.
     * @param value The double-precision floating-point number to encode.
     * @return A vector of bytes representing the encoded float.
     */
    std::vector<uint8_t> encodeFloat(double value) const;
    
    /**
     * @brief Encodes a string value.
     * @param value The string to encode.
     * @return A vector of bytes representing the encoded string.
     */
    std::vector<uint8_t> encodeString(const std::string& value) const;
    
    /**
     * @brief Encodes binary data.
     * @param value The vector of bytes to encode as binary data.
     * @return A vector of bytes representing the encoded binary data.
     */
    std::vector<uint8_t> encodeBinary(const std::vector<uint8_t>& value) const;

    // Encode compound types
    /**
     * @brief Encodes an array of elements.
     * @param elements A vector of encoded elements to be serialized as an array.
     * @return A vector of bytes representing the encoded array.
     */
    std::vector<uint8_t> encodeArray(const std::vector<std::vector<uint8_t>>& elements) const;
    
    /**
     * @brief Encodes a map of key-value pairs.
     * @param pairs A map of string keys to encoded values to be serialized as a map.
     * @return A vector of bytes representing the encoded map.
     */
    std::vector<uint8_t> encodeMap(const std::map<std::string, std::vector<uint8_t>>& pairs) const;

    // Encode timestamp
    /**
     * @brief Encodes a timestamp value.
     * @param timestamp A 64-bit integer representing a timestamp.
     * @return A vector of bytes representing the encoded timestamp.
     */
    std::vector<uint8_t> encodeTimestamp(int64_t timestamp) const;

    // Custom extension types
    std::vector<uint8_t> encodeDate(int64_t milliseconds) const;
    std::vector<uint8_t> encodeBigInt(const std::vector<uint8_t>& bytes) const;

    // Generic extension type
    std::vector<uint8_t> encodeExtension(int8_t type, const std::vector<uint8_t>& data) const;

private:
    // Helper methods for encoding variable-length integers
    std::vector<uint8_t> encodeVarInt(uint64_t value, uint8_t prefix, uint8_t bits) const;
    
    /**
     * @brief Adds an HMAC signature to the encoded data if security is enabled.
     * @param data The encoded binary data to sign.
     * @return A vector of bytes with the signature prepended if security is enabled.
     */
    std::vector<uint8_t> addSignatureIfEnabled(const std::vector<uint8_t>& data) const;

    const Security* security_ = nullptr; /**< Pointer to Security object for HMAC signing. */
    bool useSecurity_ = false;           /**< Flag to enable/disable security signing. */
};

} // namespace btoon

#endif // BTOON_ENCODER_H

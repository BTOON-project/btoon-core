//  BTOON Core
//  https://btoon.net & https://github.com/BTOON-project/btoon-core
//
// SPDX-FileCopyrightText: 2025 Alvar Laigna <https://alvarlaigna.com>
// SPDX-License-Identifier: MIT

/**
 * @file encoder.h
 * @brief Streaming, canonical BTOON encoder.
 *
 * The Encoder appends canonical encodings (docs/SPEC.md §4) to a single growing
 * buffer. Use `encode(const Value&)` for whole values, or the primitive
 * `encode*` / `*Header` methods to emit values incrementally without building
 * a Value tree first.
 */
#ifndef BTOON_ENCODER_H
#define BTOON_ENCODER_H

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "btoon.h"
#include "memory_pool.h"
#include "security.h"

namespace btoon {

class Encoder {
public:
    Encoder();
    explicit Encoder(const EncodeOptions& options);
    /// The pool is no longer used; kept for source compatibility with 0.0.x.
    explicit Encoder(MemoryPool* pool);
    /// Output is wrapped in an HMAC-SHA256 signed envelope (docs/SPEC.md §8).
    explicit Encoder(const Security& security);
    Encoder(const Security& security, MemoryPool* pool);
    ~Encoder();

    Encoder(const Encoder&) = delete;
    Encoder& operator=(const Encoder&) = delete;
    Encoder(Encoder&&) noexcept = default;
    Encoder& operator=(Encoder&&) noexcept = default;

    void setOptions(const EncodeOptions& opts) { options_ = opts; }
    const EncodeOptions& options() const noexcept { return options_; }
    void setSecurityEnabled(bool enable) { useSecurity_ = enable && security_ != nullptr; }

    /// Appends the canonical encoding of `value` (recursive, depth-limited).
    void encode(const Value& value);

    // --- Primitives (always canonical) --------------------------------------
    void encodeNil();
    void encodeBool(bool value);
    void encodeInt(int64_t value);
    void encodeUint(uint64_t value);
    void encodeFloat(double value);
    void encodeString(std::string_view value);
    void encodeBinary(std::span<const uint8_t> value);

    /// Headers for incremental encoding: write the header, then the elements.
    void encodeArrayHeader(size_t count);
    void encodeMapHeader(size_t count);
    void encodeExtensionHeader(int8_t type, size_t length);

    void encodeExtension(int8_t type, std::span<const uint8_t> data);
    void encodeTimestamp(const Timestamp& timestamp);
    void encodeDate(int64_t milliseconds);
    void encodeDateTime(int64_t nanoseconds);
    /// Two's-complement big-endian bytes; normalized to a plain int if it fits.
    void encodeBigInt(std::span<const uint8_t> bytes);
    void encodeVectorFloat(const VectorFloat& value);
    void encodeVectorDouble(const VectorDouble& value);
    void encodeVectorFloat(std::span<const float> values);
    void encodeVectorDouble(std::span<const double> values);

    /// Encodes an array, using the tabular layout when it qualifies and is smaller.
    void encodeColumnar(const Array& rows);

    // --- Incremental tabular encoding (SPEC §5.6) ---------------------------
    // For bindings that walk their own data structures instead of building a
    // Value tree. The caller decides that the rows qualify and that
    // tabular_beneficial() holds, then writes, for each column in order,
    // exactly `num_rows` values between beginTabularColumn()/endTabularColumn().
    // Column names must be in canonical (UTF-8 byte) order for canonical output.
    // Type codes are derived from each column's first encoded value. Tabular
    // sections may nest (a cell can itself be a tabular array).
    void beginTabular(std::span<const std::string_view> column_names, size_t num_rows);
    void beginTabularColumn();
    void endTabularColumn();
    void endTabular();

    // --- Pre-encoded elements (0.0.x API) -----------------------------------
    void encodeArray(const std::vector<std::vector<uint8_t>>& encoded_elements);
    void encodeMap(const std::map<std::string, std::vector<uint8_t>>& encoded_values);
    /// Appends bytes verbatim. The caller is responsible for their validity.
    void encodeRaw(std::span<const uint8_t> bytes);

    // --- Output -------------------------------------------------------------
    /// Finalizes (signs once, if security is enabled) and returns the bytes.
    std::span<const uint8_t> getBuffer();
    /// Finalizes and moves the bytes out; the encoder is left empty.
    std::vector<uint8_t> release();
    size_t size() const noexcept { return len_; }
    void clear() noexcept;

private:
    void encodeValue(const Value& value, size_t depth);
    void encodeArrayValue(const Array& array, size_t depth);
    void encodeMapValue(const Map& map, size_t depth);
    bool tryEncodeTabular(const Array& rows, size_t depth);
    void finalize();

    uint8_t* reserve(size_t n) {
        if (buf_.size() - len_ < n) grow(n);
        return buf_.data() + len_;
    }
    void grow(size_t n);
    void put8(uint8_t b) { *reserve(1) = b; ++len_; }
    void putBytes(const void* data, size_t n);

    struct TabularFrame {
        size_t header_pos;
        size_t payload_pos;
        size_t codes_pos;
        size_t column_len_pos;
        size_t columns;
        size_t columns_left;
    };
    std::vector<TabularFrame> tabular_;

    const Security* security_ = nullptr;
    bool useSecurity_ = false;
    bool finalized_ = false;
    EncodeOptions options_;
    std::vector<uint8_t> buf_;  // buf_.size() is capacity in use; len_ is the logical size
    size_t len_ = 0;
};

} // namespace btoon

#endif // BTOON_ENCODER_H

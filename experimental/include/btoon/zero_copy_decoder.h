// SPDX-FileCopyrightText: 2025 Alvar Laigna <https://alvarlaigna.com>
// SPDX-License-Identifier: MIT
/**
 * @file zero_copy_decoder.h
 * @brief EXPERIMENTAL, not built or installed: a decoder that returns views into its input.
 *
 * Moved out of include/btoon/zero_copy.h in 0.3.0 (review 2026-11-13). It decodes only a
 * subset of the format (no int8-64 markers, floats, str32, bin32, array16/32, map16/32,
 * extensions other than timestamps, or tabular), ignores its validate_utf8 and
 * check_bounds options, accepts duplicate keys and trailing bytes, and has no value
 * budget. To promote it, it needs the hardened decoder's checks (src/decoder.cpp) and the
 * conformance vectors.
 */
#ifndef BTOON_ZERO_COPY_DECODER_H
#define BTOON_ZERO_COPY_DECODER_H

#include "btoon/btoon.h"
#include "btoon/zero_copy.h"

#include <span>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace btoon {

struct ValueView;
using ArrayView = std::vector<ValueView>;
using MapView = std::unordered_map<std::string_view, ValueView>;

struct ValueView : std::variant<
    Nil,
    Bool,
    Int,
    Uint,
    Float,
    std::string_view,
    std::span<const uint8_t>,
    ArrayView,
    MapView,
    Timestamp,
    Extension
> {
    using variant::variant;
};

/**
 * @brief Zero-copy decoder that provides views into original data
 * 
 * Instead of copying strings and binary data, provides string_view
 * and span references into the original encoded buffer.
 */
class ZeroCopyDecoder {
public:
    struct Options {
        bool validate_utf8;
        bool check_bounds;
        size_t max_depth;

        Options()
            : validate_utf8(true),
              check_bounds(true),
              max_depth(128) {}
    };
    
    explicit ZeroCopyDecoder(const Options& opts = Options())
        : options_(opts) {}
    
    /**
     * @brief Decode with zero-copy for strings and binary
     * 
     * Returns a value tree where strings and binary data are
     * views into the original buffer rather than copies.
     */
    ValueView decode(const MemoryView& view);
    
    /**
     * @brief Decode from raw pointer (caller manages lifetime)
     */
    ValueView decode(const uint8_t* data, size_t size);
    
private:
    ValueView decodeValue(const uint8_t* data, size_t size, size_t& pos, size_t depth);
    Options options_;
};

} // namespace btoon

#endif // BTOON_ZERO_COPY_DECODER_H

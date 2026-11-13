// SPDX-FileCopyrightText: 2025 Alvar Laigna <https://alvarlaigna.com>
// SPDX-License-Identifier: MIT
//
// Run-length coding: a sequence of (value, run_length ≥ 1) BTOON value pairs.
#include "btoon/rle_codec.h"
#include "btoon/decoder.h"
#include "btoon/encoder.h"
#include "internal.h"

#include <algorithm>

namespace btoon {

std::vector<uint8_t> RleCodec::encode(const Array& data) {
    // Values belong to one run only if their canonical encodings match. Value equality
    // would merge 0.0 with -0.0 (and 1 with 1.0 in some languages), which changes data.
    std::vector<std::vector<uint8_t>> encoded;
    encoded.reserve(data.size());
    for (const auto& v : data) encoded.push_back(btoon::encode(v));
    Encoder encoder;
    size_t i = 0;
    while (i < data.size()) {
        size_t run = 1;
        while (i + run < data.size() && encoded[i + run] == encoded[i]) ++run;
        encoder.encode(data[i]);
        encoder.encodeUint(run);
        i += run;
    }
    return encoder.release();
}

Array RleCodec::decode(const std::vector<uint8_t>& data, size_t max_elements, size_t max_bytes) {
    Decoder decoder;
    Array result;
    size_t bytes = 0;
    std::span<const uint8_t> rest(data);
    while (!rest.empty()) {
        auto [value, used] = decoder.decode_and_get_pos(rest);
        rest = rest.subspan(used);
        if (rest.empty()) detail::fail(ErrorCode::truncated, "RleCodec: value without run length");
        auto [run_value, used_len] = decoder.decode_and_get_pos(rest);
        rest = rest.subspan(used_len);
        auto run = run_value.as_uint64();
        if (!run || *run == 0) detail::fail(ErrorCode::invalid_argument, "RleCodec: run length must be ≥ 1");
        if (*run > max_elements - result.size()) detail::fail(ErrorCode::size_limit, "RleCodec: too many elements");
        // Each copy costs the value's whole footprint, not one element.
        const size_t each = detail::value_footprint(value);
        if (*run > (max_bytes - std::min(bytes, max_bytes)) / each) {
            detail::fail(ErrorCode::size_limit, "RleCodec: decoded data would exceed max_bytes");
        }
        bytes += each * static_cast<size_t>(*run);
        result.insert(result.end(), static_cast<size_t>(*run), value);
    }
    return result;
}

} // namespace btoon

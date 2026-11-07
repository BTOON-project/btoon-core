// SPDX-FileCopyrightText: 2025 Alvar Laigna <https://alvarlaigna.com>
// SPDX-License-Identifier: MIT
//
// Run-length coding: a sequence of (value, run_length ≥ 1) BTOON value pairs.
#include "btoon/rle_codec.h"
#include "btoon/decoder.h"
#include "btoon/encoder.h"
#include "internal.h"

namespace btoon {

std::vector<uint8_t> RleCodec::encode(const Array& data) {
    Encoder encoder;
    size_t i = 0;
    while (i < data.size()) {
        size_t run = 1;
        while (i + run < data.size() && data[i + run] == data[i]) ++run;
        encoder.encode(data[i]);
        encoder.encodeUint(run);
        i += run;
    }
    return encoder.release();
}

Array RleCodec::decode(const std::vector<uint8_t>& data, size_t max_elements) {
    Decoder decoder;
    Array result;
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
        result.insert(result.end(), static_cast<size_t>(*run), value);
    }
    return result;
}

} // namespace btoon

// SPDX-FileCopyrightText: 2025 Alvar Laigna <https://alvarlaigna.com>
// SPDX-License-Identifier: MIT
//
// Delta coding for integer sequences: each element is stored as the (wrapping)
// difference from its predecessor, as a sequence of BTOON integers.
#include "btoon/delta_codec.h"
#include "btoon/decoder.h"
#include "btoon/encoder.h"
#include "internal.h"

namespace btoon {

std::vector<uint8_t> DeltaCodec::encode(const Array& data) {
    Encoder encoder;
    uint64_t last = 0;
    for (const auto& value : data) {
        auto v = value.as_int64();
        if (!v) detail::fail(ErrorCode::invalid_argument, "DeltaCodec encodes int64 values only");
        uint64_t cur = static_cast<uint64_t>(*v);
        encoder.encodeInt(static_cast<int64_t>(cur - last));  // wrapping, no signed overflow
        last = cur;
    }
    return encoder.release();
}

Array DeltaCodec::decode(const std::vector<uint8_t>& data, size_t max_elements) {
    Decoder decoder;
    Array result;
    std::span<const uint8_t> rest(data);
    uint64_t last = 0;
    while (!rest.empty()) {
        if (result.size() >= max_elements) detail::fail(ErrorCode::size_limit, "DeltaCodec: too many elements");
        auto [value, used] = decoder.decode_and_get_pos(rest);
        rest = rest.subspan(used);
        auto delta = value.as_int64();
        if (!delta) detail::fail(ErrorCode::invalid_argument, "DeltaCodec: expected an integer delta");
        last += static_cast<uint64_t>(*delta);
        result.emplace_back(Int(static_cast<int64_t>(last)));
    }
    return result;
}

} // namespace btoon

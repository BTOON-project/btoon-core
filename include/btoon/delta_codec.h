#ifndef BTOON_DELTA_CODEC_H
#define BTOON_DELTA_CODEC_H

#include "btoon.h"
#include <vector>
#include <span>

namespace btoon {

class DeltaCodec {
public:
    /// Default cap on decoded elements. Each element is an integer that takes at least one
    /// byte of input, so the input's size bounds the output's memory too (one Value each).
    static constexpr size_t kDefaultMaxElements = size_t(1) << 24;

    static std::vector<uint8_t> encode(const Array& data);
    /// @throws BtoonException on malformed input or more than `max_elements` results.
    static Array decode(const std::vector<uint8_t>& data, size_t max_elements = kDefaultMaxElements);
};

} // namespace btoon

#endif // BTOON_DELTA_CODEC_H

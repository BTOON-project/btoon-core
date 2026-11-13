#ifndef BTOON_RLE_CODEC_H
#define BTOON_RLE_CODEC_H

#include "btoon.h"
#include <vector>
#include <span>

namespace btoon {

class RleCodec {
public:
    /// Default cap on decoded elements.
    static constexpr size_t kDefaultMaxElements = size_t(1) << 24;
    /// Default cap on the decoded data's estimated memory. A short run of a large value
    /// can otherwise expand a few KB of input into gigabytes.
    static constexpr size_t kDefaultMaxBytes = size_t(256) << 20;

    /// Runs are of identical values: equal canonical encodings, so 0.0 and -0.0 stay apart.
    static std::vector<uint8_t> encode(const Array& data);
    /// @throws BtoonException on malformed input, more than `max_elements` results, or
    /// results whose estimated memory exceeds `max_bytes` (size_limit).
    static Array decode(const std::vector<uint8_t>& data, size_t max_elements = kDefaultMaxElements,
                        size_t max_bytes = kDefaultMaxBytes);
};

} // namespace btoon

#endif // BTOON_RLE_CODEC_H

#ifndef BTOON_DICTIONARY_CODEC_H
#define BTOON_DICTIONARY_CODEC_H

#include "btoon.h"
#include <vector>
#include <span>

namespace btoon {

class DictionaryCodec {
public:
    /// Default cap on decoded elements.
    static constexpr size_t kDefaultMaxElements = size_t(1) << 24;
    /// Default cap on the decoded data's estimated memory. Each one-byte index copies a
    /// whole dictionary entry, so element counts alone don't bound memory.
    static constexpr size_t kDefaultMaxBytes = size_t(256) << 20;

    static std::vector<uint8_t> encode(const Array& data);
    /// @throws BtoonException on malformed input, more than `max_elements` results, or
    /// results whose estimated memory exceeds `max_bytes` (size_limit).
    static Array decode(const std::vector<uint8_t>& data, size_t max_elements = kDefaultMaxElements,
                        size_t max_bytes = kDefaultMaxBytes);
};

} // namespace btoon

#endif // BTOON_DICTIONARY_CODEC_H

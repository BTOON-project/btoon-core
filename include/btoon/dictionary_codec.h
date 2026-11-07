#ifndef BTOON_DICTIONARY_CODEC_H
#define BTOON_DICTIONARY_CODEC_H

#include "btoon.h"
#include <vector>
#include <span>

namespace btoon {

class DictionaryCodec {
public:
    /// Default cap on decoded elements (protects against amplification attacks).
    static constexpr size_t kDefaultMaxElements = size_t(1) << 24;

    static std::vector<uint8_t> encode(const Array& data);
    /// @throws BtoonException on malformed input or more than `max_elements` results.
    static Array decode(const std::vector<uint8_t>& data, size_t max_elements = kDefaultMaxElements);
};

} // namespace btoon

#endif // BTOON_DICTIONARY_CODEC_H

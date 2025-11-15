#include "btoon/dictionary_codec.h"
#include "btoon/encoder.h"
#include "btoon/decoder.h"
#include "btoon/btoon.h"
#include <vector>
#include <algorithm>

namespace btoon {

std::vector<uint8_t> DictionaryCodec::encode(const Array& data) {
    Encoder encoder;
    if (data.empty()) {
        return {encoder.getBuffer().begin(), encoder.getBuffer().end()};
    }

    std::vector<Value> dictionary;
    for (const auto& value : data) {
        if (std::find(dictionary.begin(), dictionary.end(), value) == dictionary.end()) {
            dictionary.push_back(value);
        }
    }

    std::vector<std::vector<uint8_t>> elements;
    elements.reserve(dictionary.size());
    for (const auto& val : dictionary) {
        Encoder temp_encoder;
        temp_encoder.encode(val);
        auto buf = temp_encoder.getBuffer();
        elements.emplace_back(buf.begin(), buf.end());
    }
    encoder.encodeArray(elements);

    for (const auto& value : data) {
        auto it = std::find(dictionary.begin(), dictionary.end(), value);
        encoder.encodeInt(std::distance(dictionary.begin(), it));
    }

    auto encoded_data = encoder.getBuffer();
    return {encoded_data.begin(), encoded_data.end()};
}

Array DictionaryCodec::decode(const std::vector<uint8_t>& data) {
    Decoder decoder;
    Array result;
    size_t pos = 0;

    auto [dictionary_value, bytes_read_dict] = decoder.decode_and_get_pos({data.data() + pos, data.size() - pos});
    pos += bytes_read_dict;
    Array dictionary_array = std::get<Array>(dictionary_value);

    while (pos < data.size()) {
        auto [value, bytes_read_val] = decoder.decode_and_get_pos({data.data() + pos, data.size() - pos});
        pos += bytes_read_val;
        int index = std::get<Int>(value);
        result.push_back(dictionary_array[index]);
    }

    return result;
}

} // namespace btoon

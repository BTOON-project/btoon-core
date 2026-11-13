// SPDX-FileCopyrightText: 2025 Alvar Laigna <https://alvarlaigna.com>
// SPDX-License-Identifier: MIT
//
// Dictionary coding: an array of the distinct values (in first-seen order),
// followed by one BTOON integer index per element.
#include "btoon/dictionary_codec.h"
#include "btoon/decoder.h"
#include "btoon/encoder.h"
#include "internal.h"

#include <algorithm>
#include <string>
#include <unordered_map>

namespace btoon {

std::vector<uint8_t> DictionaryCodec::encode(const Array& data) {
    // Canonical encodings identify equal values, so hashing them gives O(n) lookup.
    std::unordered_map<std::string, size_t> index_of;
    std::vector<const Value*> dictionary;
    std::vector<size_t> indices;
    indices.reserve(data.size());
    for (const auto& value : data) {
        auto bytes = btoon::encode(value);
        auto [it, inserted] = index_of.try_emplace(std::string(bytes.begin(), bytes.end()), dictionary.size());
        if (inserted) dictionary.push_back(&value);
        indices.push_back(it->second);
    }
    Encoder encoder;
    encoder.encodeArrayHeader(dictionary.size());
    for (const Value* v : dictionary) encoder.encode(*v);
    for (size_t idx : indices) encoder.encodeUint(idx);
    return encoder.release();
}

Array DictionaryCodec::decode(const std::vector<uint8_t>& data, size_t max_elements, size_t max_bytes) {
    Array result;
    if (data.empty()) return result;
    Decoder decoder;
    auto [dict_value, used] = decoder.decode_and_get_pos(data);
    const Array* dictionary = std::get_if<Array>(&dict_value);
    if (!dictionary) detail::fail(ErrorCode::invalid_argument, "DictionaryCodec: missing dictionary array");
    std::vector<size_t> footprint;
    footprint.reserve(dictionary->size());
    for (const auto& entry : *dictionary) footprint.push_back(detail::value_footprint(entry));
    size_t bytes = 0;
    std::span<const uint8_t> rest = std::span<const uint8_t>(data).subspan(used);
    while (!rest.empty()) {
        if (result.size() >= max_elements) detail::fail(ErrorCode::size_limit, "DictionaryCodec: too many elements");
        auto [value, n] = decoder.decode_and_get_pos(rest);
        rest = rest.subspan(n);
        auto idx = value.as_uint64();
        if (!idx || *idx >= dictionary->size()) {
            detail::fail(ErrorCode::invalid_argument, "DictionaryCodec: index out of range");
        }
        // Each index copies a whole entry, so charge the entry's footprint.
        const size_t each = footprint[static_cast<size_t>(*idx)];
        if (each > max_bytes - std::min(bytes, max_bytes)) {
            detail::fail(ErrorCode::size_limit, "DictionaryCodec: decoded data would exceed max_bytes");
        }
        bytes += each;
        result.push_back((*dictionary)[static_cast<size_t>(*idx)]);
    }
    return result;
}

} // namespace btoon

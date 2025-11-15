#include "btoon/btoon.h"
#include "btoon/encoder.h"
#include "btoon/decoder.h"
#include "btoon/compression.h"
#include <set>
#include <vector>
#include <stdexcept>
#include <arpa/inet.h>

namespace btoon {

namespace {
struct CompressionHeader {
    uint32_t magic;
    uint8_t version;
    uint8_t algorithm;
    uint16_t reserved;
    uint32_t compressed_size;
    uint32_t uncompressed_size;
};
const uint32_t BTOON_MAGIC = 0x42544F4E; // "BTON"
} // namespace

const char* Value::type_name() const {
    return std::visit([](auto&& arg) -> const char* {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, Nil>) return "nil";
        else if constexpr (std::is_same_v<T, Bool>) return "bool";
        else if constexpr (std::is_same_v<T, Int>) return "int";
        else if constexpr (std::is_same_v<T, Uint>) return "uint";
        else if constexpr (std::is_same_v<T, Float>) return "float";
        else if constexpr (std::is_same_v<T, String>) return "string";
        else if constexpr (std::is_same_v<T, Binary>) return "binary";
        else if constexpr (std::is_same_v<T, Array>) return "array";
        else if constexpr (std::is_same_v<T, Map>) return "map";
        else if constexpr (std::is_same_v<T, Extension>) return "extension";
        else if constexpr (std::is_same_v<T, Timestamp>) return "timestamp";
        else if constexpr (std::is_same_v<T, Date>) return "date";
        else if constexpr (std::is_same_v<T, DateTime>) return "datetime";
        else if constexpr (std::is_same_v<T, BigInt>) return "bigint";
        else if constexpr (std::is_same_v<T, VectorFloat>) return "vector_float";
        else if constexpr (std::is_same_v<T, VectorDouble>) return "vector_double";
        else return "unknown";
    }, *this);
}

std::vector<uint8_t> encode(const Value& value, const btoon::EncodeOptions& options) {
    Encoder encoder;
    encoder.encode(value);
    auto encoded_data = encoder.getBuffer();

    if (options.compress) {
        // Compression logic here
    }

    return {encoded_data.begin(), encoded_data.end()};
}

Value decode(std::span<const uint8_t> data, const btoon::DecodeOptions& options) {
    if (options.auto_decompress) {
        // Decompression logic here
    }

    Decoder decoder;
    return decoder.decode(data);
}

bool is_tabular(const Array& arr) {
    if (arr.size() < 2) {
        return false;
    }

    const auto* first_row = std::get_if<Map>(&arr[0]);
    if (!first_row) {
        return false;
    }

    std::vector<std::string> column_names;
    for (const auto& [key, _] : *first_row) {
        column_names.push_back(key);
    }
    std::sort(column_names.begin(), column_names.end());

    for (size_t i = 1; i < arr.size(); ++i) {
        const auto* row = std::get_if<Map>(&arr[i]);
        if (!row || row->size() != column_names.size()) {
            return false;
        }
        std::vector<std::string> row_keys;
        for (const auto& [key, _] : *row) {
            row_keys.push_back(key);
        }
        std::sort(row_keys.begin(), row_keys.end());
        if (row_keys != column_names) {
            return false;
        }
    }
    return true;
}

const char* version() {
    return "0.0.1";
}

} // namespace btoon
#include "btoon/btoon.h"
#include "btoon/encoder.h"
#include "btoon/decoder.h"
#include "btoon/compression.h"
#include <set>
#include <vector>
#include <stdexcept>
#include <arpa/inet.h>

namespace btoon {

// --- Private Helper Functions ---

namespace {
// Visitor for encoding btoon::Value using the low-level encoder
struct EncodeVisitor {
    const Encoder& encoder;

    std::vector<uint8_t> operator()(Nil) const { return encoder.encodeNil(); }
    std::vector<uint8_t> operator()(Bool v) const { return encoder.encodeBool(v); }
    std::vector<uint8_t> operator()(Int v) const { return encoder.encodeInt(v); }
    std::vector<uint8_t> operator()(Uint v) const { return encoder.encodeUint(v); }
    std::vector<uint8_t> operator()(Float v) const { return encoder.encodeFloat(v); }
    std::vector<uint8_t> operator()(const String& v) const { return encoder.encodeString(v); }
    std::vector<uint8_t> operator()(StringView v) const { return encoder.encodeString(std::string(v)); }
    std::vector<uint8_t> operator()(const Binary& v) const { return encoder.encodeBinary(v); }
    std::vector<uint8_t> operator()(const Extension& v) const { return encoder.encodeExtension(v.type, v.data); }
    std::vector<uint8_t> operator()(const Timestamp& v) const { return encoder.encodeTimestamp(v.seconds); }
    std::vector<uint8_t> operator()(const Date& v) const { return encoder.encodeDate(v.milliseconds); }
    std::vector<uint8_t> operator()(const BigInt& v) const { return encoder.encodeBigInt(v.bytes); }

    std::vector<uint8_t> operator()(const Array& arr) const {
        std::vector<std::vector<uint8_t>> elements;
        elements.reserve(arr.size());
        for (const auto& val : arr) {
            elements.push_back(std::visit(*this, val));
        }
        return encoder.encodeArray(elements);
    }

    std::vector<uint8_t> operator()(const Map& map) const {
        std::map<std::string, std::vector<uint8_t>> pairs;
        for (const auto& [key, val] : map) {
            pairs[key] = std::visit(*this, val);
        }
        return encoder.encodeMap(pairs);
    }
};

// BTOON compression header format (see docs/btoon-spec.md)
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

// --- Public API Implementation ---

std::vector<uint8_t> encode(const Value& value, const EncodeOptions& options) {
    Encoder encoder;
    std::vector<uint8_t> encoded_data = std::visit(EncodeVisitor{encoder}, value);

    if (options.compress) {
        CompressionAlgorithm algo = options.compression_algorithm;
        std::vector<uint8_t> compressed_data = compress(algo, encoded_data, options.compression_level);

        CompressionHeader header;
        header.magic = htonl(BTOON_MAGIC);
        header.version = 1;
        header.algorithm = static_cast<uint8_t>(algo);
        header.reserved = 0;
        header.compressed_size = htonl(static_cast<uint32_t>(compressed_data.size()));
        header.uncompressed_size = htonl(static_cast<uint32_t>(encoded_data.size()));

        std::vector<uint8_t> final_payload;
        final_payload.resize(sizeof(header) + compressed_data.size());
        std::memcpy(final_payload.data(), &header, sizeof(header));
        std::memcpy(final_payload.data() + sizeof(header), compressed_data.data(), compressed_data.size());
        
        return final_payload;
    }

    return encoded_data;
}

Value decode(std::span<const uint8_t> data, const DecodeOptions& options) {
    if (options.decompress) {
        if (data.size() < sizeof(CompressionHeader)) {
            throw BtoonException("Invalid compressed data: header too small.");
        }
        
        CompressionHeader header;
        std::memcpy(&header, data.data(), sizeof(header));
        header.magic = ntohl(header.magic);
        header.compressed_size = ntohl(header.compressed_size);
        header.uncompressed_size = ntohl(header.uncompressed_size);

        if (header.magic != BTOON_MAGIC) {
            throw BtoonException("Invalid compressed data: incorrect magic number.");
        }

        auto algo = static_cast<CompressionAlgorithm>(header.algorithm);
        std::span<const uint8_t> compressed_payload = data.subspan(sizeof(header));

        if (compressed_payload.size() != header.compressed_size) {
            throw BtoonException("Compressed data size mismatch.");
        }

        std::vector<uint8_t> decompressed_data = decompress(algo, compressed_payload);

        if (decompressed_data.size() != header.uncompressed_size) {
            throw BtoonException("Decompressed data size mismatch.");
        }
        
        Decoder decoder;
        return decoder.decode(decompressed_data);
    }

    Decoder decoder;
    return decoder.decode(data);
}

bool is_tabular(const Array& arr) {
    if (arr.empty()) {
        return false;
    }

    const Map* first_map = std::get_if<Map>(&arr[0]);
    if (!first_map || first_map->empty()) {
        return false;
    }

    std::set<std::string> keys;
    for (const auto& pair : *first_map) {
        keys.insert(pair.first);
    }

    for (size_t i = 1; i < arr.size(); ++i) {
        const Map* map = std::get_if<Map>(&arr[i]);
        if (!map || map->size() != keys.size()) {
            return false;
        }
        for (const auto& pair : *map) {
            if (keys.find(pair.first) == keys.end()) {
                return false;
            }
        }
    }
    return true;
}

const char* version() {
    return "0.0.1";
}

} // namespace btoon

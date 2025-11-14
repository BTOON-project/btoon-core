#include "btoon/stream_encoder.h"
#include "btoon/encoder.h"
#include <iostream>

namespace btoon {

class StreamEncoderImpl {
public:
    StreamEncoderImpl(std::ostream& stream, const EncodeOptions& options)
        : stream_(stream), options_(options) {}

    void write(const Value& value) {
        Encoder encoder;
        // This is a simplified implementation. A real implementation would
        // write directly to the stream instead of creating an intermediate buffer.
        std::vector<uint8_t> encoded = std::visit(EncodeVisitor{encoder}, value);
        stream_.write(reinterpret_cast<const char*>(encoded.data()), encoded.size());
    }

    void close() {
        stream_.flush();
    }

private:
    std::ostream& stream_;
    EncodeOptions options_;
    // This struct is needed because the visitor is in btoon.cpp
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
};

StreamEncoder::StreamEncoder(std::ostream& stream, const EncodeOptions& options)
    : pimpl_(std::make_unique<StreamEncoderImpl>(stream, options)) {}

StreamEncoder::~StreamEncoder() = default;

void StreamEncoder::write(const Value& value) {
    pimpl_->write(value);
}

void StreamEncoder::close() {
    pimpl_->close();
}

} // namespace btoon

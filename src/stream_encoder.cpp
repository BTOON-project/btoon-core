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
        std::visit([&encoder](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, Nil>) {
                encoder.encodeNil();
            } else if constexpr (std::is_same_v<T, Bool>) {
                encoder.encodeBool(arg);
            } else if constexpr (std::is_same_v<T, Int>) {
                encoder.encodeInt(arg);
            } else if constexpr (std::is_same_v<T, Uint>) {
                encoder.encodeUint(arg);
            } else if constexpr (std::is_same_v<T, Float>) {
                encoder.encodeFloat(arg);
            } else if constexpr (std::is_same_v<T, String>) {
                encoder.encodeString(arg);
            } else if constexpr (std::is_same_v<T, Binary>) {
                encoder.encodeBinary(arg);
            } else if constexpr (std::is_same_v<T, Extension>) {
                encoder.encodeExtension(arg.type, arg.data);
            } else if constexpr (std::is_same_v<T, Timestamp>) {
                encoder.encodeTimestamp(arg.seconds);
            } else if constexpr (std::is_same_v<T, Date>) {
                encoder.encodeDate(arg.milliseconds);
            } else if constexpr (std::is_same_v<T, DateTime>) {
                encoder.encodeDateTime(arg.nanoseconds);
            } else if constexpr (std::is_same_v<T, BigInt>) {
                encoder.encodeBigInt(arg.bytes);
            } else if constexpr (std::is_same_v<T, VectorFloat>) {
                encoder.encodeVectorFloat(arg);
            } else if constexpr (std::is_same_v<T, VectorDouble>) {
                encoder.encodeVectorDouble(arg);
            } else if constexpr (std::is_same_v<T, Array>) {
                std::vector<std::vector<uint8_t>> elements;
                elements.reserve(arg.size());
                for (const auto& val : arg) {
                    Encoder temp_encoder;
                    temp_encoder.encode(val);
                    auto buf = temp_encoder.getBuffer();
                    elements.emplace_back(buf.begin(), buf.end());
                }
                encoder.encodeArray(elements);
            } else if constexpr (std::is_same_v<T, Map>) {
                std::map<std::string, std::vector<uint8_t>> pairs;
                for (const auto& [key, val] : arg) {
                    Encoder temp_encoder;
                    temp_encoder.encode(val);
                    auto buf = temp_encoder.getBuffer();
                    pairs[key] = std::vector<uint8_t>(buf.begin(), buf.end());
                }
                encoder.encodeMap(pairs);
            }
        }, value);
        auto encoded = encoder.getBuffer();
        stream_.write(reinterpret_cast<const char*>(encoded.data()), encoded.size());
    }

    void close() {
        stream_.flush();
    }

private:
    std::ostream& stream_;
    EncodeOptions options_;
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

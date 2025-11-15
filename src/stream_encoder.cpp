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
        encoder.setOptions(options_);
        encoder.encode(value);
        auto encoded = encoder.getBuffer();
        stream_.write(reinterpret_cast<const char*>(encoded.data()),
                      static_cast<std::streamsize>(encoded.size()));
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

#include "btoon/stream_decoder.h"
#include "btoon/decoder.h"
#include <iostream>
#include <vector>

namespace btoon {

class StreamDecoderImpl {
public:
    StreamDecoderImpl(std::istream& stream, const DecodeOptions& options)
        : stream_(stream), options_(options) {}

    std::optional<Value> read() {
        // This is a simplified implementation. A real implementation would
        // read from the stream incrementally and parse the data without
        // loading the entire object into memory first.
        std::vector<char> buffer(std::istreambuf_iterator<char>(stream_), {});
        if (buffer.empty()) {
            return std::nullopt;
        }
        
        Decoder decoder;
        return decoder.decode({reinterpret_cast<const uint8_t*>(buffer.data()), buffer.size()});
    }

    bool has_next() {
        return stream_.peek() != EOF;
    }

private:
    std::istream& stream_;
    DecodeOptions options_;
};

StreamDecoder::StreamDecoder(std::istream& stream, const DecodeOptions& options)
    : pimpl_(std::make_unique<StreamDecoderImpl>(stream, options)) {}

StreamDecoder::~StreamDecoder() = default;

std::optional<Value> StreamDecoder::read() {
    return pimpl_->read();
}

bool StreamDecoder::has_next() {
    return pimpl_->has_next();
}

} // namespace btoon

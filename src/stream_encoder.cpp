// SPDX-FileCopyrightText: 2025 Alvar Laigna <https://alvarlaigna.com>
// SPDX-License-Identifier: MIT
//
// Writes values as concatenated bare canonical BTOON documents. Envelope
// options (compress, hmac_key) don't apply to streams; each value is written
// bare so that StreamDecoder can delimit values without extra framing.
#include "btoon/stream_encoder.h"
#include "btoon/encoder.h"

#include <ostream>

namespace btoon {

class StreamEncoderImpl {
public:
    StreamEncoderImpl(std::ostream& stream, const EncodeOptions& options) : stream_(stream), encoder_(options) {}

    void write(const Value& value) {
        encoder_.clear();
        encoder_.encode(value);
        auto bytes = encoder_.getBuffer();
        stream_.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!stream_) throw BtoonException(ErrorCode::generic, "stream write failed");
    }

    void close() { stream_.flush(); }

private:
    std::ostream& stream_;
    Encoder encoder_;
};

StreamEncoder::StreamEncoder(std::ostream& stream, const EncodeOptions& options)
    : pimpl_(std::make_unique<StreamEncoderImpl>(stream, options)) {}

StreamEncoder::~StreamEncoder() = default;

void StreamEncoder::write(const Value& value) { pimpl_->write(value); }

void StreamEncoder::close() { pimpl_->close(); }

} // namespace btoon

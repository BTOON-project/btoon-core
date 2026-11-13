// SPDX-FileCopyrightText: 2025 Alvar Laigna <https://alvarlaigna.com>
// SPDX-License-Identifier: MIT
//
// Reads a stream of concatenated bare BTOON documents one value at a time.
// Input is buffered incrementally: when a value is incomplete (ErrorCode::
// truncated) more bytes are read, in geometrically growing chunks, up to
// DecodeOptions::max_decompressed_size of buffered data.
#include "btoon/stream_decoder.h"
#include "btoon/decoder.h"
#include "internal.h"

#include <istream>
#include <vector>

namespace btoon {

class StreamDecoderImpl {
public:
    StreamDecoderImpl(std::istream& stream, const DecodeOptions& options)
        : stream_(stream), options_(options), decoder_(options) {
        if (options.hmac_key.has_value()) {
            detail::fail(ErrorCode::invalid_argument, "signed envelopes can't be used in a document stream");
        }
    }

    std::optional<Value> read() {
        size_t chunk = 64 * 1024;
        for (;;) {
            if (pos_ < buf_.size()) {
                try {
                    auto [value, used] = decoder_.decode_and_get_pos({buf_.data() + pos_, buf_.size() - pos_});
                    pos_ += used;
                    compact();
                    return value;
                } catch (const BtoonException& e) {
                    if (e.code() != ErrorCode::truncated || eof_) throw;
                }
            } else if (eof_) {
                return std::nullopt;
            }
            fill(chunk);
            chunk = std::min<size_t>(chunk * 2, 64 * 1024 * 1024);
        }
    }

    bool has_next() {
        if (pos_ < buf_.size()) return true;
        if (!eof_) fill(64 * 1024);
        return pos_ < buf_.size();
    }

private:
    void fill(size_t n) {
        if (buf_.size() - pos_ + n > options_.max_decompressed_size) {
            detail::fail(ErrorCode::size_limit, "stream value exceeds max_decompressed_size");
        }
        size_t old = buf_.size();
        buf_.resize(old + n);
        stream_.read(reinterpret_cast<char*>(buf_.data() + old), static_cast<std::streamsize>(n));
        auto got = static_cast<size_t>(stream_.gcount());
        buf_.resize(old + got);
        if (got < n) eof_ = true;
    }

    void compact() {
        if (pos_ > (1u << 20) && pos_ > buf_.size() / 2) {
            buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(pos_));
            pos_ = 0;
        }
    }

    std::istream& stream_;
    DecodeOptions options_;
    Decoder decoder_;
    std::vector<uint8_t> buf_;
    size_t pos_ = 0;
    bool eof_ = false;
};

StreamDecoder::StreamDecoder(std::istream& stream, const DecodeOptions& options)
    : pimpl_(std::make_unique<StreamDecoderImpl>(stream, options)) {}

StreamDecoder::~StreamDecoder() = default;

std::optional<Value> StreamDecoder::read() { return pimpl_->read(); }

bool StreamDecoder::has_next() { return pimpl_->has_next(); }

} // namespace btoon

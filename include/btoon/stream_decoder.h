//  ██████╗ ████████╗ ██████╗  ██████╗ ███╗   ██╗
//  ██╔══██╗╚══██╔══╝██╔═══██╗██╔═══██╗████╗  ██║
//  ██████╔╝   ██║   ██║   ██║██║   ██║██╔██╗ ██║
//  ██╔══██╗   ██║   ██║   ██║██║   ██║██║╚██╗██║
//  ██████╔╝   ██║   ╚██████╔╝╚██████╔╝██║ ╚████║
//  ╚═════╝    ╚═╝    ╚═════╝  ╚═════╝ ╚═╝  ╚═══╝
//
//  BTOON Core
//  https://btoon.net & https://github.com/BTOON-project/btoon-core
//
// SPDX-FileCopyrightText: 2025 Alvar Laigna <https://alvarlaigna.com>
// SPDX-License-Identifier: MIT
/**
 * @file stream_decoder.h
 * @brief Reads a stream of concatenated bare BTOON documents.
 *
 * A stream can't carry signed or compression envelopes: their magic bytes are
 * also valid bare values (0x42 is the integer 66), so a reader couldn't tell
 * where a document ends. The constructor rejects DecodeOptions::hmac_key with
 * ErrorCode::invalid_argument rather than accept unsigned documents; to read
 * signed documents, frame them yourself and call btoon::decode() on each.
 */
#ifndef BTOON_STREAM_DECODER_H
#define BTOON_STREAM_DECODER_H

#include "btoon.h"
#include <iostream>
#include <memory>
#include <optional>

namespace btoon {

class StreamDecoderImpl; // Pimpl idiom

class StreamDecoder {
public:
    StreamDecoder(std::istream& stream, const DecodeOptions& options = {});
    ~StreamDecoder();

    std::optional<Value> read();
    bool has_next();

private:
    std::unique_ptr<StreamDecoderImpl> pimpl_;
};

} // namespace btoon

#endif // BTOON_STREAM_DECODER_H

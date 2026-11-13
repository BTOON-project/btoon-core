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
 * @file stream_encoder.h
 * @brief Writes values as a stream of concatenated bare BTOON documents.
 *
 * A stream can't carry signed or compression envelopes (see stream_decoder.h),
 * so the constructor rejects EncodeOptions::compress and
 * EncodeOptions::hmac_key with ErrorCode::invalid_argument rather than write
 * unsigned, uncompressed documents.
 */
#ifndef BTOON_STREAM_ENCODER_H
#define BTOON_STREAM_ENCODER_H

#include "btoon.h"
#include <iostream>
#include <memory>

namespace btoon {

class StreamEncoderImpl; // Pimpl idiom

class StreamEncoder {
public:
    StreamEncoder(std::ostream& stream, const EncodeOptions& options = {});
    ~StreamEncoder();

    void write(const Value& value);
    void close();

private:
    std::unique_ptr<StreamEncoderImpl> pimpl_;
};

} // namespace btoon

#endif // BTOON_STREAM_ENCODER_H

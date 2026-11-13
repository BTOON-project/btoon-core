//  BTOON Core
//  https://btoon.net & https://github.com/BTOON-project/btoon-core
//
// SPDX-FileCopyrightText: 2025 Alvar Laigna <https://alvarlaigna.com>
// SPDX-License-Identifier: MIT

/**
 * @file decoder.h
 * @brief Hardened BTOON decoder (docs/SPEC.md §7).
 *
 * Every read is bounds-checked, nesting is limited to DecodeOptions::max_depth,
 * and no allocation exceeds what the remaining input could encode. Errors are
 * reported as BtoonException with an ErrorCode.
 *
 * Decoder::decode() reads a complete document exactly as btoon::decode()
 * does: it verifies and removes a signed envelope (§8) and removes a
 * compression envelope (§6) before decoding. decode_and_get_pos() reads bare
 * values, for buffers of concatenated documents.
 *
 * A Decoder has an HMAC key if it was given a Security object or a non-empty
 * DecodeOptions::hmac_key (if both, the Security object's key is used). With
 * a key, input must be a signed envelope, and its HMAC is verified before
 * anything is parsed. Without one, a signed envelope is rejected by decode()
 * with ErrorCode::authentication_failed.
 */
#ifndef BTOON_DECODER_H
#define BTOON_DECODER_H

#include <optional>
#include <span>
#include <utility>

#include "btoon.h"
#include "memory_pool.h"
#include "security.h"

namespace btoon {

class Decoder {
public:
    Decoder();
    explicit Decoder(const DecodeOptions& options);
    /// The pool is no longer used; kept for source compatibility with 0.0.x.
    explicit Decoder(MemoryPool* pool);
    /**
     * Requires a signed envelope and verifies it with the Security object's
     * key before decoding (§8). Its type restrictions apply as well. The
     * Security object is copied, so it may be a temporary.
     */
    explicit Decoder(const Security& security);
    Decoder(const Security& security, MemoryPool* pool);
    ~Decoder();

    void setOptions(const DecodeOptions& options) { options_ = options; }
    const DecodeOptions& options() const noexcept { return options_; }
    /// Turns signing off (or back on) for a Decoder constructed with a
    /// Security object; its type restrictions still apply. A key in
    /// DecodeOptions::hmac_key is still required and verified.
    void setSecurityEnabled(bool enable) { useSecurity_ = enable && security_.has_value(); }

    /**
     * Decodes a complete document, like btoon::decode(): envelopes are
     * verified and removed, then exactly one value must fill the rest.
     * @throws BtoonException (e.g. ErrorCode::authentication_failed,
     *         ErrorCode::trailing_data in strict mode)
     */
    Value decode(std::span<const uint8_t> buffer) const;

    /**
     * Decodes one bare value from the front of the buffer and returns it with
     * the number of bytes consumed. Trailing bytes are allowed (use this to
     * read concatenated values). Envelope magic bytes are read as values,
     * since "BTON" and "BTSG" are also valid bare values; but if the Decoder
     * has an HMAC key, the buffer must be a signed envelope, which is
     * verified first, and the value is read from its payload.
     */
    std::pair<Value, size_t> decode_and_get_pos(std::span<const uint8_t> buffer) const;

private:
    /// Verifies and removes the signed envelope if the Decoder has a key.
    std::span<const uint8_t> verify(std::span<const uint8_t> buffer, size_t* header_len) const;
    /// Reads one value from the front of `data`; with `whole`, it must fill
    /// `data` (in strict mode). Returns the value and the bytes it used.
    std::pair<Value, size_t> read(std::span<const uint8_t> data, bool whole) const;

    DecodeOptions options_;
    std::optional<Security> security_;
    bool useSecurity_ = false;
};

} // namespace btoon

#endif // BTOON_DECODER_H

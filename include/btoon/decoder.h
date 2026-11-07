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
 * The Decoder handles bare documents (and, if constructed with a Security
 * object, signed envelopes). Compression envelopes are handled by the
 * top-level btoon::decode().
 */
#ifndef BTOON_DECODER_H
#define BTOON_DECODER_H

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
    /// Requires a signed envelope and verifies it before decoding (§8).
    explicit Decoder(const Security& security);
    Decoder(const Security& security, MemoryPool* pool);
    ~Decoder();

    void setOptions(const DecodeOptions& options) { options_ = options; }
    const DecodeOptions& options() const noexcept { return options_; }
    void setSecurityEnabled(bool enable) { useSecurity_ = enable && security_ != nullptr; }

    /**
     * Decodes exactly one value occupying the whole buffer.
     * @throws BtoonException (e.g. ErrorCode::trailing_data in strict mode)
     */
    Value decode(std::span<const uint8_t> buffer) const;

    /**
     * Decodes one value from the front of the buffer and returns it with the
     * number of bytes consumed. Trailing bytes are allowed (use this to read
     * concatenated values).
     */
    std::pair<Value, size_t> decode_and_get_pos(std::span<const uint8_t> buffer) const;

private:
    std::span<const uint8_t> unwrap(std::span<const uint8_t> buffer, size_t* header_len) const;

    DecodeOptions options_;
    const Security* security_ = nullptr;
    bool useSecurity_ = false;
};

} // namespace btoon

#endif // BTOON_DECODER_H

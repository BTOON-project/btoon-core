//  BTOON Core
//  https://btoon.net & https://github.com/BTOON-project/btoon-core
//
// SPDX-FileCopyrightText: 2025 Alvar Laigna <https://alvarlaigna.com>
// SPDX-License-Identifier: MIT
/**
 * @file security.h
 * @brief HMAC-SHA256 signed envelopes (docs/SPEC.md §8) and type restrictions.
 *
 * Envelope layout: "BTSG" | u8 version=1 | u8 alg=1 | u16 reserved=0 |
 * 32-byte HMAC-SHA256(key, header[0..8) || payload) | payload.
 *
 * SHA-256/HMAC are implemented in-tree (no OpenSSL dependency).
 */
#ifndef BTOON_SECURITY_H
#define BTOON_SECURITY_H

#include "btoon.h"

#include <array>
#include <cstdint>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace btoon {

/// SHA-256 digest of `data`.
std::array<uint8_t, 32> sha256(std::span<const uint8_t> data);

/// HMAC-SHA256(key, part1 || part2).
std::array<uint8_t, 32> hmac_sha256(std::span<const uint8_t> key,
                                    std::span<const uint8_t> part1,
                                    std::span<const uint8_t> part2 = {});

/// Constant-time comparison (returns false on length mismatch).
bool constant_time_equal(std::span<const uint8_t> a, std::span<const uint8_t> b) noexcept;

class Security {
public:
    static constexpr size_t kEnvelopeHeaderSize = 40;  ///< magic..mac

    Security() = default;
    explicit Security(std::string key) : secretKey_(std::move(key)) {}

    /// Sets the HMAC key (any bytes; ≥ 32 random bytes recommended).
    void setSecretKey(const std::string& key) { secretKey_ = key; }
    bool hasSecretKey() const noexcept { return !secretKey_.empty(); }

    /// Raw HMAC-SHA256 tag of `data`. @throws BtoonException if no key is set.
    std::vector<uint8_t> sign(std::span<const uint8_t> data) const;

    /// Constant-time check of a raw tag produced by sign().
    bool verify(std::span<const uint8_t> data, std::span<const uint8_t> signature) const;
    bool verify(const std::vector<uint8_t>& data, const std::vector<uint8_t>& signature) const {
        return verify(std::span<const uint8_t>(data), std::span<const uint8_t>(signature));
    }

    /// Wraps `payload` in a signed envelope.
    std::vector<uint8_t> wrap(std::span<const uint8_t> payload) const;

    /**
     * Verifies a signed envelope and returns a view of its payload.
     * @throws BtoonException (authentication_failed / invalid_envelope)
     */
    std::span<const uint8_t> unwrap(std::span<const uint8_t> envelope) const;

    /// True if `data` starts with the signed-envelope magic "BTSG".
    static bool is_signed_envelope(std::span<const uint8_t> data) noexcept;

    /// Restricts decoding to the given btoon::Value variant indices (empty = all).
    void setAllowedTypes(const std::set<size_t>& types) {
        allowedTypes_ = types;
        restrictTypes_ = !types.empty();
    }
    bool isTypeAllowed(size_t type_index) const {
        return !restrictTypes_ || allowedTypes_.count(type_index) > 0;
    }
    bool restrictsTypes() const noexcept { return restrictTypes_; }

private:
    std::span<const uint8_t> key_bytes() const {
        return {reinterpret_cast<const uint8_t*>(secretKey_.data()), secretKey_.size()};
    }

    std::string secretKey_;
    std::set<size_t> allowedTypes_;
    bool restrictTypes_ = false;
};

} // namespace btoon

#endif // BTOON_SECURITY_H

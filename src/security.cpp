// SPDX-FileCopyrightText: 2025 Alvar Laigna <https://alvarlaigna.com>
// SPDX-License-Identifier: MIT
#include "btoon/security.h"
#include "internal.h"

namespace btoon {

using namespace detail;

namespace {
constexpr uint8_t kMagic[4] = {'B', 'T', 'S', 'G'};
constexpr uint8_t kVersion = 1;
constexpr uint8_t kAlgHmacSha256 = 1;
}  // namespace

std::vector<uint8_t> Security::sign(std::span<const uint8_t> data) const {
    if (secretKey_.empty()) fail(ErrorCode::invalid_argument, "no HMAC key set");
    auto tag = hmac_sha256(key_bytes(), data);
    return {tag.begin(), tag.end()};
}

bool Security::verify(std::span<const uint8_t> data, std::span<const uint8_t> signature) const {
    if (secretKey_.empty()) fail(ErrorCode::invalid_argument, "no HMAC key set");
    auto tag = hmac_sha256(key_bytes(), data);
    return constant_time_equal(tag, signature);
}

bool Security::is_signed_envelope(std::span<const uint8_t> data) noexcept {
    return data.size() >= 4 && std::memcmp(data.data(), kMagic, 4) == 0;
}

std::vector<uint8_t> Security::wrap(std::span<const uint8_t> payload) const {
    if (secretKey_.empty()) fail(ErrorCode::invalid_argument, "no HMAC key set");
    std::vector<uint8_t> out(kEnvelopeHeaderSize + payload.size());
    std::memcpy(out.data(), kMagic, 4);
    out[4] = kVersion;
    out[5] = kAlgHmacSha256;
    out[6] = 0;
    out[7] = 0;
    auto tag = hmac_sha256(key_bytes(), {out.data(), 8}, payload);
    std::memcpy(out.data() + 8, tag.data(), tag.size());
    if (!payload.empty()) std::memcpy(out.data() + kEnvelopeHeaderSize, payload.data(), payload.size());
    return out;
}

std::span<const uint8_t> Security::unwrap(std::span<const uint8_t> env) const {
    if (secretKey_.empty()) fail(ErrorCode::invalid_argument, "no HMAC key set");
    if (!is_signed_envelope(env)) {
        fail(ErrorCode::authentication_failed, "input is not a signed envelope");
    }
    if (env.size() < kEnvelopeHeaderSize) fail(ErrorCode::invalid_envelope, "signed envelope truncated");
    if (env[4] != kVersion || env[5] != kAlgHmacSha256 || env[6] != 0 || env[7] != 0) {
        fail(ErrorCode::invalid_envelope, "unsupported signed envelope version/algorithm");
    }
    auto payload = env.subspan(kEnvelopeHeaderSize);
    auto tag = hmac_sha256(key_bytes(), env.first(8), payload);
    if (!constant_time_equal(tag, env.subspan(8, 32))) {
        fail(ErrorCode::authentication_failed, "HMAC verification failed");
    }
    return payload;
}

} // namespace btoon

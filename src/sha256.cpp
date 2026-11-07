// SPDX-FileCopyrightText: 2025 Alvar Laigna <https://alvarlaigna.com>
// SPDX-License-Identifier: MIT
//
// SHA-256 (FIPS 180-4) and HMAC-SHA256 (RFC 2104). Self-contained so the core
// library needs no OpenSSL. Verified against FIPS and RFC 4231 test vectors in
// tests/test_security.cpp. No secret-dependent branches or table lookups.
#include "btoon/security.h"

#include <cstring>

namespace btoon {
namespace {

constexpr uint32_t kK[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

inline uint32_t rotr(uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }

class Sha256 {
public:
    Sha256() {
        static constexpr uint32_t kInit[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                              0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
        std::memcpy(h_, kInit, sizeof(h_));
    }

    void update(const uint8_t* data, size_t len) {
        // An empty span may carry a null pointer; memcpy(dst, nullptr, 0) is UB.
        if (len == 0) return;
        total_ += len;
        if (buffered_) {
            size_t take = std::min(len, size_t(64) - buffered_);
            std::memcpy(buf_ + buffered_, data, take);
            buffered_ += take;
            data += take;
            len -= take;
            if (buffered_ == 64) {
                block(buf_);
                buffered_ = 0;
            }
        }
        while (len >= 64) {
            block(data);
            data += 64;
            len -= 64;
        }
        if (len) {
            std::memcpy(buf_, data, len);
            buffered_ = len;
        }
    }

    std::array<uint8_t, 32> finish() {
        const uint64_t bits = total_ * 8;
        uint8_t pad[72] = {0x80};
        size_t pad_len = (buffered_ < 56) ? (56 - buffered_) : (120 - buffered_);
        uint8_t len_be[8];
        for (int i = 0; i < 8; ++i) len_be[i] = static_cast<uint8_t>(bits >> (56 - 8 * i));
        update(pad, pad_len);
        update(len_be, 8);
        std::array<uint8_t, 32> out{};
        for (int i = 0; i < 8; ++i) {
            out[4 * i] = static_cast<uint8_t>(h_[i] >> 24);
            out[4 * i + 1] = static_cast<uint8_t>(h_[i] >> 16);
            out[4 * i + 2] = static_cast<uint8_t>(h_[i] >> 8);
            out[4 * i + 3] = static_cast<uint8_t>(h_[i]);
        }
        return out;
    }

private:
    void block(const uint8_t* p) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (uint32_t(p[4 * i]) << 24) | (uint32_t(p[4 * i + 1]) << 16) |
                   (uint32_t(p[4 * i + 2]) << 8) | uint32_t(p[4 * i + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3];
        uint32_t e = h_[4], f = h_[5], g = h_[6], h = h_[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t t1 = h + S1 + ch + kK[i] + w[i];
            uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = S0 + maj;
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        h_[0] += a; h_[1] += b; h_[2] += c; h_[3] += d;
        h_[4] += e; h_[5] += f; h_[6] += g; h_[7] += h;
    }

    uint32_t h_[8];
    uint8_t buf_[64] = {};
    size_t buffered_ = 0;
    uint64_t total_ = 0;
};

} // namespace

std::array<uint8_t, 32> sha256(std::span<const uint8_t> data) {
    Sha256 s;
    s.update(data.data(), data.size());
    return s.finish();
}

std::array<uint8_t, 32> hmac_sha256(std::span<const uint8_t> key,
                                    std::span<const uint8_t> part1,
                                    std::span<const uint8_t> part2) {
    uint8_t k[64] = {};
    if (key.size() > 64) {
        auto kh = sha256(key);
        std::memcpy(k, kh.data(), kh.size());
    } else if (!key.empty()) {
        std::memcpy(k, key.data(), key.size());
    }
    uint8_t ipad[64], opad[64];
    for (int i = 0; i < 64; ++i) {
        ipad[i] = static_cast<uint8_t>(k[i] ^ 0x36);
        opad[i] = static_cast<uint8_t>(k[i] ^ 0x5c);
    }
    Sha256 inner;
    inner.update(ipad, 64);
    inner.update(part1.data(), part1.size());
    inner.update(part2.data(), part2.size());
    auto ih = inner.finish();
    Sha256 outer;
    outer.update(opad, 64);
    outer.update(ih.data(), ih.size());
    return outer.finish();
}

bool constant_time_equal(std::span<const uint8_t> a, std::span<const uint8_t> b) noexcept {
    if (a.size() != b.size()) return false;
    volatile uint8_t diff = 0;
    for (size_t i = 0; i < a.size(); ++i) diff = static_cast<uint8_t>(diff | (a[i] ^ b[i]));
    return diff == 0;
}

} // namespace btoon

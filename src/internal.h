// SPDX-FileCopyrightText: 2025 Alvar Laigna <https://alvarlaigna.com>
// SPDX-License-Identifier: MIT
//
// Internal helpers shared by the encoder and decoder. Not part of the public API.
#ifndef BTOON_INTERNAL_H
#define BTOON_INTERNAL_H

#include "btoon/btoon.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#if defined(_MSC_VER)
#define BTOON_NOINLINE __declspec(noinline)
#define BTOON_LIKELY(x) (x)
#define BTOON_UNLIKELY(x) (x)
#else
#define BTOON_NOINLINE __attribute__((noinline))
#define BTOON_LIKELY(x) __builtin_expect(!!(x), 1)
#define BTOON_UNLIKELY(x) __builtin_expect(!!(x), 0)
#endif

namespace btoon::detail {

// Extension type ids (docs/SPEC.md §5).
inline constexpr int8_t kExtTimestamp = -1;
inline constexpr int8_t kExtDate = -2;
inline constexpr int8_t kExtDateTime = -3;
inline constexpr int8_t kExtBigInt = -4;
inline constexpr int8_t kExtVectorFloat = -5;
inline constexpr int8_t kExtVectorDouble = -6;
inline constexpr int8_t kExtTabular = -10;

inline constexpr uint32_t kTabularVersion = 1;
inline constexpr int kMaxTimezoneOffsetMinutes = 1080;
inline constexpr uint32_t kNanosPerSecond = 1000000000u;

[[noreturn]] BTOON_NOINLINE void fail(ErrorCode code, const std::string& message);

/// Removes a compression envelope (docs/SPEC.md §6) from `bytes`, decompressing into
/// `storage`, or returns `bytes` unchanged if it isn't one. Implemented in btoon.cpp.
std::span<const uint8_t> open_compression_envelope(std::span<const uint8_t> bytes, const DecodeOptions& options,
                                                   std::vector<uint8_t>& storage);

// --- Big-endian load/store ---------------------------------------------------

inline uint16_t load_be16(const uint8_t* p) {
    return static_cast<uint16_t>((uint16_t(p[0]) << 8) | uint16_t(p[1]));
}
inline uint32_t load_be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}
inline uint64_t load_be64(const uint8_t* p) {
    return (uint64_t(load_be32(p)) << 32) | uint64_t(load_be32(p + 4));
}
inline void store_be16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v >> 8);
    p[1] = static_cast<uint8_t>(v);
}
inline void store_be32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v >> 24);
    p[1] = static_cast<uint8_t>(v >> 16);
    p[2] = static_cast<uint8_t>(v >> 8);
    p[3] = static_cast<uint8_t>(v);
}
inline void store_be64(uint8_t* p, uint64_t v) {
    store_be32(p, static_cast<uint32_t>(v >> 32));
    store_be32(p + 4, static_cast<uint32_t>(v));
}

// --- Little-endian float packing (vectors, §5.5) -----------------------------

template <class T>
inline void store_le_array(uint8_t* dst, const T* src, size_t count) {
    static_assert(sizeof(T) == 4 || sizeof(T) == 8);
    if constexpr (std::endian::native == std::endian::little) {
        if (count) std::memcpy(dst, src, count * sizeof(T));
    } else {
        for (size_t i = 0; i < count; ++i) {
            uint8_t tmp[sizeof(T)];
            std::memcpy(tmp, &src[i], sizeof(T));
            for (size_t b = 0; b < sizeof(T); ++b) dst[i * sizeof(T) + b] = tmp[sizeof(T) - 1 - b];
        }
    }
}

template <class T>
inline void load_le_array(T* dst, const uint8_t* src, size_t count) {
    static_assert(sizeof(T) == 4 || sizeof(T) == 8);
    if constexpr (std::endian::native == std::endian::little) {
        if (count) std::memcpy(dst, src, count * sizeof(T));
    } else {
        for (size_t i = 0; i < count; ++i) {
            uint8_t tmp[sizeof(T)];
            for (size_t b = 0; b < sizeof(T); ++b) tmp[b] = src[i * sizeof(T) + sizeof(T) - 1 - b];
            std::memcpy(&dst[i], tmp, sizeof(T));
        }
    }
}

// --- UTF-8 -------------------------------------------------------------------

/// Well-formed UTF-8 per RFC 3629 (no overlongs, no surrogates, ≤ U+10FFFF).
bool is_valid_utf8(const uint8_t* data, size_t len) noexcept;
inline bool is_valid_utf8(std::string_view s) noexcept {
    return is_valid_utf8(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

// --- BigInt helpers ----------------------------------------------------------

/// Strips redundant sign-extension bytes from a two's-complement BE integer.
std::vector<uint8_t> bigint_minimal(std::span<const uint8_t> bytes);

/// Result of fitting a two's-complement integer into 64 bits.
struct Int64Fit {
    enum Kind { kNone, kSigned, kUnsigned } kind = kNone;
    int64_t i = 0;
    uint64_t u = 0;
};
/// Signed when it fits int64, unsigned when it fits only uint64, else none.
Int64Fit bigint_fit64(std::span<const uint8_t> bytes);

// --- Tabular -----------------------------------------------------------------

inline uint64_t str_header_size(uint64_t len) {
    return len <= 31 ? 1 : len <= 0xff ? 2 : len <= 0xffff ? 3 : 5;
}
inline uint64_t map_header_size(uint64_t count) {
    return count <= 15 ? 1 : count <= 0xffff ? 3 : 5;
}

/// Spec type code (§5.6.1) of the value whose canonical encoding starts at `p`.
uint8_t tabular_type_code_of_encoding(const uint8_t* p);

/// The qualification + size rule of §5.6.2. On success, fills `keys`.
bool tabular_layout(const Array& rows, std::vector<const std::string*>* keys);

} // namespace btoon::detail

#endif // BTOON_INTERNAL_H

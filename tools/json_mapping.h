// SPDX-FileCopyrightText: 2025 Alvar Laigna <https://alvarlaigna.com>
// SPDX-License-Identifier: MIT
//
// Lossless JSON <-> BTOON mapping used by the CLI tools (btoon, btoon-convert,
// btoon-schema).
//
// JSON types map directly. Integers become int/uint; integer literals outside the
// 64-bit range become bigints (the JSON parser rejects literals beyond about 1e308,
// so write those as $bigint). Numbers with a fraction or exponent become float64.
//
// BTOON types JSON lacks are written as single-key "$"-tagged objects (in the spirit
// of MongoDB Extended JSON), and those tags are recognized again on the way in:
//
//   binary        {"$binary": "<base64>"}
//   timestamp     {"$timestamp": "2024-01-01T00:00:00.5Z"}  (RFC 3339; offset kept)
//   date          {"$date": <milliseconds, integer>}
//   datetime      {"$datetime": "<nanoseconds, decimal string>"}
//   bigint        {"$bigint": "<decimal>"}, or "0x<hex>" / "-0x<hex>"
//   NaN/±Inf      {"$float": "NaN" | "Infinity" | "-Infinity"}
//   f32/f64 vec   {"$f32": [..]} / {"$f64": [..]}, or {"$f32": "<base64>"}
//   extension     {"$ext": [<type>, "<base64>"]}
//   map           {"$map": {...}}  (a map that would otherwise read as a tag)
//
// Rules:
// - A one-key object whose key is one of the tags above is always that tag. Malformed
//   content is an error, never a plain map. Other "$" keys are ordinary map keys.
// - On output, a one-key map whose key starts with "$" is wrapped in {"$map": ...}, so
//   that maps such as {"$binary": "x"} round-trip. The object inside $map is a plain
//   map; its values are converted as usual.
// - $timestamp is RFC 3339: YYYY-MM-DDTHH:MM:SS[.fraction](Z|±HH:MM), with nothing
//   after the offset. "Z" means no offset; ±HH:MM is kept as the timestamp's offset
//   (at most ±18:00). Fields are range-checked, the date must exist, the fraction has
//   1–9 digits, and leap seconds (:60) are rejected. Years outside 0000–9999 are
//   written with a sign and at least 6 digits (+010000, -000001), as JavaScript does.
// - $bigint is decimal while the value fits in 40,000 bytes (about 96,000 digits) and
//   hex above that, which keeps conversion fast. Both forms are accepted on input.
//   Decimal input is limited to 100,000 digits; use hex for larger values.
// - $f32/$f64 arrays hold JSON numbers and $float objects. A vector that contains a
//   NaN other than the default quiet NaN is written as base64 of its little-endian
//   payload instead, so NaN payloads survive (docs/SPEC.md §5.5). Both are accepted.
// - $ext types are integers in -128..127. The ids BTOON defines (-1..-6 and -10) are
//   rejected: use the typed tags instead (docs/SPEC.md §5.7).
// - Values nest at most kMaxDepth (128) containers deep, as in encode() and decode().
//   Tag objects and the $map wrapper don't count. Deeper input is a max_depth error.
#ifndef BTOON_TOOLS_JSON_MAPPING_H
#define BTOON_TOOLS_JSON_MAPPING_H

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "btoon/btoon.h"
#include "nlohmann/json.hpp"

namespace btoon::tools {

// Objects are std::map-backed, so inserting a key is O(log n). (ordered_json scans
// linearly on every insert, which made parsing wide objects quadratic.) Key order
// doesn't matter: BTOON maps are sorted by key bytes anyway.
using json = nlohmann::json;

/// Maximum container nesting, the same default encode() and decode() use.
inline constexpr size_t kMaxDepth = kDefaultMaxDepth;
/// $bigint is written in decimal up to this many bytes (two's complement) and in hex above.
inline constexpr size_t kBigintDecimalMaxBytes = 40000;
/// Longest decimal $bigint accepted on input (hex has no limit).
inline constexpr size_t kBigintDecimalMaxDigits = 100000;

[[noreturn]] inline void mapping_error(const std::string& message) { throw std::runtime_error(message); }

[[noreturn]] inline void depth_error(size_t max_depth) {
    throw BtoonException(ErrorCode::max_depth,
                         "nesting deeper than " + std::to_string(max_depth) + " levels (the maximum depth)");
}

// --- base64 (RFC 4648) ------------------------------------------------------------
inline std::string base64_encode(std::span<const uint8_t> in) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        uint32_t v = (uint32_t(in[i]) << 16) | (uint32_t(in[i + 1]) << 8) | in[i + 2];
        out += {t[v >> 18], t[(v >> 12) & 63], t[(v >> 6) & 63], t[v & 63]};
    }
    if (i + 1 == in.size()) {
        uint32_t v = uint32_t(in[i]) << 16;
        out += {t[v >> 18], t[(v >> 12) & 63], '=', '='};
    } else if (i + 2 == in.size()) {
        uint32_t v = (uint32_t(in[i]) << 16) | (uint32_t(in[i + 1]) << 8);
        out += {t[v >> 18], t[(v >> 12) & 63], t[(v >> 6) & 63], '='};
    }
    return out;
}

/// Strict decoding: standard or URL-safe alphabet; padding optional, but if present it
/// must complete the last quantum; nothing after it; the unused trailing bits are zero.
inline std::vector<uint8_t> base64_decode(std::string_view s) {
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+' || c == '-') return 62;
        if (c == '/' || c == '_') return 63;
        return -1;
    };
    size_t n = s.size();
    size_t pad = 0;
    while (pad < 2 && n > 0 && s[n - 1] == '=') {
        --n;
        ++pad;
    }
    if (pad > 0 && s.size() % 4 != 0) mapping_error("invalid base64: wrong padding");
    if (n % 4 == 1) mapping_error("invalid base64: truncated input");
    std::vector<uint8_t> out;
    out.reserve(n / 4 * 3 + 2);
    uint32_t acc = 0;
    int bits = 0;
    for (size_t i = 0; i < n; ++i) {
        const int v = val(s[i]);
        if (v < 0) mapping_error("invalid base64: unexpected character");
        acc = (acc << 6) | uint32_t(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>(acc >> bits));
            acc &= (1u << bits) - 1;
        }
    }
    if (acc != 0) mapping_error("invalid base64: non-zero trailing bits");
    return out;
}

// --- big integers: two's complement big-endian <-> decimal / hex ---------------------
// The arithmetic uses 32-bit limbs in base 10^9 (64-bit intermediates only, so it is
// portable to compilers without __int128).
namespace detail {

inline void negate_twos(std::vector<uint8_t>& b) {
    for (auto& x : b) x = static_cast<uint8_t>(~x);
    for (size_t i = b.size(); i-- > 0;) {
        if (++b[i] != 0) break;
    }
}

/// Sign and big-endian magnitude of a two's-complement value. Empty means zero.
inline bool split_sign(std::span<const uint8_t> twos, std::vector<uint8_t>& magnitude) {
    magnitude.assign(twos.begin(), twos.end());
    const bool negative = !magnitude.empty() && (magnitude[0] & 0x80) != 0;
    if (negative) negate_twos(magnitude);
    return negative;
}

/// Minimal two's complement bytes for sign + big-endian magnitude.
inline std::vector<uint8_t> join_sign(bool negative, std::vector<uint8_t> magnitude) {
    magnitude.insert(magnitude.begin(), 0);  // room for the sign bit
    if (negative) negate_twos(magnitude);
    return BigInt{std::move(magnitude)}.minimal_bytes();
}

/// Little-endian base-2^32 limbs of a big-endian magnitude, without high zero limbs.
inline std::vector<uint32_t> limbs_from_bytes(std::span<const uint8_t> be) {
    std::vector<uint32_t> limbs((be.size() + 3) / 4, 0);
    for (size_t i = 0; i < be.size(); ++i) {
        const size_t bit = (be.size() - 1 - i) * 8;
        limbs[bit / 32] |= uint32_t(be[i]) << (bit % 32);
    }
    while (!limbs.empty() && limbs.back() == 0) limbs.pop_back();
    return limbs;
}

/// Big-endian magnitude without leading zero bytes (empty for zero).
inline std::vector<uint8_t> bytes_from_limbs(const std::vector<uint32_t>& limbs) {
    std::vector<uint8_t> out;
    out.reserve(limbs.size() * 4);
    for (size_t i = limbs.size(); i-- > 0;) {
        for (int shift = 24; shift >= 0; shift -= 8) {
            const auto byte = static_cast<uint8_t>(limbs[i] >> shift);
            if (!out.empty() || byte != 0) out.push_back(byte);
        }
    }
    return out;
}

inline bool all_digits(std::string_view s) {
    if (s.empty()) return false;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
    }
    return true;
}

inline int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

}  // namespace detail

/// Decimal string of a two's-complement big-endian integer ("0" for no bytes).
inline std::string bigint_to_decimal(std::span<const uint8_t> twos) {
    std::vector<uint8_t> magnitude;
    const bool negative = detail::split_sign(twos, magnitude);
    std::vector<uint32_t> limbs = detail::limbs_from_bytes(magnitude);
    std::vector<uint32_t> chunks;  // base 10^9, least significant first
    chunks.reserve(limbs.size() * 32 / 29 + 1);
    while (!limbs.empty()) {
        uint64_t rem = 0;
        for (size_t i = limbs.size(); i-- > 0;) {
            const uint64_t cur = (rem << 32) | limbs[i];
            limbs[i] = static_cast<uint32_t>(cur / 1000000000u);
            rem = cur % 1000000000u;
        }
        chunks.push_back(static_cast<uint32_t>(rem));
        while (!limbs.empty() && limbs.back() == 0) limbs.pop_back();
    }
    if (chunks.empty()) return "0";
    std::string out = negative ? "-" : "";
    out.reserve(out.size() + chunks.size() * 9);
    out += std::to_string(chunks.back());
    char buf[16];
    for (size_t i = chunks.size() - 1; i-- > 0;) {
        std::snprintf(buf, sizeof buf, "%09u", static_cast<unsigned>(chunks[i]));
        out += buf;
    }
    return out;
}

/// "0x…" / "-0x…" (lowercase, no leading zeros) for a two's-complement integer.
inline std::string bigint_to_hex(std::span<const uint8_t> twos) {
    static const char* digits = "0123456789abcdef";
    std::vector<uint8_t> magnitude;
    const bool negative = detail::split_sign(twos, magnitude);
    std::string hex;
    hex.reserve(magnitude.size() * 2);
    for (uint8_t b : magnitude) {
        hex.push_back(digits[b >> 4]);
        hex.push_back(digits[b & 15]);
    }
    const size_t first = hex.find_first_not_of('0');
    return (negative ? "-0x" : "0x") + (first == std::string::npos ? std::string("0") : hex.substr(first));
}

/// The $bigint text for a value: decimal up to kBigintDecimalMaxBytes, hex above.
inline std::string bigint_to_string(const BigInt& value) {
    const std::vector<uint8_t> minimal = value.minimal_bytes();
    return minimal.size() > kBigintDecimalMaxBytes ? bigint_to_hex(minimal) : bigint_to_decimal(minimal);
}

/// Parses "[-]digits" (at most kBigintDecimalMaxDigits) or "[-]0x<hex>" into minimal
/// two's-complement big-endian bytes.
inline std::vector<uint8_t> bigint_from_string(std::string_view text) {
    std::string_view s = text;
    const bool negative = !s.empty() && s[0] == '-';
    if (negative) s.remove_prefix(1);
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s.remove_prefix(2);
        std::vector<uint8_t> magnitude((s.size() + 1) / 2, 0);
        for (size_t i = 0; i < s.size(); ++i) {
            const int v = detail::hex_value(s[s.size() - 1 - i]);
            if (v < 0) mapping_error("invalid $bigint: expected hex digits after 0x");
            magnitude[magnitude.size() - 1 - i / 2] |= static_cast<uint8_t>(v << (i % 2 ? 4 : 0));
        }
        return detail::join_sign(negative, std::move(magnitude));
    }
    if (!detail::all_digits(s)) {
        mapping_error("invalid $bigint: expected a decimal integer or 0x followed by hex digits");
    }
    if (s.size() > kBigintDecimalMaxDigits) {
        mapping_error("$bigint has " + std::to_string(s.size()) + " decimal digits; the limit is " +
                      std::to_string(kBigintDecimalMaxDigits) + ". Write larger values in hex (\"0x...\").");
    }
    std::vector<uint32_t> limbs;  // little-endian base 2^32
    limbs.reserve(s.size() / 9 + 1);
    static const uint32_t pow10[] = {1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000, 1000000000};
    size_t pos = 0;
    while (pos < s.size()) {
        const size_t len = pos == 0 && s.size() % 9 != 0 ? s.size() % 9 : 9;
        uint32_t chunk = 0;
        for (size_t k = 0; k < len; ++k) chunk = chunk * 10 + static_cast<uint32_t>(s[pos + k] - '0');
        pos += len;
        uint64_t carry = chunk;
        for (auto& limb : limbs) {
            const uint64_t cur = uint64_t(limb) * pow10[len] + carry;
            limb = static_cast<uint32_t>(cur);
            carry = cur >> 32;
        }
        if (carry != 0) limbs.push_back(static_cast<uint32_t>(carry));
    }
    return detail::join_sign(negative, detail::bytes_from_limbs(limbs));
}

// --- RFC 3339 timestamps (proleptic Gregorian, H. Hinnant's civil algorithms) --------
// All arithmetic stays inside int64 for every Timestamp (seconds anywhere in int64,
// offsets within ±18:00) and for every year the parser accepts (at most 12 digits).
namespace detail {

inline int64_t floor_div(int64_t a, int64_t b) {
    int64_t q = a / b;
    if (a % b != 0 && ((a < 0) != (b < 0))) --q;
    return q;
}

inline int64_t floor_mod(int64_t a, int64_t b) {
    int64_t r = a % b;
    if (r != 0 && ((r < 0) != (b < 0))) r += b;
    return r;
}

inline void civil_from_days(int64_t z, int64_t& y, unsigned& m, unsigned& d) {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = static_cast<int64_t>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    if (m <= 2) ++y;
}

inline int64_t days_from_civil(int64_t y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

inline bool is_leap_year(int64_t y) { return y % 4 == 0 && (y % 100 != 0 || y % 400 == 0); }

inline unsigned days_in_month(int64_t y, unsigned m) {
    static const unsigned days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return m == 2 && is_leap_year(y) ? 29 : days[m - 1];
}

/// days * 86400 + secs, or false if that is outside int64. Needs |days| < 2^62 and
/// |secs| < 2^62, which every caller guarantees by a wide margin.
inline bool seconds_from_days(int64_t days, int64_t secs, int64_t& out) {
    static_assert(INT64_MIN % 86400 != 0);
    days += floor_div(secs, 86400);
    secs = floor_mod(secs, 86400);
    constexpr int64_t kMaxDays = INT64_MAX / 86400, kMaxRem = INT64_MAX % 86400;
    constexpr int64_t kMinDays = INT64_MIN / 86400 - 1, kMinRem = INT64_MIN % 86400 + 86400;
    if (days > kMaxDays || (days == kMaxDays && secs > kMaxRem)) return false;
    if (days < kMinDays || (days == kMinDays && secs < kMinRem)) return false;
    // In range, so the wrapping unsigned arithmetic yields the exact value.
    out = static_cast<int64_t>(static_cast<uint64_t>(days) * 86400u + static_cast<uint64_t>(secs));
    return true;
}

}  // namespace detail

inline std::string format_rfc3339(const Timestamp& ts) {
    if (ts.nanoseconds >= 1000000000u) mapping_error("timestamp nanoseconds out of range");
    if (ts.has_timezone && (ts.timezone_offset < -1080 || ts.timezone_offset > 1080)) {
        mapping_error("timestamp offset out of range");
    }
    int64_t days = detail::floor_div(ts.seconds, 86400);
    int64_t secs = detail::floor_mod(ts.seconds, 86400);
    if (ts.has_timezone) {  // local time = UTC + offset
        secs += int64_t(ts.timezone_offset) * 60;
        days += detail::floor_div(secs, 86400);
        secs = detail::floor_mod(secs, 86400);
    }
    int64_t y;
    unsigned mo, d;
    detail::civil_from_days(days, y, mo, d);
    char buf[96];
    if (y >= 0 && y <= 9999) {
        std::snprintf(buf, sizeof buf, "%04lld", static_cast<long long>(y));
    } else {
        std::snprintf(buf, sizeof buf, "%c%06lld", y < 0 ? '-' : '+', static_cast<long long>(y < 0 ? -y : y));
    }
    std::string out = buf;
    std::snprintf(buf, sizeof buf, "-%02u-%02uT%02u:%02u:%02u", mo, d, static_cast<unsigned>(secs / 3600),
                  static_cast<unsigned>(secs / 60 % 60), static_cast<unsigned>(secs % 60));
    out += buf;
    if (ts.nanoseconds) {
        std::snprintf(buf, sizeof buf, ".%09u", static_cast<unsigned>(ts.nanoseconds));
        std::string frac = buf;
        while (frac.back() == '0') frac.pop_back();
        out += frac;
    }
    if (!ts.has_timezone) return out + "Z";
    const int off = ts.timezone_offset;
    std::snprintf(buf, sizeof buf, "%c%02d:%02d", off < 0 ? '-' : '+', std::abs(off) / 60, std::abs(off) % 60);
    return out + buf;
}

inline Timestamp parse_rfc3339(std::string_view s) {
    auto fail = [&](const std::string& why) {
        mapping_error("invalid $timestamp \"" + std::string(s) + "\": " + why);
    };
    auto digit = [](char c) { return c >= '0' && c <= '9'; };
    size_t i = 0;
    auto two_digits = [&](const char* field) {
        if (i + 2 > s.size() || !digit(s[i]) || !digit(s[i + 1])) fail(std::string("expected a 2-digit ") + field);
        const unsigned v = unsigned(s[i] - '0') * 10 + unsigned(s[i + 1] - '0');
        i += 2;
        return v;
    };
    auto expect = [&](char c, const char* what) {
        if (i >= s.size() || s[i] != c) fail(std::string("expected ") + what);
        ++i;
    };

    // Year: 4 digits, or a sign and 4 or more digits (ISO 8601 expanded years).
    bool negative_year = false;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) negative_year = s[i++] == '-';
    const size_t year_start = i;
    int64_t year = 0;
    while (i < s.size() && digit(s[i])) {
        if (i - year_start == 12) fail("year out of range");
        year = year * 10 + (s[i++] - '0');
    }
    if (i - year_start < 4) fail("expected a year of at least 4 digits");
    if (negative_year) year = -year;
    expect('-', "'-' after the year");
    const unsigned month = two_digits("month");
    expect('-', "'-' after the month");
    const unsigned day = two_digits("day");
    if (i >= s.size() || (s[i] != 'T' && s[i] != 't')) fail("expected 'T' between date and time");
    ++i;
    const unsigned hour = two_digits("hour");
    expect(':', "':' after the hour");
    const unsigned minute = two_digits("minute");
    expect(':', "':' after the minutes");
    const unsigned second = two_digits("second");

    uint32_t nanos = 0;
    if (i < s.size() && s[i] == '.') {
        const size_t frac_start = ++i;
        while (i < s.size() && digit(s[i])) {
            if (i - frac_start == 9) fail("more than 9 fractional digits");
            nanos = nanos * 10 + static_cast<uint32_t>(s[i++] - '0');
        }
        if (i == frac_start) fail("expected digits after '.'");
        for (size_t k = i - frac_start; k < 9; ++k) nanos *= 10;
    }

    bool has_offset = false;
    int offset = 0;
    if (i < s.size() && (s[i] == 'Z' || s[i] == 'z')) {
        ++i;
    } else if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
        const int sign = s[i++] == '-' ? -1 : 1;
        const unsigned oh = two_digits("offset hour");
        expect(':', "':' in the offset");
        const unsigned om = two_digits("offset minute");
        if (oh > 23 || om > 59) fail("offset out of range");
        offset = sign * static_cast<int>(oh * 60 + om);
        if (offset < -1080 || offset > 1080) fail("BTOON offsets are limited to ±18:00");
        has_offset = true;
    } else {
        fail("expected 'Z' or a ±HH:MM offset after the time");
    }
    if (i != s.size()) fail("unexpected text after the offset");

    if (month < 1 || month > 12) fail("month out of range");
    if (day < 1 || day > detail::days_in_month(year, month)) fail("no such day in that month");
    if (hour > 23) fail("hour out of range");
    if (minute > 59) fail("minute out of range");
    if (second > 59) fail("second out of range (leap seconds aren't supported)");

    const int64_t local = int64_t(hour) * 3600 + int64_t(minute) * 60 + second;
    int64_t utc;
    if (!detail::seconds_from_days(detail::days_from_civil(year, month, day), local - int64_t(offset) * 60, utc)) {
        fail("outside the range of a 64-bit seconds count");
    }
    return has_offset ? Timestamp(utc, nanos, static_cast<int16_t>(offset)) : Timestamp(utc, nanos);
}

// --- JSON parsing with a depth limit ------------------------------------------------
namespace detail {

/// Marks integer literals outside the 64-bit range in a parsed document. JSON text
/// can't produce binary values, so the marker can't collide with real input.
inline constexpr std::uint64_t kBigLiteralSubtype = 0x6249;

inline bool is_big_literal(const json& j) {
    return j.is_binary() && j.get_binary().has_subtype() && j.get_binary().subtype() == kBigLiteralSubtype;
}

inline std::string big_literal_text(const json& j) {
    const auto& b = j.get_binary();
    return std::string(b.begin(), b.end());
}

/// SAX handler that builds the document iteratively (no recursion) and enforces the
/// nesting limit before descending.
class DomBuilder final : public json::json_sax_t {
public:
    DomBuilder(json& root, size_t max_nesting, size_t max_depth)
        : root_(root), max_nesting_(max_nesting), max_depth_(max_depth) {}

    bool null() override {
        put(nullptr);
        return true;
    }
    bool boolean(bool v) override {
        put(v);
        return true;
    }
    bool number_integer(number_integer_t v) override {
        put(v);
        return true;
    }
    bool number_unsigned(number_unsigned_t v) override {
        put(v);
        return true;
    }
    bool number_float(number_float_t v, const string_t& raw) override {
        // nlohmann falls back to double for integer literals that overflow 64 bits.
        // Keep their digits instead, so they become exact bigints.
        const std::string_view digits = std::string_view(raw).substr(!raw.empty() && raw[0] == '-' ? 1 : 0);
        if (all_digits(digits)) {
            const auto* p = reinterpret_cast<const uint8_t*>(raw.data());
            put(json::binary(json::binary_t::container_type(p, p + raw.size()), kBigLiteralSubtype));
        } else {
            put(v);
        }
        return true;
    }
    bool string(string_t& v) override {
        put(std::move(v));
        return true;
    }
    bool binary(binary_t& v) override {
        put(json::binary(std::move(v)));
        return true;
    }
    bool start_object(std::size_t) override {
        open(json::value_t::object);
        return true;
    }
    bool key(string_t& k) override {
        member_ = &(*stack_.back())[std::move(k)];
        return true;
    }
    bool end_object() override {
        stack_.pop_back();
        return true;
    }
    bool start_array(std::size_t) override {
        open(json::value_t::array);
        return true;
    }
    bool end_array() override {
        stack_.pop_back();
        return true;
    }
    bool parse_error(std::size_t, const std::string&, const nlohmann::detail::exception& ex) override {
        mapping_error(std::string("invalid JSON: ") + ex.what());
    }

private:
    json* put(json&& v) {
        if (stack_.empty()) {
            root_ = std::move(v);
            return &root_;
        }
        json& parent = *stack_.back();
        if (parent.is_array()) {
            parent.push_back(std::move(v));
            return &parent.back();
        }
        *member_ = std::move(v);
        return member_;
    }

    void open(json::value_t type) {
        if (stack_.size() >= max_nesting_) depth_error(max_depth_);
        stack_.push_back(put(json(type)));
    }

    json& root_;
    std::vector<json*> stack_;
    json* member_ = nullptr;
    size_t max_nesting_;
    size_t max_depth_;
};

}  // namespace detail

/// Parses JSON text without recursion and with bounded nesting. `max_depth` counts
/// BTOON containers. The parser itself allows 2 * max_depth + 3 JSON levels, which
/// fits any document to_json() writes ($map wrappers and tag objects add levels);
/// from_json() then enforces max_depth exactly. Integer literals outside the 64-bit
/// range are kept exactly (from_json() turns them into bigints).
inline json parse_json(std::string_view text, size_t max_depth = kMaxDepth) {
    const size_t max_nesting = max_depth > (SIZE_MAX - 3) / 2 ? SIZE_MAX : 2 * max_depth + 3;
    json root;
    detail::DomBuilder builder(root, max_nesting, max_depth);
    if (!json::sax_parse(text.data(), text.data() + text.size(), &builder)) mapping_error("invalid JSON");
    return root;
}

// --- conversions ------------------------------------------------------------------
inline constexpr uint32_t kFloat32QuietNaN = 0x7fc00000u;
inline constexpr uint64_t kFloat64QuietNaN = 0x7ff8000000000000ull;

namespace detail {

inline json tagged(const char* tag, json value) {
    json o = json::object();
    o.emplace(tag, std::move(value));
    return o;
}

inline bool is_tag(std::string_view k) {
    return k == "$binary" || k == "$timestamp" || k == "$date" || k == "$datetime" || k == "$bigint" ||
           k == "$float" || k == "$f32" || k == "$f64" || k == "$ext" || k == "$map";
}

inline json float_json(double f) {
    if (std::isnan(f)) return tagged("$float", "NaN");
    if (std::isinf(f)) return tagged("$float", f > 0 ? "Infinity" : "-Infinity");
    return f;
}

// Bits are copied with memcpy, never through a floating-point register: an x87 load
// (32-bit x86) would quiet a signaling NaN.
template <typename T, typename Bits>
Bits bits_of(const T& f) {
    Bits bits;
    std::memcpy(&bits, &f, sizeof bits);
    return bits;
}

template <typename T, typename Bits>
json vector_json(const char* tag, const std::vector<T>& values, Bits quiet_nan) {
    bool raw = false;
    for (const T& f : values) {
        if (std::isnan(f) && bits_of<T, Bits>(f) != quiet_nan) raw = true;
    }
    if (raw) {  // keep NaN payloads: base64 of the little-endian payload
        std::vector<uint8_t> bytes;
        bytes.reserve(values.size() * sizeof(T));
        for (const T& f : values) {
            const Bits bits = bits_of<T, Bits>(f);
            for (size_t k = 0; k < sizeof(T); ++k) bytes.push_back(static_cast<uint8_t>(bits >> (8 * k)));
        }
        return tagged(tag, base64_encode(bytes));
    }
    json a = json::array();
    for (const T& f : values) a.push_back(float_json(static_cast<double>(f)));
    return tagged(tag, std::move(a));
}

inline json to_json_at(const Value& v, size_t depth, size_t max_depth) {
    switch (v.index()) {
        case 0: return nullptr;
        case 1: return std::get<Bool>(v);
        case 2: return std::get<Int>(v);
        case 3: return std::get<Uint>(v);
        case 4: return float_json(std::get<Float>(v));
        case 5: return std::get<String>(v);
        case 6: return tagged("$binary", base64_encode(std::get<Binary>(v)));
        case 7: {
            if (depth >= max_depth) depth_error(max_depth);
            json a = json::array();
            for (const auto& e : std::get<Array>(v)) a.push_back(to_json_at(e, depth + 1, max_depth));
            return a;
        }
        case 8: {
            if (depth >= max_depth) depth_error(max_depth);
            const auto& m = std::get<Map>(v);
            json o = json::object();
            for (const auto& [k, e] : m) o.emplace(k, to_json_at(e, depth + 1, max_depth));
            // A one-key map with a "$" key would read back as a tag (now or in a later
            // version), so it is escaped.
            if (m.size() == 1 && !m.begin()->first.empty() && m.begin()->first[0] == '$') {
                return tagged("$map", std::move(o));
            }
            return o;
        }
        case 9: {
            const auto& e = std::get<Extension>(v);
            return tagged("$ext", json::array({static_cast<int>(e.type), base64_encode(e.data)}));
        }
        case 10: return tagged("$timestamp", format_rfc3339(std::get<Timestamp>(v)));
        case 11: return tagged("$date", std::get<Date>(v).milliseconds);
        case 12: return tagged("$datetime", std::to_string(std::get<DateTime>(v).nanoseconds));
        case 13: return tagged("$bigint", bigint_to_string(std::get<BigInt>(v)));
        case 14: return vector_json("$f32", std::get<VectorFloat>(v).data, kFloat32QuietNaN);
        case 15: return vector_json("$f64", std::get<VectorDouble>(v).data, kFloat64QuietNaN);
    }
    mapping_error("unknown value type");
}

inline double special_float(const json& x) {
    if (x.is_string()) {
        const auto& s = x.get_ref<const std::string&>();
        if (s == "NaN") return std::bit_cast<double>(kFloat64QuietNaN);
        if (s == "Infinity") return std::numeric_limits<double>::infinity();
        if (s == "-Infinity") return -std::numeric_limits<double>::infinity();
    }
    mapping_error("$float must be \"NaN\", \"Infinity\" or \"-Infinity\" (finite floats are plain numbers)");
}

/// One element of a $f32/$f64 array: a JSON number or a {"$float": ...} object.
inline double vector_element(const json& e, const char* tag) {
    if (e.is_number()) return e.get<double>();
    if (is_big_literal(e)) return std::strtod(big_literal_text(e).c_str(), nullptr);  // digits only; finite
    if (e.is_object() && e.size() == 1 && e.begin().key() == "$float") return special_float(e.begin().value());
    mapping_error(std::string(tag) + " elements must be numbers or {\"$float\": ...}");
}

inline float to_float32(double d) {
    if (std::isnan(d)) return std::bit_cast<float>(kFloat32QuietNaN);
    // Values at or beyond the midpoint between FLT_MAX and 2^128 would round to infinity.
    if (std::isfinite(d) && std::fabs(d) >= 0x1.ffffffp127) mapping_error("$f32 element out of float32 range");
    return static_cast<float>(d);
}

template <typename T, typename Bits>
std::vector<T> vector_from_json(const json& x, const char* tag) {
    std::vector<T> out;
    if (x.is_string()) {
        const auto bytes = base64_decode(x.get_ref<const std::string&>());
        if (bytes.size() % sizeof(T) != 0) {
            mapping_error(std::string(tag) + " base64 payload must be a multiple of " + std::to_string(sizeof(T)) +
                          " bytes");
        }
        out.resize(bytes.size() / sizeof(T));
        for (size_t i = 0; i < out.size(); ++i) {
            Bits bits = 0;
            for (size_t k = 0; k < sizeof(T); ++k) bits |= Bits(bytes[i * sizeof(T) + k]) << (8 * k);
            std::memcpy(&out[i], &bits, sizeof bits);
        }
        return out;
    }
    if (!x.is_array()) mapping_error(std::string(tag) + " must be an array of numbers or a base64 string");
    out.reserve(x.size());
    for (const auto& e : x) {
        const double d = vector_element(e, tag);
        if constexpr (sizeof(T) == 4) {
            out.push_back(to_float32(d));
        } else {
            out.push_back(d);
        }
    }
    return out;
}

/// Strict "-?[0-9]+" within int64.
inline bool parse_int64(std::string_view s, int64_t& out) {
    const bool negative = !s.empty() && s[0] == '-';
    if (negative) s.remove_prefix(1);
    if (!all_digits(s)) return false;
    const uint64_t limit = negative ? uint64_t(INT64_MAX) + 1 : uint64_t(INT64_MAX);
    uint64_t v = 0;
    for (char c : s) {
        const auto digit = static_cast<uint64_t>(c - '0');
        if (v > (limit - digit) / 10) return false;
        v = v * 10 + digit;
    }
    out = negative ? static_cast<int64_t>(0 - v) : static_cast<int64_t>(v);
    return true;
}

inline Value from_json_at(const json& j, bool tags, size_t depth, size_t max_depth);

inline Map map_from_object(const json& j, bool tags, size_t depth, size_t max_depth) {
    if (depth >= max_depth) depth_error(max_depth);
    Map m;
    for (auto it = j.begin(); it != j.end(); ++it) {
        m.emplace_hint(m.end(), it.key(), from_json_at(it.value(), tags, depth + 1, max_depth));
    }
    return m;
}

inline Value from_tag(const std::string& k, const json& x, bool tags, size_t depth, size_t max_depth) {
    auto need_string = [&]() -> const std::string& {
        if (!x.is_string()) mapping_error(k + " must be a string");
        return x.get_ref<const std::string&>();
    };
    if (k == "$binary") return base64_decode(need_string());
    if (k == "$timestamp") return parse_rfc3339(need_string());
    if (k == "$date") {
        if ((x.is_number_unsigned() && x.get<uint64_t>() > uint64_t(INT64_MAX)) || is_big_literal(x)) {
            mapping_error("$date out of range");
        }
        if (!x.is_number_integer()) mapping_error("$date must be an integer (milliseconds since the epoch)");
        return Date{x.get<int64_t>()};
    }
    if (k == "$datetime") {
        int64_t ns;
        if (!x.is_string() || !parse_int64(x.get_ref<const std::string&>(), ns)) {
            mapping_error("$datetime must be a string holding a 64-bit integer (nanoseconds since the epoch)");
        }
        return DateTime{ns};
    }
    if (k == "$bigint") return BigInt{bigint_from_string(need_string())};
    if (k == "$float") return special_float(x);
    if (k == "$f32") return VectorFloat{vector_from_json<float, uint32_t>(x, "$f32")};
    if (k == "$f64") return VectorDouble{vector_from_json<double, uint64_t>(x, "$f64")};
    if (k == "$ext") {
        if (!x.is_array() || x.size() != 2 || !x[1].is_string()) mapping_error("$ext must be [type, \"<base64>\"]");
        const json& t = x[0];
        if (!t.is_number_integer() || (t.is_number_unsigned() && t.get<uint64_t>() > 127)) {
            mapping_error("$ext type must be an integer in -128..127");
        }
        const int64_t type = t.get<int64_t>();
        if (type < -128 || type > 127) mapping_error("$ext type must be an integer in -128..127");
        if ((type >= -6 && type <= -1) || type == -10) {
            mapping_error("$ext type " + std::to_string(type) +
                          " is a BTOON type; write it with its own tag ($timestamp, $date, $datetime, $bigint, "
                          "$f32, $f64) or as an array of maps");
        }
        return Extension{static_cast<int8_t>(type), base64_decode(x[1].get_ref<const std::string&>())};
    }
    // "$map": the object inside is a plain map, whatever its keys look like.
    if (!x.is_object()) mapping_error("$map must contain an object");
    return map_from_object(x, tags, depth, max_depth);
}

inline Value from_json_at(const json& j, bool tags, size_t depth, size_t max_depth) {
    switch (j.type()) {
        case json::value_t::null: return nullptr;
        case json::value_t::boolean: return j.get<bool>();
        case json::value_t::number_unsigned: return Value(j.get<uint64_t>());
        case json::value_t::number_integer: return Value(j.get<int64_t>());
        case json::value_t::number_float: return Value(j.get<double>());
        case json::value_t::string: return j.get<std::string>();
        case json::value_t::binary:
            if (is_big_literal(j)) return BigInt{bigint_from_string(big_literal_text(j))};
            mapping_error("binary values can't be converted");
        case json::value_t::array: {
            if (depth >= max_depth) depth_error(max_depth);
            Array a;
            a.reserve(j.size());
            for (const auto& e : j) a.push_back(from_json_at(e, tags, depth + 1, max_depth));
            return a;
        }
        case json::value_t::object:
            if (tags && j.size() == 1 && is_tag(j.begin().key())) {
                return from_tag(j.begin().key(), j.begin().value(), tags, depth, max_depth);
            }
            return map_from_object(j, tags, depth, max_depth);
        case json::value_t::discarded: break;
    }
    mapping_error("invalid JSON value");
}

}  // namespace detail

/// BTOON value -> JSON (see the rules at the top of this file). Values nested deeper
/// than max_depth containers throw max_depth.
inline json to_json(const Value& v, size_t max_depth = kMaxDepth) { return detail::to_json_at(v, 0, max_depth); }

/// JSON -> BTOON value. With tags=false, "$" objects are plain maps. Values nested
/// deeper than max_depth containers throw max_depth.
inline Value from_json(const json& j, bool tags = true, size_t max_depth = kMaxDepth) {
    return detail::from_json_at(j, tags, 0, max_depth);
}

}  // namespace btoon::tools

#endif

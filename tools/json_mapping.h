// SPDX-FileCopyrightText: 2025 Alvar Laigna <https://alvarlaigna.com>
// SPDX-License-Identifier: MIT
//
// Lossless JSON <-> BTOON mapping used by the CLI tools.
//
// JSON types map directly. BTOON types JSON lacks are written as single-key
// "$"-tagged objects (in the spirit of MongoDB Extended JSON), and those tags
// are recognized again on the way in:
//
//   binary        {"$binary": "<base64>"}
//   timestamp     {"$timestamp": "2024-01-01T00:00:00.5Z"}  (RFC 3339; offset kept)
//   date          {"$date": <milliseconds>}
//   datetime      {"$datetime": "<nanoseconds>"}
//   bigint        {"$bigint": "<decimal>"}
//   NaN/±Inf      {"$float": "NaN" | "Infinity" | "-Infinity"}
//   f32/f64 vec   {"$f32": [..]} / {"$f64": [..]}
//   extension     {"$ext": [<type>, "<base64>"]}
#ifndef BTOON_TOOLS_JSON_MAPPING_H
#define BTOON_TOOLS_JSON_MAPPING_H

#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>

#include "btoon/btoon.h"
#include "nlohmann/json.hpp"

namespace btoon::tools {

using json = nlohmann::ordered_json;

// --- base64 ------------------------------------------------------------------
inline std::string base64_encode(const std::vector<uint8_t>& in) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
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

inline std::vector<uint8_t> base64_decode(const std::string& s) {
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+' || c == '-') return 62;
        if (c == '/' || c == '_') return 63;
        return -1;
    };
    std::vector<uint8_t> out;
    uint32_t acc = 0;
    int bits = 0;
    for (char c : s) {
        if (c == '=') break;
        int v = val(c);
        if (v < 0) throw std::runtime_error("invalid base64");
        acc = (acc << 6) | uint32_t(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>(acc >> bits));
        }
    }
    return out;
}

// --- big integers (two's complement BE <-> decimal) ---------------------------
inline std::string bigint_to_decimal(std::vector<uint8_t> b) {
    if (b.empty()) return "0";
    bool neg = b[0] & 0x80;
    if (neg) {  // magnitude = two's complement negation
        for (auto& x : b) x = static_cast<uint8_t>(~x);
        for (size_t i = b.size(); i-- > 0;) {
            if (++b[i] != 0) break;
        }
    }
    std::string digits;
    while (!(b.size() == 1 && b[0] == 0) && !b.empty()) {
        uint32_t rem = 0;
        std::vector<uint8_t> q;
        for (uint8_t x : b) {
            uint32_t cur = (rem << 8) | x;
            uint8_t d = static_cast<uint8_t>(cur / 10);
            rem = cur % 10;
            if (!q.empty() || d) q.push_back(d);
        }
        digits.push_back(static_cast<char>('0' + rem));
        b = q.empty() ? std::vector<uint8_t>{0} : q;
    }
    if (digits.empty()) digits = "0";
    if (neg) digits.push_back('-');
    return {digits.rbegin(), digits.rend()};
}

inline std::vector<uint8_t> decimal_to_bigint(const std::string& dec) {
    bool neg = !dec.empty() && dec[0] == '-';
    std::vector<uint8_t> mag{0};
    for (size_t i = neg ? 1 : 0; i < dec.size(); ++i) {
        if (dec[i] < '0' || dec[i] > '9') throw std::runtime_error("invalid $bigint");
        int carry = dec[i] - '0';
        for (size_t j = mag.size(); j-- > 0;) {
            int v = mag[j] * 10 + carry;
            mag[j] = static_cast<uint8_t>(v & 0xff);
            carry = v >> 8;
        }
        while (carry) {
            mag.insert(mag.begin(), static_cast<uint8_t>(carry & 0xff));
            carry >>= 8;
        }
    }
    mag.insert(mag.begin(), 0);
    if (neg) {
        for (auto& x : mag) x = static_cast<uint8_t>(~x);
        for (size_t j = mag.size(); j-- > 0;) {
            if (++mag[j] != 0) break;
        }
    }
    return BigInt{mag}.minimal_bytes();
}

// --- RFC 3339 timestamps (proleptic Gregorian, H. Hinnant's civil algorithms) --
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
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

inline std::string format_rfc3339(const Timestamp& ts) {
    int64_t local = ts.seconds + (ts.has_timezone ? ts.timezone_offset * 60LL : 0);
    int64_t days = local >= 0 ? local / 86400 : (local - 86399) / 86400;
    int64_t secs = local - days * 86400;
    int64_t y;
    unsigned mo, d;
    civil_from_days(days, y, mo, d);
    char buf[96];
    std::snprintf(buf, sizeof buf, "%04lld-%02u-%02uT%02lld:%02lld:%02lld", static_cast<long long>(y), mo, d,
                  static_cast<long long>(secs / 3600), static_cast<long long>(secs / 60 % 60),
                  static_cast<long long>(secs % 60));
    std::string out = buf;
    if (ts.nanoseconds) {
        std::snprintf(buf, sizeof buf, ".%09u", ts.nanoseconds);
        std::string frac = buf;
        while (frac.back() == '0') frac.pop_back();
        out += frac;
    }
    if (!ts.has_timezone) return out + "Z";
    int off = ts.timezone_offset;
    std::snprintf(buf, sizeof buf, "%c%02d:%02d", off < 0 ? '-' : '+', std::abs(off) / 60, std::abs(off) % 60);
    return out + buf;
}

inline Timestamp parse_rfc3339(const std::string& s) {
    long long y;
    unsigned mo, d, h, mi, se;
    int consumed = 0;
    if (std::sscanf(s.c_str(), "%lld-%u-%uT%u:%u:%u%n", &y, &mo, &d, &h, &mi, &se, &consumed) != 6) {
        throw std::runtime_error("invalid $timestamp: " + s);
    }
    size_t i = static_cast<size_t>(consumed);
    uint32_t nanos = 0;
    if (i < s.size() && s[i] == '.') {
        int digits = 0;
        for (++i; i < s.size() && s[i] >= '0' && s[i] <= '9'; ++i, ++digits) {
            if (digits < 9) nanos = nanos * 10 + static_cast<uint32_t>(s[i] - '0');
        }
        for (; digits < 9; ++digits) nanos *= 10;
    }
    int64_t local = days_from_civil(y, mo, d) * 86400 + h * 3600LL + mi * 60LL + se;
    if (i < s.size() && (s[i] == 'Z' || s[i] == 'z')) return Timestamp(local, nanos);
    if (i + 6 <= s.size() && (s[i] == '+' || s[i] == '-')) {
        int oh = std::stoi(s.substr(i + 1, 2)), om = std::stoi(s.substr(i + 4, 2));
        auto off = static_cast<int16_t>((s[i] == '-' ? -1 : 1) * (oh * 60 + om));
        return Timestamp(local - off * 60LL, nanos, off);
    }
    throw std::runtime_error("invalid $timestamp (needs Z or ±HH:MM): " + s);
}

// --- conversions -----------------------------------------------------------------
inline json float_json(double f) {
    if (std::isnan(f)) return json{{"$float", "NaN"}};
    if (std::isinf(f)) return json{{"$float", f > 0 ? "Infinity" : "-Infinity"}};
    return f;
}

inline json to_json(const Value& v) {
    switch (v.index()) {
        case 0: return nullptr;
        case 1: return std::get<Bool>(v);
        case 2: return std::get<Int>(v);
        case 3: return std::get<Uint>(v);
        case 4: return float_json(std::get<Float>(v));
        case 5: return std::get<String>(v);
        case 6: return json{{"$binary", base64_encode(std::get<Binary>(v))}};
        case 7: {
            json a = json::array();
            for (const auto& e : std::get<Array>(v)) a.push_back(to_json(e));
            return a;
        }
        case 8: {
            json o = json::object();
            for (const auto& [k, e] : std::get<Map>(v)) o[k] = to_json(e);
            return o;
        }
        case 9: {
            const auto& e = std::get<Extension>(v);
            return json{{"$ext", json::array({e.type, base64_encode(e.data)})}};
        }
        case 10: return json{{"$timestamp", format_rfc3339(std::get<Timestamp>(v))}};
        case 11: return json{{"$date", std::get<Date>(v).milliseconds}};
        case 12: return json{{"$datetime", std::to_string(std::get<DateTime>(v).nanoseconds)}};
        case 13: return json{{"$bigint", bigint_to_decimal(std::get<BigInt>(v).bytes)}};
        case 14: {
            json a = json::array();
            for (float f : std::get<VectorFloat>(v).data) a.push_back(float_json(f));
            return json{{"$f32", a}};
        }
        case 15: {
            json a = json::array();
            for (double f : std::get<VectorDouble>(v).data) a.push_back(float_json(f));
            return json{{"$f64", a}};
        }
    }
    return nullptr;
}

inline double json_float(const json& j) {
    if (j.is_object() && j.contains("$float")) {
        auto s = j.at("$float").get<std::string>();
        if (s == "NaN") return std::numeric_limits<double>::quiet_NaN();
        return s == "-Infinity" ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity();
    }
    return j.get<double>();
}

inline Value from_json(const json& j, bool tags = true) {
    if (j.is_null()) return nullptr;
    if (j.is_boolean()) return j.get<bool>();
    if (j.is_number_unsigned()) return Value(j.get<uint64_t>());
    if (j.is_number_integer()) return Value(j.get<int64_t>());
    if (j.is_number_float()) return Value(j.get<double>());
    if (j.is_string()) return j.get<std::string>();
    if (j.is_array()) {
        Array a;
        a.reserve(j.size());
        for (const auto& e : j) a.push_back(from_json(e, tags));
        return a;
    }
    if (tags && j.size() == 1) {
        const auto it = j.begin();
        const std::string k = it.key();
        const json& x = it.value();
        if (k == "$binary") return base64_decode(x.get<std::string>());
        if (k == "$timestamp") return parse_rfc3339(x.get<std::string>());
        if (k == "$date") return Date{x.get<int64_t>()};
        if (k == "$datetime") return DateTime{std::stoll(x.get<std::string>())};
        if (k == "$bigint") return BigInt{decimal_to_bigint(x.get<std::string>())};
        if (k == "$float") return json_float(j);
        if (k == "$f32") {
            VectorFloat vf;
            for (const auto& e : x) vf.data.push_back(static_cast<float>(json_float(e)));
            return vf;
        }
        if (k == "$f64") {
            VectorDouble vd;
            for (const auto& e : x) vd.data.push_back(json_float(e));
            return vd;
        }
        if (k == "$ext") return Extension{x.at(0).get<int8_t>(), base64_decode(x.at(1).get<std::string>())};
    }
    Map m;
    for (const auto& [k, e] : j.items()) m.insert_or_assign(k, from_json(e, tags));
    return m;
}

}  // namespace btoon::tools

#endif

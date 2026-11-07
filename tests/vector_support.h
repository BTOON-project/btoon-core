// Helpers for working with the typed JSON form of conformance vectors
// (docs/SPEC.md §10). Shared by test_vectors.cpp and fuzz_smoke.cpp.
#ifndef BTOON_TESTS_VECTOR_SUPPORT_H
#define BTOON_TESTS_VECTOR_SUPPORT_H

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "btoon/btoon.h"
#include "nlohmann/json.hpp"

namespace vtest {

using btoon::Value;

inline std::vector<uint8_t> unhex(const std::string& h) {
    if (h.size() % 2) throw std::runtime_error("odd hex length");
    std::vector<uint8_t> out(h.size() / 2);
    for (size_t i = 0; i < out.size(); ++i) out[i] = static_cast<uint8_t>(std::stoul(h.substr(2 * i, 2), nullptr, 16));
    return out;
}

inline std::string hex(const std::vector<uint8_t>& b) {
    static const char* d = "0123456789abcdef";
    std::string s;
    s.reserve(b.size() * 2);
    for (uint8_t x : b) {
        s.push_back(d[x >> 4]);
        s.push_back(d[x & 15]);
    }
    return s;
}

/// Decimal string -> minimal two's-complement big-endian bytes.
inline std::vector<uint8_t> decimal_to_twos(const std::string& dec) {
    bool neg = !dec.empty() && dec[0] == '-';
    std::vector<uint8_t> mag{0};  // big-endian magnitude
    for (size_t i = neg ? 1 : 0; i < dec.size(); ++i) {
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
    mag.insert(mag.begin(), 0);  // room for the sign bit
    if (neg) {
        for (auto& b : mag) b = static_cast<uint8_t>(~b);
        for (size_t j = mag.size(); j-- > 0;) {
            if (++mag[j] != 0) break;
        }
    }
    return btoon::BigInt{mag}.minimal_bytes();
}

inline Value integer_from_decimal(const std::string& dec) {
    auto b = decimal_to_twos(dec);
    if (b.size() <= 8) {
        uint64_t v = (b[0] & 0x80) ? ~uint64_t(0) : 0;
        for (uint8_t x : b) v = (v << 8) | x;
        return Value(static_cast<int64_t>(v));
    }
    if (b.size() == 9 && b[0] == 0x00) {
        uint64_t v = 0;
        for (size_t i = 1; i < 9; ++i) v = (v << 8) | b[i];
        return Value(v);
    }
    return Value(btoon::BigInt{b});
}

inline double parse_float(const std::string& s) {
    if (s == "NaN") return std::numeric_limits<double>::quiet_NaN();
    if (s == "Infinity") return std::numeric_limits<double>::infinity();
    if (s == "-Infinity") return -std::numeric_limits<double>::infinity();
    return std::strtod(s.c_str(), nullptr);
}

inline Value from_typed(const nlohmann::json& t) {
    if (!t.is_object() || t.size() != 1) throw std::runtime_error("typed value must be a 1-key object");
    const auto it = t.begin();  // not t.items(): its proxy is a temporary
    const std::string kind = it.key();
    const nlohmann::json& x = it.value();
    if (kind == "nil") return Value(nullptr);
    if (kind == "bool") return Value(x.get<bool>());
    if (kind == "int") return integer_from_decimal(x.get<std::string>());
    if (kind == "float") return Value(parse_float(x.get<std::string>()));
    if (kind == "string") return Value(x.get<std::string>());
    if (kind == "binary") return Value(unhex(x.get<std::string>()));
    if (kind == "array") {
        btoon::Array a;
        for (const auto& e : x) a.push_back(from_typed(e));
        return Value(std::move(a));
    }
    if (kind == "map") {
        btoon::Map m;
        for (const auto& [k, e] : x.items()) m.emplace(k, from_typed(e));
        return Value(std::move(m));
    }
    if (kind == "timestamp") {
        btoon::Timestamp ts(std::stoll(x.at("seconds").get<std::string>()), x.at("nanoseconds").get<uint32_t>());
        if (x.contains("offset_minutes")) {
            ts.timezone_offset = x.at("offset_minutes").get<int16_t>();
            ts.has_timezone = true;
        }
        return Value(ts);
    }
    if (kind == "date") return Value(btoon::Date{std::stoll(x.get<std::string>())});
    if (kind == "datetime") return Value(btoon::DateTime{std::stoll(x.get<std::string>())});
    if (kind == "float32_vector") {
        btoon::VectorFloat v;
        for (const auto& e : x) v.data.push_back(static_cast<float>(parse_float(e.get<std::string>())));
        return Value(std::move(v));
    }
    if (kind == "float64_vector") {
        btoon::VectorDouble v;
        for (const auto& e : x) v.data.push_back(parse_float(e.get<std::string>()));
        return Value(std::move(v));
    }
    if (kind == "ext") {
        return Value(btoon::Extension{static_cast<int8_t>(x.at("type").get<int>()), unhex(x.at("data").get<std::string>())});
    }
    throw std::runtime_error("unknown typed value kind: " + kind);
}

/// Integer value as minimal two's complement, for Int/Uint/BigInt alike.
inline bool integer_bytes(const Value& v, std::vector<uint8_t>* out) {
    if (auto i = std::get_if<btoon::Int>(&v)) {
        std::vector<uint8_t> b(8);
        for (int k = 0; k < 8; ++k) b[k] = static_cast<uint8_t>(static_cast<uint64_t>(*i) >> (56 - 8 * k));
        *out = btoon::BigInt{b}.minimal_bytes();
        return true;
    }
    if (auto u = std::get_if<btoon::Uint>(&v)) {
        std::vector<uint8_t> b(9, 0);
        for (int k = 0; k < 8; ++k) b[k + 1] = static_cast<uint8_t>(*u >> (56 - 8 * k));
        *out = btoon::BigInt{b}.minimal_bytes();
        return true;
    }
    if (auto bi = std::get_if<btoon::BigInt>(&v)) {
        *out = bi->minimal_bytes();
        return true;
    }
    return false;
}

inline bool same_double(double a, double b) {
    if (std::isnan(a) || std::isnan(b)) return std::isnan(a) && std::isnan(b);
    return std::bit_cast<uint64_t>(a) == std::bit_cast<uint64_t>(b);  // distinguishes -0.0
}

/// Structural equality for tests: NaN equals NaN, -0.0 differs from 0.0, and
/// integers compare by value across Int/Uint/BigInt.
inline bool equivalent(const Value& a, const Value& b) {
    std::vector<uint8_t> ia, ib;
    if (integer_bytes(a, &ia) && integer_bytes(b, &ib)) return ia == ib;
    if (a.index() != b.index()) return false;
    if (auto f = std::get_if<btoon::Float>(&a)) return same_double(*f, std::get<btoon::Float>(b));
    if (auto x = std::get_if<btoon::Array>(&a)) {
        const auto& y = std::get<btoon::Array>(b);
        if (x->size() != y.size()) return false;
        for (size_t i = 0; i < x->size(); ++i) {
            if (!equivalent((*x)[i], y[i])) return false;
        }
        return true;
    }
    if (auto x = std::get_if<btoon::Map>(&a)) {
        const auto& y = std::get<btoon::Map>(b);
        if (x->size() != y.size()) return false;
        for (auto i = x->begin(), j = y.begin(); i != x->end(); ++i, ++j) {
            if (i->first != j->first || !equivalent(i->second, j->second)) return false;
        }
        return true;
    }
    if (auto x = std::get_if<btoon::VectorFloat>(&a)) {
        const auto& y = std::get<btoon::VectorFloat>(b);
        if (x->data.size() != y.data.size()) return false;
        for (size_t i = 0; i < x->data.size(); ++i) {
            if (!same_double(x->data[i], y.data[i])) return false;
        }
        return true;
    }
    if (auto x = std::get_if<btoon::VectorDouble>(&a)) {
        const auto& y = std::get<btoon::VectorDouble>(b);
        if (x->data.size() != y.data.size()) return false;
        for (size_t i = 0; i < x->data.size(); ++i) {
            if (!same_double(x->data[i], y.data[i])) return false;
        }
        return true;
    }
    return a == b;
}

}  // namespace vtest

#endif

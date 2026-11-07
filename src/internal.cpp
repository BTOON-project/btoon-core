// SPDX-FileCopyrightText: 2025 Alvar Laigna <https://alvarlaigna.com>
// SPDX-License-Identifier: MIT
#include "internal.h"

namespace btoon {

const char* error_code_name(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::generic: return "error";
        case ErrorCode::truncated: return "truncated";
        case ErrorCode::invalid_marker: return "invalid_marker";
        case ErrorCode::trailing_data: return "trailing_data";
        case ErrorCode::invalid_utf8: return "invalid_utf8";
        case ErrorCode::non_string_key: return "non_string_key";
        case ErrorCode::duplicate_key: return "duplicate_key";
        case ErrorCode::max_depth: return "max_depth";
        case ErrorCode::invalid_extension: return "invalid_extension";
        case ErrorCode::invalid_tabular: return "invalid_tabular";
        case ErrorCode::invalid_envelope: return "invalid_envelope";
        case ErrorCode::size_limit: return "size_limit";
        case ErrorCode::unsupported_compression: return "unsupported_compression";
        case ErrorCode::authentication_failed: return "authentication_failed";
        case ErrorCode::invalid_argument: return "invalid_argument";
    }
    return "error";
}

std::vector<uint8_t> BigInt::minimal_bytes() const { return detail::bigint_minimal(bytes); }

bool tabular_beneficial(size_t num_rows, std::span<const size_t> key_lengths) {
    if (num_rows < 2 || key_lengths.empty()) return false;
    // Size rule (§5.6.2): N * (maphdr(C) + Σ(strhdr(L)+L)) > 18 + Σ(9 + L)
    uint64_t per_row = detail::map_header_size(key_lengths.size());
    uint64_t overhead = 18;
    for (size_t len : key_lengths) {
        per_row += detail::str_header_size(len) + len;
        overhead += 9 + static_cast<uint64_t>(len);
    }
    // N * per_row > overhead  <=>  per_row > overhead / N  (integers, no overflow)
    return per_row > overhead / num_rows;
}

namespace detail {

void fail(ErrorCode code, const std::string& message) { throw BtoonException(code, message); }

bool is_valid_utf8(const uint8_t* s, size_t len) noexcept {
    size_t i = 0;
    while (i < len) {
        // ASCII fast path: 8 bytes at a time.
        if (i + 8 <= len) {
            uint64_t chunk;
            std::memcpy(&chunk, s + i, 8);
            if ((chunk & 0x8080808080808080ULL) == 0) {
                i += 8;
                continue;
            }
        }
        uint8_t c = s[i];
        if (c < 0x80) {
            ++i;
            continue;
        }
        size_t need;
        uint32_t cp;
        if (c >= 0xc2 && c <= 0xdf) {
            need = 1;
            cp = c & 0x1fu;
        } else if (c >= 0xe0 && c <= 0xef) {
            need = 2;
            cp = c & 0x0fu;
        } else if (c >= 0xf0 && c <= 0xf4) {
            need = 3;
            cp = c & 0x07u;
        } else {
            return false;  // continuation byte, overlong 2-byte lead (c0/c1), or > f4
        }
        if (len - i <= need) return false;
        for (size_t k = 1; k <= need; ++k) {
            uint8_t cc = s[i + k];
            if ((cc & 0xc0) != 0x80) return false;
            cp = (cp << 6) | (cc & 0x3fu);
        }
        if (need == 2 && (cp < 0x800 || (cp >= 0xd800 && cp <= 0xdfff))) return false;
        if (need == 3 && (cp < 0x10000 || cp > 0x10ffff)) return false;
        i += need + 1;
    }
    return true;
}

std::vector<uint8_t> bigint_minimal(std::span<const uint8_t> b) {
    size_t start = 0;
    while (start + 1 < b.size()) {
        if (b[start] == 0x00 && (b[start + 1] & 0x80) == 0) {
            ++start;
        } else if (b[start] == 0xff && (b[start + 1] & 0x80) != 0) {
            ++start;
        } else {
            break;
        }
    }
    return {b.begin() + static_cast<std::ptrdiff_t>(start), b.end()};
}

Int64Fit bigint_fit64(std::span<const uint8_t> bytes) {
    Int64Fit fit;
    if (bytes.empty()) return fit;
    auto m = bigint_minimal(bytes);
    bool negative = (m[0] & 0x80) != 0;
    if (m.size() > 9 || (m.size() == 9 && (negative || m[0] != 0x00))) return fit;
    uint64_t v = negative ? ~uint64_t(0) : 0;
    for (uint8_t byte : m) v = (v << 8) | byte;
    if (negative) {
        fit.kind = Int64Fit::kSigned;
        fit.i = static_cast<int64_t>(v);
    } else if (v <= static_cast<uint64_t>(INT64_MAX)) {
        fit.kind = Int64Fit::kSigned;
        fit.i = static_cast<int64_t>(v);
    } else {
        fit.kind = Int64Fit::kUnsigned;
        fit.u = v;
    }
    return fit;
}

uint8_t tabular_type_code_of_encoding(const uint8_t* p) {
    // §5.6.1 codes from the logical type, read off the canonical encoding.
    const uint8_t m = p[0];
    if (m <= 0x7f || m >= 0xe0) return 2;  // fixints
    if (m <= 0x8f) return 8;               // fixmap
    if (m <= 0x9f) return 7;               // fixarray
    if (m <= 0xbf) return 5;               // fixstr
    auto ext_type = [&](size_t type_offset) -> uint8_t {
        // A tabular extension is logically an array; every other extension is "other".
        return static_cast<int8_t>(p[type_offset]) == kExtTabular ? 7 : 9;
    };
    switch (m) {
        case 0xc0: return 0;
        case 0xc2: case 0xc3: return 1;
        case 0xc4: case 0xc5: case 0xc6: return 6;
        case 0xca: case 0xcb: return 4;
        case 0xcc: case 0xcd: case 0xce: return 2;
        case 0xcf: return load_be64(p + 1) > static_cast<uint64_t>(INT64_MAX) ? 3 : 2;
        case 0xd0: case 0xd1: case 0xd2: case 0xd3: return 2;
        case 0xd9: case 0xda: case 0xdb: return 5;
        case 0xdc: case 0xdd: return 7;
        case 0xde: case 0xdf: return 8;
        case 0xd4: case 0xd5: case 0xd6: case 0xd7: case 0xd8: return ext_type(1);
        case 0xc7: return ext_type(2);
        case 0xc8: return ext_type(3);
        case 0xc9: return ext_type(5);
        default: return 9;
    }
}

bool tabular_layout(const Array& rows, std::vector<const std::string*>* keys) {
    const size_t n = rows.size();
    if (n < 2) return false;
    const auto* first = std::get_if<Map>(&rows[0]);
    if (!first || first->empty()) return false;
    const size_t c = first->size();
    for (size_t r = 1; r < n; ++r) {
        const auto* row = std::get_if<Map>(&rows[r]);
        if (!row || row->size() != c) return false;
        // Both maps are sorted, so equal key sets iterate identically.
        auto a = first->begin();
        for (auto b = row->begin(); b != row->end(); ++a, ++b) {
            if (a->first != b->first) return false;
        }
    }
    std::vector<size_t> lengths;
    lengths.reserve(c);
    for (const auto& [key, _] : *first) lengths.push_back(key.size());
    if (!tabular_beneficial(n, lengths)) return false;
    if (keys) {
        keys->clear();
        keys->reserve(c);
        for (const auto& [key, _] : *first) keys->push_back(&key);
    }
    return true;
}

} // namespace detail
} // namespace btoon

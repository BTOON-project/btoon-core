// SPDX-FileCopyrightText: 2025 Alvar Laigna <https://alvarlaigna.com>
// SPDX-License-Identifier: MIT
//
// Hardened BTOON decoder (docs/SPEC.md §7). Invariants:
//  * every read goes through Reader::need() — no out-of-bounds access;
//  * containers are depth-limited (max_depth) — no unbounded recursion;
//  * nothing is reserved/allocated beyond what the remaining input can encode.
#include "btoon/decoder.h"
#include "internal.h"

#include <algorithm>
#include <set>
#include <string_view>

namespace btoon {

using namespace detail;

namespace {

struct Reader {
    const uint8_t* origin;  // start of the whole document (for error offsets)
    const uint8_t* p;
    const uint8_t* end;
    const DecodeOptions* opts;
    const Security* types;  // non-null when type restrictions are active
    size_t* values_left;    // the max_values budget, shared by the whole document
    size_t* reserve_left;   // the pre-allocation budget, shared by the whole document

    size_t remaining() const { return static_cast<size_t>(end - p); }

    [[noreturn]] BTOON_NOINLINE void error(ErrorCode code, const char* what) const {
        fail(code, std::string(what) + " (at byte " + std::to_string(p - origin) + ")");
    }
    void need(size_t n) const {
        if (BTOON_UNLIKELY(remaining() < n)) error(ErrorCode::truncated, "unexpected end of input");
    }
    uint8_t u8() {
        need(1);
        return *p++;
    }
    uint16_t u16() {
        need(2);
        uint16_t v = load_be16(p);
        p += 2;
        return v;
    }
    uint32_t u32() {
        need(4);
        uint32_t v = load_be32(p);
        p += 4;
        return v;
    }
    uint64_t u64() {
        need(8);
        uint64_t v = load_be64(p);
        p += 8;
        return v;
    }
};

Value read_value(Reader& r, size_t depth);

inline Value uint_value(uint64_t v) {
    return v <= static_cast<uint64_t>(INT64_MAX) ? Value(Int(static_cast<int64_t>(v))) : Value(Uint(v));
}

String read_str(Reader& r, size_t n) {
    r.need(n);
    if (r.opts->strict && !is_valid_utf8(r.p, n)) r.error(ErrorCode::invalid_utf8, "string is not valid UTF-8");
    String s(reinterpret_cast<const char*>(r.p), n);
    r.p += n;
    return s;
}

Binary read_bin(Reader& r, size_t n) {
    r.need(n);
    Binary b(r.p, r.p + n);
    r.p += n;
    return b;
}

// Charges `n` values against max_values (docs/SPEC.md §7.1).
void count_values(Reader& r, size_t n) {
    if (BTOON_UNLIKELY(n > *r.values_left)) {
        r.error(ErrorCode::size_limit, "document decodes to more values than max_values");
    }
    *r.values_left -= n;
}

String read_key(Reader& r) {
    uint8_t m = r.u8();
    size_t n;
    if (m >= 0xa0 && m <= 0xbf) {
        n = m & 0x1fu;
    } else if (m == 0xd9) {
        n = r.u8();
    } else if (m == 0xda) {
        n = r.u16();
    } else if (m == 0xdb) {
        n = r.u32();
    } else {
        --r.p;
        r.error(ErrorCode::non_string_key, "map key is not a string");
    }
    // Keys must be UTF-8 in non-strict mode too: they're compared by their bytes, and a
    // decoder that replaced invalid sequences could merge distinct keys (§7).
    r.need(n);
    if (!is_valid_utf8(r.p, n)) r.error(ErrorCode::invalid_utf8, "map key is not valid UTF-8");
    String s(reinterpret_cast<const char*>(r.p), n);
    r.p += n;
    return s;
}

// Pre-allocation. A length check against the remaining input isn't enough on its own:
// nested containers each check against the same remaining bytes, so 127 nested array32
// headers could reserve 40 GiB. So every reservation draws on one budget for the whole
// document, equal to its size in bytes. Every element of every container owns at least
// one byte (an array element or tabular cell its first byte, a map entry its key, a
// column name its length), so a valid document never exhausts the budget and always
// reserves exactly. A container the budget doesn't cover starts empty and grows as its
// elements decode; only input that is going to fail gets there, and its reservations
// stay within the input's size at any depth. A container that announces more elements
// than the max_values budget has left can't be decoded either (each element counts), so
// it doesn't reserve: otherwise a 64 MiB array32 of nils, a 64 KB zlib envelope, could
// reserve 64M Values (2.5 GB) before the budget stops it.
size_t reservation(Reader& r, size_t n) {
    if (n > *r.reserve_left || n > *r.values_left) return 0;
    *r.reserve_left -= n;
    return n;
}

Array read_array(Reader& r, size_t n, size_t depth) {
    if (depth >= r.opts->max_depth) r.error(ErrorCode::max_depth, "maximum nesting depth exceeded");
    if (n > r.remaining()) r.error(ErrorCode::truncated, "array length exceeds remaining input");
    Array a;
    a.reserve(reservation(r, n));
    for (size_t i = 0; i < n; ++i) a.push_back(read_value(r, depth + 1));
    return a;
}

Map read_map(Reader& r, size_t n, size_t depth) {
    if (depth >= r.opts->max_depth) r.error(ErrorCode::max_depth, "maximum nesting depth exceeded");
    if (n > r.remaining() / 2) r.error(ErrorCode::truncated, "map length exceeds remaining input");
    Map m;
    for (size_t i = 0; i < n; ++i) {
        String key = read_key(r);
        Value v = read_value(r, depth + 1);
        size_t before = m.size();
        // Canonical input arrives sorted, making the end() hint O(1).
        auto it = m.try_emplace(m.end(), std::move(key), std::move(v));
        if (m.size() == before) {
            if (r.opts->strict) r.error(ErrorCode::duplicate_key, "duplicate map key");
            it->second = std::move(v);  // non-strict: last wins (v untouched by try_emplace)
        }
    }
    return m;
}

Value read_tabular(Reader& t, size_t depth) {
    // `t` spans exactly the extension payload.
    if (depth + 1 >= t.opts->max_depth) t.error(ErrorCode::max_depth, "maximum nesting depth exceeded");
    auto need = [&](size_t n) {
        if (t.remaining() < n) t.error(ErrorCode::invalid_tabular, "tabular payload truncated");
    };
    need(12);
    const uint32_t version = load_be32(t.p);
    const uint32_t num_cols = load_be32(t.p + 4);
    const uint32_t num_rows = load_be32(t.p + 8);
    t.p += 12;
    if (version != kTabularVersion) t.error(ErrorCode::invalid_tabular, "unsupported tabular version");
    if (num_cols == 0 && num_rows > 0) t.error(ErrorCode::invalid_tabular, "tabular rows without columns");
    // Each column needs ≥ 9 bytes (name length, type code, column length).
    if (num_cols > t.remaining() / 9) t.error(ErrorCode::invalid_tabular, "tabular column count exceeds payload");

    // Names are checked front to back (§5.6.3): each must be UTF-8, in non-strict mode
    // too, and differ from every earlier name. The first failing name sets the error.
    // An ordered set keeps this O(C log C) without exposure to hash flooding.
    std::vector<String> names;
    names.reserve(reservation(t, num_cols));
    std::set<std::string_view> seen;
    uint64_t name_bytes = 0;
    for (uint32_t i = 0; i < num_cols; ++i) {
        need(4);
        uint32_t len = load_be32(t.p);
        t.p += 4;
        need(len);
        const std::string_view name(reinterpret_cast<const char*>(t.p), len);
        if (!is_valid_utf8(t.p, len)) t.error(ErrorCode::invalid_utf8, "column name is not valid UTF-8");
        if (!seen.insert(name).second) t.error(ErrorCode::invalid_tabular, "duplicate column name");
        names.emplace_back(name);
        name_bytes += len;
        t.p += len;
    }
    need(num_cols);
    t.p += num_cols;  // type codes are informational (§5.6.1)

    // Every cell is ≥ 1 byte, so N × C cells must fit in what's left.
    if (uint64_t(num_rows) * uint64_t(num_cols) > t.remaining()) {
        t.error(ErrorCode::invalid_tabular, "tabular row count exceeds payload");
    }
    // Each row map gets a copy of every name: bound those bytes like a plain array of maps
    // would be bounded (§7.1). Both factors are below 2^32, so the product can't overflow.
    if (uint64_t(num_rows) * name_bytes > uint64_t(t.opts->max_decompressed_size)) {
        t.error(ErrorCode::size_limit, "tabular column names copied into every row exceed max_decompressed_size");
    }
    // The N row maps count once the header is checked; each cell counts as it starts.
    count_values(t, num_rows);
    // Rows are created while column 0 is read, one per decoded cell, so every row is
    // backed by input already consumed. Creating num_rows maps up front let nested
    // tabulars each allocate against the same bytes (2.6 GiB from a 1 MB input).
    Array rows;
    rows.reserve(reservation(t, num_rows));

    for (uint32_t c = 0; c < num_cols; ++c) {
        need(4);
        uint32_t col_len = load_be32(t.p);
        t.p += 4;
        need(col_len);
        Reader col{t.origin, t.p, t.p + col_len, t.opts, t.types, t.values_left, t.reserve_left};
        try {
            for (uint32_t r = 0; r < num_rows; ++r) {
                Value v = read_value(col, depth + 2);
                if (c == 0) rows.emplace_back(Map{});
                auto& row = std::get<Map>(rows[r]);
                row.emplace_hint(row.end(), names[c], std::move(v));
            }
        } catch (const BtoonException& e) {
            if (e.code() == ErrorCode::truncated) {
                col.error(ErrorCode::invalid_tabular, "tabular column holds fewer values than rows");
            }
            throw;
        }
        if (col.p != col.end) col.error(ErrorCode::invalid_tabular, "tabular column length mismatch");
        t.p += col_len;
    }
    if (t.p != t.end) t.error(ErrorCode::invalid_tabular, "trailing bytes in tabular payload");
    return Value(std::move(rows));
}

Value read_ext(Reader& r, size_t len, size_t depth) {
    const auto type = static_cast<int8_t>(r.u8());
    r.need(len);
    const uint8_t* d = r.p;
    auto bad = [&](const char* what) { r.error(ErrorCode::invalid_extension, what); };
    switch (type) {
        case kExtTimestamp: {
            Timestamp ts;
            switch (len) {
                case 4:
                    ts.seconds = static_cast<int64_t>(load_be32(d));
                    break;
                case 8: {
                    uint64_t v = load_be64(d);
                    ts.nanoseconds = static_cast<uint32_t>(v >> 34);
                    ts.seconds = static_cast<int64_t>(v & 0x3ffffffffULL);
                    break;
                }
                case 12:
                case 14:
                    ts.nanoseconds = load_be32(d);
                    ts.seconds = static_cast<int64_t>(load_be64(d + 4));
                    if (len == 14) {
                        ts.timezone_offset = static_cast<int16_t>(load_be16(d + 12));
                        ts.has_timezone = true;
                        if (ts.timezone_offset < -kMaxTimezoneOffsetMinutes ||
                            ts.timezone_offset > kMaxTimezoneOffsetMinutes) {
                            bad("timestamp offset out of range");
                        }
                    }
                    break;
                default:
                    bad("invalid timestamp length");
            }
            if (ts.nanoseconds >= kNanosPerSecond) bad("timestamp nanoseconds out of range");
            r.p += len;
            return Value(ts);
        }
        case kExtDate:
            if (len != 8) bad("invalid date length");
            r.p += 8;
            return Value(Date{static_cast<int64_t>(load_be64(d))});
        case kExtDateTime:
            if (len != 8) bad("invalid datetime length");
            r.p += 8;
            return Value(DateTime{static_cast<int64_t>(load_be64(d))});
        case kExtBigInt: {
            if (len == 0) bad("empty bigint");
            BigInt b;
            b.bytes.assign(d, d + len);
            r.p += len;
            return Value(std::move(b));
        }
        case kExtVectorFloat: {
            if (len % sizeof(float) != 0) bad("float32 vector length not a multiple of 4");
            VectorFloat v;
            v.data.resize(len / sizeof(float));
            load_le_array(v.data.data(), d, v.data.size());
            r.p += len;
            return Value(std::move(v));
        }
        case kExtVectorDouble: {
            if (len % sizeof(double) != 0) bad("float64 vector length not a multiple of 8");
            VectorDouble v;
            v.data.resize(len / sizeof(double));
            load_le_array(v.data.data(), d, v.data.size());
            r.p += len;
            return Value(std::move(v));
        }
        case kExtTabular: {
            Reader sub{r.origin, d, d + len, r.opts, r.types, r.values_left, r.reserve_left};
            Value v = read_tabular(sub, depth);
            r.p += len;
            return v;
        }
        default: {
            Extension ext;
            ext.type = type;
            ext.data.assign(d, d + len);
            r.p += len;
            return Value(std::move(ext));
        }
    }
}

Value read_value_impl(Reader& r, size_t depth) {
    const uint8_t m = r.u8();
    if (m <= 0x7f) return Value(Int(m));
    if (m >= 0xe0) return Value(Int(static_cast<int8_t>(m)));
    if (m <= 0x8f) return Value(read_map(r, m & 0x0fu, depth));
    if (m <= 0x9f) return Value(read_array(r, m & 0x0fu, depth));
    if (m <= 0xbf) return Value(read_str(r, m & 0x1fu));
    switch (m) {
        case 0xc0: return Value(nullptr);
        case 0xc2: return Value(false);
        case 0xc3: return Value(true);
        case 0xc4: return Value(read_bin(r, r.u8()));
        case 0xc5: return Value(read_bin(r, r.u16()));
        case 0xc6: return Value(read_bin(r, r.u32()));
        case 0xc7: return read_ext(r, r.u8(), depth);
        case 0xc8: return read_ext(r, r.u16(), depth);
        case 0xc9: return read_ext(r, r.u32(), depth);
        case 0xca: return Value(static_cast<double>(std::bit_cast<float>(r.u32())));
        case 0xcb: return Value(std::bit_cast<double>(r.u64()));
        case 0xcc: return Value(Int(r.u8()));
        case 0xcd: return Value(Int(r.u16()));
        case 0xce: return Value(Int(r.u32()));
        case 0xcf: return uint_value(r.u64());
        case 0xd0: return Value(Int(static_cast<int8_t>(r.u8())));
        case 0xd1: return Value(Int(static_cast<int16_t>(r.u16())));
        case 0xd2: return Value(Int(static_cast<int32_t>(r.u32())));
        case 0xd3: return Value(Int(static_cast<int64_t>(r.u64())));
        case 0xd4: return read_ext(r, 1, depth);
        case 0xd5: return read_ext(r, 2, depth);
        case 0xd6: return read_ext(r, 4, depth);
        case 0xd7: return read_ext(r, 8, depth);
        case 0xd8: return read_ext(r, 16, depth);
        case 0xd9: return Value(read_str(r, r.u8()));
        case 0xda: return Value(read_str(r, r.u16()));
        case 0xdb: return Value(read_str(r, r.u32()));
        case 0xdc: return Value(read_array(r, r.u16(), depth));
        case 0xdd: return Value(read_array(r, r.u32(), depth));
        case 0xde: return Value(read_map(r, r.u16(), depth));
        case 0xdf: return Value(read_map(r, r.u32(), depth));
        default:  // 0xc1
            --r.p;
            r.error(ErrorCode::invalid_marker, "reserved marker 0xc1");
    }
}

Value read_value(Reader& r, size_t depth) {
    count_values(r, 1);  // a value counts when it starts, before anything inside it (§7.1)
    if (BTOON_LIKELY(r.types == nullptr)) return read_value_impl(r, depth);
    Value v = read_value_impl(r, depth);
    if (!r.types->isTypeAllowed(v.index())) {
        fail(ErrorCode::invalid_argument, std::string("type not allowed: ") + v.type_name());
    }
    return v;
}

} // namespace

Decoder::Decoder() = default;
Decoder::Decoder(const DecodeOptions& options) : options_(options) {}
Decoder::Decoder(MemoryPool* /*pool*/) {}
Decoder::Decoder(const Security& security) : security_(security), useSecurity_(true) {}
Decoder::Decoder(const Security& security, MemoryPool* /*pool*/) : security_(security), useSecurity_(true) {}
Decoder::~Decoder() = default;

// Options that can't be honored fail before any input is read.
static void check_decode_options(const DecodeOptions& options) {
    if (options.max_depth > kMaxDepthLimit) {
        fail(ErrorCode::invalid_argument, "max_depth is above " + std::to_string(kMaxDepthLimit));
    }
    if (options.hmac_key && options.hmac_key->empty()) {
        fail(ErrorCode::invalid_argument, "the HMAC key is empty; leave hmac_key unset for no key");
    }
}

std::span<const uint8_t> Decoder::verify(std::span<const uint8_t> buffer, size_t* header_len) const {
    check_decode_options(options_);
    *header_len = 0;
    if (useSecurity_) {
        *header_len = Security::kEnvelopeHeaderSize;
        return security_->unwrap(buffer);
    }
    if (options_.hmac_key) {
        *header_len = Security::kEnvelopeHeaderSize;
        return Security(*options_.hmac_key).unwrap(buffer);
    }
    return buffer;
}

std::pair<Value, size_t> Decoder::read(std::span<const uint8_t> data, bool whole) const {
    const Security* types = (security_ && security_->restrictsTypes()) ? &*security_ : nullptr;
    size_t values_left = options_.max_values;
    size_t reserve_left = data.size();
    Reader r{data.data(), data.data(), data.data() + data.size(), &options_, types, &values_left, &reserve_left};
    Value v = read_value(r, 0);
    if (whole && r.p != r.end && options_.strict) r.error(ErrorCode::trailing_data, "trailing bytes after value");
    return {std::move(v), static_cast<size_t>(r.p - data.data())};
}

Value Decoder::decode(std::span<const uint8_t> buffer) const {
    size_t header = 0;
    std::span<const uint8_t> bytes = verify(buffer, &header);
    if (header == 0 && Security::is_signed_envelope(bytes) && bytes.size() >= Security::kEnvelopeHeaderSize) {
        fail(ErrorCode::authentication_failed, "input is signed but no HMAC key was provided");
    }
    std::vector<uint8_t> decompressed;
    bytes = open_compression_envelope(bytes, options_, decompressed);
    return read(bytes, true).first;
}

std::pair<Value, size_t> Decoder::decode_and_get_pos(std::span<const uint8_t> buffer) const {
    size_t header = 0;
    auto [value, used] = read(verify(buffer, &header), false);
    return {std::move(value), header + used};
}

} // namespace btoon

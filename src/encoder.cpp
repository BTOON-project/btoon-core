// SPDX-FileCopyrightText: 2025 Alvar Laigna <https://alvarlaigna.com>
// SPDX-License-Identifier: MIT
//
// Canonical BTOON encoder (docs/SPEC.md §4–§5). Everything is appended to one
// growing buffer; tabular payloads and column lengths are back-patched.
#include "btoon/encoder.h"
#include "internal.h"

#include <cmath>
#include <limits>

namespace btoon {

using namespace detail;

Encoder::Encoder() = default;
Encoder::Encoder(const EncodeOptions& options) : options_(options) {}
Encoder::Encoder(MemoryPool* /*pool*/) {}
Encoder::Encoder(const Security& security) : security_(security), useSecurity_(true) {}
Encoder::Encoder(const Security& security, MemoryPool* /*pool*/) : security_(security), useSecurity_(true) {}
Encoder::~Encoder() = default;

void Encoder::grow(size_t n) {
    size_t need = len_ + n;
    if (need < len_) fail(ErrorCode::size_limit, "encoder buffer overflow");
    size_t cap = std::max<size_t>({need, buf_.size() * 2, 256});
    buf_.resize(cap);
}

void Encoder::putBytes(const void* data, size_t n) {
    if (n == 0) return;
    std::memcpy(reserve(n), data, n);
    len_ += n;
}

void Encoder::clear() noexcept {
    len_ = 0;
    finalized_ = false;
    tabular_.clear();
}

// --- Primitives --------------------------------------------------------------

void Encoder::encodeNil() { put8(0xc0); }

void Encoder::encodeBool(bool value) { put8(value ? 0xc3 : 0xc2); }

void Encoder::encodeUint(uint64_t v) {
    uint8_t* p = reserve(9);
    if (v <= 0x7f) {
        p[0] = static_cast<uint8_t>(v);
        len_ += 1;
    } else if (v <= 0xff) {
        p[0] = 0xcc;
        p[1] = static_cast<uint8_t>(v);
        len_ += 2;
    } else if (v <= 0xffff) {
        p[0] = 0xcd;
        store_be16(p + 1, static_cast<uint16_t>(v));
        len_ += 3;
    } else if (v <= 0xffffffffULL) {
        p[0] = 0xce;
        store_be32(p + 1, static_cast<uint32_t>(v));
        len_ += 5;
    } else {
        p[0] = 0xcf;
        store_be64(p + 1, v);
        len_ += 9;
    }
}

void Encoder::encodeInt(int64_t v) {
    if (v >= 0) {
        encodeUint(static_cast<uint64_t>(v));
        return;
    }
    uint8_t* p = reserve(9);
    if (v >= -32) {
        p[0] = static_cast<uint8_t>(v);
        len_ += 1;
    } else if (v >= INT8_MIN) {
        p[0] = 0xd0;
        p[1] = static_cast<uint8_t>(v);
        len_ += 2;
    } else if (v >= INT16_MIN) {
        p[0] = 0xd1;
        store_be16(p + 1, static_cast<uint16_t>(v));
        len_ += 3;
    } else if (v >= INT32_MIN) {
        p[0] = 0xd2;
        store_be32(p + 1, static_cast<uint32_t>(v));
        len_ += 5;
    } else {
        p[0] = 0xd3;
        store_be64(p + 1, static_cast<uint64_t>(v));
        len_ += 9;
    }
}

void Encoder::encodeFloat(double value) {
    uint64_t bits = std::isnan(value) ? 0x7ff8000000000000ULL : std::bit_cast<uint64_t>(value);
    uint8_t* p = reserve(9);
    p[0] = 0xcb;
    store_be64(p + 1, bits);
    len_ += 9;
}

void Encoder::encodeString(std::string_view value) {
    size_t n = value.size();
    if (options_.validate_utf8 && !is_valid_utf8(value)) {
        fail(ErrorCode::invalid_utf8, "string is not valid UTF-8");
    }
    uint8_t* p = reserve(5 + n);
    if (n <= 31) {
        p[0] = static_cast<uint8_t>(0xa0 | n);
        p += 1;
    } else if (n <= 0xff) {
        p[0] = 0xd9;
        p[1] = static_cast<uint8_t>(n);
        p += 2;
    } else if (n <= 0xffff) {
        p[0] = 0xda;
        store_be16(p + 1, static_cast<uint16_t>(n));
        p += 3;
    } else if (n <= 0xffffffffULL) {
        p[0] = 0xdb;
        store_be32(p + 1, static_cast<uint32_t>(n));
        p += 5;
    } else {
        fail(ErrorCode::size_limit, "string longer than 2^32-1 bytes");
    }
    if (n) std::memcpy(p, value.data(), n);
    len_ = static_cast<size_t>(p - buf_.data()) + n;
}

void Encoder::encodeBinary(std::span<const uint8_t> value) {
    size_t n = value.size();
    uint8_t* p = reserve(5 + n);
    if (n <= 0xff) {
        p[0] = 0xc4;
        p[1] = static_cast<uint8_t>(n);
        p += 2;
    } else if (n <= 0xffff) {
        p[0] = 0xc5;
        store_be16(p + 1, static_cast<uint16_t>(n));
        p += 3;
    } else if (n <= 0xffffffffULL) {
        p[0] = 0xc6;
        store_be32(p + 1, static_cast<uint32_t>(n));
        p += 5;
    } else {
        fail(ErrorCode::size_limit, "binary longer than 2^32-1 bytes");
    }
    if (n) std::memcpy(p, value.data(), n);
    len_ = static_cast<size_t>(p - buf_.data()) + n;
}

void Encoder::encodeArrayHeader(size_t n) {
    uint8_t* p = reserve(5);
    if (n <= 15) {
        p[0] = static_cast<uint8_t>(0x90 | n);
        len_ += 1;
    } else if (n <= 0xffff) {
        p[0] = 0xdc;
        store_be16(p + 1, static_cast<uint16_t>(n));
        len_ += 3;
    } else if (n <= 0xffffffffULL) {
        p[0] = 0xdd;
        store_be32(p + 1, static_cast<uint32_t>(n));
        len_ += 5;
    } else {
        fail(ErrorCode::size_limit, "array longer than 2^32-1 elements");
    }
}

void Encoder::encodeMapHeader(size_t n) {
    uint8_t* p = reserve(5);
    if (n <= 15) {
        p[0] = static_cast<uint8_t>(0x80 | n);
        len_ += 1;
    } else if (n <= 0xffff) {
        p[0] = 0xde;
        store_be16(p + 1, static_cast<uint16_t>(n));
        len_ += 3;
    } else if (n <= 0xffffffffULL) {
        p[0] = 0xdf;
        store_be32(p + 1, static_cast<uint32_t>(n));
        len_ += 5;
    } else {
        fail(ErrorCode::size_limit, "map larger than 2^32-1 entries");
    }
}

namespace {
// docs/SPEC.md §5.7: callers can't write the ids BTOON defines as raw extensions, because
// a verbatim payload could be malformed or non-canonical. Typed values write them.
void check_raw_extension_type(int8_t type) {
    switch (type) {
        case -1: case -2: case -3: case -4: case -5: case -6: case -10:
            fail(ErrorCode::invalid_argument,
                 "extension type " + std::to_string(type) +
                     " is defined by BTOON; encode the typed value instead (docs/SPEC.md §5.7)");
        default: return;
    }
}
}  // namespace

void Encoder::encodeExtensionHeader(int8_t type, size_t n) {
    check_raw_extension_type(type);
    writeExtensionHeader(type, n);
}

void Encoder::writeExtensionHeader(int8_t type, size_t n) {
    uint8_t* p = reserve(6);
    const auto t = static_cast<uint8_t>(type);
    switch (n) {
        case 1: p[0] = 0xd4; p[1] = t; len_ += 2; return;
        case 2: p[0] = 0xd5; p[1] = t; len_ += 2; return;
        case 4: p[0] = 0xd6; p[1] = t; len_ += 2; return;
        case 8: p[0] = 0xd7; p[1] = t; len_ += 2; return;
        case 16: p[0] = 0xd8; p[1] = t; len_ += 2; return;
        default: break;
    }
    if (n <= 0xff) {
        p[0] = 0xc7;
        p[1] = static_cast<uint8_t>(n);
        p[2] = t;
        len_ += 3;
    } else if (n <= 0xffff) {
        p[0] = 0xc8;
        store_be16(p + 1, static_cast<uint16_t>(n));
        p[3] = t;
        len_ += 4;
    } else if (n <= 0xffffffffULL) {
        p[0] = 0xc9;
        store_be32(p + 1, static_cast<uint32_t>(n));
        p[5] = t;
        len_ += 6;
    } else {
        fail(ErrorCode::size_limit, "extension payload longer than 2^32-1 bytes");
    }
}

void Encoder::encodeExtension(int8_t type, std::span<const uint8_t> data) {
    check_raw_extension_type(type);
    writeExtensionHeader(type, data.size());
    putBytes(data.data(), data.size());
}

void Encoder::encodeRaw(std::span<const uint8_t> bytes) { putBytes(bytes.data(), bytes.size()); }

// --- BTOON extension types -----------------------------------------------------

void Encoder::encodeTimestamp(const Timestamp& ts) {
    if (ts.nanoseconds >= kNanosPerSecond) {
        fail(ErrorCode::invalid_argument, "timestamp nanoseconds must be < 1e9");
    }
    uint8_t* p = reserve(17);
    if (ts.has_timezone) {
        if (ts.timezone_offset < -kMaxTimezoneOffsetMinutes ||
            ts.timezone_offset > kMaxTimezoneOffsetMinutes) {
            fail(ErrorCode::invalid_argument, "timezone offset must be within ±1080 minutes");
        }
        p[0] = 0xc7;
        p[1] = 14;
        p[2] = static_cast<uint8_t>(kExtTimestamp);
        store_be32(p + 3, ts.nanoseconds);
        store_be64(p + 7, static_cast<uint64_t>(ts.seconds));
        store_be16(p + 15, static_cast<uint16_t>(ts.timezone_offset));
        len_ += 17;
    } else if ((ts.seconds >> 34) == 0) {
        uint64_t data64 = (uint64_t(ts.nanoseconds) << 34) | static_cast<uint64_t>(ts.seconds);
        if ((data64 >> 32) == 0) {
            p[0] = 0xd6;
            p[1] = static_cast<uint8_t>(kExtTimestamp);
            store_be32(p + 2, static_cast<uint32_t>(data64));
            len_ += 6;
        } else {
            p[0] = 0xd7;
            p[1] = static_cast<uint8_t>(kExtTimestamp);
            store_be64(p + 2, data64);
            len_ += 10;
        }
    } else {
        p[0] = 0xc7;
        p[1] = 12;
        p[2] = static_cast<uint8_t>(kExtTimestamp);
        store_be32(p + 3, ts.nanoseconds);
        store_be64(p + 7, static_cast<uint64_t>(ts.seconds));
        len_ += 15;
    }
}

void Encoder::encodeDate(int64_t milliseconds) {
    uint8_t* p = reserve(10);
    p[0] = 0xd7;
    p[1] = static_cast<uint8_t>(kExtDate);
    store_be64(p + 2, static_cast<uint64_t>(milliseconds));
    len_ += 10;
}

void Encoder::encodeDateTime(int64_t nanoseconds) {
    uint8_t* p = reserve(10);
    p[0] = 0xd7;
    p[1] = static_cast<uint8_t>(kExtDateTime);
    store_be64(p + 2, static_cast<uint64_t>(nanoseconds));
    len_ += 10;
}

void Encoder::encodeBigInt(std::span<const uint8_t> bytes) {
    if (bytes.empty()) fail(ErrorCode::invalid_argument, "bigint must have at least one byte");
    auto fit = bigint_fit64(bytes);
    if (fit.kind == Int64Fit::kSigned) {
        encodeInt(fit.i);
    } else if (fit.kind == Int64Fit::kUnsigned) {
        encodeUint(fit.u);
    } else {
        const auto minimal = bigint_minimal(bytes);
        writeExtensionHeader(kExtBigInt, minimal.size());
        putBytes(minimal.data(), minimal.size());
    }
}

void Encoder::encodeVectorFloat(std::span<const float> values) {
    size_t n = values.size() * sizeof(float);
    writeExtensionHeader(kExtVectorFloat, n);
    store_le_array(reserve(n), values.data(), values.size());
    len_ += n;
}

void Encoder::encodeVectorDouble(std::span<const double> values) {
    size_t n = values.size() * sizeof(double);
    writeExtensionHeader(kExtVectorDouble, n);
    store_le_array(reserve(n), values.data(), values.size());
    len_ += n;
}

void Encoder::encodeVectorFloat(const VectorFloat& value) { encodeVectorFloat(std::span<const float>(value.data)); }

void Encoder::encodeVectorDouble(const VectorDouble& value) { encodeVectorDouble(std::span<const double>(value.data)); }

// --- Containers ----------------------------------------------------------------

void Encoder::encodeArray(const std::vector<std::vector<uint8_t>>& elements) {
    encodeArrayHeader(elements.size());
    for (const auto& e : elements) putBytes(e.data(), e.size());
}

void Encoder::encodeMap(const std::map<std::string, std::vector<uint8_t>>& pairs) {
    encodeMapHeader(pairs.size());
    for (const auto& [key, encoded] : pairs) {
        encodeString(key);
        putBytes(encoded.data(), encoded.size());
    }
}

void Encoder::encodeColumnar(const Array& rows) {
    if (!tryEncodeTabular(rows, 0)) {
        encodeArrayHeader(rows.size());
        for (const auto& v : rows) encodeValue(v, 1);
    }
}

void Encoder::encode(const Value& value) { encodeValue(value, 0); }

void Encoder::encodeValue(const Value& value, size_t depth) {
    switch (value.index()) {
        case 0: encodeNil(); return;
        case 1: encodeBool(std::get<Bool>(value)); return;
        case 2: encodeInt(std::get<Int>(value)); return;
        case 3: encodeUint(std::get<Uint>(value)); return;
        case 4: encodeFloat(std::get<Float>(value)); return;
        case 5: encodeString(std::get<String>(value)); return;
        case 6: encodeBinary(std::get<Binary>(value)); return;
        case 7: encodeArrayValue(std::get<Array>(value), depth); return;
        case 8: encodeMapValue(std::get<Map>(value), depth); return;
        case 9: {
            const auto& ext = std::get<Extension>(value);
            encodeExtension(ext.type, ext.data);
            return;
        }
        case 10: encodeTimestamp(std::get<Timestamp>(value)); return;
        case 11: encodeDate(std::get<Date>(value).milliseconds); return;
        case 12: encodeDateTime(std::get<DateTime>(value).nanoseconds); return;
        case 13: encodeBigInt(std::get<BigInt>(value).bytes); return;
        case 14: encodeVectorFloat(std::get<VectorFloat>(value)); return;
        case 15: encodeVectorDouble(std::get<VectorDouble>(value)); return;
        default: fail(ErrorCode::invalid_argument, "unsupported value type");
    }
}

void Encoder::encodeArrayValue(const Array& array, size_t depth) {
    if (depth >= options_.max_depth) fail(ErrorCode::max_depth, "maximum nesting depth exceeded");
    if (options_.auto_tabular && tryEncodeTabular(array, depth)) return;
    encodeArrayHeader(array.size());
    for (const auto& v : array) encodeValue(v, depth + 1);
}

void Encoder::encodeMapValue(const Map& map, size_t depth) {
    if (depth >= options_.max_depth) fail(ErrorCode::max_depth, "maximum nesting depth exceeded");
    encodeMapHeader(map.size());
    for (const auto& [key, v] : map) {  // std::map iterates in UTF-8 byte order
        encodeString(key);
        encodeValue(v, depth + 1);
    }
}

bool Encoder::tryEncodeTabular(const Array& rows, size_t depth) {
    std::vector<const std::string*> keys;
    if (!tabular_layout(rows, &keys)) return false;
    if (depth + 1 >= options_.max_depth) fail(ErrorCode::max_depth, "maximum nesting depth exceeded");

    std::vector<std::string_view> names;
    names.reserve(keys.size());
    for (const std::string* k : keys) names.emplace_back(*k);

    beginTabular(names, rows.size());
    // Column-major: advance one iterator per row in lockstep (all rows share sorted keys).
    std::vector<Map::const_iterator> cursors;
    cursors.reserve(rows.size());
    for (const auto& row : rows) cursors.push_back(std::get<Map>(row).begin());
    for (size_t col = 0; col < names.size(); ++col) {
        beginTabularColumn();
        for (auto& it : cursors) {
            encodeValue(it->second, depth + 2);
            ++it;
        }
        endTabularColumn();
    }
    endTabular();
    return true;
}

void Encoder::beginTabular(std::span<const std::string_view> names, size_t n) {
    const size_t c = names.size();
    if (c == 0) fail(ErrorCode::invalid_argument, "beginTabular: need at least one column");
    if (n > 0xffffffffULL || c > 0xffffffffULL) fail(ErrorCode::size_limit, "tabular section too large");

    TabularFrame frame{};
    // Reserve the largest ext header (ext32 = 6 bytes); endTabular() shrinks it.
    frame.header_pos = len_;
    reserve(6);
    len_ += 6;
    frame.payload_pos = len_;
    uint8_t* p = reserve(12);
    store_be32(p, kTabularVersion);
    store_be32(p + 4, static_cast<uint32_t>(c));
    store_be32(p + 8, static_cast<uint32_t>(n));
    len_ += 12;
    for (std::string_view name : names) {
        if (options_.validate_utf8 && !is_valid_utf8(name)) fail(ErrorCode::invalid_utf8, "map key is not valid UTF-8");
        if (name.size() > 0xffffffffULL) fail(ErrorCode::size_limit, "column name too long");
        store_be32(reserve(4), static_cast<uint32_t>(name.size()));
        len_ += 4;
        putBytes(name.data(), name.size());
    }
    frame.codes_pos = len_;
    std::memset(reserve(c), 0, c);  // patched by endTabularColumn()
    len_ += c;
    frame.column_len_pos = SIZE_MAX;
    frame.columns = c;
    frame.columns_left = c;
    tabular_.push_back(frame);
}

void Encoder::beginTabularColumn() {
    if (tabular_.empty() || tabular_.back().columns_left == 0 || tabular_.back().column_len_pos != SIZE_MAX) {
        fail(ErrorCode::invalid_argument, "beginTabularColumn: no tabular section or column available");
    }
    tabular_.back().column_len_pos = len_;
    reserve(4);
    len_ += 4;
}

void Encoder::endTabularColumn() {
    if (tabular_.empty() || tabular_.back().column_len_pos == SIZE_MAX) {
        fail(ErrorCode::invalid_argument, "endTabularColumn: no open column");
    }
    TabularFrame& f = tabular_.back();
    const size_t col_len = len_ - f.column_len_pos - 4;
    if (col_len > 0xffffffffULL) fail(ErrorCode::size_limit, "tabular column too large");
    store_be32(buf_.data() + f.column_len_pos, static_cast<uint32_t>(col_len));
    if (col_len > 0) {
        const size_t column = f.columns - f.columns_left;
        buf_[f.codes_pos + column] = tabular_type_code_of_encoding(buf_.data() + f.column_len_pos + 4);
    }
    f.column_len_pos = SIZE_MAX;
    --f.columns_left;
}

void Encoder::endTabular() {
    if (tabular_.empty() || tabular_.back().columns_left != 0 || tabular_.back().column_len_pos != SIZE_MAX) {
        fail(ErrorCode::invalid_argument, "endTabular: columns are missing or still open");
    }
    const TabularFrame f = tabular_.back();
    tabular_.pop_back();
    const size_t payload_len = len_ - f.payload_pos;
    if (payload_len > 0xffffffffULL) fail(ErrorCode::size_limit, "tabular payload too large");
    uint8_t* h = buf_.data() + f.header_pos;
    size_t header_len;
    if (payload_len <= 0xff) {
        h[0] = 0xc7;
        h[1] = static_cast<uint8_t>(payload_len);
        h[2] = static_cast<uint8_t>(kExtTabular);
        header_len = 3;
    } else if (payload_len <= 0xffff) {
        h[0] = 0xc8;
        store_be16(h + 1, static_cast<uint16_t>(payload_len));
        h[3] = static_cast<uint8_t>(kExtTabular);
        header_len = 4;
    } else {
        h[0] = 0xc9;
        store_be32(h + 1, static_cast<uint32_t>(payload_len));
        h[5] = static_cast<uint8_t>(kExtTabular);
        header_len = 6;
    }
    if (header_len < 6) {
        std::memmove(h + header_len, buf_.data() + f.payload_pos, payload_len);
        len_ -= 6 - header_len;
    }
}

// --- Output --------------------------------------------------------------------

void Encoder::finalize() {
    if (finalized_) return;
    if (!tabular_.empty()) fail(ErrorCode::invalid_argument, "unterminated tabular section");
    finalized_ = true;
    if (useSecurity_) {
        auto envelope = security_->wrap({buf_.data(), len_});
        buf_ = std::move(envelope);
        len_ = buf_.size();
    }
}

std::span<const uint8_t> Encoder::getBuffer() {
    finalize();
    return {buf_.data(), len_};
}

std::vector<uint8_t> Encoder::release() {
    finalize();
    buf_.resize(len_);
    std::vector<uint8_t> out = std::move(buf_);
    buf_.clear();
    len_ = 0;
    finalized_ = false;
    return out;
}

} // namespace btoon

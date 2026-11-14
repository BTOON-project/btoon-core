// SPDX-FileCopyrightText: 2025 Alvar Laigna <https://alvarlaigna.com>
// SPDX-License-Identifier: MIT
//
// The C API (include/btoon/capi.h). Every entry point catches: no C++ exception crosses
// into C. Values are converted to btoon::Value for encoding and back after decoding;
// both directions are exception-safe (a failure frees whatever was built).
#include "btoon/capi.h"
#include "btoon/btoon.h"
#include "internal.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <type_traits>
#include <vector>

namespace {

using btoon::BtoonException;
using btoon::ErrorCode;
using btoon::detail::fail;

struct ValueDeleter {
    void operator()(btoon_value_t* v) const noexcept { btoon_value_destroy(v); }
};
using CValue = std::unique_ptr<btoon_value_t, ValueDeleter>;

// A value of `type` with an all-zero payload, which btoon_value_destroy() can free at any
// point while it's being filled in.
CValue make_value(btoon_type_t type) {
    auto* v = new btoon_value_t{};
    v->type = type;
    return CValue(v);
}

// The integer in an enum field that C code filled in. C allows any int there, but in C++
// a value outside the enumerators' range is undefined behavior to load as the enum, so
// the bytes are read as the underlying integer type.
template <class E>
int raw_enum(const E& e) noexcept {
    std::underlying_type_t<E> v;
    static_assert(sizeof v == sizeof e);
    std::memcpy(&v, &e, sizeof v);
    return static_cast<int>(v);
}

// A heap copy of `n` elements, or NULL when n is 0 (the documented convention).
template <class T>
T* copy_array(const T* src, size_t n) {
    if (n == 0) return nullptr;
    T* out = new T[n];
    std::memcpy(out, src, n * sizeof(T));
    return out;
}

// A NUL-terminated copy (also for empty strings, so ptr is never NULL).
char* copy_string(const char* src, size_t n) {
    char* out = new char[n + 1];
    if (n) std::memcpy(out, src, n);
    out[n] = '\0';
    return out;
}

// --- btoon::Value -> btoon_value_t ---------------------------------------------------------

CValue to_c_value(const btoon::Value& value);

CValue to_c_value(const btoon::Value& value) {
    using namespace btoon;
    switch (value.index()) {
        case 0: return make_value(BTOON_TYPE_NIL);
        case 1: {
            auto v = make_value(BTOON_TYPE_BOOL);
            v->as.b = std::get<Bool>(value);
            return v;
        }
        case 2: {
            auto v = make_value(BTOON_TYPE_INT);
            v->as.i = std::get<Int>(value);
            return v;
        }
        case 3: {
            auto v = make_value(BTOON_TYPE_UINT);
            v->as.u = std::get<Uint>(value);
            return v;
        }
        case 4: {
            auto v = make_value(BTOON_TYPE_FLOAT);
            v->as.f = std::get<Float>(value);
            return v;
        }
        case 5: {
            const auto& s = std::get<String>(value);
            auto v = make_value(BTOON_TYPE_STRING);
            v->as.s.ptr = copy_string(s.data(), s.size());
            v->as.s.len = s.size();
            return v;
        }
        case 6: {
            const auto& b = std::get<Binary>(value);
            auto v = make_value(BTOON_TYPE_BINARY);
            v->as.bin.ptr = copy_array(b.data(), b.size());
            v->as.bin.len = b.size();
            return v;
        }
        case 7: {
            const auto& a = std::get<Array>(value);
            auto v = make_value(BTOON_TYPE_ARRAY);
            if (!a.empty()) {
                v->as.a.elements = new btoon_value_t*[a.size()]();
                v->as.a.len = a.size();  // NULL slots are fine for btoon_value_destroy()
                for (size_t i = 0; i < a.size(); ++i) v->as.a.elements[i] = to_c_value(a[i]).release();
            }
            return v;
        }
        case 8: {
            const auto& m = std::get<Map>(value);
            auto v = make_value(BTOON_TYPE_MAP);
            if (!m.empty()) {
                v->as.m.keys = new btoon_string_t[m.size()]();
                v->as.m.values = new btoon_value_t*[m.size()]();
                v->as.m.len = m.size();
                size_t i = 0;
                for (const auto& [key, val] : m) {
                    v->as.m.keys[i] = {copy_string(key.data(), key.size()), key.size()};
                    v->as.m.values[i] = to_c_value(val).release();
                    ++i;
                }
            }
            return v;
        }
        case 9: {
            const auto& e = std::get<Extension>(value);
            auto v = make_value(BTOON_TYPE_EXTENSION);
            v->as.ext.type = e.type;
            v->as.ext.ptr = copy_array(e.data.data(), e.data.size());
            v->as.ext.len = e.data.size();
            return v;
        }
        case 10: {
            const auto& t = std::get<Timestamp>(value);
            auto v = make_value(BTOON_TYPE_TIMESTAMP);
            v->as.ts = {t.seconds, t.nanoseconds, t.has_timezone ? t.timezone_offset : int16_t(0), t.has_timezone};
            return v;
        }
        case 11: {
            auto v = make_value(BTOON_TYPE_DATE);
            v->as.date.milliseconds = std::get<Date>(value).milliseconds;
            return v;
        }
        case 12: {
            auto v = make_value(BTOON_TYPE_DATETIME);
            v->as.dt.nanoseconds = std::get<DateTime>(value).nanoseconds;
            return v;
        }
        case 13: {
            const auto& b = std::get<BigInt>(value).bytes;
            auto v = make_value(BTOON_TYPE_BIGINT);
            v->as.bi.ptr = copy_array(b.data(), b.size());
            v->as.bi.len = b.size();
            return v;
        }
        case 14: {
            const auto& d = std::get<VectorFloat>(value).data;
            auto v = make_value(BTOON_TYPE_VECTOR_FLOAT);
            v->as.vf.ptr = copy_array(d.data(), d.size());
            v->as.vf.len = d.size();
            return v;
        }
        case 15: {
            const auto& d = std::get<VectorDouble>(value).data;
            auto v = make_value(BTOON_TYPE_VECTOR_DOUBLE);
            v->as.vd.ptr = copy_array(d.data(), d.size());
            v->as.vd.len = d.size();
            return v;
        }
        default:
            fail(ErrorCode::invalid_argument, "unsupported value type");
    }
}

// --- btoon_value_t -> btoon::Value ---------------------------------------------------------

// A buffer described by (ptr, len); a NULL pointer is only valid for an empty buffer. A
// length that runs past the end of the address space can't describe real memory, and
// forming its end pointer would itself be undefined, so it's rejected first.
template <class T>
std::span<const T> buffer(const T* ptr, size_t len, const char* what) {
    if (!ptr && len) fail(ErrorCode::invalid_argument, std::string(what) + " is NULL but its length isn't 0");
    if (len > static_cast<size_t>(PTRDIFF_MAX) / sizeof(T) ||
        reinterpret_cast<uintptr_t>(ptr) > UINTPTR_MAX - len * sizeof(T)) {
        fail(ErrorCode::invalid_argument, std::string(what) + " length is impossibly large");
    }
    return {ptr, len};
}

btoon::Value to_cpp_value(const btoon_value_t* c, size_t depth) {
    using namespace btoon;
    if (!c) fail(ErrorCode::invalid_argument, "value is NULL");
    // Hand-built trees can be arbitrarily deep, or cyclic: stop before the stack does.
    if (depth > kMaxDepthLimit) fail(ErrorCode::max_depth, "value nests more than 1000 levels");
    const int type = raw_enum(c->type);
    switch (type) {
        case BTOON_TYPE_NIL: return Value(nullptr);
        case BTOON_TYPE_BOOL: return Value(c->as.b);
        case BTOON_TYPE_INT: return Value(Int(c->as.i));
        case BTOON_TYPE_UINT: return Value(Uint(c->as.u));
        case BTOON_TYPE_FLOAT: return Value(Float(c->as.f));
        case BTOON_TYPE_STRING: {
            auto s = buffer(c->as.s.ptr, c->as.s.len, "string");
            return Value(String(s.begin(), s.end()));
        }
        case BTOON_TYPE_BINARY: {
            auto b = buffer(c->as.bin.ptr, c->as.bin.len, "binary");
            return Value(Binary(b.begin(), b.end()));
        }
        case BTOON_TYPE_ARRAY: {
            auto elements = buffer(c->as.a.elements, c->as.a.len, "array elements");
            Array a;
            a.reserve(elements.size());
            for (const btoon_value_t* e : elements) a.push_back(to_cpp_value(e, depth + 1));
            return Value(std::move(a));
        }
        case BTOON_TYPE_MAP: {
            auto keys = buffer(c->as.m.keys, c->as.m.len, "map keys");
            auto values = buffer(c->as.m.values, c->as.m.len, "map values");
            Map m;
            for (size_t i = 0; i < keys.size(); ++i) {
                auto key = buffer(keys[i].ptr, keys[i].len, "map key");
                auto [it, inserted] = m.try_emplace(String(key.begin(), key.end()));
                if (!inserted) fail(ErrorCode::invalid_argument, "duplicate map key: " + it->first);
                it->second = to_cpp_value(values[i], depth + 1);
            }
            return Value(std::move(m));
        }
        case BTOON_TYPE_TIMESTAMP: {
            const btoon_timestamp_t& t = c->as.ts;
            return t.has_timezone ? Value(Timestamp(t.seconds, t.nanoseconds, t.timezone_offset))
                                  : Value(Timestamp(t.seconds, t.nanoseconds));
        }
        case BTOON_TYPE_DATE: return Value(Date{c->as.date.milliseconds});
        case BTOON_TYPE_DATETIME: return Value(DateTime{c->as.dt.nanoseconds});
        case BTOON_TYPE_BIGINT: {
            auto b = buffer(c->as.bi.ptr, c->as.bi.len, "bigint");
            return Value(BigInt{std::vector<uint8_t>(b.begin(), b.end())});
        }
        case BTOON_TYPE_VECTOR_FLOAT: {
            auto d = buffer(c->as.vf.ptr, c->as.vf.len, "float vector");
            return Value(VectorFloat{std::vector<float>(d.begin(), d.end())});
        }
        case BTOON_TYPE_VECTOR_DOUBLE: {
            auto d = buffer(c->as.vd.ptr, c->as.vd.len, "double vector");
            return Value(VectorDouble{std::vector<double>(d.begin(), d.end())});
        }
        case BTOON_TYPE_EXTENSION: {
            auto d = buffer(c->as.ext.ptr, c->as.ext.len, "extension data");
            return Value(Extension{c->as.ext.type, std::vector<uint8_t>(d.begin(), d.end())});
        }
    }
    fail(ErrorCode::invalid_argument, "unknown btoon_type_t " + std::to_string(type));
}

// --- Errors ----------------------------------------------------------------------------

btoon_status_t status_of(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::generic: return BTOON_ERROR_GENERIC;
        case ErrorCode::truncated: return BTOON_ERROR_TRUNCATED;
        case ErrorCode::invalid_marker: return BTOON_ERROR_INVALID_MARKER;
        case ErrorCode::trailing_data: return BTOON_ERROR_TRAILING_DATA;
        case ErrorCode::invalid_utf8: return BTOON_ERROR_INVALID_UTF8;
        case ErrorCode::non_string_key: return BTOON_ERROR_NON_STRING_KEY;
        case ErrorCode::duplicate_key: return BTOON_ERROR_DUPLICATE_KEY;
        case ErrorCode::max_depth: return BTOON_ERROR_MAX_DEPTH;
        case ErrorCode::invalid_extension: return BTOON_ERROR_INVALID_EXTENSION;
        case ErrorCode::invalid_tabular: return BTOON_ERROR_INVALID_TABULAR;
        case ErrorCode::invalid_envelope: return BTOON_ERROR_INVALID_ENVELOPE;
        case ErrorCode::size_limit: return BTOON_ERROR_SIZE_LIMIT;
        case ErrorCode::unsupported_compression: return BTOON_ERROR_UNSUPPORTED_COMPRESSION;
        case ErrorCode::authentication_failed: return BTOON_ERROR_AUTHENTICATION_FAILED;
        case ErrorCode::invalid_argument: return BTOON_ERROR_INVALID_ARGUMENT;
    }
    return BTOON_ERROR_GENERIC;
}

// A message the caller frees with delete[] (btoon_free_error / btoon_free_result), or
// NULL if even that can't be allocated.
char* copy_error(const char* msg) noexcept {
    size_t n = std::strlen(msg);
    char* out = new (std::nothrow) char[n + 1];
    if (out) std::memcpy(out, msg, n + 1);
    return out;
}

// Runs `body`, translating any exception into a status and a message.
template <class F>
btoon_status_t guarded(F&& body, char** message) noexcept {
    try {
        body();
        return BTOON_OK;
    } catch (const BtoonException& e) {
        if (message) *message = copy_error(e.what());
        return status_of(e.code());
    } catch (const std::bad_alloc&) {
        if (message) *message = copy_error("out of memory");
        return BTOON_ERROR_OUT_OF_MEMORY;
    } catch (const std::length_error& e) {
        if (message) *message = copy_error(e.what());
        return BTOON_ERROR_OUT_OF_MEMORY;
    } catch (const std::exception& e) {
        if (message) *message = copy_error(e.what());
        return BTOON_ERROR_GENERIC;
    } catch (...) {
        if (message) *message = copy_error("unknown error");
        return BTOON_ERROR_GENERIC;
    }
}

// The create functions: build a C++ value, convert it, NULL on any failure.
template <class F>
btoon_value_t* create(F&& make) noexcept {
    btoon_value_t* out = nullptr;
    guarded([&] { out = to_c_value(make()).release(); }, nullptr);
    return out;
}

std::string key_string(const uint8_t* key, size_t len) {
    auto k = buffer(key, len, "hmac_key");
    if (k.empty()) fail(ErrorCode::invalid_argument, "the HMAC key is empty; pass NULL for no key");
    return std::string(k.begin(), k.end());
}

}  // namespace

extern "C" {

BTOON_API const char* btoon_status_name(btoon_status_t status) {
    const int value = raw_enum(status);
    if (value == BTOON_OK) return "ok";
    if (value == BTOON_ERROR_OUT_OF_MEMORY) return "out_of_memory";
    const int code = value - 1;
    if (code >= 0 && code <= static_cast<int>(ErrorCode::invalid_argument)) {
        return btoon::error_code_name(static_cast<ErrorCode>(code));
    }
    return "unknown";
}

BTOON_API void btoon_value_destroy(btoon_value_t* value) {
    if (!value) return;
    switch (value->type) {
        case BTOON_TYPE_STRING:
            delete[] value->as.s.ptr;
            break;
        case BTOON_TYPE_BINARY:
            delete[] value->as.bin.ptr;
            break;
        case BTOON_TYPE_ARRAY:
            for (size_t i = 0; i < value->as.a.len; ++i) btoon_value_destroy(value->as.a.elements[i]);
            delete[] value->as.a.elements;
            break;
        case BTOON_TYPE_MAP:
            for (size_t i = 0; i < value->as.m.len; ++i) {
                delete[] value->as.m.keys[i].ptr;
                btoon_value_destroy(value->as.m.values[i]);
            }
            delete[] value->as.m.keys;
            delete[] value->as.m.values;
            break;
        case BTOON_TYPE_BIGINT:
            delete[] value->as.bi.ptr;
            break;
        case BTOON_TYPE_VECTOR_FLOAT:
            delete[] value->as.vf.ptr;
            break;
        case BTOON_TYPE_VECTOR_DOUBLE:
            delete[] value->as.vd.ptr;
            break;
        case BTOON_TYPE_EXTENSION:
            delete[] value->as.ext.ptr;
            break;
        default:
            break;
    }
    delete value;
}

BTOON_API btoon_value_t* btoon_value_create_nil(void) {
    return create([] { return btoon::Value(nullptr); });
}

BTOON_API btoon_value_t* btoon_value_create_bool(bool value) {
    return create([&] { return btoon::Value(value); });
}

BTOON_API btoon_value_t* btoon_value_create_int(int64_t value) {
    return create([&] { return btoon::Value(btoon::Int(value)); });
}

BTOON_API btoon_value_t* btoon_value_create_uint(uint64_t value) {
    return create([&] { return btoon::Value(btoon::Uint(value)); });
}

BTOON_API btoon_value_t* btoon_value_create_float(double value) {
    return create([&] { return btoon::Value(btoon::Float(value)); });
}

BTOON_API btoon_value_t* btoon_value_create_string(const char* str, size_t len) {
    return create([&] {
        auto s = buffer(str, len, "string");
        return btoon::Value(btoon::String(s.begin(), s.end()));
    });
}

BTOON_API btoon_value_t* btoon_value_create_binary(const uint8_t* data, size_t len) {
    return create([&] {
        auto b = buffer(data, len, "binary");
        return btoon::Value(btoon::Binary(b.begin(), b.end()));
    });
}

BTOON_API btoon_value_t* btoon_value_create_array(const btoon_value_t** elements, size_t len) {
    return create([&] {
        btoon_array_t a{const_cast<btoon_value_t**>(elements), len};
        btoon_value_t view{};
        view.type = BTOON_TYPE_ARRAY;
        view.as.a = a;
        return to_cpp_value(&view, 0);
    });
}

BTOON_API btoon_value_t* btoon_value_create_map(const btoon_string_t* keys, const btoon_value_t** values, size_t len) {
    return create([&] {
        btoon_value_t view{};
        view.type = BTOON_TYPE_MAP;
        view.as.m = {const_cast<btoon_string_t*>(keys), const_cast<btoon_value_t**>(values), len};
        return to_cpp_value(&view, 0);
    });
}

BTOON_API btoon_value_t* btoon_value_create_timestamp(int64_t seconds, uint32_t nanoseconds) {
    return create([&] { return btoon::Value(btoon::Timestamp(seconds, nanoseconds)); });
}

BTOON_API btoon_value_t* btoon_value_create_timestamp_with_offset(int64_t seconds, uint32_t nanoseconds,
                                                                  int16_t offset_minutes) {
    return create([&] { return btoon::Value(btoon::Timestamp(seconds, nanoseconds, offset_minutes)); });
}

BTOON_API btoon_value_t* btoon_value_create_date(int64_t milliseconds) {
    return create([&] { return btoon::Value(btoon::Date{milliseconds}); });
}

BTOON_API btoon_value_t* btoon_value_create_datetime(int64_t nanoseconds) {
    return create([&] { return btoon::Value(btoon::DateTime{nanoseconds}); });
}

BTOON_API btoon_value_t* btoon_value_create_bigint(const uint8_t* bytes, size_t len) {
    return create([&] {
        auto b = buffer(bytes, len, "bigint");
        return btoon::Value(btoon::BigInt{std::vector<uint8_t>(b.begin(), b.end())});
    });
}

BTOON_API btoon_value_t* btoon_value_create_vector_float(const float* data, size_t len) {
    return create([&] {
        auto d = buffer(data, len, "float vector");
        return btoon::Value(btoon::VectorFloat{std::vector<float>(d.begin(), d.end())});
    });
}

BTOON_API btoon_value_t* btoon_value_create_vector_double(const double* data, size_t len) {
    return create([&] {
        auto d = buffer(data, len, "double vector");
        return btoon::Value(btoon::VectorDouble{std::vector<double>(d.begin(), d.end())});
    });
}

BTOON_API btoon_value_t* btoon_value_create_extension(int8_t type, const uint8_t* data, size_t len) {
    return create([&] {
        auto d = buffer(data, len, "extension data");
        return btoon::Value(btoon::Extension{type, std::vector<uint8_t>(d.begin(), d.end())});
    });
}

BTOON_API btoon_result_t btoon_encode(const btoon_value_t* value, const btoon_encode_options_t* options) {
    btoon_result_t result{nullptr, 0, nullptr, BTOON_OK};
    result.status = guarded(
        [&] {
            btoon::EncodeOptions o;
            if (options) {
                o.compress = options->compress;
                const int algorithm = raw_enum(options->compression_algorithm);
                if (algorithm < BTOON_COMPRESSION_ZLIB || algorithm > BTOON_COMPRESSION_SNAPPY) {
                    fail(ErrorCode::invalid_argument, "unknown compression algorithm " + std::to_string(algorithm));
                }
                o.compression_algorithm = static_cast<btoon::CompressionAlgorithm>(algorithm);
                o.compression_level = options->compression_level;
                o.auto_tabular = !options->no_tabular;
                if (options->hmac_key) o.hmac_key = key_string(options->hmac_key, options->hmac_key_len);
            }
            const std::vector<uint8_t> bytes = btoon::encode(to_cpp_value(value, 0), o);
            result.data = copy_array(bytes.data(), bytes.size());
            result.size = bytes.size();
        },
        &result.error);
    return result;
}

BTOON_API btoon_value_t* btoon_decode(const uint8_t* data, size_t size, const btoon_decode_options_t* options,
                                      btoon_status_t* status, char** error) {
    if (error) *error = nullptr;
    btoon_value_t* out = nullptr;
    const btoon_status_t s = guarded(
        [&] {
            auto bytes = buffer(data, size, "data");
            btoon::DecodeOptions o;
            if (options) {
                o.auto_decompress = !options->no_decompress;
                o.strict = !options->non_strict;
                if (options->max_depth) o.max_depth = options->max_depth;
                if (options->max_decompressed_size) o.max_decompressed_size = options->max_decompressed_size;
                if (options->max_values) o.max_values = options->max_values;
                if (options->hmac_key) o.hmac_key = key_string(options->hmac_key, options->hmac_key_len);
            }
            out = to_c_value(btoon::decode(bytes, o)).release();
        },
        error);
    if (status) *status = s;
    return out;
}

BTOON_API void btoon_free_result(btoon_result_t result) {
    delete[] result.data;
    delete[] result.error;
}

BTOON_API void btoon_free_error(char* error) { delete[] error; }

BTOON_API const char* btoon_version(void) { return btoon::version(); }

BTOON_API const char* btoon_spec_version(void) { return btoon::spec_version(); }

}  // extern "C"

#include <gtest/gtest.h>
#include "btoon/capi.h"
#include "btoon/btoon.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

std::vector<uint8_t> bytes_of(const btoon_result_t& r) {
    return r.data ? std::vector<uint8_t>(r.data, r.data + r.size) : std::vector<uint8_t>{};
}

// Decodes through the C API and re-encodes through it.
std::vector<uint8_t> c_round_trip(const std::vector<uint8_t>& in) {
    btoon_status_t status = BTOON_ERROR_GENERIC;
    char* err = nullptr;
    btoon_value_t* v = btoon_decode(in.data(), in.size(), nullptr, &status, &err);
    EXPECT_EQ(status, BTOON_OK) << (err ? err : "");
    EXPECT_EQ(err, nullptr);
    if (!v) return {};
    btoon_result_t r = btoon_encode(v, nullptr);
    EXPECT_EQ(r.status, BTOON_OK) << (r.error ? r.error : "");
    auto out = bytes_of(r);
    btoon_free_result(r);
    btoon_value_destroy(v);
    return out;
}

}  // namespace

TEST(CAPITest, Version) {
    const char* version = btoon_version();
    ASSERT_NE(version, nullptr);
    EXPECT_GT(strlen(version), 4);
    EXPECT_STREQ(btoon_version(), btoon::version());
    EXPECT_STREQ(btoon_spec_version(), btoon::spec_version());
}

TEST(CAPITest, NilRoundTrip) {
    btoon_value_t* original = btoon_value_create_nil();
    ASSERT_NE(original, nullptr);

    btoon_encode_options_t options{};
    btoon_result_t result = btoon_encode(original, &options);
    ASSERT_EQ(result.status, BTOON_OK);
    ASSERT_EQ(result.error, nullptr);
    ASSERT_NE(result.data, nullptr);

    char* error = nullptr;
    btoon_decode_options_t decode_options{};
    btoon_value_t* decoded = btoon_decode(result.data, result.size, &decode_options, nullptr, &error);
    ASSERT_EQ(error, nullptr);
    ASSERT_NE(decoded, nullptr);

    EXPECT_EQ(decoded->type, BTOON_TYPE_NIL);

    btoon_value_destroy(original);
    btoon_value_destroy(decoded);
    btoon_free_result(result);
}

TEST(CAPITest, StringRoundTrip) {
    const char* test_str = "Hello from C API";
    btoon_value_t* original = btoon_value_create_string(test_str, strlen(test_str));
    ASSERT_NE(original, nullptr);

    btoon_result_t result = btoon_encode(original, nullptr);
    ASSERT_EQ(result.error, nullptr);

    char* error = nullptr;
    btoon_value_t* decoded = btoon_decode(result.data, result.size, nullptr, nullptr, &error);
    ASSERT_EQ(error, nullptr);
    ASSERT_NE(decoded, nullptr);

    ASSERT_EQ(decoded->type, BTOON_TYPE_STRING);
    EXPECT_EQ(decoded->as.s.len, strlen(test_str));
    EXPECT_STREQ(decoded->as.s.ptr, test_str);

    btoon_value_destroy(original);
    btoon_value_destroy(decoded);
    btoon_free_result(result);
}

TEST(CApiTest, ErrorsDoNotThrowAcrossTheBoundary) {
    const uint8_t bad[] = {0x92, 0x01};  // truncated array
    char* err = nullptr;
    btoon_status_t status = BTOON_OK;
    EXPECT_EQ(btoon_decode(bad, sizeof bad, nullptr, &status, &err), nullptr);
    EXPECT_EQ(status, BTOON_ERROR_TRUNCATED);
    ASSERT_NE(err, nullptr);
    EXPECT_GT(std::strlen(err), 0u);
    btoon_free_error(err);
    // NULL out-parameters are allowed.
    EXPECT_EQ(btoon_decode(bad, sizeof bad, nullptr, nullptr, nullptr), nullptr);
    // Encoding a NULL value reports an error instead of crashing.
    btoon_result_t r = btoon_encode(nullptr, nullptr);
    EXPECT_EQ(r.data, nullptr);
    EXPECT_EQ(r.status, BTOON_ERROR_INVALID_ARGUMENT);
    ASSERT_NE(r.error, nullptr);
    btoon_free_result(r);
}

// --- Review 2026-11-13: the C API findings (0.3.0) -------------------------------------------

// The C timestamp had only `seconds`: nanoseconds and the UTC offset were lost on decode
// and re-encode.
TEST(CApiFixes, TimestampsKeepNanosecondsAndOffset) {
    // 2024-01-01T00:00:00.5Z (8-byte form) and the same instant at +02:00 (14-byte form).
    const std::vector<uint8_t> nanos{0xd7, 0xff, 0x77, 0x35, 0x94, 0x00, 0x65, 0x92, 0x00, 0x80};
    const std::vector<uint8_t> offset{0xc7, 0x0e, 0xff, 0x1d, 0xcd, 0x65, 0x00, 0, 0, 0, 0,
                                      0x65, 0x92, 0x00, 0x80, 0x00, 0x78};
    EXPECT_EQ(c_round_trip(nanos), nanos);
    EXPECT_EQ(c_round_trip(offset), offset);

    btoon_value_t* v = btoon_decode(offset.data(), offset.size(), nullptr, nullptr, nullptr);
    ASSERT_NE(v, nullptr);
    ASSERT_EQ(v->type, BTOON_TYPE_TIMESTAMP);
    EXPECT_EQ(v->as.ts.seconds, 1704067200);
    EXPECT_EQ(v->as.ts.nanoseconds, 500000000u);
    EXPECT_TRUE(v->as.ts.has_timezone);
    EXPECT_EQ(v->as.ts.timezone_offset, 120);
    btoon_value_destroy(v);

    btoon_value_t* made = btoon_value_create_timestamp_with_offset(1704067200, 500000000, 120);
    btoon_result_t r = btoon_encode(made, nullptr);
    EXPECT_EQ(bytes_of(r), offset);
    btoon_free_result(r);
    btoon_value_destroy(made);
    made = btoon_value_create_timestamp(1704067200, 500000000);
    r = btoon_encode(made, nullptr);
    EXPECT_EQ(bytes_of(r), nanos);
    btoon_free_result(r);
    btoon_value_destroy(made);
    // Invalid nanoseconds are reported by the encoder.
    made = btoon_value_create_timestamp(0, 1000000000);
    ASSERT_NE(made, nullptr);
    r = btoon_encode(made, nullptr);
    EXPECT_EQ(r.status, BTOON_ERROR_INVALID_ARGUMENT);
    btoon_free_result(r);
    btoon_value_destroy(made);
}

// A zero-initialized options struct, the usual C idiom, turned tabular encoding off and
// (for decoding) turned off decompression and strict mode.
TEST(CApiFixes, ZeroInitializedOptionsAreTheDefaults) {
    btoon::Array cpp_rows;
    std::vector<btoon_value_t*> rows;
    for (int i = 0; i < 40; ++i) {
        cpp_rows.push_back(btoon::Map{{"id", btoon::Int(i)}, {"name", btoon::String("the same name")}});
        const btoon_string_t keys[2] = {{"id", 2}, {"name", 4}};
        btoon_value_t* id = btoon_value_create_int(i);
        btoon_value_t* name = btoon_value_create_string("the same name", 13);
        const btoon_value_t* vals[2] = {id, name};
        rows.push_back(btoon_value_create_map(keys, vals, 2));
        btoon_value_destroy(id);
        btoon_value_destroy(name);
    }
    btoon_value_t* arr = btoon_value_create_array(const_cast<const btoon_value_t**>(rows.data()), rows.size());
    ASSERT_NE(arr, nullptr);

    btoon_encode_options_t zeroed;
    std::memset(&zeroed, 0, sizeof zeroed);
    btoon_result_t plain = btoon_encode(arr, &zeroed);
    EXPECT_EQ(bytes_of(plain), btoon::encode(btoon::Value(cpp_rows))) << "tabular, like the C++ default";
    zeroed.compress = true;
    btoon_result_t packed = btoon_encode(arr, &zeroed);
    ASSERT_EQ(std::memcmp(packed.data, "BTON", 4), 0) << "large enough to compress";
    btoon::EncodeOptions cpp_compress;
    cpp_compress.compress = true;
    EXPECT_EQ(bytes_of(packed), btoon::encode(btoon::Value(cpp_rows), cpp_compress));
    zeroed.compress = false;
    zeroed.no_tabular = true;
    btoon_result_t untabular = btoon_encode(arr, &zeroed);
    btoon::EncodeOptions cpp_plain;
    cpp_plain.auto_tabular = false;
    EXPECT_EQ(bytes_of(untabular), btoon::encode(btoon::Value(cpp_rows), cpp_plain));

    // Zeroed decode options decompress, and are strict.
    btoon_decode_options_t dzero;
    std::memset(&dzero, 0, sizeof dzero);
    btoon_status_t status = BTOON_ERROR_GENERIC;
    btoon_value_t* back = btoon_decode(packed.data, packed.size, &dzero, &status, nullptr);
    EXPECT_EQ(status, BTOON_OK);
    ASSERT_NE(back, nullptr);
    EXPECT_EQ(back->as.a.len, 40u);
    btoon_value_destroy(back);
    const uint8_t trailing[] = {0x01, 0x02};
    EXPECT_EQ(btoon_decode(trailing, sizeof trailing, &dzero, &status, nullptr), nullptr);
    EXPECT_EQ(status, BTOON_ERROR_TRAILING_DATA);
    dzero.non_strict = true;
    back = btoon_decode(trailing, sizeof trailing, &dzero, &status, nullptr);
    EXPECT_EQ(status, BTOON_OK);
    btoon_value_destroy(back);
    dzero.non_strict = false;
    dzero.no_decompress = true;
    EXPECT_EQ(btoon_decode(packed.data, packed.size, &dzero, &status, nullptr), nullptr);
    EXPECT_EQ(status, BTOON_ERROR_INVALID_ENVELOPE);

    btoon_free_result(plain);
    btoon_free_result(packed);
    btoon_free_result(untabular);
    for (auto* r : rows) btoon_value_destroy(r);
    btoon_value_destroy(arr);
}

// btoon_value_create_string("x", SIZE_MAX) let std::length_error escape into C, which
// terminated the process. The create functions now return NULL.
TEST(CApiFixes, CreateFunctionsReturnNullInsteadOfThrowing) {
    EXPECT_EQ(btoon_value_create_string("x", SIZE_MAX), nullptr);
    EXPECT_EQ(btoon_value_create_binary(nullptr, 3), nullptr);
    EXPECT_EQ(btoon_value_create_bigint(nullptr, 1), nullptr);
    EXPECT_EQ(btoon_value_create_vector_float(nullptr, 2), nullptr);
    EXPECT_EQ(btoon_value_create_vector_double(nullptr, 2), nullptr);
    EXPECT_EQ(btoon_value_create_extension(5, nullptr, 1), nullptr);
    EXPECT_EQ(btoon_value_create_array(nullptr, 1), nullptr);
    const btoon_value_t* missing[1] = {nullptr};
    EXPECT_EQ(btoon_value_create_array(missing, 1), nullptr);
    btoon_value_t* one = btoon_value_create_int(1);
    const btoon_value_t* two_values[2] = {one, one};
    const btoon_string_t dup_keys[2] = {{"k", 1}, {"k", 1}};
    EXPECT_EQ(btoon_value_create_map(dup_keys, two_values, 2), nullptr) << "duplicate keys";
    const btoon_string_t null_key[1] = {{nullptr, 1}};
    EXPECT_EQ(btoon_value_create_map(null_key, two_values, 1), nullptr);
    btoon_value_destroy(one);
    // Empty buffers may be NULL.
    for (btoon_value_t* v : {btoon_value_create_string(nullptr, 0), btoon_value_create_binary(nullptr, 0),
                             btoon_value_create_array(nullptr, 0), btoon_value_create_map(nullptr, nullptr, 0)}) {
        ASSERT_NE(v, nullptr);
        btoon_result_t r = btoon_encode(v, nullptr);
        EXPECT_EQ(r.status, BTOON_OK);
        btoon_free_result(r);
        btoon_value_destroy(v);
    }
}

// Empty binaries and extensions were copied with memcpy from a NULL vector data()
// (undefined behavior). Empty buffers now come back as ptr == NULL, len == 0.
TEST(CApiFixes, EmptyBuffersRoundTrip) {
    for (const std::vector<uint8_t>& doc : {std::vector<uint8_t>{0xc4, 0x00}, std::vector<uint8_t>{0xc7, 0x00, 0x05},
                                            std::vector<uint8_t>{0xc7, 0x00, 0xfb}, std::vector<uint8_t>{0xc7, 0x00, 0xfa},
                                            std::vector<uint8_t>{0xa0}, std::vector<uint8_t>{0x90},
                                            std::vector<uint8_t>{0x80}}) {
        btoon_value_t* v = btoon_decode(doc.data(), doc.size(), nullptr, nullptr, nullptr);
        ASSERT_NE(v, nullptr);
        switch (v->type) {
            case BTOON_TYPE_BINARY: EXPECT_EQ(v->as.bin.ptr, nullptr); break;
            case BTOON_TYPE_EXTENSION: EXPECT_EQ(v->as.ext.ptr, nullptr); break;
            case BTOON_TYPE_VECTOR_FLOAT: EXPECT_EQ(v->as.vf.ptr, nullptr); break;
            case BTOON_TYPE_VECTOR_DOUBLE: EXPECT_EQ(v->as.vd.ptr, nullptr); break;
            case BTOON_TYPE_STRING: EXPECT_STREQ(v->as.s.ptr, ""); break;
            case BTOON_TYPE_ARRAY: EXPECT_EQ(v->as.a.elements, nullptr); break;
            case BTOON_TYPE_MAP: EXPECT_EQ(v->as.m.keys, nullptr); break;
            default: ADD_FAILURE() << "unexpected type " << v->type;
        }
        btoon_value_destroy(v);
        EXPECT_EQ(c_round_trip(doc), doc);
    }
}

// The C API returned only a message; now every failure has a code.
TEST(CApiFixes, ErrorsHaveStatusCodes) {
    struct Case {
        std::vector<uint8_t> doc;
        btoon_status_t expected;
    };
    const Case cases[] = {
        {{0xc1}, BTOON_ERROR_INVALID_MARKER},
        {{0xa1, 0xff}, BTOON_ERROR_INVALID_UTF8},
        {{0x81, 0x01, 0x02}, BTOON_ERROR_NON_STRING_KEY},
        {{0x82, 0xa1, 'a', 0x01, 0xa1, 'a', 0x02}, BTOON_ERROR_DUPLICATE_KEY},
        {{0xd4, 0xff, 0x00}, BTOON_ERROR_INVALID_EXTENSION},
    };
    for (const Case& c : cases) {
        btoon_status_t status = BTOON_OK;
        char* err = nullptr;
        EXPECT_EQ(btoon_decode(c.doc.data(), c.doc.size(), nullptr, &status, &err), nullptr);
        EXPECT_EQ(status, c.expected) << btoon_status_name(status);
        EXPECT_NE(err, nullptr);
        btoon_free_error(err);
    }
    EXPECT_STREQ(btoon_status_name(BTOON_OK), "ok");
    EXPECT_STREQ(btoon_status_name(BTOON_ERROR_TRUNCATED), "truncated");
    EXPECT_STREQ(btoon_status_name(BTOON_ERROR_AUTHENTICATION_FAILED), "authentication_failed");
    EXPECT_STREQ(btoon_status_name(BTOON_ERROR_INVALID_ARGUMENT), "invalid_argument");
    EXPECT_STREQ(btoon_status_name(BTOON_ERROR_OUT_OF_MEMORY), "out_of_memory");

    // Encoder errors: invalid UTF-8, a defined extension id written raw, depth.
    btoon_value_t* bad = btoon_value_create_string("\xff", 1);
    btoon_result_t r = btoon_encode(bad, nullptr);
    EXPECT_EQ(r.status, BTOON_ERROR_INVALID_UTF8);
    EXPECT_EQ(r.data, nullptr);
    btoon_free_result(r);
    btoon_value_destroy(bad);
    bad = btoon_value_create_extension(-1, reinterpret_cast<const uint8_t*>("abcd"), 4);
    r = btoon_encode(bad, nullptr);
    EXPECT_EQ(r.status, BTOON_ERROR_INVALID_ARGUMENT);
    btoon_free_result(r);
    btoon_value_destroy(bad);
    btoon_value_t node{};
    const int bogus = 77;  // C may store any int in an enum; tests/test_capi_c.c does
    std::memcpy(&node.type, &bogus, sizeof node.type);
    r = btoon_encode(&node, nullptr);
    EXPECT_EQ(r.status, BTOON_ERROR_INVALID_ARGUMENT) << "unknown type tag";
    btoon_free_result(r);
    // A hand-built cycle stops at the depth limit instead of overflowing the stack.
    btoon_value_t cycle{};
    btoon_value_t* self = &cycle;
    cycle.type = BTOON_TYPE_ARRAY;
    cycle.as.a = {&self, 1};
    r = btoon_encode(&cycle, nullptr);
    EXPECT_EQ(r.status, BTOON_ERROR_MAX_DEPTH);
    btoon_free_result(r);
}

TEST(CApiFixes, SigningAndDecodeLimits) {
    const std::string key(32, 'k');
    btoon_value_t* v = btoon_value_create_string("signed", 6);
    btoon_encode_options_t eo{};
    eo.hmac_key = reinterpret_cast<const uint8_t*>(key.data());
    eo.hmac_key_len = key.size();
    btoon_result_t sealed = btoon_encode(v, &eo);
    ASSERT_EQ(sealed.status, BTOON_OK);
    EXPECT_EQ(std::memcmp(sealed.data, "BTSG", 4), 0);
    btoon::DecodeOptions cpp;
    cpp.hmac_key = key;
    EXPECT_EQ(btoon::decode(bytes_of(sealed), cpp), btoon::Value(btoon::String("signed")));

    btoon_status_t status = BTOON_OK;
    EXPECT_EQ(btoon_decode(sealed.data, sealed.size, nullptr, &status, nullptr), nullptr);
    EXPECT_EQ(status, BTOON_ERROR_AUTHENTICATION_FAILED);
    btoon_decode_options_t d{};
    d.hmac_key = eo.hmac_key;
    d.hmac_key_len = eo.hmac_key_len;
    btoon_value_t* back = btoon_decode(sealed.data, sealed.size, &d, &status, nullptr);
    EXPECT_EQ(status, BTOON_OK);
    ASSERT_NE(back, nullptr);
    EXPECT_STREQ(back->as.s.ptr, "signed");
    btoon_value_destroy(back);

    // An empty key is an error, not "no key" (SPEC §8).
    d.hmac_key_len = 0;
    EXPECT_EQ(btoon_decode(sealed.data, sealed.size, &d, &status, nullptr), nullptr);
    EXPECT_EQ(status, BTOON_ERROR_INVALID_ARGUMENT);
    eo.hmac_key_len = 0;
    btoon_result_t empty = btoon_encode(v, &eo);
    EXPECT_EQ(empty.status, BTOON_ERROR_INVALID_ARGUMENT);
    btoon_free_result(empty);

    // max_values and max_depth reach the decoder.
    const auto many = btoon::encode(btoon::Value(btoon::Array(10, btoon::Value(nullptr))));
    btoon_decode_options_t limits{};
    limits.max_values = 5;
    EXPECT_EQ(btoon_decode(many.data(), many.size(), &limits, &status, nullptr), nullptr);
    EXPECT_EQ(status, BTOON_ERROR_SIZE_LIMIT);
    const std::vector<uint8_t> nested{0x91, 0x91, 0x91, 0xc0};
    limits = {};
    limits.max_depth = 2;
    EXPECT_EQ(btoon_decode(nested.data(), nested.size(), &limits, &status, nullptr), nullptr);
    EXPECT_EQ(status, BTOON_ERROR_MAX_DEPTH);
    limits.max_depth = 1001;
    EXPECT_EQ(btoon_decode(nested.data(), nested.size(), &limits, &status, nullptr), nullptr);
    EXPECT_EQ(status, BTOON_ERROR_INVALID_ARGUMENT);

    btoon_encode_options_t unknown{};
    unknown.compress = true;
    const int nine = 9;
    std::memcpy(&unknown.compression_algorithm, &nine, sizeof unknown.compression_algorithm);
    btoon_result_t bad_algo = btoon_encode(v, &unknown);
    EXPECT_EQ(bad_algo.status, BTOON_ERROR_INVALID_ARGUMENT);
    btoon_free_result(bad_algo);

    btoon_free_result(sealed);
    btoon_value_destroy(v);
}

// Every type can be created through the C API and encodes like the C++ value.
TEST(CApiFixes, EveryTypeCanBeWritten) {
    const uint8_t big[9] = {0x01, 0, 0, 0, 0, 0, 0, 0, 0};
    const float floats[2] = {1.5f, -2.0f};
    const double doubles[2] = {0.25, 1e300};
    const uint8_t bin[3] = {1, 2, 3};
    std::vector<std::pair<btoon_value_t*, btoon::Value>> cases;
    cases.push_back({btoon_value_create_bool(true), btoon::Value(true)});
    cases.push_back({btoon_value_create_int(-5), btoon::Value(btoon::Int(-5))});
    cases.push_back({btoon_value_create_uint(UINT64_MAX), btoon::Value(btoon::Uint(UINT64_MAX))});
    cases.push_back({btoon_value_create_float(0.5), btoon::Value(0.5)});
    cases.push_back({btoon_value_create_binary(bin, 3), btoon::Value(btoon::Binary{1, 2, 3})});
    cases.push_back({btoon_value_create_date(-86400000), btoon::Value(btoon::Date{-86400000})});
    cases.push_back({btoon_value_create_datetime(INT64_MIN), btoon::Value(btoon::DateTime{INT64_MIN})});
    cases.push_back({btoon_value_create_bigint(big, 9), btoon::Value(btoon::BigInt{{big, big + 9}})});
    cases.push_back({btoon_value_create_vector_float(floats, 2), btoon::Value(btoon::VectorFloat{{1.5f, -2.0f}})});
    cases.push_back({btoon_value_create_vector_double(doubles, 2), btoon::Value(btoon::VectorDouble{{0.25, 1e300}})});
    cases.push_back({btoon_value_create_extension(7, bin, 3), btoon::Value(btoon::Extension{7, {1, 2, 3}})});
    for (auto& [c, cpp] : cases) {
        ASSERT_NE(c, nullptr);
        btoon_result_t r = btoon_encode(c, nullptr);
        ASSERT_EQ(r.status, BTOON_OK) << cpp.type_name();
        const auto expected = btoon::encode(cpp);
        EXPECT_EQ(bytes_of(r), expected) << cpp.type_name();
        EXPECT_EQ(c_round_trip(expected), expected) << cpp.type_name();
        btoon_free_result(r);
        btoon_value_destroy(c);
    }
}

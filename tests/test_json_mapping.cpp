// Unit tests for tools/json_mapping.h, the JSON <-> BTOON mapping of the CLI tools
// (btoon, btoon-convert, btoon-schema).
#include <gtest/gtest.h>

#include <bit>
#include <chrono>
#include <cmath>
#include <fstream>
#include <limits>
#include <random>
#include <string>

#include "btoon/btoon.h"
#include "json_mapping.h"
#include "vector_support.h"

using namespace btoon;
namespace bt = btoon::tools;
using bt::json;

namespace {

/// What the tools do: value -> JSON text -> value.
Value via_text(const Value& v, int indent = -1) {
    return bt::from_json(bt::parse_json(bt::to_json(v).dump(indent)));
}

Value str(const char* s) { return Value(std::string(s)); }

Value parse(std::string_view text, bool tags = true) { return bt::from_json(bt::parse_json(text), tags); }

std::string dump(const Value& v) { return bt::to_json(v).dump(); }

std::string hex_of(const std::vector<uint8_t>& b) { return vtest::hex(b); }

/// Expects `fn` to throw an exception whose message contains `needle`.
template <typename Fn>
void expect_error(Fn fn, const std::string& needle, const std::string& context) {
    try {
        fn();
        ADD_FAILURE() << context << ": no exception";
    } catch (const std::exception& e) {
        EXPECT_NE(std::string(e.what()).find(needle), std::string::npos) << context << ": " << e.what();
    }
}

void expect_rejected(const std::string& json_text, const std::string& needle = "") {
    expect_error([&] { parse(json_text); }, needle, json_text);
}

bool feature_available(const std::string& feature) {
    if (feature == "zstd") return compression_available(CompressionAlgorithm::ZSTD);
    if (feature == "lz4") return compression_available(CompressionAlgorithm::LZ4);
    if (feature == "brotli") return compression_available(CompressionAlgorithm::BROTLI);
    if (feature == "snappy") return compression_available(CompressionAlgorithm::SNAPPY);
    return false;
}

Value nested_arrays(size_t depth, Value leaf) {
    Value v = std::move(leaf);
    for (size_t i = 0; i < depth; ++i) v = Array{std::move(v)};
    return v;
}

double seconds_since(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

}  // namespace

// Every conformance vector survives BTOON -> JSON text -> BTOON; canonical ones byte for byte.
TEST(JsonMapping, ConformanceVectorsRoundTrip) {
    std::ifstream in(BTOON_VECTORS_FILE);
    ASSERT_TRUE(in) << "cannot open " BTOON_VECTORS_FILE;
    const auto doc = nlohmann::json::parse(in);
    size_t checked = 0;
    for (const auto& v : doc.at("vectors")) {
        if (v.contains("error") || v.contains("encode_error")) continue;  // nothing to round-trip
        if (v.contains("requires") && !feature_available(v.at("requires"))) continue;
        const std::string id = v.at("id");
        SCOPED_TRACE("vector " + id);
        DecodeOptions dopts;
        EncodeOptions eopts;
        if (v.contains("options")) {  // decoder options a vector can set (docs/SPEC.md §10)
            const auto& o = v.at("options");
            if (o.contains("hmac_key")) {
                const auto key = vtest::unhex(o.at("hmac_key").get<std::string>());
                dopts.hmac_key = std::string(key.begin(), key.end());
                eopts.hmac_key = dopts.hmac_key;
            }
            if (o.contains("strict")) dopts.strict = o.at("strict").get<bool>();
            if (o.contains("max_depth")) dopts.max_depth = o.at("max_depth").get<size_t>();
            if (o.contains("max_decompressed_size")) dopts.max_decompressed_size = o.at("max_decompressed_size").get<size_t>();
            if (o.contains("max_values")) dopts.max_values = o.at("max_values").get<size_t>();
        }
        const auto bytes = vtest::unhex(v.at("hex").get<std::string>());
        const Value decoded = decode(bytes, dopts);
        for (int indent : {-1, 2}) {
            const Value back = via_text(decoded, indent);
            const auto again = encode(back, eopts);
            if (v.at("canonical").get<bool>()) {
                EXPECT_EQ(hex_of(again), v.at("hex").get<std::string>());
            } else {
                EXPECT_EQ(hex_of(again), hex_of(encode(decoded, eopts)));
            }
        }
        ++checked;
    }
    EXPECT_GT(checked, 150u);
}

// --- integers ------------------------------------------------------------------------

TEST(JsonMapping, Uint64AboveInt64MaxStaysUnsigned) {
    EXPECT_EQ(hex_of(encode(parse("18446744073709551615"))), "cfffffffffffffffff");
    EXPECT_EQ(hex_of(encode(parse("9223372036854775808"))), "cf8000000000000000");
    EXPECT_EQ(hex_of(encode(parse("-9223372036854775808"))), "d38000000000000000");
    EXPECT_EQ(dump(Value(uint64_t(18446744073709551615ull))), "18446744073709551615");
}

TEST(JsonMapping, IntegerLiteralsBeyond64BitsBecomeBigints) {
    EXPECT_EQ(hex_of(encode(parse("18446744073709551616"))), "c709fc010000000000000000");
    EXPECT_EQ(hex_of(encode(parse("-9223372036854775809"))), "c709fcff7fffffffffffffff");
    const Value v = parse("[123456789012345678901234567890, -123456789012345678901234567890]");
    EXPECT_EQ(dump(v), R"([{"$bigint":"123456789012345678901234567890"},{"$bigint":"-123456789012345678901234567890"}])");
    // Not a tag, so --no-tags keeps the integer too.
    EXPECT_EQ(dump(parse("18446744073709551616", false)), R"({"$bigint":"18446744073709551616"})");
    // Fractions and exponents stay floats.
    EXPECT_TRUE(std::holds_alternative<Float>(parse("18446744073709551616.0")));
    EXPECT_TRUE(std::holds_alternative<Float>(parse("1e19")));
    expect_rejected("1e400", "number overflow");
}

TEST(JsonMapping, BigintDecimalAndHexMatchReference) {
    std::mt19937_64 rng(12345);
    std::vector<std::vector<uint8_t>> cases = {
        {0x00}, {0x01}, {0xff}, {0x7f}, {0x80}, {0x00, 0x80}, {0xff, 0x7f},
        {0x01, 0, 0, 0, 0, 0, 0, 0, 0}, {0xff, 0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
        {0x3b, 0x9a, 0xca, 0x00}, {0x3b, 0x9a, 0xc9, 0xff}, {0x0d, 0xe0, 0xb6, 0xb3, 0xa7, 0x64, 0x00, 0x00},
    };
    for (size_t n = 1; n <= 300; n += (n < 20 ? 1 : 7)) {
        for (int k = 0; k < 3; ++k) {
            std::vector<uint8_t> b(n);
            for (auto& x : b) x = static_cast<uint8_t>(rng());
            cases.push_back(b);
        }
    }
    for (const auto& raw : cases) {
        const auto minimal = BigInt{raw}.minimal_bytes();
        const std::string dec = bt::bigint_to_decimal(raw);
        SCOPED_TRACE(dec);
        EXPECT_EQ(vtest::decimal_to_twos(dec), minimal);  // independent (slow) reference
        EXPECT_EQ(bt::bigint_from_string(dec), minimal);
        EXPECT_EQ(bt::bigint_from_string(bt::bigint_to_hex(raw)), minimal);
    }
    EXPECT_EQ(bt::bigint_to_decimal(std::vector<uint8_t>{}), "0");
    EXPECT_EQ(bt::bigint_to_hex(std::vector<uint8_t>{0x00, 0x80}), "0x80");
    EXPECT_EQ(bt::bigint_to_hex(std::vector<uint8_t>{0x80}), "-0x80");
    EXPECT_EQ(bt::bigint_to_hex(std::vector<uint8_t>{0x00}), "0x0");
    EXPECT_EQ(bt::bigint_from_string("-0"), std::vector<uint8_t>{0x00});
    EXPECT_EQ(bt::bigint_from_string("-0x0"), std::vector<uint8_t>{0x00});
    EXPECT_EQ(bt::bigint_from_string("0XfF"), (std::vector<uint8_t>{0x00, 0xff}));
    EXPECT_EQ(bt::bigint_from_string("000123"), std::vector<uint8_t>{123});
    for (const char* bad : {"", "-", "+5", " 5", "5 ", "0x", "-0x", "0xg", "1.5", "1e5", "--1", "0x-1"}) {
        expect_error([&] { bt::bigint_from_string(bad); }, "invalid $bigint", std::string("bigint '") + bad + "'");
    }
}

// The review measured 16-54 s for a 64 KB bigint in `btoon decode`.
TEST(JsonMapping, LargeBigintsAreFastAndSwitchToHex) {
    std::mt19937_64 rng(7);
    std::vector<uint8_t> big(bt::kBigintDecimalMaxBytes);
    for (auto& x : big) x = static_cast<uint8_t>(rng());
    big[0] = 0x5a;  // positive, minimal
    const auto start = std::chrono::steady_clock::now();
    const json j = bt::to_json(BigInt{big});
    const std::string& text = j.at("$bigint").get_ref<const std::string&>();
    EXPECT_EQ(text.find('x'), std::string::npos) << "40,000 bytes is still decimal";
    EXPECT_GT(text.size(), 96000u);
    EXPECT_EQ(std::get<BigInt>(bt::from_json(j)).bytes, big);
    const double elapsed = seconds_since(start);
    // Linear-ish in practice (well under a second even with sanitizers); the old
    // byte-at-a-time code took minutes here.
    EXPECT_LT(elapsed, 30.0);

    big.insert(big.begin(), 0x01);  // one byte more: hex
    big[1] = 0x00;
    const json h = bt::to_json(BigInt{big});
    const std::string& hex = h.at("$bigint").get_ref<const std::string&>();
    ASSERT_EQ(hex.rfind("0x1", 0), 0u) << hex.substr(0, 20);
    EXPECT_EQ(std::get<BigInt>(bt::from_json(h)).bytes, big);

    std::vector<uint8_t> negative(bt::kBigintDecimalMaxBytes + 10, 0x80);
    EXPECT_EQ(bt::to_json(BigInt{negative}).at("$bigint").get<std::string>().rfind("-0x", 0), 0u);
    EXPECT_EQ(std::get<BigInt>(via_text(BigInt{negative})).bytes, negative);
}

TEST(JsonMapping, DecimalBigintInputIsLimited) {
    const std::string ok(bt::kBigintDecimalMaxDigits, '9');
    const auto start = std::chrono::steady_clock::now();
    EXPECT_NO_THROW(bt::bigint_from_string(ok));
    EXPECT_NO_THROW(bt::bigint_from_string("-" + ok));
    EXPECT_LT(seconds_since(start), 30.0);
    expect_error([&] { bt::bigint_from_string(ok + "9"); }, "hex", "100,001 digits");
    // Hex has no limit and is linear.
    const std::string hex = "0x" + std::string(1000000, 'f');
    EXPECT_EQ(bt::bigint_from_string(hex).size(), 500001u);
}

// --- timestamps -------------------------------------------------------------------------

TEST(JsonMapping, TimestampFormatting) {
    EXPECT_EQ(bt::format_rfc3339(Timestamp(0)), "1970-01-01T00:00:00Z");
    EXPECT_EQ(bt::format_rfc3339(Timestamp(1704067200, 500000000, 120)), "2024-01-01T02:00:00.5+02:00");
    EXPECT_EQ(bt::format_rfc3339(Timestamp(1704067200, 1, -330)), "2023-12-31T18:30:00.000000001-05:30");
    EXPECT_EQ(bt::format_rfc3339(Timestamp(0, 0, 0)), "1970-01-01T00:00:00+00:00");
    EXPECT_EQ(bt::format_rfc3339(Timestamp(-62167219200)), "0000-01-01T00:00:00Z");
    EXPECT_EQ(bt::format_rfc3339(Timestamp(-62167219201)), "-000001-12-31T23:59:59Z");
    EXPECT_EQ(bt::format_rfc3339(Timestamp(253402300800)), "+010000-01-01T00:00:00Z");
    EXPECT_EQ(bt::format_rfc3339(Timestamp(INT64_MAX)), "+292277026596-12-04T15:30:07Z");
    EXPECT_EQ(bt::format_rfc3339(Timestamp(INT64_MIN)), "-292277022657-01-27T08:29:52Z");
    // Local time beyond int64 while the instant itself is in range.
    EXPECT_EQ(bt::format_rfc3339(Timestamp(INT64_MAX, 0, 1080)), "+292277026596-12-05T09:30:07+18:00");
    EXPECT_EQ(bt::format_rfc3339(Timestamp(INT64_MIN, 0, -1080)), "-292277022657-01-26T14:29:52-18:00");
}

// UBSan (in the sanitizer build) catches any signed overflow here.
TEST(JsonMapping, TimestampExtremesRoundTrip) {
    const int64_t seconds[] = {INT64_MIN, INT64_MIN + 1, INT64_MIN + 86399, -86400, -1, 0, 1, 86399,
                               INT64_MAX - 86400, INT64_MAX - 1, INT64_MAX};
    const uint32_t nanos[] = {0, 1, 500000000, 999999999};
    for (int64_t s : seconds) {
        for (uint32_t ns : nanos) {
            std::vector<Timestamp> stamps = {Timestamp(s, ns)};
            for (int16_t off : {-1080, -61, -1, 0, 1, 59, 1080}) stamps.push_back(Timestamp(s, ns, off));
            for (const auto& ts : stamps) {
                const std::string text = bt::format_rfc3339(ts);
                SCOPED_TRACE(text);
                const Timestamp back = bt::parse_rfc3339(text);
                EXPECT_EQ(back.seconds, ts.seconds);
                EXPECT_EQ(back.nanoseconds, ts.nanoseconds);
                EXPECT_EQ(back.has_timezone, ts.has_timezone);
                EXPECT_EQ(back.timezone_offset, ts.timezone_offset);
            }
        }
    }
}

TEST(JsonMapping, TimestampParsingIsStrict) {
    const char* rejected[] = {
        "2024-02-30T00:00:00Z",           // no such day
        "2023-02-29T00:00:00Z",           // not a leap year
        "1900-02-29T00:00:00Z",           // not a leap year
        "2024-04-31T00:00:00Z",
        "2024-13-01T00:00:00Z",
        "2024-00-01T00:00:00Z",
        "2024-01-00T00:00:00Z",
        "2024-01-01T24:00:00Z",
        "2024-01-01T23:60:00Z",
        "2024-01-01T23:59:60Z",           // leap second
        "2024-13-45T99:99:99Z",           // the review's example
        "2024-01-01T00:00:00",            // no offset
        "2024-01-01T00:00:00Zjunk",       // trailing text
        "2024-01-01T00:00:00+01:00x",
        "2024-01-01T00:00:00+01:00Z",
        "2024-01-01T00:00:00+1:00",
        "2024-01-01T00:00:00+0100",
        "2024-01-01T00:00:00+01:60",
        "2024-01-01T00:00:00+18:01",      // beyond BTOON's ±18:00
        "2024-01-01T00:00:00-24:00",
        "2024-1-01T00:00:00Z",
        "24-01-01T00:00:00Z",
        "2024-01-01 00:00:00Z",
        "2024-01-01T00:00:00.Z",
        "2024-01-01T00:00:00.1234567891Z",  // more than 9 fractional digits
        " 2024-01-01T00:00:00Z",
        "2024-01-01T00:00:00Z ",
        "+292277026596-12-04T15:30:08Z",  // one second past INT64_MAX
        "-292277022657-01-27T08:29:51Z",  // one second before INT64_MIN
        "9999999999999-01-01T00:00:00Z",  // 13-digit year
        "-9999999999999-01-01T00:00:00Z",
        "",
    };
    for (const char* text : rejected) {
        expect_error([&] { bt::parse_rfc3339(text); }, "invalid $timestamp", text);
    }
    auto utc = [](const char* text) { return bt::parse_rfc3339(text).seconds; };
    EXPECT_EQ(utc("2024-02-29T00:00:00Z"), 1709164800);
    EXPECT_EQ(utc("2000-02-29T00:00:00Z"), 951782400);
    EXPECT_EQ(utc("2024-01-01t00:00:00z"), 1704067200);
    EXPECT_EQ(utc("+002024-01-01T00:00:00Z"), 1704067200);
    EXPECT_EQ(utc("10000-01-01T00:00:00Z"), 253402300800);  // older output had no sign
    EXPECT_EQ(utc("2024-01-01T00:00:00-00:00"), 1704067200);
    EXPECT_EQ(utc("+292277026596-12-04T15:30:07Z"), INT64_MAX);
    EXPECT_EQ(utc("+292277026596-12-05T09:30:07+18:00"), INT64_MAX);
    EXPECT_EQ(utc("-292277022657-01-27T08:29:52Z"), INT64_MIN);
    const Timestamp t = bt::parse_rfc3339("2024-02-29T23:59:59.000000001-18:00");
    EXPECT_EQ(t.nanoseconds, 1u);
    EXPECT_EQ(t.timezone_offset, -1080);
    EXPECT_TRUE(t.has_timezone);
    EXPECT_FALSE(bt::parse_rfc3339("2024-01-01T00:00:00.5Z").has_timezone);
    EXPECT_EQ(bt::parse_rfc3339("2024-01-01T00:00:00.5Z").nanoseconds, 500000000u);
}

// --- other tags ---------------------------------------------------------------------------

TEST(JsonMapping, FloatTagIsStrict) {
    EXPECT_TRUE(std::isnan(std::get<Float>(parse(R"({"$float":"NaN"})"))));
    EXPECT_EQ(std::get<Float>(parse(R"({"$float":"Infinity"})")), std::numeric_limits<double>::infinity());
    EXPECT_EQ(std::get<Float>(parse(R"({"$float":"-Infinity"})")), -std::numeric_limits<double>::infinity());
    for (const char* bad : {R"({"$float":"-inf"})", R"({"$float":"inf"})", R"({"$float":"nan"})",
                            R"({"$float":"1.5"})", R"({"$float":"infinity"})", R"({"$float":1.5})",
                            R"({"$float":null})", R"({"$float":""})"}) {
        expect_rejected(bad, "$float");
    }
}

TEST(JsonMapping, ExtTypeMustBeASignedByte) {
    const auto e = std::get<Extension>(parse(R"({"$ext":[5,"AAEC"]})"));
    EXPECT_EQ(e.type, 5);
    EXPECT_EQ(e.data, (std::vector<uint8_t>{0, 1, 2}));
    EXPECT_EQ(std::get<Extension>(parse(R"({"$ext":[127,""]})")).type, 127);
    EXPECT_EQ(std::get<Extension>(parse(R"({"$ext":[-128,""]})")).type, -128);
    EXPECT_EQ(std::get<Extension>(parse(R"({"$ext":[-7,""]})")).type, -7);  // reserved, generic
    for (const char* bad : {R"({"$ext":[128,"AA=="]})", R"({"$ext":[300,"AA=="]})", R"({"$ext":[-129,"AA=="]})",
                            R"({"$ext":[1.0,"AA=="]})", R"({"$ext":[1e300,"AA=="]})", R"({"$ext":["5","AA=="]})",
                            R"({"$ext":[18446744073709551615,"AA=="]})"}) {
        expect_rejected(bad, "-128..127");
    }
    for (const char* bad : {R"({"$ext":[5]})", R"({"$ext":[5,"AA==",1]})", R"({"$ext":{"type":5}})", R"({"$ext":[5,7]})"}) {
        expect_rejected(bad, "$ext must be");
    }
    // BTOON's own ids would write malformed or non-canonical documents (SPEC §5.7).
    for (int id : {-1, -2, -3, -4, -5, -6, -10}) {
        expect_rejected(R"({"$ext":[)" + std::to_string(id) + R"(,"AAAA"]})", "is a BTOON type");
    }
}

TEST(JsonMapping, DateAndDatetimeAreStrict) {
    EXPECT_EQ(std::get<Date>(parse(R"({"$date":9223372036854775807})")).milliseconds, INT64_MAX);
    EXPECT_EQ(std::get<Date>(parse(R"({"$date":-9223372036854775808})")).milliseconds, INT64_MIN);
    expect_rejected(R"({"$date":9223372036854775808})", "$date");
    expect_rejected(R"({"$date":1e30})", "$date");
    expect_rejected(R"({"$date":1.0})", "$date");
    expect_rejected(R"({"$date":"5"})", "$date");
    EXPECT_EQ(std::get<DateTime>(parse(R"({"$datetime":"-9223372036854775808"})")).nanoseconds, INT64_MIN);
    EXPECT_EQ(std::get<DateTime>(parse(R"({"$datetime":"9223372036854775807"})")).nanoseconds, INT64_MAX);
    for (const char* bad : {R"({"$datetime":"12abc"})", R"({"$datetime":"9223372036854775808"})",
                            R"({"$datetime":"-9223372036854775809"})", R"({"$datetime":" 1"})",
                            R"({"$datetime":"+1"})", R"({"$datetime":""})", R"({"$datetime":12})"}) {
        expect_rejected(bad, "$datetime");
    }
}

TEST(JsonMapping, Base64IsStrict) {
    EXPECT_EQ(bt::base64_decode("AAAA"), (std::vector<uint8_t>{0, 0, 0}));
    EXPECT_EQ(bt::base64_decode("AAA="), (std::vector<uint8_t>{0, 0}));
    EXPECT_EQ(bt::base64_decode("AA=="), std::vector<uint8_t>{0});
    EXPECT_EQ(bt::base64_decode("AA"), std::vector<uint8_t>{0});  // padding is optional
    EXPECT_EQ(bt::base64_decode("-_8"), (std::vector<uint8_t>{0xfb, 0xff}));  // URL-safe alphabet
    EXPECT_EQ(bt::base64_decode(""), std::vector<uint8_t>{});
    for (const char* bad : {"AB=CD", "A", "AB==", "AAB=", "AA=", "====", "A===", "AAAA=", "AA==AA==", "AA AA", "AA\n"}) {
        expect_error([&] { bt::base64_decode(bad); }, "invalid base64", bad);
    }
    expect_rejected(R"({"$binary":"AB=CD"})", "invalid base64");
}

TEST(JsonMapping, TagShapedMapsRoundTrip) {
    const Value maps[] = {
        Map{{"$binary", str("AAAA")}},
        Map{{"$map", Value(int64_t(1))}},
        Map{{"$float", str("NaN")}},
        Map{{"$foo", Map{{"$bar", Map{{"$timestamp", str("x")}}}}}},
        Map{{"$", Value(nullptr)}},
        Map{{"$a", Value(int64_t(1))}, {"$b", Value(int64_t(2))}},
    };
    for (const auto& m : maps) {
        SCOPED_TRACE(dump(m));
        EXPECT_EQ(via_text(m), m);
    }
    EXPECT_EQ(dump(maps[0]), R"({"$map":{"$binary":"AAAA"}})");
    EXPECT_EQ(dump(maps[5]), R"({"$a":1,"$b":2})");  // two keys: not a tag, not escaped
    EXPECT_EQ(parse(R"({"$unknown":1})"), (Value(Map{{"$unknown", Value(int64_t(1))}})));
    // Inside $map the keys are plain, but values are still converted.
    EXPECT_EQ(parse(R"({"$map":{"$binary":{"$binary":"AAEC"}}})"),
              (Value(Map{{"$binary", Value(Binary{0, 1, 2})}})));
    expect_rejected(R"({"$map":5})", "$map");
    expect_rejected(R"({"$map":[1]})", "$map");
    // Without tags everything is a plain map.
    EXPECT_EQ(parse(R"({"$map":{"a":1}})", false), (Value(Map{{"$map", Map{{"a", Value(int64_t(1))}}}})));
}

TEST(JsonMapping, VectorsKeepEveryBit) {
    auto f32 = [](uint32_t bits) { return std::bit_cast<float>(bits); };
    auto f64 = [](uint64_t bits) { return std::bit_cast<double>(bits); };
    const VectorFloat plain{{1.5f, -0.0f, f32(0x7fc00000), std::numeric_limits<float>::infinity(),
                             -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::max(),
                             std::numeric_limits<float>::denorm_min(), 0.1f}};
    const VectorFloat payloads{{f32(0x7fc00001), f32(0xffc00000), f32(0x7f800001), 1.0f}};
    const VectorDouble plain64{{2.5, -0.0, f64(0x7ff8000000000000), std::numeric_limits<double>::infinity(),
                                std::numeric_limits<double>::denorm_min()}};
    const VectorDouble payloads64{{f64(0x7ff8000000000001), f64(0xfff8000000000000)}};
    EXPECT_TRUE(bt::to_json(plain).at("$f32").is_array());
    EXPECT_TRUE(bt::to_json(payloads).at("$f32").is_string());
    EXPECT_TRUE(bt::to_json(plain64).at("$f64").is_array());
    EXPECT_TRUE(bt::to_json(payloads64).at("$f64").is_string());
    for (const Value& v : {Value(plain), Value(payloads), Value(plain64), Value(payloads64)}) {
        SCOPED_TRACE(dump(v));
        EXPECT_EQ(hex_of(encode(via_text(v))), hex_of(encode(v)));  // bit-exact, NaN payloads included
    }
    // The review's example: d6fb0100807f (a float32 NaN with payload 1).
    const auto doc = vtest::unhex("d6fb0100807f");
    EXPECT_EQ(hex_of(encode(via_text(decode(doc)))), "d6fb0100807f");

    EXPECT_EQ(std::get<VectorFloat>(parse(R"({"$f32":[3.4028235e38]})")).data[0], std::numeric_limits<float>::max());
    EXPECT_EQ(std::get<VectorFloat>(parse(R"({"$f32":[1, -2, 0.5]})")).data, (std::vector<float>{1.0f, -2.0f, 0.5f}));
    EXPECT_EQ(std::get<VectorFloat>(parse(R"({"$f32":"AACAPw=="})")).data, std::vector<float>{1.0f});
    expect_rejected(R"({"$f32":[1e300]})", "float32 range");
    expect_rejected(R"({"$f32":[3.5e38]})", "float32 range");
    expect_rejected(R"({"$f32":"AAA="})", "multiple of 4");
    expect_rejected(R"({"$f64":"AAAAAA=="})", "multiple of 8");
    expect_rejected(R"({"$f32":[true]})", "$f32 elements");
    expect_rejected(R"({"$f32":[[1]]})", "$f32 elements");
    expect_rejected(R"({"$f64":[{"$float":"nan"}]})", "$float");
    expect_rejected(R"({"$f64":7})", "$f64 must be");
}

// --- depth ---------------------------------------------------------------------------------

TEST(JsonMapping, DeepJsonIsAnErrorNotACrash) {
    for (size_t n : {size_t(129), size_t(300), size_t(100000)}) {
        const std::string text = std::string(n, '[') + std::string(n, ']');
        try {
            parse(text);
            ADD_FAILURE() << n << " levels accepted";
        } catch (const BtoonException& e) {
            EXPECT_EQ(e.code(), ErrorCode::max_depth) << e.what();
        }
    }
    std::string objects;
    for (int i = 0; i < 100000; ++i) objects += R"({"a":)";
    objects += "1" + std::string(100000, '}');
    EXPECT_THROW(parse(objects), BtoonException);
}

TEST(JsonMapping, DepthLimitMatchesTheEncoder) {
    // 128 containers deep is what encode()/decode() accept; 129 isn't.
    const std::string ok = std::string(128, '[') + std::string(128, ']');
    const Value v = parse(ok);
    EXPECT_NO_THROW(encode(v));
    EXPECT_THROW(bt::to_json(nested_arrays(129, Value(nullptr))), BtoonException);
    EXPECT_THROW(encode(nested_arrays(129, Value(nullptr))), BtoonException);

    // Tag objects and $map wrappers don't count, so documents at the limit round-trip.
    Value leaf = Map{{"$k", VectorFloat{{std::bit_cast<float>(0x7fc00000u)}}}};
    for (int i = 0; i < 127; ++i) leaf = Array{std::move(leaf)};
    EXPECT_NO_THROW(encode(leaf));
    EXPECT_EQ(hex_of(encode(via_text(leaf, 2))), hex_of(encode(leaf)));

    Value maps = Value(int64_t(1));
    for (int i = 0; i < 128; ++i) maps = Map{{"$k", std::move(maps)}};  // 256 JSON levels
    EXPECT_NO_THROW(encode(maps));
    EXPECT_EQ(via_text(maps), maps);
    EXPECT_THROW(bt::to_json(Map{{"$k", maps}}), BtoonException);
}

// --- parsing ---------------------------------------------------------------------------------

TEST(JsonMapping, ParserIsStrict) {
    for (const char* bad : {"", "1 2", "{} x", "[1,]", "{\"a\":1,}", "// c\n1", "/* c */ 1", "NaN", "'a'",
                            "\"\xff\"", "{\"a\" 1}", "01"}) {
        expect_error([&] { bt::parse_json(bad); }, "invalid JSON", std::string("JSON '") + bad + "'");
    }
    EXPECT_EQ(parse("\xEF\xBB\xBF[1]"), (Value(Array{Value(int64_t(1))})));  // a UTF-8 BOM is fine
    EXPECT_EQ(parse(" \t\r\n{\"b\":2,\"a\":[]} \n"),
              (Value(Map{{"a", Array{}}, {"b", Value(int64_t(2))}})));
}

// Inserting into ordered_json is linear per key, which made wide objects quadratic
// (40,000 keys took 51 s in the sanitizer build).
TEST(JsonMapping, WideObjectsParseQuickly) {
    std::string text = "{";
    for (int i = 0; i < 100000; ++i) text += (i ? ",\"k" : "\"k") + std::to_string(i) + "\":" + std::to_string(i);
    text += "}";
    const auto start = std::chrono::steady_clock::now();
    const Value v = parse(text);
    EXPECT_EQ(std::get<Map>(v).size(), 100000u);
    EXPECT_LT(seconds_since(start), 30.0);
}

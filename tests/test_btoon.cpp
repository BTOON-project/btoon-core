#include <gtest/gtest.h>
#include "btoon/btoon.h"

using namespace btoon;

// --- Test Fixture for Round-trip Tests ---
class BtoonRoundTripTest : public ::testing::TestWithParam<Value> {};

TEST_P(BtoonRoundTripTest, EncodesAndDecodesCorrectly) {
    Value original_value = GetParam();
    
    std::vector<uint8_t> encoded_data;
    ASSERT_NO_THROW(encoded_data = encode(original_value));
    
    Value decoded_value;
    ASSERT_NO_THROW(decoded_value = decode(encoded_data));

    // GTest doesn't have a default way to compare std::variant,
    // so we rely on the overloaded operator== for btoon::Value's underlying variant.
    EXPECT_EQ(original_value, decoded_value);
}

// --- Test Cases ---

INSTANTIATE_TEST_SUITE_P(
    BasicTypes,
    BtoonRoundTripTest,
    ::testing::Values(
        Value(Nil()),
        Value(Bool(true)),
        Value(Bool(false)),
        Value(Int(0)),
        Value(Int(42)),
        Value(Int(-1)),
        Value(Int(-12345)),
        Value(Int(1000000)),
        Value(Uint(0)),
        Value(Uint(255)),
        Value(Uint(65535)),
        Value(Uint(4294967295)),
        Value(Float(0.0)),
        Value(Float(3.14159)),
        Value(Float(-1.2e34)),
        Value(String("")),
        Value(String("Hello, world!")),
        Value(String("😊")),
        Value(Timestamp{1234567890}),
        Value(Date{9876543210}),
        Value(BigInt{{0x12, 0x34, 0x56, 0x78}}),
        Value(StringView("hello"))
    )
);

INSTANTIATE_TEST_SUITE_P(
    CompositeTypes,
    BtoonRoundTripTest,
    ::testing::Values(
        // Empty Array
        Value(Array{}),
        // Simple Array
        Value(Array{Int(1), Int(2), Int(3)}),
        // Mixed Type Array
        Value(Array{Int(1), String("two"), Bool(true), Nil()}),
        // Empty Map
        Value(Map{}),
        // Simple Map
        Value(Map{{"a", Int(1)}, {"b", String("two")}}),
        // Nested Structure
        Value(Map{
            {"user", String("test")},
            {"permissions", Array{String("read"), String("write")}},
            {"metadata", Map{
                {"level", Int(4)},
                {"active", Bool(true)}
            }}
        })
    )
);

TEST(BtoonVersionTest, VersionStringIsValid) {

    const char* version_str = version();

    ASSERT_NE(version_str, nullptr);

    // Expect format like "x.y.z"

    EXPECT_GT(strlen(version_str), 4); 

}



// --- Test Fixture for Compression Tests ---

struct CompressionTestParam {

    CompressionAlgorithm algorithm;

    std::string name;

};



class BtoonCompressionTest : public ::testing::TestWithParam<CompressionTestParam> {};



TEST_P(BtoonCompressionTest, RoundTrip) {
    auto param = GetParam();
    Value original = Map{
        {"a", Int(1)},
        {"b", Array{String("x"), String("y"), String(std::string(1024, 'z'))}}
    };

    EncodeOptions encode_opts;
    encode_opts.compress = true;
    encode_opts.compression_algorithm = param.algorithm;

    std::vector<uint8_t> compressed_data;
    ASSERT_NO_THROW(compressed_data = encode(original, encode_opts));

    DecodeOptions decode_opts;
    decode_opts.decompress = true;

    Value decoded;
    ASSERT_NO_THROW(decoded = decode(compressed_data, decode_opts));

    EXPECT_EQ(original, decoded);
}



// --- Instantiate Compression Tests ---

std::vector<CompressionTestParam> GetCompressionTestParams() {

    std::vector<CompressionTestParam> params;

    params.push_back({CompressionAlgorithm::ZLIB, "ZLIB"});

#ifdef BTOON_WITH_LZ4

    params.push_back({CompressionAlgorithm::LZ4, "LZ4"});

#endif

#ifdef BTOON_WITH_ZSTD

    params.push_back({CompressionAlgorithm::ZSTD, "ZSTD"});

#endif

    return params;

}



INSTANTIATE_TEST_SUITE_P(

    CompressionAlgorithms,

    BtoonCompressionTest,

    ::testing::ValuesIn(GetCompressionTestParams()),

    [](const ::testing::TestParamInfo<CompressionTestParam>& info) {

        return info.param.name;

    }

);

#include <gtest/gtest.h>
#include "btoon/btoon.h"

using namespace btoon;

TEST(EncoderTest, EncodeNil) {
    Value v = Nil();
    auto encoded = encode(v);
    ASSERT_EQ(encoded.size(), 1);
    EXPECT_EQ(encoded[0], 0xc0);
}

TEST(EncoderTest, EncodeBool) {
    Value v_true = Bool(true);
    auto encoded_true = encode(v_true);
    ASSERT_EQ(encoded_true.size(), 1);
    EXPECT_EQ(encoded_true[0], 0xc3);
    
    Value v_false = Bool(false);
    auto encoded_false = encode(v_false);
    ASSERT_EQ(encoded_false.size(), 1);
    EXPECT_EQ(encoded_false[0], 0xc2);
}

TEST(EncoderTest, EncodeInts) {
    // Small positive integer (fixint)
    Value v_pos = Uint(42);
    auto enc_pos = encode(v_pos);
    ASSERT_EQ(enc_pos.size(), 1);
    EXPECT_EQ(enc_pos[0], 0x2a);
    
    // Negative integer (fixint)
    Value v_neg = Int(-15);
    auto enc_neg = encode(v_neg);
    ASSERT_EQ(enc_neg.size(), 1);
    EXPECT_EQ(enc_neg[0], 0xf1);
    
    // Large integer (int32)
    Value v_large = Int(1000000);
    auto enc_large = encode(v_large);
    ASSERT_EQ(enc_large.size(), 5);
    EXPECT_EQ(enc_large[0], 0xd2);
}

TEST(EncoderTest, EncodeString) {
    Value v = String("Hello");
    auto encoded = encode(v);
    std::vector<uint8_t> expected = {0xa5, 'H', 'e', 'l', 'l', 'o'};
    EXPECT_EQ(encoded, expected);
}

TEST(EncoderTest, EncodeBinary) {
    Value v = Binary{0x01, 0x02, 0x03};
    auto encoded = encode(v);
    std::vector<uint8_t> expected = {0xc4, 0x03, 0x01, 0x02, 0x03};
    EXPECT_EQ(encoded, expected);
}

TEST(EncoderTest, EncodeArray) {
    Value v = Array{Int(1), Bool(true), String("go")};
    auto encoded = encode(v);
    std::vector<uint8_t> expected = {0x93, 0x01, 0xc3, 0xa2, 'g', 'o'};
    EXPECT_EQ(encoded, expected);
}

TEST(EncoderTest, EncodeMap) {
    Value v = Map{
        {"name", String("Alice")},
        {"age", Uint(30)}
    };
    auto encoded = encode(v);
    // Note: Map encoding is not guaranteed to be ordered in MessagePack,
    // but our implementation happens to be ordered alphabetically.
    // A more robust test would decode and check values.
    std::vector<uint8_t> expected = {
        0x82, 
        0xa3, 'a', 'g', 'e', 0x1e,
        0xa4, 'n', 'a', 'm', 'e', 0xa5, 'A', 'l', 'i', 'c', 'e'
    };
    EXPECT_EQ(encoded, expected);
}

TEST(EncoderTest, RoundTrip) {
    Value original = Map{
        {"a", Int(1)},
        {"b", Array{String("x"), String("y")}}
    };
    
    auto encoded = encode(original);
    auto decoded = decode(encoded);

    auto* map = std::get_if<Map>(&decoded);
    ASSERT_NE(map, nullptr);
    
    // Check for key "a", allowing for Int/Uint flexibility
    Value& a_val_variant = (*map)["a"];
    bool a_ok = false;
    if (auto* i = std::get_if<Int>(&a_val_variant)) {
        EXPECT_EQ(*i, 1);
        a_ok = true;
    } else if (auto* u = std::get_if<Uint>(&a_val_variant)) {
        EXPECT_EQ(*u, 1);
        a_ok = true;
    }
    ASSERT_TRUE(a_ok) << "Value for key 'a' was not the correct integer value.";

    auto* b_val = std::get_if<Array>(&(*map)["b"]);
    ASSERT_NE(b_val, nullptr);
    ASSERT_EQ(b_val->size(), 2);
    EXPECT_EQ(std::get<String>((*b_val)[0]), "x");
}

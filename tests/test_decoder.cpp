#include <gtest/gtest.h>
#include "btoon/btoon.h"

using namespace btoon;

TEST(DecoderTest, DecodeNil) {
    std::vector<uint8_t> data = {0xc0};
    Value decoded = decode(data);
    ASSERT_TRUE(std::holds_alternative<Nil>(decoded));
}

TEST(DecoderTest, DecodeBool) {
    std::vector<uint8_t> data_true = {0xc3};
    Value decoded_true = decode(data_true);
    ASSERT_TRUE(std::holds_alternative<Bool>(decoded_true));
    EXPECT_EQ(std::get<Bool>(decoded_true), true);
    
    std::vector<uint8_t> data_false = {0xc2};
    Value decoded_false = decode(data_false);
    ASSERT_TRUE(std::holds_alternative<Bool>(decoded_false));
    EXPECT_EQ(std::get<Bool>(decoded_false), false);
}

TEST(DecoderTest, DecodePositiveFixint) {
    std::vector<uint8_t> data = {0x2a}; // 42
    Value decoded = decode(data);
    ASSERT_TRUE(std::holds_alternative<Uint>(decoded));
    EXPECT_EQ(std::get<Uint>(decoded), 42);
}

TEST(DecoderTest, DecodeNegativeFixint) {
    std::vector<uint8_t> data = {0xf4}; // -12
    Value decoded = decode(data);
    ASSERT_TRUE(std::holds_alternative<Int>(decoded));
    EXPECT_EQ(std::get<Int>(decoded), -12);
}

TEST(DecoderTest, DecodeString) {
    std::vector<uint8_t> data = {0xad, 'H', 'e', 'l', 'l', 'o', ',', ' ', 
                                  'B', 'T', 'O', 'O', 'N', '!'};
    Value decoded = decode(data);
    ASSERT_TRUE(std::holds_alternative<String>(decoded));
    auto* str = std::get_if<String>(&decoded);
    ASSERT_NE(str, nullptr);
    EXPECT_EQ(*str, "Hello, BTOON!");
}

TEST(DecoderTest, DecodeArray) {
    std::vector<uint8_t> data = {0x93, 0x01, 0x02, 0x03};
    Value decoded = decode(data);
    auto* arr = std::get_if<Array>(&decoded);
    ASSERT_NE(arr, nullptr);
    ASSERT_EQ(arr->size(), 3);
    EXPECT_EQ(std::get<Uint>((*arr)[0]), 1);
    EXPECT_EQ(std::get<Uint>((*arr)[1]), 2);
    EXPECT_EQ(std::get<Uint>((*arr)[2]), 3);
}

TEST(DecoderTest, DecodeMap) {
    std::vector<uint8_t> data = {
        0x82, // fixmap 2
        0xa4, 'n', 'a', 'm', 'e', // "name"
        0xa5, 'A', 'l', 'i', 'c', 'e', // "Alice"
        0xa3, 'a', 'g', 'e', // "age"
        0x1e // 30
    };
    Value decoded = decode(data);
    auto* map = std::get_if<Map>(&decoded);
    ASSERT_NE(map, nullptr);
    ASSERT_EQ(map->size(), 2);
    
    auto& name_val = (*map)["name"];
    auto* str = std::get_if<String>(&name_val);
    ASSERT_NE(str, nullptr);
    EXPECT_EQ(*str, "Alice");

    EXPECT_EQ(std::get<Uint>((*map)["age"]), 30);
}

TEST(DecoderTest, InvalidBuffer) {
    std::vector<uint8_t> empty;
    EXPECT_THROW(decode(empty), BtoonException);
}

TEST(DecoderTest, BoundsChecking) {
    // str16 with length 16, but only 2 bytes of data follow
    std::vector<uint8_t> data = {0xda, 0x00, 0x10, 'h', 'i'}; 
    EXPECT_THROW(decode(data), BtoonException);
}

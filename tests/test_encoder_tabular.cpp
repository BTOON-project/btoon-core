#include <gtest/gtest.h>
#include "btoon/btoon.h"

using namespace btoon;

TEST(EncoderTest, EncodeTabular) {
    Value v = Array{
        Map{{"a", Int(1)}, {"b", String("x")}},
        Map{{"a", Int(2)}, {"b", String("y")}}
    };
    auto encoded = encode(v);
    
    auto decoded = decode(encoded);
    ASSERT_TRUE(std::holds_alternative<Array>(decoded));
    auto& arr = std::get<Array>(decoded);
    ASSERT_EQ(arr.size(), 2);
    
    auto* row1 = std::get_if<Map>(&arr[0]);
    ASSERT_NE(row1, nullptr);
    EXPECT_EQ(std::get<Int>((*row1)["a"]), 1);
    EXPECT_EQ(std::get<String>((*row1)["b"]), "x");

    auto* row2 = std::get_if<Map>(&arr[1]);
    ASSERT_NE(row2, nullptr);
    EXPECT_EQ(std::get<Int>((*row2)["a"]), 2);
    EXPECT_EQ(std::get<String>((*row2)["b"]), "y");
}

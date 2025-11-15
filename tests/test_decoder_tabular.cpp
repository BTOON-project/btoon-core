
#include <gtest/gtest.h>
#include "btoon/btoon.h"

using namespace btoon;

TEST(DecoderTest, DecodeTabular) {
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
    
    // Check what type "a" actually is
    auto& val_a = (*row1)["a"];
    if (std::holds_alternative<Int>(val_a)) {
        EXPECT_EQ(std::get<Int>(val_a), 1);
    } else if (std::holds_alternative<Uint>(val_a)) {
        EXPECT_EQ(std::get<Uint>(val_a), 1);
    } else {
        ADD_FAILURE() << "Unexpected type for column 'a' in row 1";
    }
    
    // Check what type "b" actually is
    auto& val_b = (*row1)["b"];
    if (std::holds_alternative<String>(val_b)) {
        EXPECT_EQ(std::get<String>(val_b), "x");
    } else {
        ADD_FAILURE() << "Unexpected type for column 'b' in row 1";
    }

    auto* row2 = std::get_if<Map>(&arr[1]);
    ASSERT_NE(row2, nullptr);
    
    // Check what type "a" actually is in row 2
    auto& val2_a = (*row2)["a"];
    if (std::holds_alternative<Int>(val2_a)) {
        EXPECT_EQ(std::get<Int>(val2_a), 2);
    } else if (std::holds_alternative<Uint>(val2_a)) {
        EXPECT_EQ(std::get<Uint>(val2_a), 2);
    } else {
        ADD_FAILURE() << "Unexpected type for column 'a' in row 2";
    }
    
    // Check what type "b" actually is in row 2
    auto& val2_b = (*row2)["b"];
    if (std::holds_alternative<String>(val2_b)) {
        EXPECT_EQ(std::get<String>(val2_b), "y");
    } else {
        ADD_FAILURE() << "Unexpected type for column 'b' in row 2";
    }
}

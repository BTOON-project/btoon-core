#include <gtest/gtest.h>
#include "btoon/capi.h"

TEST(CAPITest, Version) {
    const char* version = btoon_version();
    ASSERT_NE(version, nullptr);
    EXPECT_GT(strlen(version), 4);
}

TEST(CAPITest, NilRoundTrip) {
    btoon_value_t* original = btoon_value_create_nil();
    ASSERT_NE(original, nullptr);

    btoon_encode_options_t options = {false, BTOON_COMPRESSION_ZLIB, 0, false};
    btoon_result_t result = btoon_encode(original, &options);
    ASSERT_EQ(result.error, nullptr);
    ASSERT_NE(result.data, nullptr);

    char* error = nullptr;
    btoon_decode_options_t decode_options = {false, true};
    btoon_value_t* decoded = btoon_decode(result.data, result.size, &decode_options, &error);
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

    btoon_encode_options_t options = {false, BTOON_COMPRESSION_ZLIB, 0, false};
    btoon_result_t result = btoon_encode(original, &options);
    ASSERT_EQ(result.error, nullptr);

    char* error = nullptr;
    btoon_decode_options_t decode_options = {false, true};
    btoon_value_t* decoded = btoon_decode(result.data, result.size, &decode_options, &error);
    ASSERT_EQ(error, nullptr);
    ASSERT_NE(decoded, nullptr);

    ASSERT_EQ(decoded->type, BTOON_TYPE_STRING);
    EXPECT_EQ(decoded->as.s.len, strlen(test_str));
    EXPECT_STREQ(decoded->as.s.ptr, test_str);

    btoon_value_destroy(original);
    btoon_value_destroy(decoded);
    btoon_free_result(result);
}

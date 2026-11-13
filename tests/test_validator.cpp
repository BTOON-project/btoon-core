#include <gtest/gtest.h>
#include "btoon/validator.h"
#include "btoon/encoder.h"
#include "btoon/decoder.h"
#include <vector>
#include <string>

using namespace btoon;

class ValidatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create various test data
        valid_int = encode(Int(42));
        valid_string = encode(String("Hello, BTOON!"));
        valid_array = encode(Array{Int(1), Int(2), Int(3)});
        valid_map = encode(Map{{"key", String("value")}, {"number", Int(123)}});
        
        // Create invalid data
        truncated_data = {0xda, 0x00, 0x10};  // str16 with size but no data
        invalid_utf8 = {0xa4, 0xFF, 0xFF, 0xFF, 0xFF};  // fixstr with invalid UTF-8
        excessive_depth = createDeeplyNested(200);
        huge_string_claim = {0xdb, 0x7F, 0xFF, 0xFF, 0xFF, 'a'};  // str32 claiming 2GB
        
        // Circular reference (simulated)
        circular_ref = {0x91, 0x91, 0x91};  // Incomplete nested arrays
    }
    
    std::vector<uint8_t> createDeeplyNested(size_t depth) {
        std::vector<uint8_t> data;
        // Create deeply nested arrays
        for (size_t i = 0; i < depth; i++) {
            data.push_back(0x91);  // fixarray with 1 element
        }
        data.push_back(0xc0);  // nil at the bottom
        return data;
    }
    
    std::vector<uint8_t> valid_int;
    std::vector<uint8_t> valid_string;
    std::vector<uint8_t> valid_array;
    std::vector<uint8_t> valid_map;
    std::vector<uint8_t> truncated_data;
    std::vector<uint8_t> invalid_utf8;
    std::vector<uint8_t> excessive_depth;
    std::vector<uint8_t> huge_string_claim;
    std::vector<uint8_t> circular_ref;
};

TEST_F(ValidatorTest, ValidateValidData) {
    Validator validator;
    
    EXPECT_TRUE(validator.quickCheck(valid_int));
    EXPECT_TRUE(validator.quickCheck(valid_string));
    EXPECT_TRUE(validator.quickCheck(valid_array));
    EXPECT_TRUE(validator.quickCheck(valid_map));
    
    auto result = validator.validate(std::span<const uint8_t>(valid_int));
    EXPECT_TRUE(result.valid);
    EXPECT_TRUE(result.errors.empty());
    
    result = validator.validate(std::span<const uint8_t>(valid_string));
    EXPECT_TRUE(result.valid);
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(ValidatorTest, ValidateTruncatedData) {
    Validator validator;
    
    auto result = validator.validate(std::span<const uint8_t>(truncated_data));
    EXPECT_FALSE(result.valid);
    EXPECT_FALSE(result.errors.empty());
}

TEST_F(ValidatorTest, ValidateInvalidUTF8) {
    ValidationOptions opts;
    opts.require_utf8_strings = true;
    
    Validator validator(opts);
    auto result = validator.validate(std::span<const uint8_t>(invalid_utf8));
    EXPECT_FALSE(result.valid);
    // Should detect invalid UTF-8
}

TEST_F(ValidatorTest, ValidateExcessiveDepth) {
    ValidationOptions opts;
    opts.max_depth = 100;
    
    Validator validator(opts);
    auto result = validator.validate(std::span<const uint8_t>(excessive_depth));
    EXPECT_FALSE(result.valid);
    EXPECT_FALSE(result.errors.empty());
    // Should detect excessive nesting
}

TEST_F(ValidatorTest, ValidateHugeStringClaim) {
    Validator validator;
    
    auto result = validator.validate(std::span<const uint8_t>(huge_string_claim));
    EXPECT_FALSE(result.valid);
    // Should detect unreasonable size claim
}

TEST_F(ValidatorTest, ValidateWithSizeLimits) {
    ValidationOptions opts;
    opts.max_string_length = 10;
    opts.max_array_size = 2;
    
    Validator validator(opts);
    
    // String exceeding limit
    auto long_string = encode(String("This is a very long string that exceeds the limit"));
    auto result = validator.validate(std::span<const uint8_t>(long_string));
    EXPECT_FALSE(result.valid);
    
    // Array exceeding limit
    auto large_array = encode(Array{Int(1), Int(2), Int(3), Int(4), Int(5)});
    result = validator.validate(std::span<const uint8_t>(large_array));
    EXPECT_FALSE(result.valid);
}

TEST_F(ValidatorTest, BoundsChecker) {
    std::vector<uint8_t> data = {0x01, 0x02, 0x03, 0x04, 0x05};
    BoundsChecker checker(data);
    
    size_t pos = 0;
    
    EXPECT_TRUE(checker.canRead(pos, 5));
    EXPECT_FALSE(checker.canRead(pos, 6));
    
    auto byte = checker.readByte(pos);
    EXPECT_TRUE(byte.has_value());
    EXPECT_EQ(*byte, 0x01);
    EXPECT_EQ(pos, 1u);
    
    auto bytes = checker.readBytes(pos, 2);
    EXPECT_TRUE(bytes.has_value());
    EXPECT_EQ(bytes->size(), 2u);
    EXPECT_EQ(pos, 3u);
    
    EXPECT_EQ(checker.remaining(pos), 2u);
    EXPECT_TRUE(checker.inBounds(pos));
    
    pos = 10;
    EXPECT_FALSE(checker.inBounds(pos));
    EXPECT_EQ(checker.remaining(pos), 0u);
}

TEST_F(ValidatorTest, UTF8Validation) {
    // Valid UTF-8
    EXPECT_TRUE(UTF8Validator::isValid("Hello"));
    EXPECT_TRUE(UTF8Validator::isValid("Hello, 世界"));
    EXPECT_TRUE(UTF8Validator::isValid("😀🎉"));
    
    // Invalid UTF-8
    EXPECT_FALSE(UTF8Validator::isValid("\xFF\xFE"));
    EXPECT_FALSE(UTF8Validator::isValid("\xC0\x80"));  // Overlong encoding
    EXPECT_FALSE(UTF8Validator::isValid("\xED\xA0\x80"));  // Surrogate
    
    // Sanitization
    std::string sanitized = UTF8Validator::sanitize("Hello\xFF\xFEWorld");
    EXPECT_TRUE(UTF8Validator::isValid(sanitized));
    EXPECT_NE(sanitized.find("Hello"), std::string::npos);
    EXPECT_NE(sanitized.find("World"), std::string::npos);
}

TEST_F(ValidatorTest, TypeValidation) {
    // Integer validation
    EXPECT_TRUE(TypeValidator::validateInt(100, 0, 200));
    EXPECT_FALSE(TypeValidator::validateInt(300, 0, 200));
    
    // Float validation
    EXPECT_TRUE(TypeValidator::validateFloat(3.14, false, false));
    EXPECT_FALSE(TypeValidator::validateFloat(std::numeric_limits<double>::quiet_NaN(), false, false));
    EXPECT_TRUE(TypeValidator::validateFloat(std::numeric_limits<double>::quiet_NaN(), true, false));
    EXPECT_FALSE(TypeValidator::validateFloat(std::numeric_limits<double>::infinity(), false, false));
    EXPECT_TRUE(TypeValidator::validateFloat(std::numeric_limits<double>::infinity(), false, true));
    
    // Timestamp validation
    Timestamp valid_ts{1234567890};
    EXPECT_TRUE(TypeValidator::validateTimestamp(valid_ts));
    
    // docs/SPEC.md §5.1: any int64 seconds is valid, including before 1970 and after 2100.
    EXPECT_TRUE(TypeValidator::validateTimestamp(Timestamp{-1}));
    EXPECT_TRUE(TypeValidator::validateTimestamp(Timestamp{4102444801}));
    EXPECT_FALSE(TypeValidator::validateTimestamp(Timestamp{0, 1000000000}));
    EXPECT_FALSE(TypeValidator::validateTimestamp(Timestamp{0, 0, 1081}));
    EXPECT_TRUE(TypeValidator::validateTimestamp(Timestamp{0, 999999999, -1080}));
    
    // Extension validation
    Extension valid_ext{1, {0x01, 0x02}};
    EXPECT_TRUE(TypeValidator::validateExtension(valid_ext));
    
    Extension reserved_ext{-50, {0x01}};  // Reserved range
    EXPECT_TRUE(TypeValidator::validateExtension(reserved_ext));  // SPEC §5: reserved ids are generic, not invalid
}

TEST_F(ValidatorTest, SecurityChecks) {
    // Check for zip bomb
    std::vector<uint8_t> zip_bomb_header = {
        0x42, 0x54, 0x4F, 0x4E,  // Magic "BTON"
        0x01, 0x00, 0x00, 0x00,  // Version
        0x00, 0x00, 0x00, 0x10,  // Compressed size: 16
        0x00, 0x10, 0x00, 0x00   // Uncompressed size: 1MB (65536:1 ratio)
    };
    
    auto issues = SecurityValidator::checkSecurity(zip_bomb_header);
    EXPECT_FALSE(issues.empty());
    
    // Check nesting depth
    auto deep_data = createDeeplyNested(200);
    EXPECT_FALSE(SecurityValidator::checkNestingDepth(deep_data, 100));
    EXPECT_TRUE(SecurityValidator::checkNestingDepth(valid_array, 100));
    
    // Check size claims
    EXPECT_FALSE(SecurityValidator::checkSizeClaims(huge_string_claim));
    EXPECT_TRUE(SecurityValidator::checkSizeClaims(valid_string));
}

TEST_F(ValidatorTest, ValidateWithStatistics) {
    ValidationOptions opts;
    opts.collect_stats = true;
    
    Validator validator(opts);
    
    // Create test data with various types
    Value complex = Map{
        {"strings", Array{String("a"), String("bb"), String("ccc")}},
        {"numbers", Array{Int(1), Int(2), Int(3)}},
        {"nested", Map{{"inner", String("value")}}},
        {"binary", Binary{0x01, 0x02, 0x03}}
    };
    
    auto encoded = encode(complex);
    auto result = validator.validate(std::span<const uint8_t>(encoded));
    
    EXPECT_TRUE(result.valid);
    EXPECT_TRUE(result.stats.has_value());
    
    if (result.stats) {
        EXPECT_GT(result.stats->string_count, 0u);
        EXPECT_GT(result.stats->array_count, 0u);
        EXPECT_GT(result.stats->map_count, 0u);
        EXPECT_GT(result.stats->binary_count, 0u);
        EXPECT_GT(result.stats->max_depth_reached, 0u);
    }
}

TEST_F(ValidatorTest, Sanitization) {
    Validator validator;
    
    // Valid data should pass through
    auto sanitized = validator.sanitize(valid_string);
    EXPECT_FALSE(sanitized.empty());
    EXPECT_EQ(sanitized, valid_string);
    
    // Invalid data should return empty
    sanitized = validator.sanitize(truncated_data);
    EXPECT_TRUE(sanitized.empty());
    
    sanitized = validator.sanitize(huge_string_claim);
    EXPECT_TRUE(sanitized.empty());
}

TEST_F(ValidatorTest, StrictValidation) {
    // Use strict validation function
    auto result = validateStrict(valid_map);
    EXPECT_TRUE(result.valid);
    
    // Create data that exceeds strict limits
    std::string large_string(2 * 1024 * 1024, 'a');  // 2MB string
    auto large_data = encode(String(large_string));
    
    result = validateStrict(large_data);
    EXPECT_FALSE(result.valid);  // Should fail strict size limit
}

TEST_F(ValidatorTest, DuplicateMapKeys) {
    // This is hard to test with our current encoder which doesn't allow duplicates
    // But we can test the option exists
    ValidationOptions opts;
    opts.allow_duplicate_map_keys = false;
    
    Validator validator(opts);
    auto result = validator.validate(std::span<const uint8_t>(valid_map));
    EXPECT_TRUE(result.valid);
}

// Review 2026-11-13: the byte path ignored the options and couldn't validate signed or
// pre-1970 documents.
TEST(ValidatorFixes, ByteInputHonorsTheOptions) {
    const std::string key(32, 'k');
    EncodeOptions signed_opts;
    signed_opts.hmac_key = key;
    const auto signed_doc = encode(Map{{"a", Int(1)}}, signed_opts);

    EXPECT_FALSE(Validator().validate(signed_doc).valid);  // no key: authentication_failed

    ValidationOptions with_key;
    with_key.hmac_key = key;
    EXPECT_TRUE(Validator(with_key).validate(signed_doc).valid);
    EXPECT_FALSE(Validator(with_key).sanitize(signed_doc).empty());

    ValidationOptions wrong_key;
    wrong_key.hmac_key = std::string(32, 'x');
    EXPECT_FALSE(Validator(wrong_key).validate(signed_doc).valid);

    ValidationOptions empty_key;
    empty_key.hmac_key = std::string();
    EXPECT_FALSE(Validator(empty_key).validate(signed_doc).valid);  // an empty key is an error
    EXPECT_FALSE(Validator(empty_key).validate(encode(Int(1))).valid);

    // max_depth reaches the decoder: 200 nested arrays pass only when allowed.
    std::vector<uint8_t> deep(200, 0x91);
    deep.push_back(0xc0);
    EXPECT_FALSE(Validator().validate(deep).valid);
    ValidationOptions deep_ok;
    deep_ok.max_depth = 300;
    EXPECT_TRUE(Validator(deep_ok).validate(deep).valid);

    // max_total_size bounds decompression too.
    EncodeOptions zip;
    zip.compress = true;
    const auto compressed = encode(String(std::string(100000, 'a')), zip);
    ValidationOptions small;
    small.max_total_size = 50000;
    EXPECT_TRUE(Validator().validate(compressed).valid);
    EXPECT_FALSE(Validator(small).validate(compressed).valid);

    // Pre-1970 and post-2100 timestamps are valid documents.
    EXPECT_TRUE(Validator().validate(encode(Timestamp{-86400})).valid);
    EXPECT_TRUE(Validator().validate(encode(Timestamp{4200000000LL, 5})).valid);
}

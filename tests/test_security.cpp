#include <gtest/gtest.h>
#include "btoon/btoon.h"
#include "btoon/security.h"

// Note: The underlying security features (HMAC signing) are not fully
// integrated into the high-level API yet. These tests are placeholders
// and will likely fail or need adjustment once the EncodeOptions and
// DecodeOptions are implemented for security.

using namespace btoon;

class SecurityTest : public ::testing::Test {
protected:
    // This test setup is a placeholder for when security options are available.
    // For now, we can't test the high-level API's security features.
};

TEST_F(SecurityTest, SigningAndVerification) {
    // This test case demonstrates how the Security class is intended to be used.
    // It does not test the high-level encode/decode functions.
    
    Security sec;
    sec.setSecretKey("a-very-secret-key");

    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    
    std::vector<uint8_t> signature;
    ASSERT_NO_THROW(signature = sec.sign(data));
    EXPECT_FALSE(signature.empty());

    bool verified = false;
    ASSERT_NO_THROW(verified = sec.verify(data, signature));
    EXPECT_TRUE(verified);
}

TEST_F(SecurityTest, VerificationFailureOnTamperedData) {
    Security sec;
    sec.setSecretKey("a-very-secret-key");

    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    std::vector<uint8_t> signature = sec.sign(data);

    // Tamper with the data
    data[2] = 0xff;

    bool verified = true;
    ASSERT_NO_THROW(verified = sec.verify(data, signature));
    EXPECT_FALSE(verified);
}

TEST_F(SecurityTest, VerificationFailureOnWrongKey) {
    Security sec1;
    sec1.setSecretKey("key-one");
    
    Security sec2;
    sec2.setSecretKey("key-two");

    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    std::vector<uint8_t> signature = sec1.sign(data);

    bool verified = true;
    ASSERT_NO_THROW(verified = sec2.verify(data, signature));
    EXPECT_FALSE(verified);
}

TEST_F(SecurityTest, TypeRestriction) {
    Security sec;
    // Allow only String (index 5) and Int (index 2)
    sec.setAllowedTypes({5, 2});

    EXPECT_TRUE(sec.isTypeAllowed(5));  // String
    EXPECT_TRUE(sec.isTypeAllowed(2));  // Int
    EXPECT_FALSE(sec.isTypeAllowed(0)); // Nil
    EXPECT_FALSE(sec.isTypeAllowed(7)); // Array
}

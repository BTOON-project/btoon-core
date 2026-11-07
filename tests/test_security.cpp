#include <gtest/gtest.h>
#include "btoon/btoon.h"
#include "btoon/security.h"

// HMAC-SHA256 signing (docs/SPEC.md §8). End-to-end envelope tests are in
// test_hardening.cpp; known-answer tests for SHA-256/HMAC are at the bottom.

using namespace btoon;

class SecurityTest : public ::testing::Test {};

TEST_F(SecurityTest, SigningAndVerification) {
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

// --- Known-answer tests for the in-tree SHA-256 / HMAC-SHA256 -----------------

namespace {
std::string hex32(const std::array<uint8_t, 32>& d) {
    static const char* x = "0123456789abcdef";
    std::string s;
    for (uint8_t b : d) {
        s.push_back(x[b >> 4]);
        s.push_back(x[b & 15]);
    }
    return s;
}
std::span<const uint8_t> bytes_of(const std::string& s) {
    return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}
}  // namespace

TEST(Sha256, Fips180Vectors) {
    EXPECT_EQ(hex32(sha256(bytes_of(""))), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(hex32(sha256(bytes_of("abc"))), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    EXPECT_EQ(hex32(sha256(bytes_of("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"))),
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST(Sha256, PaddingBoundariesMatchHashlib) {
    // Lengths straddling the 56-byte padding boundary and a full block.
    EXPECT_EQ(hex32(sha256(bytes_of(std::string(55, 'a')))), "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318");
    EXPECT_EQ(hex32(sha256(bytes_of(std::string(56, 'a')))), "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a");
    EXPECT_EQ(hex32(sha256(bytes_of(std::string(64, 'a')))), "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb");
}

TEST(Sha256, LargeInputMatchesHashlib) {
    std::vector<uint8_t> data(1000003);
    for (size_t i = 0; i < data.size(); ++i) data[i] = static_cast<uint8_t>((i * 31 + 7) & 0xff);
    EXPECT_EQ(hex32(sha256(data)), "98a99a78c43949f17251c669c6e3ff37064482fdc65e85ef1f21b70c2e48a81b");
    std::string key(100, 'k');  // longer than the block size: key is hashed first
    EXPECT_EQ(hex32(hmac_sha256(bytes_of(key), data)), "2fb6ee200d193f6137d596c4d051f7bfea5f1e147d73648797a2c98844954324");
}

TEST(HmacSha256, Rfc4231Vectors) {
    // Test case 1
    EXPECT_EQ(hex32(hmac_sha256(bytes_of(std::string(20, '\x0b')), bytes_of("Hi There"))),
              "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
    // Test case 2
    EXPECT_EQ(hex32(hmac_sha256(bytes_of("Jefe"), bytes_of("what do ya want for nothing?"))),
              "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
    // Test case 6: 131-byte key
    EXPECT_EQ(hex32(hmac_sha256(bytes_of(std::string(131, '\xaa')),
                                bytes_of("Test Using Larger Than Block-Size Key - Hash Key First"))),
              "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");
    // Split input gives the same tag as contiguous input.
    EXPECT_EQ(hmac_sha256(bytes_of("Jefe"), bytes_of("what do ya "), bytes_of("want for nothing?")),
              hmac_sha256(bytes_of("Jefe"), bytes_of("what do ya want for nothing?")));
}

TEST(SecurityKeys, EmptyKeyIsRejected) {
    Security sec;
    std::vector<uint8_t> data{1};
    EXPECT_THROW(sec.sign(data), BtoonException);
    EXPECT_THROW(sec.wrap(data), BtoonException);
}

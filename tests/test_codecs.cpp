// Round-trip and robustness tests for the auxiliary Delta/RLE/Dictionary codecs.
#include <gtest/gtest.h>

#include <limits>

#include "btoon/delta_codec.h"
#include "btoon/dictionary_codec.h"
#include "btoon/rle_codec.h"

using namespace btoon;

TEST(Codecs, DeltaRoundTripIncludingExtremes) {
    Array in{Int(0), Int(5), Int(3), Int(127), Int(128), Int(-1),
             Int(std::numeric_limits<int64_t>::max()), Int(std::numeric_limits<int64_t>::min()), Int(42)};
    EXPECT_EQ(DeltaCodec::decode(DeltaCodec::encode(in)), in);
    EXPECT_TRUE(DeltaCodec::decode(DeltaCodec::encode({})).empty());
    EXPECT_THROW(DeltaCodec::encode({String("x")}), BtoonException);
}

TEST(Codecs, DeltaIsCompactForSequences) {
    Array seq;
    for (int i = 0; i < 1000; ++i) seq.push_back(Int(1700000000 + i));
    EXPECT_LT(DeltaCodec::encode(seq).size(), 1010u);  // ~1 byte per element
}

TEST(Codecs, RleRoundTrip) {
    Array in{Int(1), Int(1), Int(1), String("a"), String("a"), nullptr, Int(1)};
    EXPECT_EQ(RleCodec::decode(RleCodec::encode(in)), in);
    EXPECT_TRUE(RleCodec::decode(RleCodec::encode({})).empty());
}

TEST(Codecs, RleRejectsHostileRuns) {
    // value 1, run length 0 / negative / enormous
    EXPECT_THROW(RleCodec::decode({0x01, 0x00}), BtoonException);
    EXPECT_THROW(RleCodec::decode({0x01, 0xff}), BtoonException);
    EXPECT_THROW(RleCodec::decode({0x01, 0xce, 0xff, 0xff, 0xff, 0xff}), BtoonException);
    EXPECT_THROW(RleCodec::decode({0x01}), BtoonException);
}

TEST(Codecs, DictionaryRoundTrip) {
    Array in{String("red"), String("green"), String("red"), Int(7), String("red"), Int(7)};
    auto enc = DictionaryCodec::encode(in);
    EXPECT_EQ(DictionaryCodec::decode(enc), in);
}

TEST(Codecs, DictionaryRejectsOutOfRangeIndex) {
    // dictionary ["a"], then index 1 (out of range) and -1
    EXPECT_THROW(DictionaryCodec::decode({0x91, 0xa1, 'a', 0x01}), BtoonException);
    EXPECT_THROW(DictionaryCodec::decode({0x91, 0xa1, 'a', 0xff}), BtoonException);
    EXPECT_THROW(DictionaryCodec::decode({0x01, 0x00}), BtoonException);  // no dictionary
}

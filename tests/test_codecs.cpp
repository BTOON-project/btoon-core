// Round-trip and robustness tests for the auxiliary Delta/RLE/Dictionary codecs.
#include <gtest/gtest.h>

#include <limits>

#include "btoon/delta_codec.h"
#include "btoon/dictionary_codec.h"
#include "btoon/rle_codec.h"
#include "btoon/utils.h"

#include <cmath>
#include <random>
#include <zlib.h>

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

// Review 2026-11-13: element caps didn't bound memory. One run, or one-byte indices, of a
// large value expanded 100 KB into 967 MiB.
TEST(CodecFixes, RleAndDictionaryBoundDecodedMemory) {
    auto put32 = [](std::vector<uint8_t>& b, uint32_t v) {
        for (int s = 24; s >= 0; s -= 8) b.push_back(static_cast<uint8_t>(v >> s));
    };
    const uint32_t n = 100000;  // an array of 100k nils, about 5 MB once decoded
    std::vector<uint8_t> rle{0xdd};
    put32(rle, n);
    rle.insert(rle.end(), n, 0xc0);
    rle.push_back(0xce);
    put32(rle, 200);  // 200 copies: about 1 GB
    try {
        RleCodec::decode(rle);
        ADD_FAILURE() << "RLE amplification accepted";
    } catch (const BtoonException& e) {
        EXPECT_EQ(e.code(), ErrorCode::size_limit);
    }
    std::vector<uint8_t> dict{0x91, 0xdd};
    put32(dict, n);
    dict.insert(dict.end(), n, 0xc0);
    dict.insert(dict.end(), 200, 0x00);  // 200 one-byte indices, each copying the array
    try {
        DictionaryCodec::decode(dict);
        ADD_FAILURE() << "dictionary amplification accepted";
    } catch (const BtoonException& e) {
        EXPECT_EQ(e.code(), ErrorCode::size_limit);
    }
    // Ordinary inputs still decode, and a caller can raise the limit.
    Array many(10000, Value(Int(7)));
    EXPECT_EQ(RleCodec::decode(RleCodec::encode(many)), many);
    EXPECT_EQ(DictionaryCodec::decode(DictionaryCodec::encode(many)), many);
    EXPECT_NO_THROW(RleCodec::decode(rle, RleCodec::kDefaultMaxElements, size_t(2) << 30));
}

TEST(CodecFixes, RleKeepsNegativeZero) {
    Array in{Float(0.0), Float(-0.0), Float(-0.0), Float(0.0)};
    Array out = RleCodec::decode(RleCodec::encode(in));
    ASSERT_EQ(out.size(), in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        EXPECT_EQ(std::signbit(std::get<Float>(out[i])), std::signbit(std::get<Float>(in[i]))) << i;
    }
}

TEST(UtilsFixes, Crc32MatchesZlib) {
    std::mt19937 rng(7);
    for (int round = 0; round < 500; ++round) {
        std::vector<uint8_t> data(rng() % 300);
        for (auto& b : data) b = static_cast<uint8_t>(rng());
        const uLong expected = ::crc32(::crc32(0L, Z_NULL, 0), data.data(), static_cast<uInt>(data.size()));
        ASSERT_EQ(utils::crc32(data), static_cast<uint32_t>(expected)) << round;
    }
}

TEST(UtilsFixes, BufferReaderBoundsDoNotWrap) {
    std::vector<uint8_t> buf{1, 2, 3, 4};
    utils::BufferReader r(buf);
    r.readUint8();
    EXPECT_THROW(r.readBytes(SIZE_MAX), BtoonException);
    EXPECT_THROW(r.readBytes(SIZE_MAX - 1), BtoonException);
    EXPECT_THROW(r.skip(SIZE_MAX), BtoonException);
    EXPECT_EQ(r.readBytes(3).size(), 3u);
    EXPECT_THROW(r.readUint8(), BtoonException);
}

TEST(UtilsFixes, VarintRejectsOverflow) {
    std::vector<uint8_t> max_u64;
    utils::encodeVarint(UINT64_MAX, max_u64);
    size_t pos = 0;
    EXPECT_EQ(utils::decodeVarint(max_u64, pos), UINT64_MAX);
    std::vector<uint8_t> too_big(9, 0xff);
    too_big.push_back(0x02);  // bit 64
    pos = 0;
    EXPECT_THROW(utils::decodeVarint(too_big, pos), BtoonException);
    std::vector<uint8_t> eleven(10, 0xff);
    eleven.push_back(0x00);
    pos = 0;
    EXPECT_THROW(utils::decodeVarint(eleven, pos), BtoonException);
}

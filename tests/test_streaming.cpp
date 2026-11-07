#include <gtest/gtest.h>

#include <sstream>

#include "btoon/stream_decoder.h"
#include "btoon/stream_encoder.h"

using namespace btoon;

TEST(StreamingTest, RoundTrip) {
    std::stringstream ss;
    std::vector<Value> values{Int(123), String("abc"), Array{Int(1), Int(2)}, Map{{"k", nullptr}}};
    {
        StreamEncoder encoder(ss);
        for (const auto& v : values) encoder.write(v);
        encoder.close();
    }
    StreamDecoder decoder(ss);
    for (const auto& expected : values) {
        ASSERT_TRUE(decoder.has_next());
        auto got = decoder.read();
        ASSERT_TRUE(got.has_value());
        EXPECT_EQ(*got, expected);
    }
    EXPECT_FALSE(decoder.has_next());
    EXPECT_FALSE(decoder.read().has_value());
}

TEST(StreamingTest, ValuesLargerThanTheReadChunk) {
    std::stringstream ss;
    Value big = String(std::string(300 * 1024, 'z'));  // spans several 64 KiB reads
    {
        StreamEncoder encoder(ss);
        encoder.write(big);
        encoder.write(Int(7));
    }
    StreamDecoder decoder(ss);
    EXPECT_EQ(*decoder.read(), big);
    EXPECT_EQ(*decoder.read(), Value(Int(7)));
    EXPECT_FALSE(decoder.read().has_value());
}

TEST(StreamingTest, TruncatedFinalValueIsAnError) {
    std::string bytes = "\x01\x92\x01";  // 1, then an array missing an element
    std::stringstream ss(bytes);
    StreamDecoder decoder(ss);
    EXPECT_EQ(*decoder.read(), Value(Int(1)));
    EXPECT_THROW(decoder.read(), BtoonException);
}

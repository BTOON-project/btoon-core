// Robustness tests beyond the conformance vectors: truncation sweeps,
// decompression bombs, encoder limits, non-strict mode and envelope misuse.
// Run under -DENABLE_SANITIZERS=ON to turn any memory error into a failure.
#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <limits>
#include <random>

#include "btoon/btoon.h"
#include "btoon/decoder.h"
#include "btoon/encoder.h"
#include "btoon/security.h"

using namespace btoon;

namespace {

Value sample_document() {
    Array rows;
    for (int i = 0; i < 12; ++i) {
        rows.push_back(Map{{"id", Int(i)},
                           {"name", String("user" + std::to_string(i))},
                           {"score", Float(i * 1.25)},
                           {"tags", Array{String("a"), Int(-i)}}});
    }
    return Map{{"rows", rows},
               {"ts", Timestamp(1704067200, 123)},
               {"date", Date{1700000000123}},
               {"vec", VectorFloat{{1.0f, -2.0f}}},
               {"big", BigInt{{0x01, 0, 0, 0, 0, 0, 0, 0, 0}}},
               {"bin", Binary{1, 2, 3}},
               {"ext", Extension{7, {9, 9}}}};
}

ErrorCode code_of(std::span<const uint8_t> bytes, const DecodeOptions& opts = {}) {
    try {
        decode(bytes, opts);
    } catch (const BtoonException& e) {
        return e.code();
    }
    return ErrorCode::generic;  // "no error" sentinel for these tests
}

}  // namespace

TEST(Hardening, EveryTruncationIsRejected) {
    auto full = encode(sample_document());
    ASSERT_GT(full.size(), 100u);
    for (size_t n = 0; n < full.size(); ++n) {
        std::span<const uint8_t> prefix(full.data(), n);
        try {
            decode(prefix);
            ADD_FAILURE() << "prefix of length " << n << " decoded";
        } catch (const BtoonException&) {
        }
    }
}

TEST(Hardening, RandomBytesNeverCrash) {
    std::mt19937 rng(12345);
    std::uniform_int_distribution<int> len(0, 64), byte(0, 255);
    for (int iter = 0; iter < 20000; ++iter) {
        std::vector<uint8_t> buf(static_cast<size_t>(len(rng)));
        for (auto& b : buf) b = static_cast<uint8_t>(byte(rng));
        try {
            decode(buf);
        } catch (const BtoonException&) {
        }
    }
}

TEST(Hardening, ContainerLengthClaimsAreCheckedBeforeAllocating) {
    // A 5-byte input claiming 4 billion elements must fail fast, not allocate.
    const std::vector<uint8_t> arr{0xdd, 0xff, 0xff, 0xff, 0xff};
    const std::vector<uint8_t> map{0xdf, 0xff, 0xff, 0xff, 0xff};
    EXPECT_EQ(code_of(arr), ErrorCode::truncated);
    EXPECT_EQ(code_of(map), ErrorCode::truncated);
}

TEST(Hardening, DepthLimitIsConfigurable) {
    std::vector<uint8_t> deep(10, 0x91);
    deep.push_back(0xc0);
    DecodeOptions opts;
    opts.max_depth = 10;
    EXPECT_NO_THROW(decode(deep, opts));
    opts.max_depth = 9;
    EXPECT_EQ(code_of(deep, opts), ErrorCode::max_depth);
}

TEST(Hardening, VeryDeepInputFailsWithoutStackOverflow) {
    std::vector<uint8_t> deep(1'000'000, 0x91);
    deep.push_back(0xc0);
    EXPECT_EQ(code_of(deep), ErrorCode::max_depth);
}

TEST(Hardening, EncoderDepthLimit) {
    Value v = Array{};
    for (int i = 0; i < 200; ++i) v = Array{v};
    EXPECT_THROW(encode(v), BtoonException);
    EncodeOptions opts;
    opts.max_depth = 1000;
    EXPECT_NO_THROW(encode(v, opts));
}

TEST(Hardening, EncoderRejectsInvalidUtf8) {
    EXPECT_THROW(encode(Value(String("\xff\xfe"))), BtoonException);
    EXPECT_THROW(encode(Value(Map{{String("\xc0\x80"), Int(1)}})), BtoonException);
    EncodeOptions lax;
    lax.validate_utf8 = false;
    EXPECT_NO_THROW(encode(Value(String("\xff")), lax));
}

TEST(Hardening, EncoderRejectsInvalidTimestamps) {
    EXPECT_THROW(encode(Value(Timestamp(0, 1000000000u))), BtoonException);
    EXPECT_THROW(encode(Value(Timestamp(0, 0, int16_t(2000)))), BtoonException);
}

TEST(Hardening, NonStrictModeIsLenientButStillSafe) {
    DecodeOptions lax;
    lax.strict = false;
    const std::vector<uint8_t> trailing{0x01, 0x02};
    EXPECT_EQ(decode(trailing, lax), Value(Int(1)));
    const std::vector<uint8_t> dup{0x82, 0xa1, 'a', 0x01, 0xa1, 'a', 0x02};
    auto m = std::get<Map>(decode(dup, lax));
    EXPECT_EQ(m.at("a"), Value(Int(2)));  // last wins
    const std::vector<uint8_t> bad_utf8{0xa1, 0xff};
    EXPECT_NO_THROW(decode(bad_utf8, lax));
    const std::vector<uint8_t> truncated{0x92, 0x01};
    EXPECT_THROW(decode(truncated, lax), BtoonException);  // bounds are never relaxed
}

TEST(Hardening, DecompressionBombIsCapped) {
    // 64 MiB of zeros compresses to ~64 KiB with zlib.
    Value big = Binary(64 * 1024 * 1024, 0);
    EncodeOptions eo;
    eo.compress = true;
    auto packed = encode(big, eo);
    ASSERT_LT(packed.size(), 200u * 1024);

    DecodeOptions small;
    small.max_decompressed_size = 1024 * 1024;
    EXPECT_EQ(code_of(packed, small), ErrorCode::size_limit);
    EXPECT_EQ(decode(packed), big);  // default 256 MiB limit allows it
}

TEST(Hardening, LyingUncompressedSizeIsRejected) {
    auto packed = encode(Value(String(std::string(4096, 'x'))), EncodeOptions{.compress = true});
    ASSERT_EQ(packed[0], 'B');
    // Claim one byte more than the real size: the stream ends early.
    auto lie = packed;
    lie[15] = static_cast<uint8_t>(lie[15] + 1);
    EXPECT_EQ(code_of(lie), ErrorCode::invalid_envelope);
    // Claim one byte less: the stream produces more than declared.
    lie = packed;
    lie[15] = static_cast<uint8_t>(lie[15] - 1);
    EXPECT_EQ(code_of(lie), ErrorCode::invalid_envelope);
}

TEST(Hardening, CompressedInputRejectedWhenAutoDecompressIsOff) {
    auto packed = encode(Value(String(std::string(4096, 'x'))), EncodeOptions{.compress = true});
    DecodeOptions opts;
    opts.auto_decompress = false;
    EXPECT_EQ(code_of(packed, opts), ErrorCode::invalid_envelope);
}

TEST(Hardening, CompressionOnlyWhenWorthIt) {
    // Small or incompressible payloads stay bare.
    auto small = encode(Value(String("hi")), EncodeOptions{.compress = true});
    EXPECT_EQ(small, encode(Value(String("hi"))));
    std::mt19937 rng(1);
    Binary noise(4096);
    for (auto& b : noise) b = static_cast<uint8_t>(rng());
    auto packed = encode(Value(noise), EncodeOptions{.compress = true});
    EXPECT_EQ(packed, encode(Value(noise)));
}

TEST(Hardening, UnavailableAlgorithmIsAnError) {
    if (compression_available(CompressionAlgorithm::SNAPPY)) GTEST_SKIP();
    EncodeOptions eo;
    eo.compress = true;
    eo.compression_algorithm = CompressionAlgorithm::SNAPPY;
    try {
        encode(Value(String(std::string(4096, 'x'))), eo);
        FAIL() << "expected unsupported_compression";
    } catch (const BtoonException& e) {
        EXPECT_EQ(e.code(), ErrorCode::unsupported_compression);
    }
}

TEST(Hardening, SignedEnvelopeRoundTripAndTamper) {
    const std::string key(32, 'k');
    EncodeOptions eo;
    eo.hmac_key = key;
    eo.compress = true;
    Value doc = sample_document();
    auto sealed = encode(doc, eo);
    ASSERT_EQ(std::memcmp(sealed.data(), "BTSG", 4), 0);

    DecodeOptions ok;
    ok.hmac_key = key;
    EXPECT_EQ(decode(sealed, ok), doc);

    EXPECT_EQ(code_of(sealed), ErrorCode::authentication_failed);  // no key
    DecodeOptions wrong;
    wrong.hmac_key = std::string(32, 'x');
    EXPECT_EQ(code_of(sealed, wrong), ErrorCode::authentication_failed);
    for (size_t i : {size_t(5), size_t(10), sealed.size() - 1}) {
        auto t = sealed;
        t[i] ^= 0x01;
        EXPECT_NE(code_of(t, ok), ErrorCode::generic) << "tampered byte " << i << " accepted";
    }
}

TEST(Hardening, EncoderClassSignsOnceAndDecoderVerifies) {
    Security sec("secret-secret-secret-secret-1234");
    Encoder enc(sec);
    enc.encode(Value(Int(42)));
    auto first = std::vector<uint8_t>(enc.getBuffer().begin(), enc.getBuffer().end());
    auto second = std::vector<uint8_t>(enc.getBuffer().begin(), enc.getBuffer().end());
    EXPECT_EQ(first, second);  // getBuffer() is idempotent
    EXPECT_EQ(first.size(), Security::kEnvelopeHeaderSize + 1);
    Decoder dec(sec);
    EXPECT_EQ(dec.decode(first), Value(Int(42)));
}

TEST(Hardening, TypeRestrictions) {
    Security sec;
    sec.setAllowedTypes({0, 2, 7});  // nil, int, array
    Decoder dec(Security{});
    Decoder restricted(sec);
    restricted.setSecurityEnabled(false);  // restrictions apply without signing
    auto ok = encode(Value(Array{Int(1), nullptr}));
    EXPECT_NO_THROW(restricted.decode(ok));
    auto bad = encode(Value(Array{String("x")}));
    EXPECT_THROW(restricted.decode(bad), BtoonException);
}

TEST(Hardening, IncrementalEncodingMatchesValueEncoding) {
    Encoder enc;
    enc.encodeMapHeader(2);
    enc.encodeString("a");
    enc.encodeArrayHeader(2);
    enc.encodeInt(1);
    enc.encodeUint(2);
    enc.encodeString("b");
    enc.encodeNil();
    auto bytes = enc.release();
    EXPECT_EQ(bytes, encode(Value(Map{{"a", Array{Int(1), Int(2)}}, {"b", nullptr}})));
    EXPECT_EQ(enc.size(), 0u);  // release() empties the encoder
}

TEST(Hardening, DecodeAndGetPosReadsConcatenatedValues) {
    std::vector<uint8_t> stream;
    for (int i : {1, 300, -5}) {
        auto e = encode(Value(Int(i)));
        stream.insert(stream.end(), e.begin(), e.end());
    }
    Decoder dec;
    std::span<const uint8_t> rest(stream);
    std::vector<int64_t> got;
    while (!rest.empty()) {
        auto [v, used] = dec.decode_and_get_pos(rest);
        got.push_back(*v.as_int64());
        rest = rest.subspan(used);
    }
    EXPECT_EQ(got, (std::vector<int64_t>{1, 300, -5}));
}

TEST(Hardening, IntegerEqualityAcrossRepresentations) {
    EXPECT_EQ(Value(Int(5)), Value(Uint(5)));
    EXPECT_NE(Value(Int(-1)), Value(Uint(std::numeric_limits<uint64_t>::max())));
    EXPECT_EQ(decode(encode(Value(Uint(5)))), Value(Int(5)));
    EXPECT_EQ(decode(encode(Value(Uint(std::numeric_limits<uint64_t>::max())))),
              Value(Uint(std::numeric_limits<uint64_t>::max())));
    EXPECT_EQ((BigInt{{0x00, 0x05}}), (BigInt{{0x05}}));
}

TEST(Hardening, NanIsCanonical) {
    double weird_nan = std::bit_cast<double>(0x7ff0000000000123ULL);
    ASSERT_TRUE(std::isnan(weird_nan));
    auto e = encode(Value(weird_nan));
    EXPECT_EQ(e, (std::vector<uint8_t>{0xcb, 0x7f, 0xf8, 0, 0, 0, 0, 0, 0}));
}

TEST(Hardening, EveryAvailableCodecRoundTripsAndCapsOutput) {
    Value doc = sample_document();
    for (auto algo : {CompressionAlgorithm::ZLIB, CompressionAlgorithm::LZ4, CompressionAlgorithm::ZSTD,
                      CompressionAlgorithm::BROTLI, CompressionAlgorithm::SNAPPY}) {
        if (!compression_available(algo)) continue;
        SCOPED_TRACE(compression_algorithm_name(algo));
        EncodeOptions eo;
        eo.compress = true;
        eo.compression_algorithm = algo;
        eo.min_compression_size = 0;
        Value big = Map{{"doc", doc}, {"pad", String(std::string(20000, 'q'))}};
        auto packed = encode(big, eo);
        ASSERT_EQ(std::memcmp(packed.data(), "BTON", 4), 0);
        EXPECT_EQ(packed[5], static_cast<uint8_t>(algo));
        EXPECT_EQ(decode(packed), big);
        DecodeOptions tight;
        tight.max_decompressed_size = 1000;
        EXPECT_EQ(code_of(packed, tight), ErrorCode::size_limit);
        // Corrupting the payload is detected, never mis-decoded.
        auto bad = packed;
        bad[bad.size() / 2] ^= 0xff;
        try {
            Value v = decode(bad);
            EXPECT_EQ(v, big);  // only acceptable outcome without an error (e.g. a no-op bit)
        } catch (const BtoonException&) {
        }
        // Raw API: unknown-size decompression respects its bound.
        auto raw = encode(big);
        auto payload = compress(algo, raw, 0);
        if (algo != CompressionAlgorithm::LZ4) {
            EXPECT_EQ(decompress(algo, payload), raw);
            EXPECT_THROW(decompress_bounded(algo, payload, raw.size() - 1), BtoonException);
        }
        EXPECT_EQ(decompress(algo, payload, raw.size()), raw);
        EXPECT_THROW(decompress(algo, payload, raw.size() + 1), BtoonException);
    }
}

// Spec §6: a Zstd frame may not declare a window larger than max(8 MiB, min(size, 128 MiB)).
// The frames are hand-built (one raw block, no content size) so no zstd headers are needed.
TEST(Hardening, ZstdWindowIsBoundedByContent) {
    if (!compression_available(CompressionAlgorithm::ZSTD)) GTEST_SKIP() << "built without zstd";
    const Value doc = String(std::string(300, 'z'));
    const auto bare = encode(doc);
    auto envelope = [&](uint8_t window_descriptor) {
        std::vector<uint8_t> frame{0x28, 0xb5, 0x2f, 0xfd, 0x00, window_descriptor};
        const uint32_t block = 1u | (uint32_t(bare.size()) << 3);  // last block, raw
        frame.insert(frame.end(), {uint8_t(block), uint8_t(block >> 8), uint8_t(block >> 16)});
        frame.insert(frame.end(), bare.begin(), bare.end());
        std::vector<uint8_t> out{'B', 'T', 'O', 'N', 1, 2, 0, 0};
        for (uint32_t n : {uint32_t(frame.size()), uint32_t(bare.size())}) {
            out.insert(out.end(), {uint8_t(n >> 24), uint8_t(n >> 16), uint8_t(n >> 8), uint8_t(n)});
        }
        out.insert(out.end(), frame.begin(), frame.end());
        return out;
    };
    EXPECT_EQ(decode(envelope(13 << 3)), doc);                                // 8 MiB window: accepted
    EXPECT_EQ(code_of(envelope(14 << 3)), ErrorCode::invalid_envelope);       // 16 MiB
    EXPECT_EQ(code_of(envelope((13 << 3) | 1)), ErrorCode::invalid_envelope);  // 9 MiB
    EXPECT_EQ(code_of(envelope(31 << 3)), ErrorCode::invalid_envelope);       // 2 TiB
}

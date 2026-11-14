// Robustness tests beyond the conformance vectors: truncation sweeps,
// decompression bombs, encoder limits, non-strict mode and envelope misuse.
// Run under -DENABLE_SANITIZERS=ON to turn any memory error into a failure.
#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
#include <random>
#include <string_view>

#include <sstream>

#include "btoon/btoon.h"
#include "btoon/decoder.h"
#include "btoon/encoder.h"
#include "btoon/security.h"
#include "btoon/stream_decoder.h"
#include "btoon/stream_encoder.h"

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
    // 32 MiB of zeros compresses to ~32 KiB with zlib.
    Value big = Binary(32 * 1024 * 1024, 0);
    EncodeOptions eo;
    eo.compress = true;
    auto packed = encode(big, eo);
    ASSERT_LT(packed.size(), 100u * 1024);

    DecodeOptions small;
    small.max_decompressed_size = 1024 * 1024;
    EXPECT_EQ(code_of(packed, small), ErrorCode::size_limit);
    EXPECT_EQ(decode(packed), big);  // within the default 64 MiB limit (docs/SPEC.md §7.1)

    // 64 MiB of payload plus its header is over the default.
    auto over = encode(Value(Binary(64 * 1024 * 1024, 0)), eo);
    EXPECT_EQ(code_of(over), ErrorCode::size_limit);
    DecodeOptions raised;
    raised.max_decompressed_size = 65 * 1024 * 1024;
    EXPECT_NO_THROW(decode(over, raised));
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

// The Decoder class must enforce DecodeOptions::hmac_key as decode() does.
// Before 0.1.4 it ignored the key and returned forged, unsigned input.
TEST(Hardening, DecoderClassVerifiesHmacKeyFromOptions) {
    const std::string key(32, 'k');
    DecodeOptions with_key;
    with_key.hmac_key = key;
    const Decoder dec(with_key);
    auto forged = encode(Value(String("forged")));
    auto expect_code = [](auto&& f, ErrorCode want) {
        try {
            f();
            ADD_FAILURE() << "accepted";
        } catch (const BtoonException& e) {
            EXPECT_EQ(e.code(), want) << e.what();
        }
    };
    expect_code([&] { dec.decode(forged); }, ErrorCode::authentication_failed);
    expect_code([&] { dec.decode_and_get_pos(forged); }, ErrorCode::authentication_failed);
    DecodeOptions lax = with_key;
    lax.strict = false;
    expect_code([&] { Decoder(lax).decode(forged); }, ErrorCode::authentication_failed);

    EncodeOptions eo;
    eo.hmac_key = key;
    auto sealed = encode(Value(String("hello")), eo);
    EXPECT_EQ(dec.decode(sealed), Value(String("hello")));
    auto [v, used] = dec.decode_and_get_pos(sealed);
    EXPECT_EQ(v, Value(String("hello")));
    EXPECT_EQ(used, sealed.size());
    DecodeOptions wrong;
    wrong.hmac_key = std::string(32, 'x');
    expect_code([&] { Decoder(wrong).decode(sealed); }, ErrorCode::authentication_failed);
    auto tampered = sealed;
    tampered.back() ^= 1;
    expect_code([&] { dec.decode(tampered); }, ErrorCode::authentication_failed);
}

// Decoder::decode() opens envelopes exactly like decode(). Before 0.1.4 a
// non-strict Decoder read "BTON..." and "BTSG..." as the integer 66.
TEST(Hardening, DecoderClassOpensEnvelopesLikeDecode) {
    const std::string key(32, 'k');
    EncodeOptions zo;
    zo.compress = true;
    const Value text(String(std::string(300, 'a')));
    auto compressed = encode(text, zo);
    ASSERT_EQ(std::memcmp(compressed.data(), "BTON", 4), 0);
    EncodeOptions so;
    so.hmac_key = key;
    auto sealed = encode(text, so);
    EncodeOptions both = zo;
    both.hmac_key = key;
    auto sealed_compressed = encode(sample_document(), both);

    for (bool strict : {true, false}) {
        DecodeOptions o;
        o.strict = strict;
        EXPECT_EQ(Decoder(o).decode(compressed), text) << "strict=" << strict;
        EXPECT_EQ(code_of(sealed, o), ErrorCode::authentication_failed);
        try {
            Decoder(o).decode(sealed);
            ADD_FAILURE() << "signed input accepted without a key, strict=" << strict;
        } catch (const BtoonException& e) {
            EXPECT_EQ(e.code(), ErrorCode::authentication_failed);
        }
        o.hmac_key = key;
        EXPECT_EQ(Decoder(o).decode(sealed_compressed), sample_document());
        o.hmac_key.reset();
        o.auto_decompress = false;
        EXPECT_THROW(Decoder(o).decode(compressed), BtoonException);
    }
    // A Security object verifies, and the compressed payload is opened too.
    EXPECT_EQ(Decoder(Security(key)).decode(sealed_compressed), sample_document());

    // decode_and_get_pos() reads bare values: "BTON" is the integer 66 followed
    // by more values, as in a stream of concatenated documents.
    auto [first, used] = Decoder().decode_and_get_pos(compressed);
    EXPECT_EQ(first, Value(Int(66)));
    EXPECT_EQ(used, 1u);
}

// Decoder and Encoder copy the Security object. Before 0.1.4 they kept a
// pointer to it, so a temporary dangled (stack-use-after-scope under ASan).
TEST(Hardening, DecoderAndEncoderCopyTheSecurityObject) {
    const std::string key = "an HMAC key that is long enough to be heap allocated";
    EncodeOptions eo;
    eo.hmac_key = key;
    auto sealed = encode(Value(Int(42)), eo);
    Decoder dec{Security{key}};
    EXPECT_EQ(dec.decode(sealed), Value(Int(42)));

    Encoder enc{Security{key}};
    enc.encode(Value(Int(42)));
    EXPECT_EQ(enc.release(), sealed);

    Security sec(key);
    Decoder copy(sec);
    sec.setSecretKey("a different key");
    EXPECT_EQ(copy.decode(sealed), Value(Int(42)));  // changes to `sec` don't reach `copy`
}

// A stream of bare documents can't carry envelopes, so the stream classes
// reject the options instead of silently writing or accepting bare documents.
TEST(Hardening, StreamsRejectEnvelopeOptions) {
    std::stringstream ss;
    EncodeOptions compress;
    compress.compress = true;
    EncodeOptions sign;
    sign.hmac_key = std::string(32, 'k');
    DecodeOptions verify;
    verify.hmac_key = std::string(32, 'k');
    for (const EncodeOptions& o : {compress, sign}) {
        try {
            StreamEncoder w(ss, o);
            ADD_FAILURE() << "StreamEncoder accepted envelope options";
        } catch (const BtoonException& e) {
            EXPECT_EQ(e.code(), ErrorCode::invalid_argument);
        }
    }
    try {
        StreamDecoder r(ss, verify);
        ADD_FAILURE() << "StreamDecoder accepted an HMAC key";
    } catch (const BtoonException& e) {
        EXPECT_EQ(e.code(), ErrorCode::invalid_argument);
    }
    // Options without envelopes still work.
    EncodeOptions plain;
    plain.auto_tabular = false;
    StreamEncoder w(ss, plain);
    w.write(Value(String("x")));
    w.write(Value(Int(66)));
    StreamDecoder r(ss);
    EXPECT_EQ(r.read(), std::optional<Value>(Value(String("x"))));
    EXPECT_EQ(r.read(), std::optional<Value>(Value(Int(66))));
    EXPECT_EQ(r.read(), std::nullopt);
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
    EXPECT_FALSE(Value(Int(5)) != Value(Uint(5)));  // != uses the same semantic comparison
    EXPECT_TRUE(Value(String("a")) != Value(String("b")));
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

#if !defined(_WIN32) && !defined(__ANDROID__)
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>

#include "btoon/zero_copy.h"

// Only the creator of a named segment removes the name; a process that opened it
// must not unlink it for everyone else.
TEST(SharedMemory, OpenerDoesNotUnlinkTheCreatorsSegment) {
    const std::string name = "/btoon-t" + std::to_string(getpid());
    auto creator = SharedMemoryBuffer::create(name, 4096, true);
    ASSERT_TRUE(creator);
    creator->data()[0] = 42;
    {
        auto opener = SharedMemoryBuffer::create(name, 4096, false);
        ASSERT_TRUE(opener);
        EXPECT_EQ(opener->data()[0], 42);
    }
    auto again = SharedMemoryBuffer::create(name, 4096, false);
    ASSERT_TRUE(again);
    EXPECT_EQ(again->data()[0], 42);
    again.reset();
    creator.reset();
    EXPECT_FALSE(SharedMemoryBuffer::create(name, 4096, false));
}

TEST(SharedMemory, OwnerOnlyAndSizeChecked) {
    const std::string name = "/btoon-p" + std::to_string(getpid());
    auto creator = SharedMemoryBuffer::create(name, 4096, true);
    ASSERT_TRUE(creator);
    int fd = shm_open(name.c_str(), O_RDONLY, 0);
    ASSERT_GE(fd, 0);
    struct stat st {};
    ASSERT_EQ(fstat(fd, &st), 0);
    close(fd);
    EXPECT_EQ(st.st_mode & 077, 0u) << "group/other must have no access";
    // macOS rounds segments up to whole pages, so compare against the real size.
    const size_t real_size = static_cast<size_t>(st.st_size);
    EXPECT_FALSE(SharedMemoryBuffer::create(name, real_size + 1, false)) << "larger than the segment";
    EXPECT_TRUE(SharedMemoryBuffer::create(name, real_size, false));
    auto whole = SharedMemoryBuffer::create(name, 0, false);
    ASSERT_TRUE(whole);
    EXPECT_GE(whole->size(), 4096u);
}

// The C API's views must own their mapping: reading them after the call returns used
// to touch unmapped memory.
TEST(ZeroCopyCApi, ViewsOwnTheirMapping) {
    const std::string path = ::testing::TempDir() + "btoon-mmap-" + std::to_string(getpid());
    {
        std::FILE* f = std::fopen(path.c_str(), "wb");
        ASSERT_TRUE(f);
        const char bytes[] = "btoon-mmap";
        std::fwrite(bytes, 1, sizeof bytes, f);
        std::fclose(f);
    }
    btoon_memory_view_t file_view = btoon_mmap_file(path.c_str());
    ASSERT_TRUE(file_view);
    ASSERT_EQ(btoon_memory_view_size(file_view), 11u);
    EXPECT_EQ(std::memcmp(btoon_memory_view_data(file_view), "btoon-mmap", 11), 0);
    btoon_munmap_file(file_view);
    std::remove(path.c_str());

    EXPECT_FALSE(btoon_mmap_file("/dev/null")) << "only regular files are mapped";

    const std::string name = "/btoon-c" + std::to_string(getpid());
    btoon_memory_view_t shm_view = btoon_shm_create(name.c_str(), 4096, 1);
    ASSERT_TRUE(shm_view);
    EXPECT_EQ(btoon_memory_view_size(shm_view), 4096u);
    EXPECT_EQ(btoon_memory_view_data(shm_view)[0], 0);
    EXPECT_TRUE(SharedMemoryBuffer::create(name, 4096, false)) << "segment alive while the view is";
    btoon_shm_close(shm_view);
    EXPECT_FALSE(SharedMemoryBuffer::create(name, 4096, false)) << "closing the creator's view removes it";
}
#endif

// Review 2026-11-13 (X1): nested containers each pre-allocated against the same remaining
// bytes. 127 nested array32 headers reserved 40 GiB, and 64 nested tabulars built 2.6 GiB
// from 1 MB. Both must now fail cheaply.
TEST(Hardening, NestedHeadersDoNotPreallocate) {
    std::vector<uint8_t> doc;
    for (int i = 0; i < 127; ++i) {
        doc.push_back(0xdd);  // array32 claiming almost all remaining input
        const uint32_t n = static_cast<uint32_t>(1 << 20);
        for (int s = 24; s >= 0; s -= 8) doc.push_back(static_cast<uint8_t>(n >> s));
    }
    doc.resize(doc.size() + (1 << 20), 0xc0);
    DecodeOptions opts;
    opts.max_depth = 1000;
    try {
        decode(doc, opts);
        ADD_FAILURE() << "accepted";
    } catch (const BtoonException& e) {
        EXPECT_NE(e.code(), ErrorCode::generic);
    }
}

TEST(Hardening, NestedTabularsDoNotPreallocateRows) {
    // A one-column tabular whose first cell is another tabular, 64 deep, each claiming as
    // many rows as its payload could hold.
    auto put32 = [](std::vector<uint8_t>& b, uint32_t v) {
        for (int s = 24; s >= 0; s -= 8) b.push_back(static_cast<uint8_t>(v >> s));
    };
    std::vector<uint8_t> inner(16384, 0xc0);  // 16k nil cells
    for (int level = 0; level < 64; ++level) {
        std::vector<uint8_t> payload;
        put32(payload, 1);  // version
        put32(payload, 1);  // one column
        const auto rows = static_cast<uint32_t>(inner.size());
        put32(payload, rows);  // claims one row per byte of the column
        put32(payload, 1);
        payload.push_back('c');
        payload.push_back(0);  // type code
        put32(payload, static_cast<uint32_t>(inner.size()));
        payload.insert(payload.end(), inner.begin(), inner.end());
        std::vector<uint8_t> ext{0xc9};
        put32(ext, static_cast<uint32_t>(payload.size()));
        ext.push_back(0xf6);  // -10: tabular
        ext.insert(ext.end(), payload.begin(), payload.end());
        inner = std::move(ext);
    }
    DecodeOptions opts;
    opts.max_depth = 1000;
    EXPECT_THROW(decode(inner, opts), BtoonException);
}

// --- Spec 0.2: D23 (limits), D24 (§5.7), D25 (empty keys) ---------------------------------

TEST(Spec02, OptionsOutsideTheSpecFailUpFront) {
    const auto doc = encode(Int(1));
    DecodeOptions deep;
    deep.max_depth = kMaxDepthLimit + 1;
    EXPECT_EQ(code_of(doc, deep), ErrorCode::invalid_argument);
    deep.max_depth = kMaxDepthLimit;
    EXPECT_NO_THROW(decode(doc, deep));

    EncodeOptions edeep;
    edeep.max_depth = kMaxDepthLimit + 1;
    try {
        encode(Int(1), edeep);
        ADD_FAILURE() << "max_depth above the ceiling accepted";
    } catch (const BtoonException& e) {
        EXPECT_EQ(e.code(), ErrorCode::invalid_argument);
    }

    EncodeOptions empty_key;
    empty_key.hmac_key = std::string();
    try {
        encode(Int(1), empty_key);
        ADD_FAILURE() << "empty key accepted on encode";
    } catch (const BtoonException& e) {
        EXPECT_EQ(e.code(), ErrorCode::invalid_argument);
    }
    DecodeOptions empty_dkey;
    empty_dkey.hmac_key = std::string();
    EXPECT_EQ(code_of(doc, empty_dkey), ErrorCode::invalid_argument);
}

TEST(Spec02, RawExtensionsWithDefinedIdsAreRejected) {
    for (int8_t t : {-1, -2, -3, -4, -5, -6, -10}) {
        Encoder enc;
        const std::vector<uint8_t> one{0};
        EXPECT_THROW(enc.encodeExtension(t, one), BtoonException) << int(t);
        EXPECT_THROW(enc.encodeExtensionHeader(t, 1), BtoonException) << int(t);
        try {
            encode(Value(Extension{t, std::vector<uint8_t>(8, 0)}));
            ADD_FAILURE() << int(t);
        } catch (const BtoonException& e) {
            EXPECT_EQ(e.code(), ErrorCode::invalid_argument);
        }
    }
    // Future ids and application ids are written as given; typed values still encode.
    EXPECT_EQ(encode(Value(Extension{-11, {1}})), (std::vector<uint8_t>{0xd4, 0xf5, 0x01}));
    EXPECT_NO_THROW(encode(Value(Extension{7, {1, 2}})));
    BigInt big{std::vector<uint8_t>(9, 0x11)};
    for (const Value& v : {Value(Timestamp{1}), Value(Date{1}), Value(DateTime{1}), Value(big),
                           Value(VectorFloat{{1.0f}}), Value(VectorDouble{{1.0}})}) {
        EXPECT_EQ(decode(encode(v)), v);
    }
}

TEST(Spec02, MaxValuesBoundsLargeDocumentsAndCanBeRaised) {
    const Array big(3000000, Value(nullptr));
    const auto bytes = encode(Value(big));
    EXPECT_EQ(code_of(bytes), ErrorCode::size_limit);  // default 2^21
    DecodeOptions raised;
    raised.max_values = 3000001;
    EXPECT_EQ(std::get<Array>(decode(bytes, raised)).size(), big.size());
    raised.max_values = 3000000;
    EXPECT_EQ(code_of(bytes, raised), ErrorCode::size_limit);
}

// --- Review 2026-11-13: the remaining codec findings (0.3.0) -------------------------------

// Security::setAllowedTypes rejected a plain array of maps but accepted the same rows in
// tabular form: the decoder built the row maps without checking that maps were allowed.
TEST(Fixes030, TabularRowsObeyTypeRestrictions) {
    Array rows;
    for (int i = 0; i < 8; ++i) rows.push_back(Map{{"id", Int(i)}});
    const auto tabular = encode(Value(rows));
    ASSERT_EQ(tabular[2], 0xf6) << "the canonical form is tabular";
    EncodeOptions plain_opts;
    plain_opts.auto_tabular = false;
    const auto plain = encode(Value(rows), plain_opts);

    Security no_maps;
    no_maps.setAllowedTypes({0, 2, 7});  // nil, int, array
    Decoder restricted(no_maps);
    restricted.setSecurityEnabled(false);
    for (const auto* bytes : {&plain, &tabular}) {
        try {
            restricted.decode(*bytes);
            ADD_FAILURE() << "maps were accepted (" << (bytes == &plain ? "plain" : "tabular") << ")";
        } catch (const BtoonException& e) {
            EXPECT_EQ(e.code(), ErrorCode::invalid_argument);
            EXPECT_NE(std::string(e.what()).find("map"), std::string::npos) << e.what();
        }
    }

    // With maps allowed, both forms decode; an empty table has no rows to restrict.
    Security with_maps;
    with_maps.setAllowedTypes({0, 2, 7, 8});
    Decoder allowed(with_maps);
    allowed.setSecurityEnabled(false);
    EXPECT_EQ(allowed.decode(tabular), Value(rows));
    EXPECT_EQ(allowed.decode(plain), Value(rows));
    const std::vector<uint8_t> empty_table{0xc7, 0x0c, 0xf6, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0};
    EXPECT_EQ(restricted.decode(empty_table), Value(Array{}));
    // Cells are still checked one by one.
    Security no_strings;
    no_strings.setAllowedTypes({0, 2, 7, 8});
    Decoder cells(no_strings);
    cells.setSecurityEnabled(false);
    Array named;
    for (int i = 0; i < 8; ++i) named.push_back(Map{{"name", String("n")}});
    EXPECT_THROW(cells.decode(encode(Value(named))), BtoonException);
}

// The LZ4 plausibility bound in.size() * 255 + 16 was computed in size_t, which wraps on
// 32-bit targets once the payload passes 16.8 MB: a valid 51 MB document whose LZ4
// payload is 17 MB was rejected as invalid_envelope on i386. This test only discriminates
// on 32-bit builds (CI's linux-i386 job); elsewhere it checks the round trip.
TEST(Fixes030, LargeLz4EnvelopeDecodes) {
    if (!compression_available(CompressionAlgorithm::LZ4)) GTEST_SKIP() << "built without LZ4";
    // 16 random bytes and three copies of them, repeated: LZ4 compresses this about 3:1.
    const size_t blocks = 808000;
    Binary data(blocks * 64);
    std::mt19937 rng(1);
    for (size_t b = 0; b < blocks; ++b) {
        uint8_t* p = data.data() + b * 64;
        for (int i = 0; i < 16; ++i) p[i] = static_cast<uint8_t>(rng());
        for (int r = 1; r < 4; ++r) std::memcpy(p + 16 * r, p, 16);
    }
    EncodeOptions lz4;
    lz4.compress = true;
    lz4.compression_algorithm = CompressionAlgorithm::LZ4;
    lz4.compression_level = 1;
    const auto envelope = encode(Value(data), lz4);
    ASSERT_EQ(std::memcmp(envelope.data(), "BTON", 4), 0);
    const uint64_t payload = envelope.size() - 16;
    ASSERT_GT(payload * 255 + 16, uint64_t(UINT32_MAX)) << "the payload must be large enough to wrap 32 bits";
    const Value back = decode(envelope);
    ASSERT_TRUE(std::holds_alternative<Binary>(back));
    EXPECT_TRUE(std::get<Binary>(back) == data);
}

// StreamDecoder read in 64 KiB chunks and refused to buffer more than
// max_decompressed_size, so a limit below 64 KiB failed before reading anything.
TEST(Fixes030, StreamDecoderReadsWithASmallLimit) {
    std::ostringstream out;
    {
        StreamEncoder writer(out);
        writer.write(Value(Int(1)));
        writer.write(Value(String(std::string(3000, 'x'))));
        writer.write(Value(Array{Int(2), Int(3)}));
    }
    const std::string bytes = out.str();
    for (size_t limit : {size_t(1), size_t(4096), size_t(64 * 1024)}) {
        DecodeOptions small;
        small.max_decompressed_size = limit;
        std::istringstream in(bytes);
        StreamDecoder reader(in, small);
        auto first = reader.read();
        ASSERT_TRUE(first) << limit;
        EXPECT_EQ(*first, Value(Int(1)));
        if (limit < 3003) {
            // The 3003-byte string doesn't fit in the limit.
            try {
                reader.read();
                ADD_FAILURE() << "a value larger than the limit was read, limit " << limit;
            } catch (const BtoonException& e) {
                EXPECT_EQ(e.code(), ErrorCode::size_limit);
            }
            continue;
        }
        EXPECT_EQ(*reader.read(), Value(String(std::string(3000, 'x'))));
        EXPECT_EQ(*reader.read(), Value(Array{Int(2), Int(3)}));
        EXPECT_FALSE(reader.read());
        EXPECT_FALSE(reader.has_next());
    }
}

// Timestamp::to_milliseconds() and to_microseconds() multiplied without a range check:
// signed overflow (undefined behavior) for valid 12-byte timestamps.
TEST(Fixes030, TimestampUnitConversionsAreChecked) {
    // seconds = 2^62, a valid 12-byte timestamp (SPEC §5.1).
    const std::vector<uint8_t> far{0xc7, 0x0c, 0xff, 0, 0, 0, 0, 0x40, 0, 0, 0, 0, 0, 0, 0};
    const auto ts = std::get<Timestamp>(decode(far));
    EXPECT_EQ(ts.seconds, int64_t(1) << 62);
    for (auto convert : {&Timestamp::to_milliseconds, &Timestamp::to_microseconds}) {
        try {
            (void)(ts.*convert)();
            ADD_FAILURE() << "an unrepresentable instant was converted";
        } catch (const BtoonException& e) {
            EXPECT_EQ(e.code(), ErrorCode::invalid_argument);
        }
    }
    // In range, including the extremes and negative instants (rounded down).
    EXPECT_EQ(Timestamp(-1, 999999999).to_milliseconds(), -1);
    EXPECT_EQ(Timestamp(-1, 999999999).to_microseconds(), -1);
    EXPECT_EQ(Timestamp(1704067200, 123456789).to_milliseconds(), 1704067200123);
    EXPECT_EQ(Timestamp(1704067200, 123456789).to_microseconds(), 1704067200123456);
    const int64_t max_ms = std::numeric_limits<int64_t>::max();
    EXPECT_EQ(Timestamp::from_milliseconds(max_ms).to_milliseconds(), max_ms);
    EXPECT_EQ(Timestamp::from_milliseconds(INT64_MIN).to_milliseconds(), INT64_MIN);
    EXPECT_EQ(Timestamp::from_microseconds(max_ms).to_microseconds(), max_ms);
    EXPECT_EQ(Timestamp::from_microseconds(INT64_MIN).to_microseconds(), INT64_MIN);
    EXPECT_THROW(Timestamp(INT64_MAX / 1000, 999999999).to_milliseconds(), BtoonException);
    EXPECT_THROW(Timestamp(INT64_MIN / 1000 - 1, 0).to_milliseconds(), BtoonException);
}

// --- LZ4 block structure (SPEC §6, 0.3.0) ---------------------------------------------------
//
// liblz4's decoder skips the end-of-block checks, and accepts offset 0, for a sequence it
// decodes on its fast path, which it takes while at least 32 bytes of output remain. Core
// checks the block's structure itself before liblz4 runs. Each rule is tested with a short
// output and, where liblz4's fast path applies, with an output of 32 bytes or more.

namespace {

// Builds an LZ4 block (lz4_Block_format.md) by hand.
struct Lz4Block {
    std::vector<uint8_t> bytes;

    static void put_length(std::vector<uint8_t>& out, size_t n) {
        for (; n >= 255; n -= 255) out.push_back(255);
        out.push_back(static_cast<uint8_t>(n));
    }
    // Literals, then a match of `length` bytes (at least 4) from `offset` bytes back.
    Lz4Block& seq(std::string_view literals, uint16_t offset, size_t length) {
        const size_t lit = literals.size(), ml = length - 4;
        bytes.push_back(static_cast<uint8_t>((std::min<size_t>(lit, 15) << 4) | std::min<size_t>(ml, 15)));
        if (lit >= 15) put_length(bytes, lit - 15);
        bytes.insert(bytes.end(), literals.begin(), literals.end());
        bytes.push_back(static_cast<uint8_t>(offset & 0xff));
        bytes.push_back(static_cast<uint8_t>(offset >> 8));
        if (ml >= 15) put_length(bytes, ml - 15);
        return *this;
    }
    // The last sequence: literals only. `match_bits` fills the token's low nibble.
    Lz4Block& last(std::string_view literals, uint8_t match_bits = 0) {
        const size_t lit = literals.size();
        bytes.push_back(static_cast<uint8_t>((std::min<size_t>(lit, 15) << 4) | (match_bits & 15)));
        if (lit >= 15) put_length(bytes, lit - 15);
        bytes.insert(bytes.end(), literals.begin(), literals.end());
        return *this;
    }
};

// The decompressed block, or the error code it fails with (generic if none).
ErrorCode lz4_code(const std::vector<uint8_t>& block, size_t size) {
    try {
        (void)decompress(CompressionAlgorithm::LZ4, block, size);
        return ErrorCode::generic;
    } catch (const BtoonException& e) {
        return e.code();
    }
}

std::vector<uint8_t> lz4_bytes(const std::vector<uint8_t>& block, size_t size) {
    return decompress(CompressionAlgorithm::LZ4, block, size);
}

std::vector<uint8_t> text(std::string_view s) { return {s.begin(), s.end()}; }

}  // namespace

TEST(Lz4Block, OffsetZeroIsInvalid) {
    if (!compression_available(CompressionAlgorithm::LZ4)) GTEST_SKIP() << "built without LZ4";
    // 14 literals, a match at offset 0, 5 literals: 23 and (fast path) 37 bytes of output.
    const auto short_block = Lz4Block{}.seq("abcdefghijklmn", 0, 4).last("vwxyz").bytes;
    EXPECT_EQ(lz4_code(short_block, 23), ErrorCode::invalid_envelope);
    const auto long_block = Lz4Block{}.seq("abcdefghijklmn", 0, 18).last("vwxyz").bytes;
    EXPECT_EQ(lz4_code(long_block, 37), ErrorCode::invalid_envelope);
    // The same blocks with offset 1 are fine.
    const auto ok = Lz4Block{}.seq("abcdefghijklmn", 1, 18).last("vwxyz").bytes;
    EXPECT_EQ(lz4_bytes(ok, 37), text("abcdefghijklmnnnnnnnnnnnnnnnnnnnvwxyz"));
}

TEST(Lz4Block, OffsetBeforeTheStartIsInvalid) {
    if (!compression_available(CompressionAlgorithm::LZ4)) GTEST_SKIP() << "built without LZ4";
    for (size_t length : {size_t(7), size_t(18)}) {
        const auto bad = Lz4Block{}.seq("abcdefghijklmn", 15, length).last("vwxyz").bytes;  // 14 written
        EXPECT_EQ(lz4_code(bad, 14 + length + 5), ErrorCode::invalid_envelope) << length;
        const auto edge = Lz4Block{}.seq("abcdefghijklmn", 14, length).last("vwxyz").bytes;  // reaches byte 0
        EXPECT_EQ(lz4_code(edge, 14 + length + 5), ErrorCode::generic) << length;
    }
    // A later match can reach into earlier literals and matches.
    const auto chained = Lz4Block{}.seq("abcd", 4, 8).seq("xyz", 15, 7).last("12345").bytes;
    EXPECT_EQ(lz4_bytes(chained, 27), text("abcdabcdabcdxyzabcdabc12345"));
    const auto past = Lz4Block{}.seq("abcd", 4, 8).seq("xyz", 16, 7).last("12345").bytes;
    EXPECT_EQ(lz4_code(past, 27), ErrorCode::invalid_envelope);
}

TEST(Lz4Block, OutputMustBeTheDeclaredSize) {
    if (!compression_available(CompressionAlgorithm::LZ4)) GTEST_SKIP() << "built without LZ4";
    const auto block = Lz4Block{}.seq("abcdefghijklmnop", 16, 20).last("vwxyz").bytes;  // 41 bytes
    EXPECT_EQ(lz4_code(block, 41), ErrorCode::generic);
    for (size_t wrong : {size_t(40), size_t(42), size_t(0), size_t(1000)}) {
        EXPECT_EQ(lz4_code(block, wrong), ErrorCode::invalid_envelope) << wrong;
    }
    const auto literals = Lz4Block{}.last("hello").bytes;
    EXPECT_EQ(lz4_code(literals, 4), ErrorCode::invalid_envelope);
    EXPECT_EQ(lz4_code(literals, 6), ErrorCode::invalid_envelope);
}

TEST(Lz4Block, MustEndWithLiterals) {
    if (!compression_available(CompressionAlgorithm::LZ4)) GTEST_SKIP() << "built without LZ4";
    // Ends right after a match: 6 bytes, 32 bytes, 140 bytes.
    EXPECT_EQ(lz4_code(Lz4Block{}.seq("ab", 1, 4).bytes, 6), ErrorCode::invalid_envelope);
    EXPECT_EQ(lz4_code(Lz4Block{}.seq("\xbf" "abcdefghijklm", 12, 18).bytes, 32), ErrorCode::invalid_envelope);
    EXPECT_EQ(lz4_code(Lz4Block{}.seq(std::string(40, 'a'), 1, 100).bytes, 140), ErrorCode::invalid_envelope);
    // The match ends the output and an empty literal run follows: the vector
    // long-output-ends-with-match, which liblz4's fast path accepts (the sequence starts 32
    // bytes before the end).
    EXPECT_EQ(lz4_code(Lz4Block{}.seq("\xbf" "abcdefghijklm", 12, 18).last("").bytes, 32), ErrorCode::invalid_envelope);
    EXPECT_EQ(lz4_code(Lz4Block{}.seq("ab", 1, 4).last("").bytes, 6), ErrorCode::invalid_envelope);
}

TEST(Lz4Block, TruncatedInputIsInvalid) {
    if (!compression_available(CompressionAlgorithm::LZ4)) GTEST_SKIP() << "built without LZ4";
    EXPECT_EQ(lz4_code({}, 0), ErrorCode::invalid_envelope) << "an empty block";
    EXPECT_EQ(lz4_code({0x50, 'a', 'b'}, 5), ErrorCode::invalid_envelope) << "fewer literals than announced";
    EXPECT_EQ(lz4_code({0xf0}, 15), ErrorCode::invalid_envelope) << "literal length cut off";
    EXPECT_EQ(lz4_code({0xf0, 0xff, 0xff}, 600), ErrorCode::invalid_envelope) << "continuation cut off";
    auto one_offset_byte = Lz4Block{}.seq("abcd", 4, 4).bytes;
    one_offset_byte.resize(one_offset_byte.size() - 1);
    EXPECT_EQ(lz4_code(one_offset_byte, 8), ErrorCode::invalid_envelope) << "one offset byte";
    auto match_length_cut = Lz4Block{}.seq("abcdefghijklmnopqrst", 1, 600).last("vwxyz").bytes;
    match_length_cut.resize(25);  // token, length byte, 20 literals, offset, one 255: then nothing
    EXPECT_EQ(lz4_code(match_length_cut, 625), ErrorCode::invalid_envelope) << "match length cut off";
}

TEST(Lz4Block, LastFiveBytesAreLiterals) {
    if (!compression_available(CompressionAlgorithm::LZ4)) GTEST_SKIP() << "built without LZ4";
    // The vector last-literals-too-short: a match, then 4 literals.
    const auto short_block = Lz4Block{}.seq("\xb3" "a", 1, 14).last("aaaa").bytes;
    EXPECT_EQ(lz4_code(short_block, 20), ErrorCode::invalid_envelope);
    // 40 bytes: the sequence starts 40 bytes before the end, the match ends 4 before it.
    const auto long_block = Lz4Block{}.seq("abcd", 4, 32).last("wxyz").bytes;
    EXPECT_EQ(lz4_code(long_block, 40), ErrorCode::invalid_envelope);
    // liblz4's fast path: 10 literals and an 18-byte match from 32 bytes before the end,
    // then 4 literals.
    EXPECT_EQ(lz4_code(Lz4Block{}.seq("abcdefghij", 8, 18).last("wxyz").bytes, 32), ErrorCode::invalid_envelope);
    EXPECT_EQ(lz4_bytes(Lz4Block{}.seq("abcdefghij", 8, 18).last("vwxyz").bytes, 33),
              text("abcdefghij" "cdefghijcdefghijcd" "vwxyz"))
        << "with 5 final literals it's valid";
    EXPECT_EQ(lz4_code(Lz4Block{}.seq("abcd", 4, 32).last("").bytes, 36), ErrorCode::invalid_envelope);
    const auto ok = Lz4Block{}.seq("abcd", 4, 32).last("vwxyz").bytes;
    EXPECT_EQ(lz4_code(ok, 41), ErrorCode::generic);
}

TEST(Lz4Block, LastMatchStartsTwelveBytesBeforeTheEnd) {
    if (!compression_available(CompressionAlgorithm::LZ4)) GTEST_SKIP() << "built without LZ4";
    // The vector last-match-too-close: the match starts 9 bytes before the end.
    EXPECT_EQ(lz4_code(Lz4Block{}.seq("\xaa" "a", 1, 4).last("aaaaa").bytes, 11), ErrorCode::invalid_envelope);
    // 25 or 55 literals, then a 4-byte match 9 bytes before the end, then 5 literals.
    for (size_t lits : {size_t(25), size_t(55)}) {
        const std::string q(lits, 'q');
        EXPECT_EQ(lz4_code(Lz4Block{}.seq(q, 8, 4).last("vwxyz").bytes, lits + 9), ErrorCode::invalid_envelope)
            << lits;
    }
    // Starting exactly 12 bytes before the end is allowed (the vector last-match-at-the-limit),
    // short and long.
    EXPECT_EQ(lz4_bytes(Lz4Block{}.seq("\xad" "a", 1, 7).last("aaaaa").bytes, 14),
              text("\xad" "aaaaaaaaaaaaa"));
    const std::string thirty(30, 'r');
    EXPECT_EQ(lz4_bytes(Lz4Block{}.seq(thirty, 1, 7).last("vwxyz").bytes, 42), text(thirty + "rrrrrrr" + "vwxyz"));
    // An earlier match counts only through the last one.
    EXPECT_EQ(lz4_code(Lz4Block{}.seq(thirty, 1, 7).seq("", 1, 4).last("vwxyz").bytes, 46),
              ErrorCode::invalid_envelope);
}

TEST(Lz4Block, LiteralOnlyBlocksAreValidAtAnyLength) {
    if (!compression_available(CompressionAlgorithm::LZ4)) GTEST_SKIP() << "built without LZ4";
    for (size_t n : {size_t(1), size_t(4), size_t(5), size_t(11), size_t(12), size_t(14), size_t(15), size_t(31),
                     size_t(32), size_t(300), size_t(5000)}) {
        std::string lits(n, 'x');
        for (size_t i = 0; i < n; ++i) lits[i] = static_cast<char>('a' + i % 26);
        EXPECT_EQ(lz4_bytes(Lz4Block{}.last(lits).bytes, n), text(lits)) << n;
        // The final token's match-length bits are ignored.
        EXPECT_EQ(lz4_bytes(Lz4Block{}.last(lits, 0xf).bytes, n), text(lits)) << n;
    }
    // An empty output: the block 00, and also 01 to 0f (liblz4 accepts only 00).
    for (uint8_t token = 0; token <= 15; ++token) {
        EXPECT_TRUE(lz4_bytes({token}, 0).empty()) << int(token);
    }
    EXPECT_EQ(lz4_code({0x10, 'a'}, 0), ErrorCode::invalid_envelope);
}

// Whatever liblz4 writes still decodes, and the rules apply through the envelope too.
TEST(Lz4Block, CompressorOutputDecodesAndEnvelopesAreChecked) {
    if (!compression_available(CompressionAlgorithm::LZ4)) GTEST_SKIP() << "built without LZ4";
    std::mt19937 rng(5);
    for (size_t n = 0; n < 2000; n += 1 + n / 8) {
        std::vector<uint8_t> data(n);
        for (auto& b : data) b = static_cast<uint8_t>(rng() % 4 == 0 ? rng() : 'a' + rng() % 3);
        for (int level : {1, 9}) {
            const auto block = compress(CompressionAlgorithm::LZ4, data, level);
            EXPECT_EQ(decompress(CompressionAlgorithm::LZ4, block, n), data) << n << " level " << level;
        }
    }
    // An envelope around the 37-byte offset-0 block (the vector lz4-block/offset-zero).
    const auto block = Lz4Block{}.seq("\xd9#abcdefghijkl", 0, 18).last("vwxyz").bytes;
    std::vector<uint8_t> env{'B', 'T', 'O', 'N', 1, 1, 0, 0, 0, 0, 0, static_cast<uint8_t>(block.size()), 0, 0, 0, 37};
    env.insert(env.end(), block.begin(), block.end());
    EXPECT_EQ(code_of(env), ErrorCode::invalid_envelope);
}

namespace {

// A reference LZ4 block decoder that applies SPEC §6 while it decodes (after btoon-go's
// codecs/lz4block.go): every match starts at least 12 bytes before the end and ends at
// least 5 before it, offsets are 1 … bytes written, and the output is exactly `size`.
std::optional<std::vector<uint8_t>> lz4_reference(const std::vector<uint8_t>& src, size_t size) {
    if (src.empty()) return std::nullopt;
    std::vector<uint8_t> dst(size);
    size_t si = 0, di = 0;
    for (;;) {
        if (si >= src.size()) return std::nullopt;  // the block ends with a match
        const uint8_t token = src[si++];
        size_t lit = token >> 4;
        if (lit == 15) {
            for (;;) {
                if (si >= src.size()) return std::nullopt;
                const uint8_t b = src[si++];
                lit += b;
                if (lit > src.size()) return std::nullopt;
                if (b != 255) break;
            }
        }
        if (lit > src.size() - si || lit > size - di) return std::nullopt;
        std::copy_n(src.begin() + static_cast<std::ptrdiff_t>(si), lit, dst.begin() + static_cast<std::ptrdiff_t>(di));
        si += lit;
        di += lit;
        if (si == src.size()) break;  // literals only: the last sequence
        if (size < 12 || di > size - 12) return std::nullopt;
        if (src.size() - si < 2) return std::nullopt;
        const size_t offset = size_t(src[si]) | (size_t(src[si + 1]) << 8);
        si += 2;
        if (offset == 0 || offset > di) return std::nullopt;
        size_t n = token & 15u;
        if (n == 15) {
            for (;;) {
                if (si >= src.size()) return std::nullopt;
                const uint8_t b = src[si++];
                n += b;
                if (n > size) return std::nullopt;
                if (b != 255) break;
            }
        }
        n += 4;
        if (n > size - 5 - di) return std::nullopt;
        for (size_t k = 0; k < n; ++k) dst[di + k] = dst[di + k - offset];
        di += n;
    }
    if (di != size) return std::nullopt;
    return dst;
}

}  // namespace

// Core (the structural pass, then liblz4) and the reference decoder agree on every block:
// the same verdict, and the same output when valid. The blocks are random sequences around
// the rules' edges, and mutations of what liblz4's compressor writes.
TEST(Lz4Block, AgreesWithAReferenceDecoder) {
    if (!compression_available(CompressionAlgorithm::LZ4)) GTEST_SKIP() << "built without LZ4";
    std::mt19937 rng(11);
    auto pick = [&](size_t lo, size_t hi) { return lo + rng() % (hi - lo + 1); };
    size_t accepted = 0, rejected = 0;
    auto compare = [&](const std::vector<uint8_t>& block, size_t size) {
        const auto expected = lz4_reference(block, size);
        std::optional<std::vector<uint8_t>> got;
        try {
            got = decompress(CompressionAlgorithm::LZ4, block, size);
        } catch (const BtoonException& e) {
            ASSERT_EQ(e.code(), ErrorCode::invalid_envelope) << e.what();
        }
        ASSERT_EQ(got.has_value(), expected.has_value())
            << "size " << size << ", block of " << block.size() << " bytes, core "
            << (got ? "accepts" : "rejects") << " it";
        if (got) {
            ASSERT_EQ(*got, *expected);
            ++accepted;
        } else {
            ++rejected;
        }
    };
    for (int round = 0; round < 20000; ++round) {
        // A random block: sequences with lengths and offsets near the limits.
        Lz4Block b;
        size_t written = 0;
        const size_t sequences = pick(0, 4);
        for (size_t s = 0; s < sequences; ++s) {
            std::string lits(pick(0, 40), 'x');
            for (auto& c : lits) c = static_cast<char>('a' + rng() % 4);
            written += lits.size();
            const size_t offset = pick(0, written + 2);
            const size_t length = pick(4, 40);
            b.seq(lits, static_cast<uint16_t>(offset), length);
            written += length;
        }
        std::string last(pick(0, 8), 'z');
        written += last.size();
        if (rng() % 8 != 0) b.last(last, static_cast<uint8_t>(rng() % 16));
        size_t size = written;
        if (rng() % 10 == 0) size = std::max<size_t>(written, 1) - 1 + pick(0, 2);  // near-miss sizes
        compare(b.bytes, size);
    }
    for (int round = 0; round < 3000; ++round) {
        // liblz4's own output for runny data, then mutated.
        std::vector<uint8_t> data(pick(1, 300));
        for (auto& c : data) c = static_cast<uint8_t>(rng() % 3 == 0 ? rng() : 'a' + rng() % 2);
        auto block = compress(CompressionAlgorithm::LZ4, data, rng() % 2 ? 1 : 9);
        compare(block, data.size());
        switch (rng() % 4) {
            case 0: block[rng() % block.size()] ^= static_cast<uint8_t>(1u << (rng() % 8)); break;
            case 1: block.resize(block.size() - rng() % std::min<size_t>(block.size(), 4)); break;
            case 2: block.push_back(static_cast<uint8_t>(rng())); break;
            default: block[rng() % block.size()] = static_cast<uint8_t>(rng()); break;
        }
        compare(block, data.size());
    }
    EXPECT_GT(accepted, 1000u);
    EXPECT_GT(rejected, 1000u);
}

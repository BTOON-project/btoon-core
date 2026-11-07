// Conformance: decode every vector in tests/vectors/btoon-vectors.json, re-encode
// the canonical ones byte-for-byte, and check that invalid ones are rejected
// with the expected error code (docs/SPEC.md §10).
#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>

#include "btoon/btoon.h"
#include "nlohmann/json.hpp"
#include "vector_support.h"

using namespace btoon;
using json = nlohmann::json;

namespace {

json load_vectors() {
    std::ifstream in(BTOON_VECTORS_FILE);
    if (!in) throw std::runtime_error("cannot open " BTOON_VECTORS_FILE);
    return json::parse(in);
}

const json& vectors() {
    static const json doc = load_vectors();
    return doc;
}

bool feature_available(const std::string& feature) {
    if (feature == "zstd") return compression_available(CompressionAlgorithm::ZSTD);
    if (feature == "lz4") return compression_available(CompressionAlgorithm::LZ4);
    if (feature == "brotli") return compression_available(CompressionAlgorithm::BROTLI);
    if (feature == "snappy") return compression_available(CompressionAlgorithm::SNAPPY);
    return false;
}

}  // namespace

TEST(Vectors, FileIsWellFormed) {
    const auto& doc = vectors();
    EXPECT_EQ(doc.at("spec").get<std::string>(), spec_version());
    EXPECT_GT(doc.at("vectors").size(), 150u);
}

TEST(Vectors, AllVectors) {
    size_t checked = 0, skipped = 0;
    for (const auto& v : vectors().at("vectors")) {
        const std::string id = v.at("id");
        SCOPED_TRACE("vector " + id);
        if (v.contains("requires") && !feature_available(v.at("requires"))) {
            ++skipped;
            continue;
        }
        const auto bytes = vtest::unhex(v.at("hex").get<std::string>());
        DecodeOptions dopts;
        EncodeOptions eopts;
        if (v.contains("options") && v.at("options").contains("hmac_key")) {
            auto key = vtest::unhex(v.at("options").at("hmac_key").get<std::string>());
            dopts.hmac_key.assign(key.begin(), key.end());
            eopts.hmac_key = dopts.hmac_key;
        }

        if (v.contains("error")) {
            const std::string expected = v.at("error");
            try {
                Value got = decode(bytes, dopts);
                ADD_FAILURE() << "expected error " << expected << ", decoded a " << got.type_name();
            } catch (const BtoonException& e) {
                EXPECT_EQ(std::string(e.code_name()), expected) << e.what();
            }
            ++checked;
            continue;
        }

        const Value expected = vtest::from_typed(v.at("value"));
        Value got;
        try {
            got = decode(bytes, dopts);
        } catch (const BtoonException& e) {
            ADD_FAILURE() << "decode failed: " << e.code_name() << ": " << e.what();
            continue;
        }
        EXPECT_TRUE(vtest::equivalent(got, expected)) << "decoded value differs from expected";

        if (v.at("canonical").get<bool>()) {
            auto encoded = encode(expected, eopts);
            EXPECT_EQ(vtest::hex(encoded), v.at("hex").get<std::string>()) << "canonical re-encoding differs";
        }
        ++checked;
    }
    EXPECT_GT(checked, 150u);
    if (skipped) std::printf("[vectors] %zu checked, %zu skipped (optional codecs not built)\n", checked, skipped);
}

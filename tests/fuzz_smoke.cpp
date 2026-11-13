// Dependency-free mutation fuzzer. Seeds come from the conformance vectors;
// each iteration mutates a seed (bit flips, byte sets, splices, truncation,
// insertion) and decodes it. Any crash, sanitizer report or non-BtoonException
// exception is a bug. Build with -DENABLE_SANITIZERS=ON for real coverage.
//
//   btoon_fuzz_smoke [iterations] [seed]
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <random>

#include "btoon/btoon.h"
#include "nlohmann/json.hpp"
#include "vector_support.h"

int main(int argc, char** argv) {
    const long iterations = argc > 1 ? std::atol(argv[1]) : 100000;
    const unsigned seed = argc > 2 ? static_cast<unsigned>(std::atol(argv[2])) : 0xB7001u;

    std::vector<std::vector<uint8_t>> corpus;
    std::ifstream in(BTOON_VECTORS_FILE);
    auto doc = nlohmann::json::parse(in);
    for (const auto& v : doc.at("vectors")) {
        if (!v.contains("hex")) continue;  // encode_error vectors have no bytes
        auto bytes = vtest::unhex(v.at("hex").get<std::string>());
        if (!bytes.empty() && bytes.size() < 4096) corpus.push_back(std::move(bytes));
    }
    // Also seed with the canonical encoding of every decodable vector value.
    std::mt19937_64 rng(seed);
    auto pick = [&](size_t n) { return static_cast<size_t>(rng() % n); };
    long decoded = 0, rejected = 0;
    btoon::DecodeOptions strict, lax;
    lax.strict = false;

    for (long it = 0; it < iterations; ++it) {
        std::vector<uint8_t> buf = corpus[pick(corpus.size())];
        int mutations = 1 + static_cast<int>(pick(4));
        for (int m = 0; m < mutations && !buf.empty(); ++m) {
            switch (pick(6)) {
                case 0: buf[pick(buf.size())] ^= static_cast<uint8_t>(1u << pick(8)); break;
                case 1: buf[pick(buf.size())] = static_cast<uint8_t>(rng()); break;
                case 2: buf.resize(pick(buf.size() + 1)); break;
                case 3: buf.insert(buf.begin() + static_cast<long>(pick(buf.size() + 1)), static_cast<uint8_t>(rng())); break;
                case 4: {  // splice in part of another seed
                    const auto& other = corpus[pick(corpus.size())];
                    size_t from = pick(other.size());
                    size_t len = pick(other.size() - from + 1);
                    buf.insert(buf.begin() + static_cast<long>(pick(buf.size() + 1)),
                               other.begin() + static_cast<long>(from), other.begin() + static_cast<long>(from + len));
                    break;
                }
                default: {  // interesting values into a random position
                    static const uint8_t interesting[] = {0x00, 0x7f, 0x80, 0xff, 0xc1, 0xdd, 0xdf, 0xc9, 0xf6, 0xff};
                    buf[pick(buf.size())] = interesting[pick(sizeof interesting)];
                }
            }
        }
        for (const auto* opts : {&strict, &lax}) {
            try {
                auto v = btoon::decode(buf, *opts);
                ++decoded;
                // Whatever decodes must re-encode and decode to the same value.
                btoon::EncodeOptions eo;
                eo.validate_utf8 = opts->strict;
                auto again = btoon::encode(v, eo);
                btoon::DecodeOptions back = *opts;
                if (!vtest::equivalent(btoon::decode(again, back), v)) {
                    std::fprintf(stderr, "round-trip mismatch at iteration %ld\n", it);
                    return 1;
                }
            } catch (const btoon::BtoonException&) {
                ++rejected;
            }
        }
    }
    std::printf("fuzz_smoke: %ld iterations, %ld decoded, %ld rejected, no crashes\n", iterations, decoded, rejected);
    return 0;
}

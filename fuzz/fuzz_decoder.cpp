// SPDX-FileCopyrightText: 2025 Alvar Laigna <https://alvarlaigna.com>
// SPDX-License-Identifier: MIT
//
// libFuzzer target for the BTOON decoder (and the validator, which also parses
// untrusted bytes). Invariants checked on every input:
//   * decoding either succeeds or throws BtoonException — any other exception,
//     crash or sanitizer report is a bug;
//   * whatever decodes re-encodes canonically and decodes back to the same value;
//   * the validator never throws anything but BtoonException;
//   * UTF8Validator::sanitize() returns valid UTF-8 and keeps valid input unchanged.
//
//   cmake -B build-fuzz -DCMAKE_CXX_COMPILER=$(brew --prefix llvm)/bin/clang++ \
//         -DBUILD_FUZZ_TESTS=ON -DBUILD_TESTS=OFF -DBUILD_TOOLS=OFF
//   cmake --build build-fuzz --target fuzz_decoder
//   ./build-fuzz/fuzz_decoder -max_total_time=300 fuzz/corpus
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <string>
#include <string_view>

#include "btoon/btoon.h"
#include "btoon/validator.h"

namespace {

bool same(const btoon::Value& a, const btoon::Value& b) {
    // Byte equality of canonical encodings: NaN == NaN, -0.0 != 0.0, Int == Uint.
    btoon::EncodeOptions eo;
    eo.validate_utf8 = false;
    return btoon::encode(a, eo) == btoon::encode(b, eo);
}

void check_decode(std::span<const uint8_t> input, bool strict) {
    btoon::DecodeOptions opts;
    opts.strict = strict;
    opts.max_decompressed_size = 16 * 1024 * 1024;  // keep the fuzzer's RSS bounded
    btoon::Value value;
    try {
        value = btoon::decode(input, opts);
    } catch (const btoon::BtoonException&) {
        return;
    }
    btoon::EncodeOptions eo;
    eo.validate_utf8 = strict;  // non-strict decoding may have accepted invalid UTF-8
    auto encoded = btoon::encode(value, eo);
    btoon::Value again = btoon::decode(encoded, opts);  // must not throw
    if (!same(value, again)) std::abort();
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    std::span<const uint8_t> input(data, size);
    check_decode(input, /*strict=*/true);
    check_decode(input, /*strict=*/false);
    try {
        btoon::ValidationOptions vopts;
        vopts.max_depth = 64;
        btoon::Validator validator(vopts);
        (void)validator.validate(input);
    } catch (const btoon::BtoonException&) {
    }
    // UTF8Validator::sanitize() always returns valid UTF-8 and leaves valid UTF-8 alone.
    const std::string_view text(reinterpret_cast<const char*>(data), size);
    const std::string clean = btoon::UTF8Validator::sanitize(text);
    if (!btoon::UTF8Validator::isValid(clean)) std::abort();
    if (btoon::UTF8Validator::isValid(text) && clean != text) std::abort();
    return 0;
}

// SPDX-FileCopyrightText: 2025 Alvar Laigna <https://alvarlaigna.com>
// SPDX-License-Identifier: MIT
//
// Top-level encode()/decode(): canonical encoding plus the compression
// (docs/SPEC.md §6) and signed (§8) envelopes, layered as
// sign(compress(encode(value))).
#include "btoon/btoon.h"
#include "btoon/decoder.h"
#include "btoon/encoder.h"
#include "btoon/security.h"
#include "internal.h"

#include <chrono>
#include <cmath>

namespace btoon {

using namespace detail;

namespace {

constexpr uint8_t kCompressionMagic[4] = {'B', 'T', 'O', 'N'};
constexpr size_t kCompressionHeaderSize = 16;
constexpr uint8_t kCompressionVersion = 1;

bool is_compressed_envelope(std::span<const uint8_t> d) {
    return d.size() >= 4 && std::memcmp(d.data(), kCompressionMagic, 4) == 0;
}

CompressionAlgorithm choose_algorithm(const EncodeOptions& o, std::span<const uint8_t> raw, int* level,
                                      size_t* min_size) {
    CompressionAlgorithm algo;
    *min_size = o.min_compression_size;
    if (o.use_profile) {
        algo = o.compression_profile.algorithm;
        *level = o.compression_profile.numeric_level;
        *min_size = o.compression_profile.min_size;
        if (algo == CompressionAlgorithm::AUTO || o.compression_profile.adaptive) {
            algo = select_best_algorithm(raw, *level <= 3);
        }
    } else if (o.adaptive_compression || o.compression_algorithm == CompressionAlgorithm::AUTO) {
        algo = select_best_algorithm(raw, o.compression_level <= 2 || o.compression_preset == CompressionLevel::FAST);
        *level = o.compression_level;
    } else {
        algo = o.compression_algorithm == CompressionAlgorithm::NONE ? CompressionAlgorithm::ZLIB
                                                                      : o.compression_algorithm;
        *level = o.compression_level;
    }
    if (algo != CompressionAlgorithm::NONE && *level == 0 && o.compression_preset != CompressionLevel::CUSTOM) {
        *level = get_numeric_level(algo, o.compression_preset);
    }
    return algo;
}

std::vector<uint8_t> maybe_compress(std::vector<uint8_t> raw, const EncodeOptions& o) {
    int level = 0;
    size_t min_size = 0;
    CompressionAlgorithm algo = choose_algorithm(o, raw, &level, &min_size);
    if (algo == CompressionAlgorithm::NONE) return raw;
    // An unavailable algorithm is a configuration error regardless of input size (§6).
    if (!compression_available(algo)) {
        fail(ErrorCode::unsupported_compression,
             std::string("compression algorithm not available in this build: ") + compression_algorithm_name(algo));
    }
    if (raw.size() < min_size) return raw;
    if (raw.size() > 0xffffffffULL) fail(ErrorCode::size_limit, "document too large to compress");
    std::vector<uint8_t> payload = compress(algo, raw, level);
    // Only worth it if the envelope saves at least 5%: 20 × envelope ≤ 19 × input (§6).
    const uint64_t envelope_size = uint64_t(payload.size()) + kCompressionHeaderSize;
    if (20 * envelope_size > 19 * uint64_t(raw.size())) return raw;
    std::vector<uint8_t> out(kCompressionHeaderSize + payload.size());
    std::memcpy(out.data(), kCompressionMagic, 4);
    out[4] = kCompressionVersion;
    out[5] = static_cast<uint8_t>(algo);
    out[6] = 0;
    out[7] = 0;
    store_be32(out.data() + 8, static_cast<uint32_t>(payload.size()));
    store_be32(out.data() + 12, static_cast<uint32_t>(raw.size()));
    std::memcpy(out.data() + kCompressionHeaderSize, payload.data(), payload.size());
    return out;
}

}  // namespace

std::vector<uint8_t> apply_envelopes(std::vector<uint8_t> out, const EncodeOptions& options) {
    if (options.compress) out = maybe_compress(std::move(out), options);
    if (!options.hmac_key.empty()) out = Security(options.hmac_key).wrap(out);
    return out;
}

std::vector<uint8_t> encode(const Value& value, const EncodeOptions& options) {
    Encoder encoder(options);
    encoder.encode(value);
    return apply_envelopes(encoder.release(), options);
}

std::span<const uint8_t> detail::open_compression_envelope(std::span<const uint8_t> bytes,
                                                          const DecodeOptions& options,
                                                          std::vector<uint8_t>& storage) {
    if (!is_compressed_envelope(bytes) || bytes.size() < kCompressionHeaderSize) return bytes;
    if (!options.auto_decompress) {
        fail(ErrorCode::invalid_envelope, "input is compressed but auto_decompress is disabled");
    }
    const uint8_t* h = bytes.data();
    const uint8_t version = h[4];
    const auto algo = static_cast<CompressionAlgorithm>(h[5]);
    const uint16_t reserved = load_be16(h + 6);
    const uint32_t compressed_size = load_be32(h + 8);
    const uint32_t uncompressed_size = load_be32(h + 12);
    if (version != kCompressionVersion) fail(ErrorCode::invalid_envelope, "unsupported compression envelope version");
    if (reserved != 0) fail(ErrorCode::invalid_envelope, "compression envelope reserved bytes must be zero");
    if (compressed_size != bytes.size() - kCompressionHeaderSize) {
        fail(ErrorCode::invalid_envelope, "compressed size does not match the envelope length");
    }
    if (uncompressed_size > options.max_decompressed_size) {
        fail(ErrorCode::size_limit, "declared uncompressed size exceeds max_decompressed_size");
    }
    if (h[5] > static_cast<uint8_t>(CompressionAlgorithm::SNAPPY)) {
        fail(ErrorCode::unsupported_compression, "unknown compression algorithm id " + std::to_string(h[5]));
    }
    storage = decompress(algo, bytes.subspan(kCompressionHeaderSize), uncompressed_size);
    return storage;
}

Value decode(std::span<const uint8_t> data, const DecodeOptions& options) { return Decoder(options).decode(data); }

bool is_tabular(const Array& arr) { return tabular_layout(arr, nullptr); }

const char* version() { return BTOON_VERSION_STRING; }

const char* spec_version() { return BTOON_SPEC_VERSION; }

// --- Value -------------------------------------------------------------------

const char* Value::type_name() const {
    static constexpr const char* kNames[] = {
        "nil",       "bool",   "int",  "uint",     "float",  "string",       "binary",       "array",
        "map",       "extension", "timestamp", "date", "datetime", "bigint", "vector_float", "vector_double"};
    static_assert(std::size(kNames) == std::variant_size_v<Value::variant>);
    return kNames[index()];
}

std::optional<int64_t> Value::as_int64() const noexcept {
    if (auto i = std::get_if<Int>(this)) return *i;
    if (auto u = std::get_if<Uint>(this)) {
        if (*u <= static_cast<uint64_t>(INT64_MAX)) return static_cast<int64_t>(*u);
    }
    return std::nullopt;
}

std::optional<uint64_t> Value::as_uint64() const noexcept {
    if (auto u = std::get_if<Uint>(this)) return *u;
    if (auto i = std::get_if<Int>(this)) {
        if (*i >= 0) return static_cast<uint64_t>(*i);
    }
    return std::nullopt;
}

std::optional<double> Value::as_double() const noexcept {
    if (auto f = std::get_if<Float>(this)) return *f;
    if (auto i = std::get_if<Int>(this)) return static_cast<double>(*i);
    if (auto u = std::get_if<Uint>(this)) return static_cast<double>(*u);
    return std::nullopt;
}

// --- Timestamp -----------------------------------------------------------------

Timestamp Timestamp::now() {
    using namespace std::chrono;
    int64_t ns = duration_cast<std::chrono::nanoseconds>(system_clock::now().time_since_epoch()).count();
    int64_t sec = ns / 1000000000LL;
    int64_t rem = ns % 1000000000LL;
    if (rem < 0) {
        rem += 1000000000LL;
        --sec;
    }
    return Timestamp(sec, static_cast<uint32_t>(rem));
}

Timestamp Timestamp::from_microseconds(int64_t micros) {
    // Floor division so that negative instants keep 0 <= nanoseconds < 1e9.
    int64_t sec = micros / 1000000;
    int64_t rem = micros % 1000000;
    if (rem < 0) {
        rem += 1000000;
        --sec;
    }
    return Timestamp(sec, static_cast<uint32_t>(rem * 1000));
}

Timestamp Timestamp::from_milliseconds(int64_t millis) {
    int64_t sec = millis / 1000;
    int64_t rem = millis % 1000;
    if (rem < 0) {
        rem += 1000;
        --sec;
    }
    return Timestamp(sec, static_cast<uint32_t>(rem * 1000000));
}

} // namespace btoon

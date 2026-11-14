#include "btoon/compression.h"
#include "btoon/btoon.h"
#include "internal.h"
#include <zlib.h>
#include <stdexcept>
#include <array>

#ifdef BTOON_WITH_LZ4
#include <lz4.h>
#include <lz4hc.h>
#endif

#ifdef BTOON_WITH_ZSTD
#include <zstd.h>
#endif

#ifdef BTOON_WITH_BROTLI
#include <brotli/encode.h>
#include <brotli/decode.h>
#endif

#ifdef BTOON_WITH_SNAPPY
#include <snappy.h>
#endif

#include <algorithm>
#include <cstring>

namespace btoon {

// --- Compression Profile Implementations ---

CompressionProfile CompressionProfile::realtime() {
    // For real-time applications: prioritize speed
#ifdef BTOON_WITH_LZ4
    return CompressionProfile(CompressionAlgorithm::LZ4, 1, 128, true);
#else
    return CompressionProfile(CompressionAlgorithm::ZLIB, 1, 128, true);
#endif
}

CompressionProfile CompressionProfile::network() {
    // For network transmission: balance speed and size
#ifdef BTOON_WITH_ZSTD
    return CompressionProfile(CompressionAlgorithm::ZSTD, 3, 256, true);
#else
    return CompressionProfile(CompressionAlgorithm::ZLIB, 6, 256, true);
#endif
}

CompressionProfile CompressionProfile::storage() {
    // For storage: prioritize compression ratio
#ifdef BTOON_WITH_ZSTD
    return CompressionProfile(CompressionAlgorithm::ZSTD, 9, 64, false);
#else
    return CompressionProfile(CompressionAlgorithm::ZLIB, 9, 64, false);
#endif
}

CompressionProfile CompressionProfile::streaming() {
    // For streaming: low latency with decent compression
#ifdef BTOON_WITH_LZ4
    return CompressionProfile(CompressionAlgorithm::LZ4, 0, 512, true);
#elif defined(BTOON_WITH_ZSTD)
    return CompressionProfile(CompressionAlgorithm::ZSTD, 1, 512, true);
#else
    return CompressionProfile(CompressionAlgorithm::ZLIB, 3, 512, true);
#endif
}

// --- Helper Functions ---

int get_numeric_level(CompressionAlgorithm algo, CompressionLevel level) {
    if (level == CompressionLevel::CUSTOM) {
        return 0; // Will use default
    }
    
    switch (algo) {
        case CompressionAlgorithm::ZLIB:
            switch (level) {
                case CompressionLevel::FASTEST: return 1;
                case CompressionLevel::FAST: return 3;
                case CompressionLevel::BALANCED: return 6;
                case CompressionLevel::HIGH: return 7;
                case CompressionLevel::MAXIMUM: return 9;
                default: return 6;
            }
            
        case CompressionAlgorithm::LZ4:
            switch (level) {
                case CompressionLevel::FASTEST: return 0;  // LZ4 fast
                case CompressionLevel::FAST: return 1;     // LZ4 default
                case CompressionLevel::BALANCED: return 4; // LZ4HC level 4
                case CompressionLevel::HIGH: return 9;     // LZ4HC level 9
                case CompressionLevel::MAXIMUM: return 12; // LZ4HC level 12
                default: return 1;
            }
            
#ifdef BTOON_WITH_ZSTD
        case CompressionAlgorithm::ZSTD:
            switch (level) {
                case CompressionLevel::FASTEST: return 1;
                case CompressionLevel::FAST: return 3;
                case CompressionLevel::BALANCED: return 5;
                case CompressionLevel::HIGH: return 9;
                case CompressionLevel::MAXIMUM: return 19; // ZSTD can go up to 22
                default: return 3;
            }
#endif

#ifdef BTOON_WITH_BROTLI
        case CompressionAlgorithm::BROTLI:
            switch (level) {
                case CompressionLevel::FASTEST: return 0;
                case CompressionLevel::FAST: return 2;
                case CompressionLevel::BALANCED: return 6;
                case CompressionLevel::HIGH: return 9;
                case CompressionLevel::MAXIMUM: return 11; // Brotli max
                default: return 6;
            }
#endif

        case CompressionAlgorithm::SNAPPY:
            // Snappy doesn't have compression levels
            return 0;
            
        default:
            return 0;
    }
}

CompressionAlgorithm select_best_algorithm(std::span<const uint8_t> data, bool prefer_speed) {
    // Simple heuristic-based selection
    size_t size = data.size();
    
    // For very small data, prefer no compression
    if (size < 128) {
        return CompressionAlgorithm::NONE;
    }
    
    // Check data entropy (simple check for compressibility)
    bool highly_compressible = false;
    if (size >= 256) {
        // Sample first 256 bytes for patterns
        std::array<int, 256> freq = {0};
        size_t sample_size = std::min(size_t(1024), size);
        for (size_t i = 0; i < sample_size; ++i) {
            freq[data[i]]++;
        }
        
        // Count unique bytes
        int unique_bytes = 0;
        for (int f : freq) {
            if (f > 0) unique_bytes++;
        }
        
        // If less than 50% unique bytes, data is likely highly compressible
        highly_compressible = (unique_bytes < 128);
    }
    
    if (prefer_speed) {
#ifdef BTOON_WITH_LZ4
        return CompressionAlgorithm::LZ4;
#elif defined(BTOON_WITH_ZSTD)
        return CompressionAlgorithm::ZSTD;
#else
        return CompressionAlgorithm::ZLIB;
#endif
    }
    
    if (highly_compressible) {
#ifdef BTOON_WITH_ZSTD
        return CompressionAlgorithm::ZSTD;
#else
        return CompressionAlgorithm::ZLIB;
#endif
    }
    
    // For moderately compressible data
#ifdef BTOON_WITH_LZ4
    if (size < 65536) {
        return CompressionAlgorithm::LZ4;
    }
#endif
    
#ifdef BTOON_WITH_ZSTD
    return CompressionAlgorithm::ZSTD;
#else
    return CompressionAlgorithm::ZLIB;
#endif
}

float estimate_compression_ratio(CompressionAlgorithm algo, std::span<const uint8_t> data) {
    // Quick estimation based on data characteristics
    // This is a simplified heuristic
    size_t size = data.size();
    if (size < 64) return 1.0f; // No compression for tiny data
    
    // Sample entropy
    std::array<int, 256> freq = {0};
    size_t sample_size = std::min(size_t(1024), size);
    for (size_t i = 0; i < sample_size; ++i) {
        freq[data[i]]++;
    }
    
    int unique_bytes = 0;
    for (int f : freq) {
        if (f > 0) unique_bytes++;
    }
    
    float entropy_factor = unique_bytes / 256.0f;
    
    // Rough estimates based on algorithm and entropy
    switch (algo) {
        case CompressionAlgorithm::NONE:
            return 1.0f;
            
        case CompressionAlgorithm::LZ4:
            return 0.7f + (0.25f * entropy_factor);
            
        case CompressionAlgorithm::ZLIB:
            return 0.5f + (0.4f * entropy_factor);
            
        case CompressionAlgorithm::ZSTD:
            return 0.45f + (0.45f * entropy_factor);
            
        case CompressionAlgorithm::BROTLI:
            return 0.4f + (0.5f * entropy_factor);  // Brotli typically achieves best compression
            
        case CompressionAlgorithm::SNAPPY:
            return 0.75f + (0.2f * entropy_factor);  // Snappy prioritizes speed over ratio
            
        default:
            return 1.0f;
    }
}

// --- Generic Dispatch Functions ---

std::vector<uint8_t> compress(CompressionAlgorithm algorithm, std::span<const uint8_t> data, CompressionLevel level) {
    return compress(algorithm, data, get_numeric_level(algorithm, level));
}

std::vector<uint8_t> compress(const CompressionProfile& profile, std::span<const uint8_t> data) {
    if (data.size() < profile.min_size) {
        return std::vector<uint8_t>(data.begin(), data.end());
    }
    CompressionAlgorithm algo = profile.algorithm;
    if (algo == CompressionAlgorithm::AUTO || profile.adaptive) {
        algo = select_best_algorithm(data, profile.numeric_level <= 3);
    }
    if (algo == CompressionAlgorithm::NONE) {
        return std::vector<uint8_t>(data.begin(), data.end());
    }
    return compress(algo, data, profile.numeric_level);
}

bool compression_available(CompressionAlgorithm algorithm) noexcept {
    switch (algorithm) {
        case CompressionAlgorithm::ZLIB: return true;
#ifdef BTOON_WITH_LZ4
        case CompressionAlgorithm::LZ4: return true;
#endif
#ifdef BTOON_WITH_ZSTD
        case CompressionAlgorithm::ZSTD: return true;
#endif
#ifdef BTOON_WITH_BROTLI
        case CompressionAlgorithm::BROTLI: return true;
#endif
#ifdef BTOON_WITH_SNAPPY
        case CompressionAlgorithm::SNAPPY: return true;
#endif
        default: return false;
    }
}

const char* compression_algorithm_name(CompressionAlgorithm algorithm) noexcept {
    switch (algorithm) {
        case CompressionAlgorithm::ZLIB: return "zlib";
        case CompressionAlgorithm::LZ4: return "lz4";
        case CompressionAlgorithm::ZSTD: return "zstd";
        case CompressionAlgorithm::BROTLI: return "brotli";
        case CompressionAlgorithm::SNAPPY: return "snappy";
        case CompressionAlgorithm::NONE: return "none";
        case CompressionAlgorithm::AUTO: return "auto";
    }
    return "unknown";
}

namespace {

using detail::fail;

[[noreturn]] void unsupported(CompressionAlgorithm algorithm) {
    fail(ErrorCode::unsupported_compression,
         std::string("compression algorithm not available in this build: ") +
             compression_algorithm_name(algorithm));
}

[[noreturn]] void corrupt(const char* algo, const char* what) {
    fail(ErrorCode::invalid_envelope, std::string(algo) + " payload " + what);
}

// Output buffer that grows geometrically up to `limit`, so a lying size
// header can't make us allocate the full limit for a tiny input.
struct GrowBuf {
    std::vector<uint8_t> out;
    size_t used = 0;
    size_t limit;
    explicit GrowBuf(size_t lim, size_t input_size) : limit(lim) {
        // 4x the input to start with; saturating, since input_size * 4 can wrap on 32-bit.
        const size_t guess = input_size > limit / 4 ? limit : input_size * 4;
        size_t initial = std::min(limit, std::max<size_t>(64 * 1024, guess));
        out.resize(initial);
    }
    // Returns false if already at the limit.
    bool grow() {
        if (out.size() >= limit) return false;
        size_t next = out.size() > limit / 2 ? limit : std::max<size_t>(out.size() * 2, 1);
        out.resize(next);
        return true;
    }
    std::vector<uint8_t> take() {
        out.resize(used);
        return std::move(out);
    }
};

// zlib (RFC 1950). If `exact`, the stream must produce exactly `limit` bytes.
// At the limit we probe with a 1-byte scratch buffer: any further output means
// the payload is larger than allowed; otherwise inflate reports completion.
std::vector<uint8_t> inflate_zlib(std::span<const uint8_t> in, size_t limit, bool exact) {
    z_stream strm{};
    if (inflateInit(&strm) != Z_OK) fail(ErrorCode::generic, "zlib: inflateInit failed");
    GrowBuf buf(limit, in.size());
    strm.next_in = const_cast<Bytef*>(in.data());
    strm.avail_in = static_cast<uInt>(in.size());
    uint8_t scratch = 0;
    int ret = Z_OK;
    bool overflow = false;
    for (;;) {
        const bool at_limit = buf.used == buf.out.size() && !buf.grow();
        uint8_t* out = at_limit ? &scratch : buf.out.data() + buf.used;
        size_t room = at_limit ? 1 : std::min<size_t>(buf.out.size() - buf.used, 1u << 30);
        strm.next_out = out;
        strm.avail_out = static_cast<uInt>(room);
        ret = inflate(&strm, Z_NO_FLUSH);
        size_t produced = room - strm.avail_out;
        if (at_limit && produced) {
            overflow = true;
            break;
        }
        buf.used += produced;
        if (ret == Z_STREAM_END) break;
        if ((ret != Z_OK && ret != Z_BUF_ERROR) || (ret == Z_BUF_ERROR && strm.avail_in == 0)) {
            inflateEnd(&strm);
            corrupt("zlib", ret == Z_BUF_ERROR ? "is truncated" : "is corrupt");
        }
    }
    const bool trailing = strm.avail_in != 0;
    inflateEnd(&strm);
    if (overflow) {
        if (exact) corrupt("zlib", "decompresses to more than the declared size");
        fail(ErrorCode::size_limit, "zlib: decompressed data exceeds the size limit");
    }
    if (trailing) corrupt("zlib", "has trailing bytes");
    if (exact && buf.used != limit) corrupt("zlib", "decompressed size does not match the header");
    return buf.take();
}

std::vector<uint8_t> deflate_zlib(std::span<const uint8_t> data, int level) {
    uLongf bound = compressBound(static_cast<uLong>(data.size()));
    std::vector<uint8_t> out(bound);
    int rc = compress2(out.data(), &bound, data.data(), static_cast<uLong>(data.size()),
                       std::clamp(level == 0 ? 6 : level, 1, 9));
    if (rc != Z_OK) fail(ErrorCode::generic, "zlib: compression failed");
    out.resize(bound);
    return out;
}

#ifdef BTOON_WITH_LZ4
std::vector<uint8_t> lz4_compress(std::span<const uint8_t> data, int level) {
    if (data.size() > static_cast<size_t>(LZ4_MAX_INPUT_SIZE)) fail(ErrorCode::size_limit, "lz4: input too large");
    int bound = LZ4_compressBound(static_cast<int>(data.size()));
    std::vector<uint8_t> out(static_cast<size_t>(bound));
    int n = level <= 1
        ? LZ4_compress_default(reinterpret_cast<const char*>(data.data()), reinterpret_cast<char*>(out.data()),
                               static_cast<int>(data.size()), bound)
        : LZ4_compress_HC(reinterpret_cast<const char*>(data.data()), reinterpret_cast<char*>(out.data()),
                          static_cast<int>(data.size()), bound, std::min(level, 12));
    if (n <= 0) fail(ErrorCode::generic, "lz4: compression failed");
    out.resize(static_cast<size_t>(n));
    return out;
}

// Checks the structure of an LZ4 block (lz4_Block_format.md) as docs/SPEC.md §6 requires,
// before liblz4 decodes it. liblz4 skips the end-of-block checks, and accepts offset 0,
// for a sequence it decodes on its fast path, which it takes while at least 32 bytes of
// output remain. This pass parses the tokens, the literal and match lengths (with their
// 255-continuation bytes) and the offsets, and tracks the output position without
// writing anything. Every violation is invalid_envelope:
//   - a match offset of 0, or one that reaches back before the start of the output;
//   - output that isn't exactly `size` bytes;
//   - a block that ends with a match, or input that ends inside a sequence;
//   - if the block has a match: a final literal run shorter than 5 bytes, or a last match
//     that starts less than 12 bytes before the end of the output.
// A block without matches is one literal run, valid at any length. The final token's
// match-length bits are ignored. Positions are 64-bit, so 32-bit targets can't wrap.
void lz4_check_block(std::span<const uint8_t> in, size_t size) {
    constexpr uint64_t kMinMatch = 4;      // a match copies at least 4 bytes
    constexpr uint64_t kLastLiterals = 5;  // the last 5 bytes of output are literals
    constexpr uint64_t kMatchLimit = 12;   // a match starts at least 12 bytes before the end
    const uint64_t n = in.size();
    const uint64_t out_size = size;
    uint64_t si = 0;  // input position
    uint64_t di = 0;  // output position
    bool has_match = false;
    uint64_t last_match = 0;   // where the last match starts in the output
    uint64_t last_literals = 0;  // length of the final literal run
    if (n == 0) corrupt("lz4", "is truncated");
    // A length is a 4-bit token field plus, when that's 15, bytes up to and including the
    // first one below 255. `cap` bounds it: past that the block is invalid anyway, and the
    // sum can't overflow.
    auto read_length = [&](uint64_t length, uint64_t cap, const char* too_long) {
        if (length != 15) return length;
        for (;;) {
            if (si >= n) corrupt("lz4", "is truncated");
            const uint8_t b = in[si++];
            length += b;
            if (length > cap) corrupt("lz4", too_long);
            if (b != 255) return length;
        }
    };
    for (;;) {
        if (si >= n) corrupt("lz4", "ends with a match instead of a literals-only sequence");
        const uint8_t token = in[si++];
        const uint64_t literals = read_length(token >> 4, n, "is truncated");
        if (literals > n - si) corrupt("lz4", "is truncated");
        if (literals > out_size - di) corrupt("lz4", "decompresses to more than the declared size");
        si += literals;
        di += literals;
        if (si == n) {  // the last sequence: literals only
            last_literals = literals;
            break;
        }
        // A match follows the literals.
        if (n - si < 2) corrupt("lz4", "is truncated");
        const uint64_t offset = uint64_t(in[si]) | (uint64_t(in[si + 1]) << 8);
        si += 2;
        if (offset == 0) corrupt("lz4", "has a match offset of 0");
        if (offset > di) corrupt("lz4", "has a match offset that reaches before the start of the output");
        const uint64_t length = read_length(token & 15u, out_size, "decompresses to more than the declared size") + kMinMatch;
        if (length > out_size - di) corrupt("lz4", "decompresses to more than the declared size");
        has_match = true;
        last_match = di;
        di += length;
    }
    if (di != out_size) corrupt("lz4", "decompressed size does not match the header");
    if (has_match) {
        if (last_literals < kLastLiterals) corrupt("lz4", "doesn't end with at least 5 literal bytes");
        if (out_size - last_match < kMatchLimit) {
            corrupt("lz4", "has a match that starts less than 12 bytes before the end");
        }
    }
}

std::vector<uint8_t> lz4_decompress(std::span<const uint8_t> in, size_t expected) {
    // An LZ4 block expands at most ~255x; reject impossible size claims up front. The bound
    // is computed in 64 bits: in.size() * 255 overflows a 32-bit size_t from 16.8 MB on.
    if (uint64_t(expected) > uint64_t(in.size()) * 255 + 16 || expected > static_cast<size_t>(LZ4_MAX_INPUT_SIZE)) {
        corrupt("lz4", "declares an impossible decompressed size");
    }
    lz4_check_block(in, expected);
    // liblz4 accepts only the block 00 for an empty output; SPEC §6 ignores the final
    // token's match bits, so 01 to 0f are empty too. The check above has accepted it.
    if (expected == 0) return {};
    std::vector<uint8_t> out(expected);
    int n = LZ4_decompress_safe(reinterpret_cast<const char*>(in.data()), reinterpret_cast<char*>(out.data()),
                                static_cast<int>(in.size()), static_cast<int>(expected));
    if (n < 0 || static_cast<size_t>(n) != expected) corrupt("lz4", "is corrupt or has the wrong size");
    return out;
}
#endif

#ifdef BTOON_WITH_ZSTD
std::vector<uint8_t> zstd_compress(std::span<const uint8_t> data, int level) {
    size_t bound = ZSTD_compressBound(data.size());
    std::vector<uint8_t> out(bound);
    size_t n = ZSTD_compress(out.data(), bound, data.data(), data.size(), level == 0 ? 3 : level);
    if (ZSTD_isError(n)) fail(ErrorCode::generic, std::string("zstd: ") + ZSTD_getErrorName(n));
    out.resize(n);
    return out;
}

// Window size from a zstd frame header (RFC 8878 §3.1.1.1.2), or 0 if `in` doesn't start
// with a complete zstd frame header. A single-segment frame's window is its content size.
uint64_t zstd_window_size(std::span<const uint8_t> in, unsigned long long content_size) {
    if (in.size() < 6) return 0;
    const uint32_t magic = in[0] | (in[1] << 8) | (in[2] << 16) | (uint32_t(in[3]) << 24);
    if (magic != 0xFD2FB528u) return 0;  // skippable frames are rejected here too
    if (in[4] & 0x20) return content_size;  // Single_Segment_flag
    const uint64_t base = uint64_t(1) << (10 + (in[5] >> 3));
    return base + (base >> 3) * (in[5] & 7);
}

std::vector<uint8_t> zstd_decompress(std::span<const uint8_t> in, size_t limit, bool exact) {
    unsigned long long declared = ZSTD_getFrameContentSize(in.data(), in.size());
    if (declared == ZSTD_CONTENTSIZE_ERROR) corrupt("zstd", "is not a zstd frame");
    if (exact && declared != ZSTD_CONTENTSIZE_UNKNOWN && declared != limit) {
        corrupt("zstd", "frame size does not match the header");
    }
    // Spec §6: the window is what the decoder must allocate, so bound it by what the
    // content can need: max(8 MiB, min(size, 128 MiB)).
    const uint64_t window = zstd_window_size(in, declared);
    if (window == 0) corrupt("zstd", "is not a zstd frame");
    if (window > std::clamp<uint64_t>(limit, uint64_t(8) << 20, uint64_t(128) << 20)) {
        corrupt("zstd", "declares a window larger than its content can need");
    }
    ZSTD_DStream* ds = ZSTD_createDStream();
    if (!ds) fail(ErrorCode::generic, "zstd: out of memory");
    GrowBuf buf(limit, in.size());
    ZSTD_inBuffer ib{in.data(), in.size(), 0};
    uint8_t scratch = 0;
    size_t ret = 1;
    bool overflow = false;
    while (ret != 0) {
        const bool at_limit = buf.used == buf.out.size() && !buf.grow();
        ZSTD_outBuffer ob = at_limit ? ZSTD_outBuffer{&scratch, 1, 0}
                                     : ZSTD_outBuffer{buf.out.data() + buf.used, buf.out.size() - buf.used, 0};
        size_t in_before = ib.pos;
        ret = ZSTD_decompressStream(ds, &ob, &ib);
        if (ZSTD_isError(ret)) {
            ZSTD_freeDStream(ds);
            corrupt("zstd", "is corrupt");
        }
        if (at_limit && ob.pos) {
            overflow = true;
            break;
        }
        buf.used += at_limit ? 0 : ob.pos;
        if (ret != 0 && ob.pos == 0 && ib.pos == in_before) {  // no progress possible
            ZSTD_freeDStream(ds);
            corrupt("zstd", "is truncated");
        }
    }
    ZSTD_freeDStream(ds);
    if (overflow) {
        if (exact) corrupt("zstd", "decompresses to more than the declared size");
        fail(ErrorCode::size_limit, "zstd: decompressed data exceeds the size limit");
    }
    if (ib.pos != ib.size) corrupt("zstd", "has trailing bytes");
    if (exact && buf.used != limit) corrupt("zstd", "decompressed size does not match the header");
    return buf.take();
}
#endif

#ifdef BTOON_WITH_BROTLI
std::vector<uint8_t> brotli_compress(std::span<const uint8_t> data, int level) {
    size_t size = BrotliEncoderMaxCompressedSize(data.size());
    if (size == 0) size = data.size() + 1024;
    std::vector<uint8_t> out(size);
    if (!BrotliEncoderCompress(std::clamp(level == 0 ? 6 : level, 0, 11), BROTLI_DEFAULT_WINDOW,
                               BROTLI_DEFAULT_MODE, data.size(), data.data(), &size, out.data())) {
        fail(ErrorCode::generic, "brotli: compression failed");
    }
    out.resize(size);
    return out;
}

std::vector<uint8_t> brotli_decompress(std::span<const uint8_t> in, size_t limit, bool exact) {
    BrotliDecoderState* st = BrotliDecoderCreateInstance(nullptr, nullptr, nullptr);
    if (!st) fail(ErrorCode::generic, "brotli: out of memory");
    GrowBuf buf(limit, in.size());
    const uint8_t* next_in = in.data();
    size_t avail_in = in.size();
    uint8_t scratch = 0;
    bool overflow = false;
    BrotliDecoderResult res = BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT;
    while (res == BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT) {
        const bool at_limit = buf.used == buf.out.size() && !buf.grow();
        uint8_t* start_out = at_limit ? &scratch : buf.out.data() + buf.used;
        uint8_t* next_out = start_out;
        size_t avail_out = at_limit ? 1 : buf.out.size() - buf.used;
        res = BrotliDecoderDecompressStream(st, &avail_in, &next_in, &avail_out, &next_out, nullptr);
        size_t produced = static_cast<size_t>(next_out - start_out);
        if (at_limit && produced) {
            overflow = true;
            break;
        }
        buf.used += at_limit ? 0 : produced;
    }
    BrotliDecoderDestroyInstance(st);
    if (overflow) {
        if (exact) corrupt("brotli", "decompresses to more than the declared size");
        fail(ErrorCode::size_limit, "brotli: decompressed data exceeds the size limit");
    }
    if (res == BROTLI_DECODER_RESULT_NEEDS_MORE_INPUT) corrupt("brotli", "is truncated");
    if (res != BROTLI_DECODER_RESULT_SUCCESS) corrupt("brotli", "is corrupt");
    if (avail_in != 0) corrupt("brotli", "has trailing bytes");
    if (exact && buf.used != limit) corrupt("brotli", "decompressed size does not match the header");
    return buf.take();
}
#endif

#ifdef BTOON_WITH_SNAPPY
std::vector<uint8_t> snappy_compress(std::span<const uint8_t> data) {
    std::string out;
    snappy::Compress(reinterpret_cast<const char*>(data.data()), data.size(), &out);
    return {out.begin(), out.end()};
}

std::vector<uint8_t> snappy_decompress(std::span<const uint8_t> in, size_t limit, bool exact) {
    size_t n = 0;
    if (!snappy::GetUncompressedLength(reinterpret_cast<const char*>(in.data()), in.size(), &n)) {
        corrupt("snappy", "is corrupt");
    }
    if (exact && n != limit) corrupt("snappy", "decompressed size does not match the header");
    if (!exact && n > limit) fail(ErrorCode::size_limit, "snappy: decompressed data exceeds the size limit");
    // Snappy expands at most ~32x. 64-bit math, as for LZ4: on 32-bit targets in.size() * 32
    // overflows from 134 MB on.
    if (uint64_t(n) > uint64_t(in.size()) * 32 + 64) corrupt("snappy", "declares an impossible decompressed size");
    std::vector<uint8_t> out(n);
    if (!snappy::RawUncompress(reinterpret_cast<const char*>(in.data()), in.size(),
                               reinterpret_cast<char*>(out.data()))) {
        corrupt("snappy", "is corrupt");
    }
    return out;
}
#endif

std::vector<uint8_t> decompress_impl(CompressionAlgorithm algorithm, std::span<const uint8_t> in,
                                     size_t limit, bool exact) {
    switch (algorithm) {
        case CompressionAlgorithm::ZLIB:
            return inflate_zlib(in, limit, exact);
#ifdef BTOON_WITH_LZ4
        case CompressionAlgorithm::LZ4:
            if (!exact) fail(ErrorCode::invalid_argument, "lz4: block payloads need the decompressed size");
            return lz4_decompress(in, limit);
#endif
#ifdef BTOON_WITH_ZSTD
        case CompressionAlgorithm::ZSTD:
            return zstd_decompress(in, limit, exact);
#endif
#ifdef BTOON_WITH_BROTLI
        case CompressionAlgorithm::BROTLI:
            return brotli_decompress(in, limit, exact);
#endif
#ifdef BTOON_WITH_SNAPPY
        case CompressionAlgorithm::SNAPPY:
            return snappy_decompress(in, limit, exact);
#endif
        case CompressionAlgorithm::NONE:
            if (in.size() > limit || (exact && in.size() != limit)) {
                corrupt("uncompressed", "has the wrong size");
            }
            return {in.begin(), in.end()};
        default:
            unsupported(algorithm);
    }
}

}  // namespace

std::vector<uint8_t> compress(CompressionAlgorithm algorithm, std::span<const uint8_t> data, int level) {
    switch (algorithm) {
        case CompressionAlgorithm::NONE:
            return std::vector<uint8_t>(data.begin(), data.end());
        case CompressionAlgorithm::ZLIB:
            return deflate_zlib(data, level);
#ifdef BTOON_WITH_LZ4
        case CompressionAlgorithm::LZ4:
            return lz4_compress(data, level);
#endif
#ifdef BTOON_WITH_ZSTD
        case CompressionAlgorithm::ZSTD:
            return zstd_compress(data, level);
#endif
#ifdef BTOON_WITH_BROTLI
        case CompressionAlgorithm::BROTLI:
            return brotli_compress(data, level);
#endif
#ifdef BTOON_WITH_SNAPPY
        case CompressionAlgorithm::SNAPPY:
            return snappy_compress(data);
#endif
        case CompressionAlgorithm::AUTO: {
            CompressionAlgorithm best = select_best_algorithm(data, level <= 3);
            if (best == CompressionAlgorithm::AUTO) best = CompressionAlgorithm::ZLIB;
            return compress(best, data, level);
        }
        default:
            unsupported(algorithm);
    }
}

std::vector<uint8_t> decompress(CompressionAlgorithm algorithm, std::span<const uint8_t> data,
                                size_t expected_size) {
    return decompress_impl(algorithm, data, expected_size, true);
}

std::vector<uint8_t> decompress_bounded(CompressionAlgorithm algorithm, std::span<const uint8_t> data,
                                        size_t max_size) {
    return decompress_impl(algorithm, data, max_size, false);
}

std::vector<uint8_t> decompress(CompressionAlgorithm algorithm, std::span<const uint8_t> data) {
    return decompress_impl(algorithm, data, kDefaultMaxDecompressedSize, false);
}

} // namespace btoon

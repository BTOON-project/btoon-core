// SPDX-FileCopyrightText: 2025 Alvar Laigna <https://alvarlaigna.com>
// SPDX-License-Identifier: MIT
//
// btoon — convert between JSON and BTOON, and inspect BTOON files.
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "btoon/btoon.h"
#include "json_mapping.h"

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

using namespace btoon;
using btoon::tools::json;

namespace {

void usage(std::ostream& out) {
    out << "btoon " << version() << " (format spec " << spec_version() << ")\n\n"
        << "Usage:\n"
        << "  btoon encode [IN.json|-] [OUT.btoon|-] [options]   JSON -> BTOON\n"
        << "  btoon decode [IN.btoon|-] [OUT.json|-] [options]   BTOON -> JSON\n"
        << "  btoon info   [IN.btoon|-]                          Describe a BTOON document\n"
        << "  btoon --version | --help\n\n"
        << "Options:\n"
        << "  --compress[=ALGO]    Wrap output in a compression envelope (zlib, lz4, zstd, brotli, snappy;\n"
        << "                       default zlib). Decoding detects envelopes automatically.\n"
        << "  --no-tabular         Don't use the columnar encoding for arrays of objects\n"
        << "  --hmac-key-file F    Sign (encode) or verify (decode) with the key in file F\n"
        << "  --compact            Decode to single-line JSON (default: indented)\n"
        << "  --no-tags            Treat {\"$binary\": ...}-style objects as plain maps\n\n"
        << "Non-JSON types are written as tagged objects, e.g. {\"$binary\": \"<base64>\"},\n"
        << "{\"$timestamp\": \"2024-01-01T00:00:00Z\"}, {\"$bigint\": \"<decimal>\"}; see tools/json_mapping.h.\n";
}

std::vector<uint8_t> read_all(const std::string& path) {
    if (path == "-") {
        std::cin >> std::noskipws;
        return {std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>()};
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void write_all(const std::string& path, const void* data, size_t n) {
    if (path == "-") {
        std::cout.write(static_cast<const char*>(data), static_cast<std::streamsize>(n));
        std::cout.flush();
        return;
    }
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("cannot write " + path);
    out.write(static_cast<const char*>(data), static_cast<std::streamsize>(n));
}

std::optional<CompressionAlgorithm> parse_algorithm(const std::string& name) {
    if (name == "zlib") return CompressionAlgorithm::ZLIB;
    if (name == "lz4") return CompressionAlgorithm::LZ4;
    if (name == "zstd") return CompressionAlgorithm::ZSTD;
    if (name == "brotli") return CompressionAlgorithm::BROTLI;
    if (name == "snappy") return CompressionAlgorithm::SNAPPY;
    return std::nullopt;
}

struct Counts {
    size_t values = 0, maps = 0, arrays = 0, max_depth = 0;
};

void count(const Value& v, size_t depth, Counts& c) {
    ++c.values;
    c.max_depth = std::max(c.max_depth, depth);
    if (auto a = std::get_if<Array>(&v)) {
        ++c.arrays;
        for (const auto& e : *a) count(e, depth + 1, c);
    } else if (auto m = std::get_if<Map>(&v)) {
        ++c.maps;
        for (const auto& [_, e] : *m) count(e, depth + 1, c);
    }
}

int info(const std::vector<uint8_t>& bytes, const DecodeOptions& opts) {
    std::cout << "size:        " << bytes.size() << " bytes\n";
    std::span<const uint8_t> s(bytes);
    if (s.size() >= 40 && std::memcmp(s.data(), "BTSG", 4) == 0) {
        std::cout << "envelope:    signed (HMAC-SHA256)\n";
        if (opts.hmac_key.empty()) {
            std::cout << "             pass --hmac-key-file to verify and inspect the payload\n";
            return 0;
        }
        s = s.subspan(40);
    }
    if (s.size() >= 16 && std::memcmp(s.data(), "BTON", 4) == 0) {
        auto u32 = [&](size_t o) { return (uint32_t(s[o]) << 24) | (uint32_t(s[o + 1]) << 16) | (uint32_t(s[o + 2]) << 8) | s[o + 3]; };
        std::cout << "envelope:    compressed with " << compression_algorithm_name(static_cast<CompressionAlgorithm>(s[5]))
                  << ", " << u32(8) << " -> " << u32(12) << " bytes ("
                  << (u32(12) ? 100.0 * u32(8) / u32(12) : 0.0) << "%)\n";
    }
    Value v = decode(bytes, opts);
    Counts c;
    count(v, 0, c);
    std::string as_json = tools::to_json(v).dump();
    std::cout << "top level:   " << v.type_name() << "\n"
              << "values:      " << c.values << " (" << c.maps << " maps, " << c.arrays << " arrays), max depth "
              << c.max_depth << "\n"
              << "as JSON:     " << as_json.size() << " bytes (BTOON is " << (100.0 * bytes.size() / as_json.size())
              << "% of that)\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    // Binary data flows through stdin/stdout; stop Windows from translating newlines.
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty()) {
        usage(std::cerr);
        return 2;
    }
    const std::string cmd = args[0];
    if (cmd == "--version" || cmd == "-v") {
        std::cout << "btoon " << version() << " (format spec " << spec_version() << ")\n";
        return 0;
    }
    if (cmd == "--help" || cmd == "-h" || cmd == "help") {
        usage(std::cout);
        return 0;
    }

    EncodeOptions eopts;
    DecodeOptions dopts;
    bool compact = false, tags = true;
    std::vector<std::string> paths;
    try {
        for (size_t i = 1; i < args.size(); ++i) {
            const std::string& a = args[i];
            if (a == "--compress") {
                eopts.compress = true;
            } else if (a.rfind("--compress=", 0) == 0) {
                auto algo = parse_algorithm(a.substr(11));
                if (!algo) throw std::runtime_error("unknown compression algorithm: " + a.substr(11));
                eopts.compress = true;
                eopts.compression_algorithm = *algo;
            } else if (a == "--no-tabular") {
                eopts.auto_tabular = false;
            } else if (a == "--compact") {
                compact = true;
            } else if (a == "--no-tags") {
                tags = false;
            } else if (a == "--hmac-key-file" && i + 1 < args.size()) {
                auto key = read_all(args[++i]);
                eopts.hmac_key.assign(key.begin(), key.end());
                dopts.hmac_key = eopts.hmac_key;
            } else if (a.size() > 1 && a[0] == '-' && a != "-") {
                throw std::runtime_error("unknown option: " + a);
            } else {
                paths.push_back(a);
            }
        }
        while (paths.size() < 2) paths.push_back("-");

        if (cmd == "encode") {
            auto text = read_all(paths[0]);
            json j = json::parse(text.begin(), text.end());
            auto out = encode(tools::from_json(j, tags), eopts);
            write_all(paths[1], out.data(), out.size());
        } else if (cmd == "decode") {
            auto bytes = read_all(paths[0]);
            std::string text = tools::to_json(decode(bytes, dopts)).dump(compact ? -1 : 2) + "\n";
            write_all(paths[1], text.data(), text.size());
        } else if (cmd == "info") {
            return info(read_all(paths[0]), dopts);
        } else {
            usage(std::cerr);
            return 2;
        }
    } catch (const BtoonException& e) {
        std::cerr << "btoon: " << e.code_name() << ": " << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "btoon: " << e.what() << "\n";
        return 1;
    }
    return 0;
}

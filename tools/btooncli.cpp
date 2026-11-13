// SPDX-FileCopyrightText: 2025 Alvar Laigna <https://alvarlaigna.com>
// SPDX-License-Identifier: MIT
//
// btoon — convert between JSON and BTOON, and inspect BTOON files.
#include <cstring>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "btoon/btoon.h"
#include "json_mapping.h"
#include "tool_io.h"

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

using namespace btoon;
using btoon::tools::json;

namespace {

/// Bad command line: exit status 2.
struct UsageError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

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
        << "  --hmac-key-file F    Sign (encode) or verify (decode) with the key in file F (must not be empty)\n"
        << "  --compact            Decode to single-line JSON (default: indented)\n"
        << "  --no-tags            Treat {\"$binary\": ...}-style objects as plain maps\n\n"
        << "Non-JSON types are written as tagged objects, e.g. {\"$binary\": \"<base64>\"},\n"
        << "{\"$timestamp\": \"2024-01-01T00:00:00Z\"}, {\"$bigint\": \"<decimal>\" or \"0x<hex>\"}; a map whose\n"
        << "only key starts with \"$\" is written as {\"$map\": {...}}. See btoon(1) or tools/json_mapping.h.\n\n"
        << "Exit status: 0 on success, 1 on a conversion or I/O error, 2 on a usage error.\n";
}

std::span<const uint8_t> bytes_of(const std::string& s) {
    return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
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

void info(const std::string& input, const DecodeOptions& opts) {
    const auto bytes = bytes_of(input);
    std::cout << "size:        " << bytes.size() << " bytes\n";
    std::span<const uint8_t> s = bytes;
    if (s.size() >= 40 && std::memcmp(s.data(), "BTSG", 4) == 0) {
        std::cout << "envelope:    signed (HMAC-SHA256)\n";
        if (opts.hmac_key.empty()) {
            std::cout << "             pass --hmac-key-file to verify and inspect the payload\n";
            return;
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
}

int run(const std::vector<std::string>& args) {
    const std::string cmd = args[0];
    if (cmd == "--version" || cmd == "-v") {
        std::cout << "btoon " << version() << " (format spec " << spec_version() << ")\n";
        return tools::flush_stdout() ? 0 : 1;
    }
    if (cmd == "--help" || cmd == "-h" || cmd == "help") {
        usage(std::cout);
        return tools::flush_stdout() ? 0 : 1;
    }
    if (cmd != "encode" && cmd != "decode" && cmd != "info") throw UsageError("unknown command: " + cmd);

    EncodeOptions eopts;
    DecodeOptions dopts;
    bool compact = false, tags = true;
    std::vector<std::string> paths;
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--compress") {
            eopts.compress = true;
        } else if (a.rfind("--compress=", 0) == 0) {
            auto algo = parse_algorithm(a.substr(11));
            if (!algo) throw UsageError("unknown compression algorithm: " + a.substr(11));
            eopts.compress = true;
            eopts.compression_algorithm = *algo;
        } else if (a == "--no-tabular") {
            eopts.auto_tabular = false;
        } else if (a == "--compact") {
            compact = true;
        } else if (a == "--no-tags") {
            tags = false;
        } else if (a == "--hmac-key-file") {
            if (i + 1 >= args.size()) throw UsageError("--hmac-key-file needs a file name");
            const std::string& file = args[++i];
            std::string key = tools::read_file(file);
            // An empty key would mean "no HMAC": encode would sign nothing and decode
            // would accept unsigned input (docs/SPEC.md §8).
            if (key.empty()) throw UsageError("the HMAC key file " + file + " is empty");
            eopts.hmac_key = key;
            dopts.hmac_key = std::move(key);
        } else if (a.size() > 1 && a[0] == '-') {
            throw UsageError("unknown option: " + a);
        } else {
            paths.push_back(a);
        }
    }
    const size_t max_paths = cmd == "info" ? 1 : 2;
    if (paths.size() > max_paths) throw UsageError("too many arguments for " + cmd + ": " + paths[max_paths]);
    while (paths.size() < 2) paths.push_back("-");

    if (cmd == "encode") {
        const json j = tools::parse_json(tools::read_file(paths[0]));
        const auto out = encode(tools::from_json(j, tags), eopts);
        tools::write_file(paths[1], std::string_view(reinterpret_cast<const char*>(out.data()), out.size()));
    } else if (cmd == "decode") {
        const std::string input = tools::read_file(paths[0]);
        const std::string text = tools::to_json(decode(bytes_of(input), dopts)).dump(compact ? -1 : 2) + "\n";
        tools::write_file(paths[1], text);
    } else {
        info(tools::read_file(paths[0]), dopts);
    }
    if (!tools::flush_stdout()) throw std::runtime_error("error writing to standard output");
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
    try {
        return run(args);
    } catch (const UsageError& e) {
        std::cerr << "btoon: " << e.what() << "\n(run 'btoon --help' for usage)\n";
        return 2;
    } catch (const BtoonException& e) {
        std::cerr << "btoon: " << e.code_name() << ": " << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "btoon: " << e.what() << "\n";
        return 1;
    }
}

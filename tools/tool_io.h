// SPDX-FileCopyrightText: 2025 Alvar Laigna <https://alvarlaigna.com>
// SPDX-License-Identifier: MIT
//
// File and standard-stream helpers shared by the CLI tools. Reads work on pipes and
// other unseekable files. Writes throw when any part fails (a full disk, /dev/full, a
// closed standard output), so the tools can exit nonzero instead of reporting success.
#ifndef BTOON_TOOLS_TOOL_IO_H
#define BTOON_TOOLS_TOOL_IO_H

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace btoon::tools {

inline std::string read_stream(std::istream& in, const std::string& name) {
    std::string out;
    char buf[1 << 16];
    while (in) {
        in.read(buf, sizeof buf);
        out.append(buf, static_cast<size_t>(in.gcount()));
    }
    if (in.bad()) throw std::runtime_error("error reading " + name);
    return out;
}

/// Reads a whole file; "-" reads standard input.
inline std::string read_file(const std::string& path) {
    if (path == "-") return read_stream(std::cin, "standard input");
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path);
    return read_stream(in, path);
}

/// Writes `data` to a file ("-" = standard output) and throws if the write fails.
inline void write_file(const std::string& path, std::string_view data) {
    if (path == "-") {
        std::cout.write(data.data(), static_cast<std::streamsize>(data.size()));
        std::cout.flush();
        if (!std::cout) throw std::runtime_error("error writing to standard output");
        return;
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot open " + path + " for writing");
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    out.close();
    if (!out) throw std::runtime_error("error writing " + path);
}

/// Flushes standard output. False if anything written to it was lost.
inline bool flush_stdout() {
    std::cout.flush();
    return static_cast<bool>(std::cout);
}

}  // namespace btoon::tools

#endif

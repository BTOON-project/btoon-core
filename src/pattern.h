// SPDX-FileCopyrightText: 2025 Alvar Laigna <https://alvarlaigna.com>
// SPDX-License-Identifier: MIT
//
// A regular-expression matcher for Schema "pattern" constraints.
//
// std::regex is unsafe on untrusted strings. libstdc++ and MSVC match recursively, one
// stack frame per input character, so a 30 KB string overflows the stack whatever the
// pattern. This engine compiles the pattern to a Thompson NFA and simulates it with a
// Pike VM:
// - matching takes O(text length × program size) time and O(program size) memory;
// - nothing recurses on the text;
// - only the pattern parser recurses, and its nesting is capped.
//
// Syntax: the commonly used subset of ECMAScript (and so JSON Schema) regular expressions:
//   - literals, ".", "^", "$", "\b", "\B", alternation "|";
//   - groups "( )", "(?: )" and "(?<name> )", all non-capturing here;
//   - quantifiers "*", "+", "?", "{n}", "{n,}" and "{n,m}", each optionally lazy ("?"), which
//     doesn't change whether a text matches;
//   - character classes "[...]" and "[^...]" with ranges;
//   - the escapes \d \D \w \W \s \S \t \n \v \f \r \0 \cX \xHH \uHHHH \u{H...}, and "\"
//     before punctuation.
// Back-references, lookaround, \p{...} and flags are rejected with invalid_argument,
// because they have no linear-time implementation.
//
// Semantics:
// - matching works on the Unicode code points of the UTF-8 text, and each invalid UTF-8
//   byte matches as U+FFFD;
// - "." matches anything but a line terminator, and \d, \w and \b are ASCII-only, as in
//   ECMAScript without the "u" flag;
// - like std::regex_match, the whole text must match.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

namespace btoon::detail {

class Pattern {
public:
    // Limits that keep compilation and matching cheap. A pattern over a limit is rejected
    // with invalid_argument.
    static constexpr size_t kMaxPatternBytes = 4096;  // the pattern's UTF-8 length
    static constexpr size_t kMaxProgramSize = 10000;  // NFA instructions after expanding {n,m}
    static constexpr int kMaxRepeat = 1000;           // n and m in {n,m}
    static constexpr int kMaxNesting = 100;           // group nesting depth

    // Throws BtoonException with ErrorCode::invalid_argument for a malformed or unsupported
    // pattern, or one over the limits.
    explicit Pattern(std::string_view pattern);

    // True if the whole of `text` matches.
    bool full_match(std::string_view text) const;

    enum class Op : uint8_t { Class, Split, Jmp, Bol, Eol, WordBoundary, NotWordBoundary, Match };
    struct Inst {
        Op op;
        uint32_t x = 0;  // Class: class index; Split/Jmp: target
        uint32_t y = 0;  // Split: second target
    };
    using Ranges = std::vector<std::pair<uint32_t, uint32_t>>;  // sorted, disjoint, inclusive

private:
    std::vector<Inst> prog_;
    std::vector<Ranges> classes_;
};

}  // namespace btoon::detail

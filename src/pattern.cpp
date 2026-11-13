// SPDX-FileCopyrightText: 2025 Alvar Laigna <https://alvarlaigna.com>
// SPDX-License-Identifier: MIT
//
// Linear-time regular-expression matching for Schema patterns. See pattern.h.

#include "pattern.h"

#include "btoon/btoon.h"
#include "internal.h"

#include <algorithm>
#include <string>

namespace btoon::detail {

namespace {

using Inst = Pattern::Inst;
using Op = Pattern::Op;
using Ranges = Pattern::Ranges;

constexpr uint32_t kMaxCodePoint = 0x10FFFF;
constexpr uint32_t kNone = 0xFFFFFFFF;  // no code point: before the start or after the end
constexpr uint32_t kReplacement = 0xFFFD;

[[noreturn]] void bad_pattern(const std::string& why) {
    fail(ErrorCode::invalid_argument, "invalid pattern: " + why);
}

// Decodes the UTF-8 sequence at s[i] and advances i past it. Returns kNone for an invalid
// sequence, after advancing one byte.
uint32_t decode_utf8(std::string_view s, size_t& i) {
    const auto c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) {
        ++i;
        return c;
    }
    size_t n;
    uint32_t cp;
    uint32_t min;
    if ((c & 0xE0) == 0xC0) {
        n = 1; cp = c & 0x1Fu; min = 0x80;
    } else if ((c & 0xF0) == 0xE0) {
        n = 2; cp = c & 0x0Fu; min = 0x800;
    } else if ((c & 0xF8) == 0xF0) {
        n = 3; cp = c & 0x07u; min = 0x10000;
    } else {
        ++i;
        return kNone;
    }
    if (n >= s.size() - i) {
        ++i;
        return kNone;
    }
    for (size_t k = 1; k <= n; ++k) {
        const auto cc = static_cast<unsigned char>(s[i + k]);
        if ((cc & 0xC0) != 0x80) {
            ++i;
            return kNone;
        }
        cp = (cp << 6) | (cc & 0x3Fu);
    }
    if (cp < min || cp > kMaxCodePoint || (cp >= 0xD800 && cp <= 0xDFFF)) {
        ++i;
        return kNone;
    }
    i += n + 1;
    return cp;
}

void normalize(Ranges& r) {
    std::sort(r.begin(), r.end());
    Ranges out;
    for (const auto& p : r) {
        if (!out.empty() && p.first <= out.back().second + 1) {
            out.back().second = std::max(out.back().second, p.second);
        } else {
            out.push_back(p);
        }
    }
    r.swap(out);
}

Ranges complement(Ranges r) {
    normalize(r);
    Ranges out;
    uint32_t next = 0;
    for (const auto& p : r) {
        if (p.first > next) out.emplace_back(next, p.first - 1);
        next = p.second + 1;
    }
    if (next <= kMaxCodePoint) out.emplace_back(next, kMaxCodePoint);
    return out;
}

bool in_ranges(const Ranges& r, uint32_t cp) {
    auto it = std::upper_bound(r.begin(), r.end(), cp,
                               [](uint32_t v, const std::pair<uint32_t, uint32_t>& p) { return v < p.first; });
    if (it == r.begin()) return false;
    --it;
    return cp <= it->second;
}

// ECMAScript's class escapes. \d, \w and \b are ASCII-only (no "u" flag); \s is
// WhiteSpace plus LineTerminator.
Ranges digit_set() { return {{'0', '9'}}; }
Ranges word_set() { return {{'0', '9'}, {'A', 'Z'}, {'_', '_'}, {'a', 'z'}}; }
Ranges space_set() {
    return {{0x09, 0x0D}, {0x20, 0x20},     {0xA0, 0xA0},     {0x1680, 0x1680}, {0x2000, 0x200A},
            {0x2028, 0x2029}, {0x202F, 0x202F}, {0x205F, 0x205F}, {0x3000, 0x3000}, {0xFEFF, 0xFEFF}};
}
Ranges dot_set() { return complement({{0x0A, 0x0A}, {0x0D, 0x0D}, {0x2028, 0x2029}}); }

bool is_word(uint32_t cp) {
    return (cp >= '0' && cp <= '9') || (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') || cp == '_';
}

bool is_hex(uint32_t c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

uint32_t hex_value(uint32_t c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return c - 'A' + 10;
}

struct Node {
    enum class Kind : uint8_t { Empty, Class, Concat, Alt, Repeat, Bol, Eol, WordBoundary, NotWordBoundary };
    Kind kind = Kind::Empty;
    uint32_t cls = 0;      // Class: index into the class table
    int min = 0;           // Repeat
    int max = 0;           // Repeat; -1 means unbounded
    std::vector<Node> kids;  // Concat and Alt: the parts; Repeat: the repeated node
};

bool is_assertion(const Node& n) {
    return n.kind == Node::Kind::Bol || n.kind == Node::Kind::Eol ||
           n.kind == Node::Kind::WordBoundary || n.kind == Node::Kind::NotWordBoundary;
}

// A class escape like \d, or a single code point.
struct ClassAtom {
    bool single = true;
    uint32_t cp = 0;
    Ranges set;
};

class Parser {
public:
    Parser(std::vector<uint32_t> cps, std::vector<Ranges>& classes)
        : cps_(std::move(cps)), classes_(classes) {}

    Node parse() {
        Node n = alternation(0);
        if (!at_end()) bad_pattern("unmatched ')'");
        return n;
    }

private:
    bool at_end() const { return pos_ >= cps_.size(); }
    uint32_t peek() const { return cps_[pos_]; }
    uint32_t next() { return cps_[pos_++]; }

    Node class_node(Ranges r) {
        normalize(r);
        classes_.push_back(std::move(r));
        Node n;
        n.kind = Node::Kind::Class;
        n.cls = static_cast<uint32_t>(classes_.size() - 1);
        return n;
    }

    Node simple(Node::Kind kind) {
        Node n;
        n.kind = kind;
        return n;
    }

    Node alternation(int depth) {
        Node first = concatenation(depth);
        if (at_end() || peek() != '|') return first;
        Node n;
        n.kind = Node::Kind::Alt;
        n.kids.push_back(std::move(first));
        while (!at_end() && peek() == '|') {
            ++pos_;
            n.kids.push_back(concatenation(depth));
        }
        return n;
    }

    Node concatenation(int depth) {
        Node n;
        n.kind = Node::Kind::Concat;
        while (!at_end() && peek() != '|' && peek() != ')') n.kids.push_back(repetition(depth));
        if (n.kids.empty()) return simple(Node::Kind::Empty);
        if (n.kids.size() == 1) {
            Node only = std::move(n.kids.front());
            return only;
        }
        return n;
    }

    // Parses an unsigned decimal number. Returns false, consuming nothing, if there's no
    // digit. Values above kMaxRepeat are clamped to kMaxRepeat + 1, which callers reject.
    bool number(int& value) {
        if (at_end() || peek() < '0' || peek() > '9') return false;
        long v = 0;
        while (!at_end() && peek() >= '0' && peek() <= '9') {
            v = std::min<long>(v * 10 + static_cast<long>(next() - '0'), Pattern::kMaxRepeat + 1L);
        }
        value = static_cast<int>(v);
        return true;
    }

    // Parses a quantifier at the current position. Returns false, consuming nothing, if
    // there isn't one.
    bool quantifier(int& min, int& max) {
        if (at_end()) return false;
        switch (peek()) {
            case '*': ++pos_; min = 0; max = -1; return true;
            case '+': ++pos_; min = 1; max = -1; return true;
            case '?': ++pos_; min = 0; max = 1; return true;
            case '{': break;
            default: return false;
        }
        const size_t save = pos_;
        ++pos_;
        int n = 0;
        int m = 0;
        if (!number(n)) {
            pos_ = save;
            return false;
        }
        if (!at_end() && peek() == '}') {
            ++pos_;
            m = n;
        } else if (!at_end() && peek() == ',') {
            ++pos_;
            if (!at_end() && peek() == '}') {
                ++pos_;
                m = -1;
            } else if (number(m) && !at_end() && peek() == '}') {
                ++pos_;
            } else {
                pos_ = save;
                return false;
            }
        } else {
            pos_ = save;
            return false;
        }
        if (n > Pattern::kMaxRepeat || m > Pattern::kMaxRepeat) {
            bad_pattern("a repetition count is above " + std::to_string(Pattern::kMaxRepeat));
        }
        if (m >= 0 && m < n) bad_pattern("numbers out of order in a {} quantifier");
        min = n;
        max = m;
        return true;
    }

    Node repetition(int depth) {
        Node atom_node = atom(depth);
        int min = 0;
        int max = 0;
        if (!quantifier(min, max)) return atom_node;
        if (is_assertion(atom_node)) bad_pattern("nothing to repeat");
        if (!at_end() && peek() == '?') ++pos_;  // lazy: doesn't change whether the text matches
        int m2 = 0;
        int x2 = 0;
        if (quantifier(m2, x2)) bad_pattern("nothing to repeat");
        Node n;
        n.kind = Node::Kind::Repeat;
        n.min = min;
        n.max = max;
        n.kids.push_back(std::move(atom_node));
        return n;
    }

    Node atom(int depth) {
        const uint32_t c = next();
        switch (c) {
            case '(': return group(depth);
            case '[': return char_class();
            case '.': return class_node(dot_set());
            case '^': return simple(Node::Kind::Bol);
            case '$': return simple(Node::Kind::Eol);
            case '\\': return escape();
            case '*':
            case '+':
            case '?': bad_pattern("nothing to repeat");
            case '{': bad_pattern("'{' must be escaped as '\\{' when it doesn't start a quantifier");
            default: return class_node({{c, c}});
        }
    }

    Node group(int depth) {
        if (depth + 1 > Pattern::kMaxNesting) {
            bad_pattern("groups are nested deeper than " + std::to_string(Pattern::kMaxNesting));
        }
        if (!at_end() && peek() == '?') {
            ++pos_;
            if (at_end()) bad_pattern("unterminated group");
            const uint32_t c = next();
            if (c == '=' || c == '!') bad_pattern("lookahead is not supported");
            if (c == '<') {
                if (!at_end() && (peek() == '=' || peek() == '!')) bad_pattern("lookbehind is not supported");
                size_t len = 0;
                while (!at_end() && peek() != '>') {
                    const uint32_t ch = next();
                    if (!(is_word(ch) || ch == '$' || ch >= 0x80)) bad_pattern("invalid group name");
                    ++len;
                }
                if (at_end() || len == 0) bad_pattern("invalid group name");
                ++pos_;  // '>'
            } else if (c != ':') {
                bad_pattern("unsupported group syntax");
            }
        }
        Node inner = alternation(depth + 1);
        if (at_end() || peek() != ')') bad_pattern("missing ')'");
        ++pos_;
        return inner;  // groups don't capture here
    }

    // The value of a character escape, after the backslash and the escape letter `c`.
    uint32_t char_escape(uint32_t c) {
        switch (c) {
            case 't': return 0x09;
            case 'n': return 0x0A;
            case 'v': return 0x0B;
            case 'f': return 0x0C;
            case 'r': return 0x0D;
            case '0':
                if (!at_end() && peek() >= '0' && peek() <= '9') bad_pattern("octal escapes are not supported");
                return 0;
            case 'c': {
                if (at_end()) bad_pattern("incomplete \\c escape");
                const uint32_t l = next();
                if (!((l >= 'a' && l <= 'z') || (l >= 'A' && l <= 'Z'))) bad_pattern("invalid \\c escape");
                return l % 32;
            }
            case 'x': {
                if (cps_.size() - pos_ < 2 || !is_hex(cps_[pos_]) || !is_hex(cps_[pos_ + 1])) {
                    bad_pattern("invalid \\x escape");
                }
                const uint32_t v = hex_value(cps_[pos_]) * 16 + hex_value(cps_[pos_ + 1]);
                pos_ += 2;
                return v;
            }
            case 'u': return unicode_escape();
            default:
                if (c < 0x80 && !is_word(c)) return c;  // "\" before punctuation is that character
                if (c >= '1' && c <= '9') bad_pattern("back-references are not supported");
                if (c == 'k') bad_pattern("named back-references are not supported");
                if (c == 'p' || c == 'P') bad_pattern("Unicode property escapes are not supported");
                bad_pattern("unknown escape");
        }
    }

    uint32_t hex4() {
        if (cps_.size() - pos_ < 4) bad_pattern("invalid \\u escape");
        uint32_t v = 0;
        for (int k = 0; k < 4; ++k) {
            if (!is_hex(cps_[pos_])) bad_pattern("invalid \\u escape");
            v = v * 16 + hex_value(cps_[pos_++]);
        }
        return v;
    }

    uint32_t unicode_escape() {
        if (!at_end() && peek() == '{') {
            ++pos_;
            uint32_t v = 0;
            size_t digits = 0;
            while (!at_end() && is_hex(peek())) {
                v = v * 16 + hex_value(next());
                if (v > kMaxCodePoint) bad_pattern("\\u{...} is above U+10FFFF");
                ++digits;
            }
            if (digits == 0 || at_end() || next() != '}') bad_pattern("invalid \\u{...} escape");
            return v;
        }
        uint32_t v = hex4();
        // A surrogate pair written as two escapes is one code point.
        if (v >= 0xD800 && v <= 0xDBFF && cps_.size() - pos_ >= 6 && cps_[pos_] == '\\' &&
            cps_[pos_ + 1] == 'u') {
            const size_t save = pos_;
            pos_ += 2;
            bool ok = true;
            uint32_t lo = 0;
            for (int k = 0; k < 4; ++k) {
                if (!is_hex(cps_[pos_ + static_cast<size_t>(k)])) ok = false;
            }
            if (ok) {
                lo = hex4();
                if (lo >= 0xDC00 && lo <= 0xDFFF) return 0x10000 + ((v - 0xD800) << 10) + (lo - 0xDC00);
            }
            pos_ = save;
        }
        return v;  // a lone surrogate never matches: decoded text has none
    }

    Node escape() {
        if (at_end()) bad_pattern("trailing backslash");
        const uint32_t c = next();
        switch (c) {
            case 'd': return class_node(digit_set());
            case 'D': return class_node(complement(digit_set()));
            case 'w': return class_node(word_set());
            case 'W': return class_node(complement(word_set()));
            case 's': return class_node(space_set());
            case 'S': return class_node(complement(space_set()));
            case 'b': return simple(Node::Kind::WordBoundary);
            case 'B': return simple(Node::Kind::NotWordBoundary);
            default: {
                const uint32_t v = char_escape(c);
                return class_node({{v, v}});
            }
        }
    }

    ClassAtom class_atom() {
        ClassAtom a;
        const uint32_t c = next();
        if (c != '\\') {
            a.cp = c;
            return a;
        }
        if (at_end()) bad_pattern("trailing backslash");
        const uint32_t e = next();
        switch (e) {
            case 'd': a.single = false; a.set = digit_set(); break;
            case 'D': a.single = false; a.set = complement(digit_set()); break;
            case 'w': a.single = false; a.set = word_set(); break;
            case 'W': a.single = false; a.set = complement(word_set()); break;
            case 's': a.single = false; a.set = space_set(); break;
            case 'S': a.single = false; a.set = complement(space_set()); break;
            case 'b': a.cp = 0x08; break;  // backspace inside a class
            case '-': a.cp = '-'; break;
            default: a.cp = char_escape(e); break;
        }
        return a;
    }

    Node char_class() {
        bool negate = false;
        if (!at_end() && peek() == '^') {
            ++pos_;
            negate = true;
        }
        Ranges r;
        for (;;) {
            if (at_end()) bad_pattern("missing ']'");
            if (peek() == ']') {
                ++pos_;
                break;
            }
            ClassAtom a = class_atom();
            const bool dash_range = !at_end() && peek() == '-' && pos_ + 1 < cps_.size() && cps_[pos_ + 1] != ']';
            if (a.single && dash_range) {
                ++pos_;  // '-'
                ClassAtom b = class_atom();
                if (b.single) {
                    if (a.cp > b.cp) bad_pattern("character class range out of order");
                    r.emplace_back(a.cp, b.cp);
                } else {
                    // ECMAScript (Annex B): "x-\d" is x, "-" and \d.
                    r.emplace_back(a.cp, a.cp);
                    r.emplace_back('-', '-');
                    r.insert(r.end(), b.set.begin(), b.set.end());
                }
            } else if (a.single) {
                r.emplace_back(a.cp, a.cp);
            } else {
                r.insert(r.end(), a.set.begin(), a.set.end());  // a following "-" is a literal
            }
        }
        return class_node(negate ? complement(std::move(r)) : std::move(r));
    }

    const std::vector<uint32_t> cps_;
    size_t pos_ = 0;
    std::vector<Ranges>& classes_;
};

class Compiler {
public:
    explicit Compiler(std::vector<Inst>& prog) : prog_(prog) {}

    size_t emit(Op op, uint32_t x = 0, uint32_t y = 0) {
        if (prog_.size() >= Pattern::kMaxProgramSize) {
            bad_pattern("the pattern is too complex (more than " + std::to_string(Pattern::kMaxProgramSize) +
                        " states)");
        }
        prog_.push_back(Inst{op, x, y});
        return prog_.size() - 1;
    }

    uint32_t here() const { return static_cast<uint32_t>(prog_.size()); }

    void compile(const Node& n) {
        switch (n.kind) {
            case Node::Kind::Empty: return;
            case Node::Kind::Class: emit(Op::Class, n.cls); return;
            case Node::Kind::Bol: emit(Op::Bol); return;
            case Node::Kind::Eol: emit(Op::Eol); return;
            case Node::Kind::WordBoundary: emit(Op::WordBoundary); return;
            case Node::Kind::NotWordBoundary: emit(Op::NotWordBoundary); return;
            case Node::Kind::Concat:
                for (const auto& kid : n.kids) compile(kid);
                return;
            case Node::Kind::Alt: {
                std::vector<size_t> to_end;
                for (size_t k = 0; k < n.kids.size(); ++k) {
                    if (k + 1 == n.kids.size()) {
                        compile(n.kids[k]);
                        break;
                    }
                    const size_t split = emit(Op::Split);
                    prog_[split].x = here();
                    compile(n.kids[k]);
                    to_end.push_back(emit(Op::Jmp));
                    prog_[split].y = here();
                }
                for (size_t j : to_end) prog_[j].x = here();
                return;
            }
            case Node::Kind::Repeat: {
                const Node& body = n.kids.front();
                for (int k = 0; k < n.min; ++k) compile(body);
                if (n.max < 0) {
                    const size_t loop = emit(Op::Split);
                    prog_[loop].x = here();
                    compile(body);
                    emit(Op::Jmp, static_cast<uint32_t>(loop));
                    prog_[loop].y = here();
                } else {
                    std::vector<size_t> skips;
                    for (int k = n.min; k < n.max; ++k) {
                        const size_t split = emit(Op::Split);
                        prog_[split].x = here();
                        skips.push_back(split);
                        compile(body);
                    }
                    for (size_t s : skips) prog_[s].y = here();
                }
                return;
            }
        }
    }

private:
    std::vector<Inst>& prog_;
};

// A set of program counters, with O(1) insert, lookup and clear (Briggs & Torczon).
struct ThreadList {
    std::vector<uint32_t> dense;
    std::vector<uint32_t> sparse;
    size_t size = 0;

    explicit ThreadList(size_t n) : dense(n), sparse(n) {}
    bool contains(uint32_t pc) const {
        const uint32_t i = sparse[pc];
        return i < size && dense[i] == pc;
    }
    void insert(uint32_t pc) {
        sparse[pc] = static_cast<uint32_t>(size);
        dense[size++] = pc;
    }
};

// What the assertions can see at a position between two code points.
struct Position {
    uint32_t prev;  // kNone at the start
    uint32_t next;  // kNone at the end
};

}  // namespace

Pattern::Pattern(std::string_view pattern) {
    if (pattern.size() > kMaxPatternBytes) {
        bad_pattern("longer than " + std::to_string(kMaxPatternBytes) + " bytes");
    }
    std::vector<uint32_t> cps;
    cps.reserve(pattern.size());
    for (size_t i = 0; i < pattern.size();) {
        const uint32_t cp = decode_utf8(pattern, i);
        if (cp == kNone) bad_pattern("not valid UTF-8");
        cps.push_back(cp);
    }
    Node root = Parser(std::move(cps), classes_).parse();
    Compiler compiler(prog_);
    compiler.compile(root);
    compiler.emit(Op::Match);
}

bool Pattern::full_match(std::string_view text) const {
    const size_t n = prog_.size();
    ThreadList current(n);
    ThreadList upcoming(n);
    std::vector<uint32_t> stack;
    stack.reserve(n);

    // Adds pc and everything reachable from it without consuming a code point.
    auto add = [&](ThreadList& list, uint32_t start, Position at) {
        stack.clear();
        stack.push_back(start);
        while (!stack.empty()) {
            const uint32_t pc = stack.back();
            stack.pop_back();
            if (list.contains(pc)) continue;
            list.insert(pc);
            const Inst& in = prog_[pc];
            switch (in.op) {
                case Op::Jmp: stack.push_back(in.x); break;
                case Op::Split:
                    stack.push_back(in.y);
                    stack.push_back(in.x);
                    break;
                case Op::Bol:
                    if (at.prev == kNone) stack.push_back(pc + 1);
                    break;
                case Op::Eol:
                    if (at.next == kNone) stack.push_back(pc + 1);
                    break;
                case Op::WordBoundary:
                    if (is_word(at.prev) != is_word(at.next)) stack.push_back(pc + 1);
                    break;
                case Op::NotWordBoundary:
                    if (is_word(at.prev) == is_word(at.next)) stack.push_back(pc + 1);
                    break;
                case Op::Class:
                case Op::Match: break;
            }
        }
    };
    auto read = [&](size_t& i) -> uint32_t {
        if (i >= text.size()) return kNone;
        const uint32_t cp = decode_utf8(text, i);
        return cp == kNone ? kReplacement : cp;
    };

    size_t i = 0;
    uint32_t cur = read(i);
    add(current, 0, Position{kNone, cur});
    while (cur != kNone) {
        const uint32_t nxt = read(i);
        upcoming.size = 0;
        for (size_t k = 0; k < current.size; ++k) {
            const uint32_t pc = current.dense[k];
            const Inst& in = prog_[pc];
            if (in.op == Op::Class && in_ranges(classes_[in.x], cur)) add(upcoming, pc + 1, Position{cur, nxt});
        }
        std::swap(current, upcoming);
        if (current.size == 0) return false;
        cur = nxt;
    }
    for (size_t k = 0; k < current.size; ++k) {
        if (prog_[current.dense[k]].op == Op::Match) return true;
    }
    return false;
}

}  // namespace btoon::detail

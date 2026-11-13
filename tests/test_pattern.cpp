// Schema "pattern" matching (src/pattern.h) and the Schema fixes from the 2026-11-13
// review:
// - std::regex crashed on long strings;
// - optional fields that were present weren't validated;
// - min and max were skipped across Int, Uint and Float.
#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <random>
#include <regex>
#include <string>

#include "btoon/btoon.h"
#include "btoon/schema.h"
#include "pattern.h"

using namespace btoon;
using btoon::detail::Pattern;

namespace {

bool matches(const std::string& pattern, const std::string& text) {
    return Pattern(pattern).full_match(text);
}

void expect_rejected(const std::string& pattern) {
    try {
        Pattern p(pattern);
        ADD_FAILURE() << "pattern accepted: " << pattern;
    } catch (const BtoonException& e) {
        EXPECT_EQ(e.code(), ErrorCode::invalid_argument) << pattern;
    }
}

}  // namespace

TEST(Pattern, MatchesTheWholeText) {
    EXPECT_TRUE(matches("ab", "ab"));
    EXPECT_FALSE(matches("ab", "abc"));
    EXPECT_FALSE(matches("ab", "xab"));
    EXPECT_FALSE(matches("ab", ""));
    EXPECT_TRUE(matches("", ""));
    EXPECT_TRUE(matches("a|b", "b"));
    EXPECT_FALSE(matches("a|b", "ab"));
    EXPECT_TRUE(matches("(ab|cd)+", "abcdab"));
    EXPECT_TRUE(matches("(?:ab){2}", "abab"));
    EXPECT_FALSE(matches("(?:ab){2}", "ab"));
}

TEST(Pattern, Quantifiers) {
    EXPECT_FALSE(matches("a{2,3}", "a"));
    EXPECT_TRUE(matches("a{2,3}", "aa"));
    EXPECT_TRUE(matches("a{2,3}", "aaa"));
    EXPECT_FALSE(matches("a{2,3}", "aaaa"));
    EXPECT_TRUE(matches("a{2,}", "aaaaa"));
    EXPECT_FALSE(matches("a{2,}", "a"));
    EXPECT_TRUE(matches("a{0}", ""));
    EXPECT_TRUE(matches("x?y*z+", "zz"));
    EXPECT_TRUE(matches("a+?b*?", "aab"));  // lazy forms are accepted
    EXPECT_TRUE(matches("(a*)*", "aaa"));
    EXPECT_TRUE(matches("(a?){3}a{3}", "aaa"));
}

TEST(Pattern, ClassesAndEscapes) {
    EXPECT_TRUE(matches("[a-c]+", "abcabc"));
    EXPECT_FALSE(matches("[a-c]+", "abd"));
    EXPECT_TRUE(matches("[^0-9]*", "abc"));
    EXPECT_FALSE(matches("[^0-9]*", "ab1"));
    EXPECT_FALSE(matches("[]", "a"));
    EXPECT_TRUE(matches("[^]", "\n"));
    EXPECT_TRUE(matches("[\\w-]+", "a_b-c"));
    EXPECT_TRUE(matches("[\\w.-]+@[\\w-]+\\.com", "j.doe@ex-ample.com"));
    EXPECT_TRUE(matches("\\d{3}-\\d{4}", "555-1234"));
    EXPECT_FALSE(matches("\\d", "x"));
    EXPECT_TRUE(matches("\\D\\W\\S", "a b"));
    EXPECT_TRUE(matches("a\\.b\\+c\\*\\?\\(\\)\\[\\]\\{\\}\\|\\\\\\/\\^\\$", "a.b+c*?()[]{}|\\/^$"));
    EXPECT_TRUE(matches("\\x41\\u00e9\\u{1F600}\\uD83D\\uDE00", "A\xC3\xA9\xF0\x9F\x98\x80\xF0\x9F\x98\x80"));
    EXPECT_TRUE(matches("\\t\\n\\v\\f\\r\\0\\cJ", std::string("\t\n\v\f\r\0\n", 7)));
    EXPECT_TRUE(matches("[\\b]", "\b"));
    EXPECT_TRUE(matches("a]b}", "a]b}"));
    EXPECT_TRUE(matches("\\s", "\xC2\xA0"));  // U+00A0 is WhiteSpace
}

TEST(Pattern, CodePointsNotBytes) {
    EXPECT_TRUE(matches(".", "\xC3\xA9"));      // é is one code point
    EXPECT_TRUE(matches(".", "\xF0\x9F\x98\x80"));  // 😀 too
    EXPECT_FALSE(matches("..", "\xC3\xA9"));
    EXPECT_TRUE(matches("[\xC3\xA0-\xC3\xBF]+", "\xC3\xA9\xC3\xA8"));  // [à-ÿ]
    EXPECT_FALSE(matches(".", "\n"));
    EXPECT_FALSE(matches(".", "\xE2\x80\xA8"));  // U+2028
    EXPECT_TRUE(matches(".", "\xFF"));    // an invalid byte matches as U+FFFD
    EXPECT_FALSE(matches("a", "\xFF"));
}

TEST(Pattern, Assertions) {
    EXPECT_TRUE(matches("^abc$", "abc"));
    EXPECT_FALSE(matches("a^b", "ab"));
    EXPECT_FALSE(matches("a$b", "ab"));
    EXPECT_TRUE(matches("\\bfoo\\b", "foo"));
    EXPECT_TRUE(matches(".*\\bfoo\\b.*", "a foo b"));
    EXPECT_FALSE(matches(".*\\bfoo\\b.*", "afoob"));
    EXPECT_TRUE(matches(".*\\Boo\\B.*", "foob"));
    EXPECT_TRUE(matches("(?<year>\\d{4})-\\d{2}", "2024-01"));
}

TEST(Pattern, RejectsWhatItCannotMatchInLinearTime) {
    for (const char* p : {"(a", "a)", "[a", "a{2,1}", "*a", "a**", "a+*", "^*", "\\b+", "a{1001}", "a{0,1001}",
                          "(?=a)", "(?!a)", "(?<=a)", "(?<!a)", "(?i)a", "(a)\\1", "\\k<x>", "\\p{L}", "\\P{L}",
                          "\\q", "\\", "x{", "x{1", "x{1,", "\\c1", "\\xZZ", "\\u12", "\\u{110000}", "\\u{}",
                          "[b-a]", "\\01", "(?<>a)", "(?<a-b>a)"}) {
        expect_rejected(p);
    }
    expect_rejected(std::string(Pattern::kMaxPatternBytes + 1, 'a'));
    expect_rejected("(?:a{1000}){1000}");  // program too large
    expect_rejected(std::string(Pattern::kMaxNesting + 1, '(') + std::string(Pattern::kMaxNesting + 1, ')'));
    expect_rejected("\xFF");  // patterns must be UTF-8
    EXPECT_NO_THROW(Pattern(std::string(Pattern::kMaxNesting, '(') + std::string(Pattern::kMaxNesting, ')')));
    EXPECT_NO_THROW(Pattern("a{1000}"));
}

// The review's crash: std::regex recursed once per character, so a 30 KB string
// overflowed the stack with any pattern. Here a megabyte is fine, and the classic
// catastrophic-backtracking patterns stay linear.
TEST(Pattern, LongTextsAreLinear) {
    const std::string mb(256 << 10, 'a');
    const auto start = std::chrono::steady_clock::now();
    EXPECT_TRUE(matches(".*", mb));
    EXPECT_TRUE(matches("([a-z0-9]|_|-)*", mb));
    EXPECT_FALSE(matches("([a-z0-9]|_|-)*", mb + "!"));
    EXPECT_FALSE(matches("(a|aa)*b", std::string(100000, 'a')));
    EXPECT_FALSE(matches("(a+)+b", std::string(100000, 'a')));
    EXPECT_TRUE(matches("(?:a?){50}a{50}", std::string(50, 'a')));
    const auto elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_LT(std::chrono::duration_cast<std::chrono::seconds>(elapsed).count(), 30);
}

// On short ASCII texts std::regex is safe and agrees with ECMAScript, so the two engines
// must give the same answer for every pattern in the shared subset.
TEST(Pattern, AgreesWithStdRegexOnShortTexts) {
    std::mt19937 rng(20261113);
    auto pick = [&](size_t n) { return static_cast<size_t>(rng() % n); };
    const char* atoms[] = {"a", "b", "c", "1", " ", "[ab]", "[^a]", "[a-c]", "\\d", "\\w", "\\s", ".", "[\\w ]"};
    const char* quants[] = {"*", "+", "?", "{2}", "{1,2}", "{0,}", "{1,3}", "*?", "+?"};
    std::function<std::string(int)> gen = [&](int depth) -> std::string {
        const size_t choice = depth <= 0 ? 0 : pick(6);
        switch (choice) {
            case 0: return atoms[pick(std::size(atoms))];
            case 1: return "(" + gen(depth - 1) + ")";
            case 2: return "(?:" + gen(depth - 1) + ")";
            case 3: return gen(depth - 1) + gen(depth - 1);
            case 4: return gen(depth - 1) + "|" + gen(depth - 1);
            default: return "(?:" + gen(depth - 1) + ")" + quants[pick(std::size(quants))];
        }
    };
    const std::string alphabet = "abc1 _";
    int compared = 0;
    for (int round = 0; round < 3000; ++round) {
        const std::string pattern = gen(static_cast<int>(pick(4)) + 1);
        std::regex reference;
        try {
            reference = std::regex(pattern, std::regex::ECMAScript);
        } catch (const std::regex_error&) {
            continue;
        }
        const Pattern ours(pattern);
        for (int t = 0; t < 8; ++t) {
            std::string text;
            for (size_t len = pick(7); len > 0; --len) text += alphabet[pick(alphabet.size())];
            bool expected = false;
            try {
                expected = std::regex_match(text, reference);
            } catch (const std::regex_error&) {
                continue;  // std::regex gave up (error_complexity): the backtracking we avoid
            }
            ASSERT_EQ(ours.full_match(text), expected) << "pattern " << pattern << " text \"" << text << "\"";
            ++compared;
        }
    }
    EXPECT_GT(compared, 20000);
}

// --- Schema -------------------------------------------------------------------------------

TEST(SchemaFixes, PatternOnLongStringDoesNotCrash) {
    // The review's probe: a harmless-looking username pattern against a long value.
    auto schema = SchemaBuilder("user")
                      .field("name", "string")
                      .withConstraint("name", Map{{"pattern", String("([a-z0-9]|_|-)*")}})
                      .build();
    EXPECT_TRUE(schema->validate(Map{{"name", String(std::string(256 << 10, 'a'))}}));
    EXPECT_FALSE(schema->validate(Map{{"name", String(std::string(256 << 10, 'a') + "!")}}));
}

TEST(SchemaFixes, LongVersionStringIsAnErrorNotACrash) {
    const Value doc = Map{{"name", String("s")}, {"version", String(std::string(100000, '1') + ".0.0")}};
    try {
        Schema::fromValue(doc);
        ADD_FAILURE() << "accepted a 100 KB version";
    } catch (const BtoonException& e) {
        EXPECT_EQ(e.code(), ErrorCode::invalid_argument);
        EXPECT_LT(std::string(e.what()).size(), 200u);
    }
    EXPECT_EQ(SchemaVersion::fromString("1.2.3").toString(), "1.2.3");
    EXPECT_EQ(SchemaVersion::fromString("4294967295.0.10").toString(), "4294967295.0.10");
    for (const char* bad : {"", "1", "1.2", "1.2.3.4", "1..3", "a.b.c", "1.2.3 ", " 1.2.3", "4294967296.0.0",
                            "1.2.-3", "+1.2.3"}) {
        EXPECT_THROW(SchemaVersion::fromString(bad), BtoonException) << bad;
    }
}

TEST(SchemaFixes, PresentOptionalFieldsAreValidated) {
    auto schema = SchemaBuilder("person")
                      .field("name", "string")
                      .optionalField("age", "int")
                      .withConstraint("age", Map{{"min", Int(0)}})
                      .build();
    EXPECT_TRUE(schema->validate(Map{{"name", String("a")}}));
    EXPECT_TRUE(schema->validate(Map{{"name", String("a")}, {"age", Int(30)}}));
    EXPECT_FALSE(schema->validate(Map{{"name", String("a")}, {"age", Int(-5)}}));
    EXPECT_FALSE(schema->validate(Map{{"name", String("a")}, {"age", String("x")}}));
}

TEST(SchemaFixes, MinAndMaxCompareExactlyAcrossNumberTypes) {
    auto range = [](Value min, Value max) {
        return SchemaBuilder("r").field("n", "any").withConstraint("n", Map{{"min", min}, {"max", max}}).build();
    };
    auto ok = [](const std::shared_ptr<Schema>& s, Value v) { return s->validate(Map{{"n", std::move(v)}}); };

    auto s = range(Int(0), Uint(10));
    EXPECT_TRUE(ok(s, Uint(5)));
    EXPECT_TRUE(ok(s, Float(10.0)));
    EXPECT_TRUE(ok(s, Float(0.0)));
    EXPECT_TRUE(ok(s, Float(-0.0)));
    EXPECT_FALSE(ok(s, Float(10.5)));
    EXPECT_FALSE(ok(s, Float(-0.5)));
    EXPECT_FALSE(ok(s, Int(-1)));
    EXPECT_FALSE(ok(s, Uint(std::numeric_limits<uint64_t>::max())));
    EXPECT_FALSE(ok(s, Float(std::nan(""))));
    EXPECT_FALSE(ok(s, Float(std::numeric_limits<double>::infinity())));
    EXPECT_FALSE(ok(s, BigInt{{0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}}));  // 2^64
    EXPECT_FALSE(ok(s, BigInt{{0xff}}));                                                 // -1
    EXPECT_TRUE(ok(s, BigInt{{0x07}}));                                                  // 7
    EXPECT_TRUE(ok(s, String("not a number")));  // min/max apply to numbers only

    // 2^63 is one more than INT64_MAX: a double rounds there, so compare exactly.
    auto s2 = range(Int(std::numeric_limits<int64_t>::min()), Int(std::numeric_limits<int64_t>::max()));
    EXPECT_TRUE(ok(s2, Int(std::numeric_limits<int64_t>::max())));
    EXPECT_FALSE(ok(s2, Uint(uint64_t{1} << 63)));
    EXPECT_FALSE(ok(s2, Float(9223372036854775808.0)));
    EXPECT_TRUE(ok(s2, Float(-9223372036854775808.0)));
    EXPECT_FALSE(ok(s2, Float(-9223372036854777856.0)));  // the next double below -2^63

    auto s3 = range(Float(0.5), Float(1e300));
    EXPECT_FALSE(ok(s3, Int(0)));
    EXPECT_TRUE(ok(s3, Int(1)));
    EXPECT_TRUE(ok(s3, Uint(std::numeric_limits<uint64_t>::max())));
    std::vector<uint8_t> big(130, 0x7f);  // about 2^1039, above 1e300 (about 2^997)
    EXPECT_FALSE(ok(s3, BigInt{big}));
}

TEST(SchemaFixes, BrokenConstraintsFailWhenTheSchemaIsBuilt) {
    auto build = [](Value constraint) {
        return SchemaBuilder("b").field("x", "any").withConstraint("x", std::move(constraint)).build();
    };
    for (Value bad : {Value(Map{{"min", String("5")}}), Value(Map{{"max", Float(std::nan(""))}}),
                      Value(Map{{"pattern", String("(")}}), Value(Map{{"pattern", Int(1)}}),
                      Value(Map{{"pattern", String("(?=x)")}})}) {
        try {
            build(bad);
            ADD_FAILURE() << "accepted a broken constraint";
        } catch (const BtoonException& e) {
            EXPECT_EQ(e.code(), ErrorCode::invalid_argument);
        }
    }
    const Value doc = Map{{"name", String("s")},
                          {"fields", Array{Map{{"name", String("x")},
                                               {"type", String("string")},
                                               {"constraints", Map{{"pattern", String("a(")}}}}}}};
    EXPECT_THROW(Schema::fromValue(doc), BtoonException);
}

TEST(SchemaFixes, InferredBoundsAreExactAndAcceptTheirOwnInput) {
    // Review 2026-11-13: inference went through double, so UINT64_MAX overflowed when cast
    // back (UB) and bounds near 2^63 were off by up to 1024.
    const uint64_t big = std::numeric_limits<uint64_t>::max();
    const int64_t low = std::numeric_limits<int64_t>::min() + 1;
    Array rows{Map{{"id", Uint(big)}, {"n", Int(low)}}, Map{{"id", Int(1)}, {"n", Int(5)}}};
    SchemaInferrer inferrer;
    Schema schema = inferrer.inferFromArray(rows);
    for (const auto& row : rows) EXPECT_TRUE(schema.validate(row));
    EXPECT_FALSE(schema.validate(Map{{"id", Int(1)}, {"n", Int(std::numeric_limits<int64_t>::min())}}));
}

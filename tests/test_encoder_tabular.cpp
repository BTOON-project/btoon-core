// Tabular (columnar) encoding: when it is used, that it pays off, and that
// options propagate into nested values (docs/SPEC.md §5.6).
#include <gtest/gtest.h>

#include <algorithm>

#include "btoon/btoon.h"
#include "btoon/encoder.h"

using namespace btoon;

namespace {

Array records(int n) {
    Array rows;
    for (int i = 0; i < n; ++i) {
        rows.push_back(Map{{"id", Int(i)}, {"name", String("user" + std::to_string(i))}, {"active", Bool(i % 2 == 0)}});
    }
    return rows;
}

bool contains_tabular_ext(const std::vector<uint8_t>& bytes) {
    // ext8/16/32 header followed by type byte 0xf6 (-10)
    for (size_t i = 0; i + 5 < bytes.size(); ++i) {
        if (bytes[i] == 0xc7 && bytes[i + 2] == 0xf6) return true;
        if (bytes[i] == 0xc8 && bytes[i + 3] == 0xf6) return true;
        if (bytes[i] == 0xc9 && bytes[i + 5] == 0xf6) return true;
    }
    return false;
}

}  // namespace

TEST(TabularEncoding, UsedWhenSmaller) {
    Value v = records(10);
    auto tab = encode(v);
    EncodeOptions plain;
    plain.auto_tabular = false;
    auto flat = encode(v, plain);
    EXPECT_TRUE(contains_tabular_ext(tab));
    EXPECT_FALSE(contains_tabular_ext(flat));
    EXPECT_LT(tab.size(), flat.size());
    EXPECT_EQ(decode(tab), v);
    EXPECT_EQ(decode(flat), v);
}

TEST(TabularEncoding, SmallTablesStayPlain) {
    Value v = records(2);
    auto bytes = encode(v);
    EXPECT_EQ(bytes[0], 0x92);  // plain fixarray: tabular would be larger here
    EXPECT_EQ(decode(bytes), v);
}

TEST(TabularEncoding, NeverLargerThanPlain) {
    for (int n = 0; n <= 40; ++n) {
        Value v = records(n);
        EncodeOptions plain;
        plain.auto_tabular = false;
        EXPECT_LE(encode(v).size(), encode(v, plain).size()) << n << " rows";
    }
}

TEST(TabularEncoding, NonUniformRowsArePlain) {
    Array rows = records(10);
    std::get<Map>(rows[3]).erase("name");
    EXPECT_FALSE(is_tabular(rows));
    EXPECT_FALSE(contains_tabular_ext(encode(Value(rows))));
}

TEST(TabularEncoding, DisablingPropagatesToNestedArrays) {
    Value v = Map{{"outer", Array{Map{{"rows", records(10)}}}}};
    EncodeOptions plain;
    plain.auto_tabular = false;
    EXPECT_FALSE(contains_tabular_ext(encode(v, plain)));
    EXPECT_TRUE(contains_tabular_ext(encode(v)));
}

TEST(TabularEncoding, NestedTablesRoundTrip) {
    Array groups;
    for (int g = 0; g < 5; ++g) groups.push_back(Map{{"g", Int(g)}, {"rows", records(6)}});
    Value v = groups;
    EXPECT_EQ(decode(encode(v)), v);
}

TEST(TabularEncoding, CellsCanHoldAnyValue) {
    Array rows;
    for (int i = 0; i < 8; ++i) {
        rows.push_back(Map{{"ts", Timestamp(1704067200 + i, 5)},
                           {"vec", VectorFloat{{float(i), 1.5f}}},
                           {"nested", Map{{"k", Array{Int(i), nullptr}}}},
                           {"bin", Binary(static_cast<size_t>(i), uint8_t(7))}});
    }
    Value v = rows;
    auto bytes = encode(v);
    EXPECT_TRUE(contains_tabular_ext(bytes));
    EXPECT_EQ(decode(bytes), v);
}

TEST(TabularEncoding, LargeTableUsesExt32) {
    Value v = records(10000);  // payload > 64 KiB
    auto bytes = encode(v);
    EXPECT_EQ(bytes[0], 0xc9);
    EXPECT_EQ(bytes[5], 0xf6);
    EXPECT_EQ(decode(bytes), v);
}

TEST(TabularEncoding, IncrementalApiMatchesValueEncoding) {
    // Two-level nesting through the incremental API must equal encode(Value).
    Array inner;
    for (int i = 0; i < 8; ++i) inner.push_back(Map{{"n", Int(i)}, {"sq", Int(i * i)}});
    Array outer;
    for (int g = 0; g < 6; ++g) outer.push_back(Map{{"g", Int(g)}, {"rows", inner}});
    ASSERT_TRUE(is_tabular(outer));  // 6 groups needed: per-row cost 8 > 41/N
    ASSERT_TRUE(is_tabular(inner));  // 8 rows: the size rule picks tabular (6 rows would not)

    Encoder enc;
    const std::string_view outer_names[] = {"g", "rows"};
    const std::string_view inner_names[] = {"n", "sq"};
    enc.beginTabular(outer_names, 6);
    enc.beginTabularColumn();
    for (int g = 0; g < 6; ++g) enc.encodeInt(g);
    enc.endTabularColumn();
    enc.beginTabularColumn();
    for (int g = 0; g < 6; ++g) {
        enc.beginTabular(inner_names, 8);
        enc.beginTabularColumn();
        for (int i = 0; i < 8; ++i) enc.encodeInt(i);
        enc.endTabularColumn();
        enc.beginTabularColumn();
        for (int i = 0; i < 8; ++i) enc.encodeInt(i * i);
        enc.endTabularColumn();
        enc.endTabular();
    }
    enc.endTabularColumn();
    enc.endTabular();
    EXPECT_EQ(enc.release(), encode(Value(outer)));
}

TEST(TabularEncoding, IncrementalApiMisuseIsAnError) {
    Encoder enc;
    EXPECT_THROW(enc.beginTabularColumn(), BtoonException);
    const std::string_view names[] = {"a"};
    enc.beginTabular(names, 2);
    EXPECT_THROW(enc.endTabular(), BtoonException);  // column missing
    enc.beginTabularColumn();
    EXPECT_THROW(enc.beginTabularColumn(), BtoonException);  // already open
    enc.encodeInt(1);
    enc.encodeInt(2);
    enc.endTabularColumn();
    EXPECT_THROW(enc.endTabularColumn(), BtoonException);
    enc.endTabular();
    EXPECT_EQ(decode(enc.release()), Value(Array{Map{{"a", Int(1)}}, Map{{"a", Int(2)}}}));
    Encoder open;
    open.beginTabular(names, 1);
    EXPECT_THROW(open.release(), BtoonException);  // unterminated
}

TEST(TabularEncoding, SizeRuleHelper) {
    const size_t one_two_byte_key[] = {2};
    EXPECT_FALSE(tabular_beneficial(7, one_two_byte_key));
    EXPECT_TRUE(tabular_beneficial(8, one_two_byte_key));
    EXPECT_FALSE(tabular_beneficial(1000, std::span<const size_t>{}));
}

// SPDX-FileCopyrightText: 2025 Alvar Laigna <https://alvarlaigna.com>
// SPDX-License-Identifier: MIT
//
// Compiles the C++ that `btoon-schema generate -l cpp -n gen` writes for
// codegen.schema.json (CMakeLists.txt runs the generator first) and reads decoded data
// with it. Before 0.3.0 the generated code threw bad_variant_access on valid data (an
// integer in a "uint" or "number" field) and didn't compile for names like "first-name".
#include "sensor_reading.h"

#include <cstdio>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "codegen check failed: %s\n", what);
        ++failures;
    }
}
template <class F>
void expect_invalid(F&& f, const char* what) {
    try {
        f();
        check(false, what);
    } catch (const btoon::BtoonException& e) {
        check(e.code() == btoon::ErrorCode::invalid_argument, what);
    }
}
}  // namespace

int main() {
    using namespace btoon;
    const BigInt big{{0x01, 0, 0, 0, 0, 0, 0, 0, 0}};
    const Map fields{{"count", Uint(5)},  // decodes as Int (SPEC: smallest form)
                     {"value", Int(2)},   // an integer in a "number" field
                     {"id", Int(-1)},
                     {"class", true},
                     {"m", Map{{"k", Int(1)}}},
                     {"first-name", String("Ada")},
                     {"2fa", false},
                     {"self", Float(0.5)},
                     {"at", Timestamp(1, 2)},
                     {"big", big},
                     {"emb", VectorFloat{{1.5f}}},
                     {"blob", Binary{1, 2}},
                     {"fail", String("anything")},
                     {"quote\"key", String("q")}};
    const Value decoded = decode(encode(Value(fields)));

    const gen::SensorReading r = gen::SensorReading::fromValue(decoded);
    check(r.count == 5, "count");
    check(r.value == 2.0, "value");
    check(r.id == -1, "id");
    check(r.class_, "class");
    check(r.first_name && *r.first_name == "Ada", "first-name");
    check(r._2fa && !*r._2fa, "2fa");
    check(r.self && *r.self == 0.5, "self");
    check(r.at && r.at->seconds == 1 && r.at->nanoseconds == 2, "at");
    check(r.big && *r.big == Value(big), "big");
    check(r.emb && r.emb->data.size() == 1, "emb");
    check(r.blob && r.blob->size() == 2, "blob");
    check(r.fail_ && *r.fail_ == Value(String("anything")), "fail");
    check(r.quote_key && *r.quote_key == "q", "quote\"key");
    check(!r.note, "note is absent");

    // toValue() writes the same document back (value comes back as a float).
    Map expected = fields;
    expected["value"] = Float(2.0);
    check(r.toValue() == Value(expected), "toValue");
    check(gen::SensorReading::fromValue(r.toValue()).toValue() == r.toValue(), "round trip");

    // Wrong types and missing fields are BtoonException(invalid_argument).
    expect_invalid([&] { gen::SensorReading::fromValue(Value(Int(1))); }, "not a map");
    Map missing = fields;
    missing.erase("id");
    expect_invalid([&] { gen::SensorReading::fromValue(Value(missing)); }, "missing id");
    Map negative = fields;
    negative["count"] = Int(-1);
    expect_invalid([&] { gen::SensorReading::fromValue(Value(negative)); }, "negative uint");
    Map text = fields;
    text["value"] = String("2");
    expect_invalid([&] { gen::SensorReading::fromValue(Value(text)); }, "string in a number field");
    Map huge = fields;
    huge["id"] = Uint(UINT64_MAX);
    expect_invalid([&] { gen::SensorReading::fromValue(Value(huge)); }, "uint64 in an int field");

    if (failures == 0) std::printf("generated C++ works\n");
    return failures == 0 ? 0 : 1;
}

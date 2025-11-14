#include <gtest/gtest.h>
#include "btoon/schema.h"

using namespace btoon;

TEST(SchemaTest, BasicValidation) {
    Value schema_def = Map{
        {"name", String("string")},
        {"age", String("int")}
    };
    Schema schema(schema_def);

    Value valid_user = Map{
        {"name", String("Alice")},
        {"age", Int(30)}
    };
    EXPECT_TRUE(schema.validate(valid_user));

    Value invalid_user_type = Map{
        {"name", String("Bob")},
        {"age", String("25")} // age should be Int
    };
    EXPECT_FALSE(schema.validate(invalid_user_type));

    Value missing_key = Map{
        {"name", String("Charlie")}
    };
    EXPECT_FALSE(schema.validate(missing_key));
}

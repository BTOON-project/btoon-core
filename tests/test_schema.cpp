#include <gtest/gtest.h>
#include "btoon/schema.h"

using namespace btoon;

TEST(SchemaTest, BasicValidation) {
    // Old style schema definition for backward compatibility
    Value old_schema_def = Map{
        {"name", String("string")},
        {"age", String("int")}
    };
    
    // Convert to new schema format
    Value schema_def = Map{
        {"name", String("user")},
        {"version", String("1.0.0")},
        {"fields", Array{
            Map{
                {"name", String("name")},
                {"type", String("string")},
                {"required", Bool(true)}
            },
            Map{
                {"name", String("age")},
                {"type", String("int")},
                {"required", Bool(true)}
            }
        }}
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

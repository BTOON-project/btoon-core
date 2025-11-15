#include <gtest/gtest.h>
#include "btoon/schema.h"
#include "btoon/btoon.h"
#include <iostream>

using namespace btoon;

class SchemaInferenceTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Sample data for testing
        simple_map = Map{
            {"id", Int(1)},
            {"name", String("John")},
            {"age", Int(30)},
            {"active", Bool(true)}
        };
        
        // Tabular data with consistent types
        tabular_data = Array{
            Map{{"id", Int(1)}, {"name", String("Alice")}, {"age", Int(25)}},
            Map{{"id", Int(2)}, {"name", String("Bob")}, {"age", Int(30)}},
            Map{{"id", Int(3)}, {"name", String("Charlie")}, {"age", Int(35)}}
        };
        
        // Tabular data with optional fields
        tabular_with_optional = Array{
            Map{{"id", Int(1)}, {"name", String("Alice")}, {"email", String("alice@test.com")}},
            Map{{"id", Int(2)}, {"name", String("Bob")}},  // Missing email
            Map{{"id", Int(3)}, {"name", String("Charlie")}, {"email", String("charlie@test.com")}},
            Map{{"id", Int(4)}, {"name", String("David")}},  // Missing email
            Map{{"id", Int(5)}, {"name", String("Eve")}, {"email", String("eve@test.com")}}
        };
        
        // Mixed numeric types
        mixed_numeric = Array{
            Map{{"value", Int(10)}},
            Map{{"value", Uint(20)}},
            Map{{"value", Float(30.5)}}
        };
        
        // Data with enum candidates
        enum_data = Array{
            Map{{"status", String("pending")}, {"priority", String("high")}},
            Map{{"status", String("active")}, {"priority", String("low")}},
            Map{{"status", String("pending")}, {"priority", String("medium")}},
            Map{{"status", String("completed")}, {"priority", String("high")}},
            Map{{"status", String("active")}, {"priority", String("low")}}
        };
    }
    
    Value simple_map;
    Value tabular_data;
    Value tabular_with_optional;
    Value mixed_numeric;
    Value enum_data;
};

TEST_F(SchemaInferenceTest, InferFromSimpleMap) {
    InferenceOptions options;
    options.merge_numeric_types = false;  // Test without merging numeric types
    SchemaInferrer inferrer(options);
    Schema schema = inferrer.infer(simple_map, "TestSchema");
    
    EXPECT_EQ(schema.getName(), "TestSchema");
    EXPECT_EQ(schema.getVersion(), SchemaVersion(1, 0, 0));
    
    auto fields = schema.getFields();
    EXPECT_EQ(fields.size(), 4u);
    
    // Check field types
    auto id_field = schema.getField("id");
    ASSERT_TRUE(id_field.has_value());
    EXPECT_EQ(id_field->type, "int");
    EXPECT_TRUE(id_field->required);
    
    auto name_field = schema.getField("name");
    ASSERT_TRUE(name_field.has_value());
    EXPECT_EQ(name_field->type, "string");
    
    auto age_field = schema.getField("age");
    ASSERT_TRUE(age_field.has_value());
    EXPECT_EQ(age_field->type, "int");
    
    auto active_field = schema.getField("active");
    ASSERT_TRUE(active_field.has_value());
    EXPECT_EQ(active_field->type, "bool");
    
    auto stats = inferrer.getStatistics();
    EXPECT_EQ(stats.samples_analyzed, 1u);
    EXPECT_EQ(stats.fields_discovered, 4u);
}

TEST_F(SchemaInferenceTest, InferFromTabularData) {
    SchemaInferrer inferrer;
    Schema schema = inferrer.inferFromArray(std::get<Array>(tabular_data), "TableSchema");
    
    auto fields = schema.getFields();
    EXPECT_EQ(fields.size(), 3u);
    
    // All fields should be required (present in all rows)
    for (const auto& field : fields) {
        EXPECT_TRUE(field.required) << "Field " << field.name << " should be required";
    }
    
    auto stats = inferrer.getStatistics();
    EXPECT_EQ(stats.samples_analyzed, 3u);
    EXPECT_EQ(stats.fields_discovered, 3u);
}

TEST_F(SchemaInferenceTest, InferOptionalFields) {
    InferenceOptions options;
    options.required_threshold = 0.6;  // Field needs to be in 60% of samples
    
    SchemaInferrer inferrer(options);
    Schema schema = inferrer.inferFromArray(std::get<Array>(tabular_with_optional));
    
    auto id_field = schema.getField("id");
    ASSERT_TRUE(id_field.has_value());
    EXPECT_TRUE(id_field->required);  // Present in all rows
    
    auto name_field = schema.getField("name");
    ASSERT_TRUE(name_field.has_value());
    EXPECT_TRUE(name_field->required);  // Present in all rows
    
    auto email_field = schema.getField("email");
    ASSERT_TRUE(email_field.has_value());
    EXPECT_FALSE(email_field->required);  // Present in 60% (3/5), below threshold
    
    auto stats = inferrer.getStatistics();
    EXPECT_EQ(stats.optional_fields, 1u);  // email is optional
    EXPECT_DOUBLE_EQ(stats.field_presence_ratio["email"], 0.6);
}

TEST_F(SchemaInferenceTest, InferWithMergedNumericTypes) {
    InferenceOptions options;
    options.merge_numeric_types = true;
    
    SchemaInferrer inferrer(options);
    Schema schema = inferrer.inferFromArray(std::get<Array>(mixed_numeric));
    
    auto value_field = schema.getField("value");
    ASSERT_TRUE(value_field.has_value());
    EXPECT_EQ(value_field->type, "number");  // Merged to generic number type
}

TEST_F(SchemaInferenceTest, InferWithStrictTypes) {
    InferenceOptions options;
    options.merge_numeric_types = false;
    options.strict_types = false;  // Allow type variations but don't merge
    
    SchemaInferrer inferrer(options);
    Schema schema = inferrer.inferFromArray(std::get<Array>(mixed_numeric));
    
    auto value_field = schema.getField("value");
    ASSERT_TRUE(value_field.has_value());
    EXPECT_EQ(value_field->type, "any");  // Mixed types become "any"
}

TEST_F(SchemaInferenceTest, InferEnumConstraints) {
    InferenceOptions options;
    options.infer_constraints = true;
    options.max_enum_values = 5;  // Allow up to 5 unique values for enum
    
    SchemaInferrer inferrer(options);
    Schema schema = inferrer.inferFromArray(std::get<Array>(enum_data));
    
    auto status_field = schema.getField("status");
    ASSERT_TRUE(status_field.has_value());
    ASSERT_TRUE(status_field->constraints.has_value());
    
    // Check enum constraint
    auto constraints = std::get<Map>(status_field->constraints.value());
    ASSERT_TRUE(constraints.find("enum") != constraints.end());
    
    auto enum_values = std::get<Array>(constraints["enum"]);
    EXPECT_EQ(enum_values.size(), 3u);  // pending, active, completed
    
    auto stats = inferrer.getStatistics();
    EXPECT_GE(stats.enum_fields, 1u);  // At least status should be enum
}

TEST_F(SchemaInferenceTest, InferStringConstraints) {
    Array string_data = Array{
        Map{{"text", String("Hello")}},
        Map{{"text", String("World")}},
        Map{{"text", String("Test")}},
        Map{{"text", String("Example")}}
    };
    
    InferenceOptions options;
    options.infer_constraints = true;
    
    SchemaInferrer inferrer(options);
    Schema schema = inferrer.inferFromArray(string_data);
    
    auto text_field = schema.getField("text");
    ASSERT_TRUE(text_field.has_value());
    ASSERT_TRUE(text_field->constraints.has_value());
    
    auto constraints = std::get<Map>(text_field->constraints.value());
    ASSERT_TRUE(constraints.find("minLength") != constraints.end());
    ASSERT_TRUE(constraints.find("maxLength") != constraints.end());
    
    EXPECT_EQ(std::get<Uint>(constraints["minLength"]), 4u);  // "Test"
    EXPECT_EQ(std::get<Uint>(constraints["maxLength"]), 7u);  // "Example"
}

TEST_F(SchemaInferenceTest, InferNumberConstraints) {
    Array numeric_data = Array{
        Map{{"score", Int(10)}},
        Map{{"score", Int(20)}},
        Map{{"score", Int(15)}},
        Map{{"score", Int(25)}}
    };
    
    InferenceOptions options;
    options.infer_constraints = true;
    
    SchemaInferrer inferrer(options);
    Schema schema = inferrer.inferFromArray(numeric_data);
    
    auto score_field = schema.getField("score");
    ASSERT_TRUE(score_field.has_value());
    ASSERT_TRUE(score_field->constraints.has_value());
    
    auto constraints = std::get<Map>(score_field->constraints.value());
    ASSERT_TRUE(constraints.find("min") != constraints.end());
    ASSERT_TRUE(constraints.find("max") != constraints.end());
    
    EXPECT_EQ(std::get<Int>(constraints["min"]), 10);
    EXPECT_EQ(std::get<Int>(constraints["max"]), 25);
}

TEST_F(SchemaInferenceTest, MergeSchemas) {
    std::vector<Schema> schemas;
    schemas.push_back(Schema("Schema1", SchemaVersion(1, 0, 0), {
        {"id", "int", true, std::nullopt, std::nullopt, std::nullopt},
        {"name", "string", true, std::nullopt, std::nullopt, std::nullopt}
    }));
    
    schemas.push_back(Schema("Schema2", SchemaVersion(1, 0, 0), {
        {"id", "int", true, std::nullopt, std::nullopt, std::nullopt},
        {"email", "string", true, std::nullopt, std::nullopt, std::nullopt}
    }));
    
    schemas.push_back(Schema("Schema3", SchemaVersion(1, 0, 0), {
        {"id", "uint", true, std::nullopt, std::nullopt, std::nullopt},  // Different type
        {"name", "string", true, std::nullopt, std::nullopt, std::nullopt},
        {"age", "int", true, std::nullopt, std::nullopt, std::nullopt}
    }));
    
    InferenceOptions options;
    options.merge_numeric_types = true;
    
    SchemaInferrer inferrer(options);
    Schema merged = inferrer.merge(std::move(schemas), "MergedSchema");
    
    auto fields = merged.getFields();
    
    // Check merged fields
    auto id_field = merged.getField("id");
    ASSERT_TRUE(id_field.has_value());
    EXPECT_EQ(id_field->type, "number");  // Merged int and uint
    EXPECT_TRUE(id_field->required);  // Present in all schemas
    
    auto name_field = merged.getField("name");
    ASSERT_TRUE(name_field.has_value());
    EXPECT_FALSE(name_field->required);  // Not in schema2
    
    auto email_field = merged.getField("email");
    ASSERT_TRUE(email_field.has_value());
    EXPECT_FALSE(email_field->required);  // Not in schema1 and schema3
    
    auto age_field = merged.getField("age");
    ASSERT_TRUE(age_field.has_value());
    EXPECT_FALSE(age_field->required);  // Only in schema3
}

TEST_F(SchemaInferenceTest, InferFromPrimitiveValue) {
    Value primitive = String("test");
    
    SchemaInferrer inferrer;
    Schema schema = inferrer.infer(primitive, "StringSchema");
    
    auto fields = schema.getFields();
    EXPECT_EQ(fields.size(), 1u);
    
    auto value_field = schema.getField("value");
    ASSERT_TRUE(value_field.has_value());
    EXPECT_EQ(value_field->type, "string");
}

TEST_F(SchemaInferenceTest, InferFromArrayOfPrimitives) {
    Array primitive_array = Array{
        Int(1), Int(2), Int(3), Int(4), Int(5)
    };
    
    SchemaInferrer inferrer;
    Schema schema = inferrer.inferFromArray(primitive_array);
    
    auto fields = schema.getFields();
    EXPECT_EQ(fields.size(), 1u);
    
    auto items_field = schema.getField("items");
    ASSERT_TRUE(items_field.has_value());
    EXPECT_EQ(items_field->type, "int");
    ASSERT_TRUE(items_field->constraints.has_value());
    
    // Check inferred constraints
    auto constraints = std::get<Map>(items_field->constraints.value());
    EXPECT_EQ(std::get<Int>(constraints["min"]), 1);
    EXPECT_EQ(std::get<Int>(constraints["max"]), 5);
}

TEST_F(SchemaInferenceTest, ConvenienceFunction) {
    // Test the convenience function
    Schema schema = inferSchema(simple_map);
    
    EXPECT_EQ(schema.getName(), "InferredSchema");
    auto fields = schema.getFields();
    EXPECT_EQ(fields.size(), 4u);
}

TEST_F(SchemaInferenceTest, ValidationAfterInference) {
    // Infer schema and then validate data against it
    SchemaInferrer inferrer;
    Schema schema = inferrer.inferFromArray(std::get<Array>(tabular_data));
    
    // Validate original data
    for (const auto& row : std::get<Array>(tabular_data)) {
        EXPECT_TRUE(schema.validate(row));
    }
    
    // Invalid data (missing required field)
    Value invalid = Map{{"id", Int(4)}, {"age", Int(40)}};  // Missing "name"
    EXPECT_FALSE(schema.validate(invalid));
    
    // Invalid data (wrong type)
    Value wrong_type = Map{{"id", String("not_an_int")}, {"name", String("Test")}, {"age", Int(25)}};
    EXPECT_FALSE(schema.validate(wrong_type));
}

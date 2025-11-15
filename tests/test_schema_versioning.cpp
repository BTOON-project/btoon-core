#include <gtest/gtest.h>
#include "btoon/schema.h"

using namespace btoon;

class SchemaVersioningTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create schemas for different versions
        schema_v1 = SchemaBuilder("user")
            .version(1, 0, 0)
            .description("User schema v1")
            .field("id", "int")
            .field("name", "string")
            .field("email", "string")
            .build();
            
        schema_v1_1 = SchemaBuilder("user")
            .version(1, 1, 0)
            .description("User schema v1.1 - added optional age")
            .field("id", "int")
            .field("name", "string")
            .field("email", "string")
            .optionalField("age", "int", Int(0))
            .build();
            
        schema_v2 = SchemaBuilder("user")
            .version(2, 0, 0)
            .description("User schema v2 - restructured")
            .field("id", "int")
            .field("profile", "map")  // name and email moved to profile
            .optionalField("age", "int", Int(0))
            .optionalField("created_at", "int")
            .build();
            
        // Sample data
        user_v1 = Map{
            {"id", Int(1)},
            {"name", String("Alice")},
            {"email", String("alice@example.com")}
        };
        
        user_v1_1 = Map{
            {"id", Int(2)},
            {"name", String("Bob")},
            {"email", String("bob@example.com")},
            {"age", Int(30)}
        };
        
        user_v2 = Map{
            {"id", Int(3)},
            {"profile", Map{
                {"name", String("Charlie")},
                {"email", String("charlie@example.com")}
            }},
            {"age", Int(25)},
            {"created_at", Int(1234567890)}
        };
    }
    
    std::shared_ptr<Schema> schema_v1;
    std::shared_ptr<Schema> schema_v1_1;
    std::shared_ptr<Schema> schema_v2;
    Value user_v1;
    Value user_v1_1;
    Value user_v2;
};

TEST_F(SchemaVersioningTest, VersionParsing) {
    SchemaVersion v1 = SchemaVersion::fromString("1.2.3");
    EXPECT_EQ(v1.major, 1u);
    EXPECT_EQ(v1.minor, 2u);
    EXPECT_EQ(v1.patch, 3u);
    EXPECT_EQ(v1.toString(), "1.2.3");
    
    SchemaVersion v2(2, 0, 1);
    EXPECT_EQ(v2.toString(), "2.0.1");
    
    EXPECT_THROW(SchemaVersion::fromString("invalid"), BtoonException);
}

TEST_F(SchemaVersioningTest, VersionComparison) {
    SchemaVersion v1(1, 0, 0);
    SchemaVersion v1_1(1, 1, 0);
    SchemaVersion v2(2, 0, 0);
    
    EXPECT_TRUE(v1 < v1_1);
    EXPECT_TRUE(v1_1 < v2);
    EXPECT_TRUE(v1 < v2);
    
    EXPECT_FALSE(v2 < v1);
    EXPECT_FALSE(v1_1 < v1);
    
    EXPECT_TRUE(v2 > v1);
    EXPECT_TRUE(v2 >= v2);
    EXPECT_TRUE(v1 <= v1_1);
}

TEST_F(SchemaVersioningTest, BasicValidation) {
    // v1 schema validates v1 data
    EXPECT_TRUE(schema_v1->validate(user_v1));
    
    // v1 schema rejects v1.1 data (has extra field)
    schema_v1->setEvolutionStrategy(EvolutionStrategy::STRICT);
    EXPECT_FALSE(schema_v1->validate(user_v1_1));
    
    // v1 schema accepts v1.1 data with flexible strategy
    schema_v1->setEvolutionStrategy(EvolutionStrategy::FLEXIBLE);
    EXPECT_TRUE(schema_v1->validate(user_v1_1));
    
    // v1.1 schema validates both v1 and v1.1 data
    EXPECT_TRUE(schema_v1_1->validate(user_v1));
    EXPECT_TRUE(schema_v1_1->validate(user_v1_1));
    
    // v2 schema validates v2 data
    EXPECT_TRUE(schema_v2->validate(user_v2));
    
    // v2 schema rejects v1 data (structure mismatch)
    EXPECT_FALSE(schema_v2->validate(user_v1));
}

TEST_F(SchemaVersioningTest, ValidationWithErrors) {
    Value invalid_user = Map{
        {"id", String("not-a-number")},  // Should be int
        {"name", String("Test")}
        // Missing email
    };
    
    auto errors = schema_v1->validateWithErrors(invalid_user);
    EXPECT_FALSE(errors.empty());
    
    // Should have error about wrong type for id
    bool has_type_error = false;
    bool has_missing_field = false;
    for (const auto& error : errors) {
        if (error.find("Invalid type") != std::string::npos) {
            has_type_error = true;
        }
        if (error.find("Missing required field") != std::string::npos) {
            has_missing_field = true;
        }
    }
    EXPECT_TRUE(has_type_error);
    EXPECT_TRUE(has_missing_field);
}

TEST_F(SchemaVersioningTest, FieldManagement) {
    auto schema = SchemaBuilder("test")
        .version(1, 0, 0)
        .field("id", "int")
        .build();
    
    // Add field
    SchemaField new_field{"status", "string", false, String("active"), std::nullopt, std::nullopt};
    schema->addField(new_field);
    
    auto field = schema->getField("status");
    ASSERT_TRUE(field.has_value());
    EXPECT_EQ(field->name, "status");
    EXPECT_EQ(field->type, "string");
    EXPECT_FALSE(field->required);
    
    // Remove field
    schema->removeField("status");
    field = schema->getField("status");
    EXPECT_FALSE(field.has_value());
    
    // Get all fields
    auto fields = schema->getFields();
    EXPECT_EQ(fields.size(), 1u);
    EXPECT_EQ(fields[0].name, "id");
}

TEST_F(SchemaVersioningTest, EvolutionStrategies) {
    // STRICT - no changes allowed
    schema_v1->setEvolutionStrategy(EvolutionStrategy::STRICT);
    EXPECT_FALSE(schema_v1->isCompatibleWith(*schema_v1_1));
    
    // ADDITIVE - only new optional fields
    schema_v1->setEvolutionStrategy(EvolutionStrategy::ADDITIVE);
    EXPECT_TRUE(schema_v1->isCompatibleWith(*schema_v1_1));
    EXPECT_FALSE(schema_v1->isCompatibleWith(*schema_v2));  // Structure changed
    
    // BACKWARD_COMPATIBLE
    schema_v1->setEvolutionStrategy(EvolutionStrategy::BACKWARD_COMPATIBLE);
    EXPECT_TRUE(schema_v1->isCompatibleWith(*schema_v1_1));
    
    // FLEXIBLE - always compatible
    schema_v1->setEvolutionStrategy(EvolutionStrategy::FLEXIBLE);
    EXPECT_TRUE(schema_v1->isCompatibleWith(*schema_v2));
}

TEST_F(SchemaVersioningTest, SchemaMigration) {
    // Define migration from v1 to v2
    schema_v1->addMigration(
        SchemaVersion(1, 0, 0),
        SchemaVersion(2, 0, 0),
        [](const Value& v1_data) -> Value {
            const Map* v1_map = std::get_if<Map>(&v1_data);
            if (!v1_map) return v1_data;
            
            Map v2_map;
            v2_map["id"] = v1_map->at("id");
            v2_map["profile"] = Map{
                {"name", v1_map->at("name")},
                {"email", v1_map->at("email")}
            };
            
            if (v1_map->count("age")) {
                v2_map["age"] = v1_map->at("age");
            }
            
            return v2_map;
        }
    );
    
    // Migrate v1 data to v2 format
    auto migrated = schema_v1->migrate(user_v1, SchemaVersion(2, 0, 0));
    ASSERT_TRUE(migrated.has_value());
    
    // Verify migrated data
    const Map* migrated_map = std::get_if<Map>(&migrated.value());
    ASSERT_NE(migrated_map, nullptr);
    
    EXPECT_EQ(std::get<Int>(migrated_map->at("id")), 1);
    
    const Map* profile = std::get_if<Map>(&migrated_map->at("profile"));
    ASSERT_NE(profile, nullptr);
    EXPECT_EQ(std::get<String>(profile->at("name")), "Alice");
    EXPECT_EQ(std::get<String>(profile->at("email")), "alice@example.com");
    
    // Validate migrated data with v2 schema
    EXPECT_TRUE(schema_v2->validate(migrated.value()));
}

TEST_F(SchemaVersioningTest, SchemaDiff) {
    auto differences = schema_v1->diff(*schema_v1_1);
    
    bool found_version_change = false;
    bool found_field_added = false;
    
    for (const auto& diff : differences) {
        if (diff.find("Version changed") != std::string::npos) {
            found_version_change = true;
        }
        if (diff.find("Field added: age") != std::string::npos) {
            found_field_added = true;
        }
    }
    
    EXPECT_TRUE(found_version_change);
    EXPECT_TRUE(found_field_added);
}

TEST_F(SchemaVersioningTest, SchemaSerialization) {
    // Serialize schema to Value
    Value schema_value = schema_v1->toValue();
    
    const Map* schema_map = std::get_if<Map>(&schema_value);
    ASSERT_NE(schema_map, nullptr);
    
    EXPECT_EQ(std::get<String>(schema_map->at("name")), "user");
    EXPECT_EQ(std::get<String>(schema_map->at("version")), "1.0.0");
    
    // Deserialize schema from Value
    Schema deserialized = Schema::fromValue(schema_value);
    
    EXPECT_EQ(deserialized.getName(), "user");
    EXPECT_EQ(deserialized.getVersion(), SchemaVersion(1, 0, 0));
    
    // Validate same data
    EXPECT_TRUE(deserialized.validate(user_v1));
}

TEST_F(SchemaVersioningTest, SchemaRegistry) {
    SchemaRegistry registry;
    
    // Register schemas
    registry.registerSchema("user", schema_v1);
    registry.registerSchema("user", schema_v1_1);
    registry.registerSchema("user", schema_v2);
    
    // Get specific version
    auto retrieved_v1 = registry.getSchema("user", SchemaVersion(1, 0, 0));
    ASSERT_NE(retrieved_v1, nullptr);
    EXPECT_EQ(retrieved_v1->getVersion(), SchemaVersion(1, 0, 0));
    
    // Get latest version
    auto latest = registry.getLatestSchema("user");
    ASSERT_NE(latest, nullptr);
    EXPECT_EQ(latest->getVersion(), SchemaVersion(2, 0, 0));
    
    // Get all versions
    auto versions = registry.getVersions("user");
    EXPECT_EQ(versions.size(), 3u);
    
    // Validate data (auto-detection)
    EXPECT_TRUE(registry.validate(user_v1));
    EXPECT_TRUE(registry.validate(user_v1_1));
    EXPECT_TRUE(registry.validate(user_v2));
    
    // Remove specific version
    registry.removeSchema("user", SchemaVersion(1, 0, 0));
    versions = registry.getVersions("user");
    EXPECT_EQ(versions.size(), 2);
    
    // Clear registry
    registry.clear();
    versions = registry.getVersions("user");
    EXPECT_TRUE(versions.empty());
}

TEST_F(SchemaVersioningTest, SchemaConstraints) {
    auto schema = SchemaBuilder("constrained")
        .version(1, 0, 0)
        .field("age", "int")
        .withConstraint("age", Map{
            {"min", Int(0)},
            {"max", Int(150)}
        })
        .field("email", "string")
        .withConstraint("email", Map{
            {"pattern", String(R"(^[\w\.-]+@[\w\.-]+\.\w+$)")}
        })
        .field("status", "string")
        .withConstraint("status", Map{
            {"enum", Array{String("active"), String("inactive"), String("pending")}}
        })
        .build();
    
    // Valid data
    Value valid = Map{
        {"age", Int(25)},
        {"email", String("user@example.com")},
        {"status", String("active")}
    };
    EXPECT_TRUE(schema->validate(valid));
    
    // Invalid age (negative)
    Value invalid_age = Map{
        {"age", Int(-5)},
        {"email", String("user@example.com")},
        {"status", String("active")}
    };
    EXPECT_FALSE(schema->validate(invalid_age));
    
    // Invalid email pattern
    Value invalid_email = Map{
        {"age", Int(25)},
        {"email", String("not-an-email")},
        {"status", String("active")}
    };
    EXPECT_FALSE(schema->validate(invalid_email));
    
    // Invalid enum value
    Value invalid_status = Map{
        {"age", Int(25)},
        {"email", String("user@example.com")},
        {"status", String("unknown")}
    };
    EXPECT_FALSE(schema->validate(invalid_status));
}

TEST_F(SchemaVersioningTest, PredefinedSchemas) {
    // Time series schema
    auto ts_schema = schemas::createTimeSeries();
    EXPECT_EQ(ts_schema->getName(), "time_series");
    
    Value ts_data = Map{
        {"timestamp", Int(1234567890)},
        {"value", Float(42.5)},
        {"tags", Map{{"sensor", String("temp1")}}}
    };
    EXPECT_TRUE(ts_schema->validate(ts_data));
    
    // Key-value schema
    auto kv_schema = schemas::createKeyValue();
    Value kv_data = Map{
        {"key", String("config:app:timeout")},
        {"value", Int(30)},
        {"ttl", Int(3600)}
    };
    EXPECT_TRUE(kv_schema->validate(kv_data));
    
    // Document schema
    auto doc_schema = schemas::createDocument();
    Value doc_data = Map{
        {"id", String("doc-123")},
        {"content", Map{
            {"title", String("Test Document")},
            {"body", String("Content here")}
        }},
        {"created_at", Int(1234567890)}
    };
    EXPECT_TRUE(doc_schema->validate(doc_data));
    
    // Table schema with custom columns
    std::vector<SchemaField> columns = {
        {"id", "int", true, std::nullopt, std::nullopt, std::nullopt},
        {"name", "string", true, std::nullopt, std::nullopt, std::nullopt},
        {"balance", "float", false, Float(0.0), std::nullopt, std::nullopt}
    };
    auto table_schema = schemas::createTable(columns);
    
    Value table_row = Map{
        {"id", Int(1)},
        {"name", String("Account A")},
        {"balance", Float(1000.50)}
    };
    EXPECT_TRUE(table_schema->validate(table_row));
}

TEST_F(SchemaVersioningTest, SchemaMetadata) {
    auto schema = SchemaBuilder("metadata_test")
        .version(1, 0, 0)
        .description("Schema with metadata")
        .field("id", "int")
        .metadata("author", String("test@example.com"))
        .metadata("created", Int(1234567890))
        .metadata("tags", Array{String("test"), String("example")})
        .build();
    
    EXPECT_EQ(schema->getDescription(), "Schema with metadata");
    
    auto author = schema->getMetadata("author");
    ASSERT_TRUE(author.has_value());
    EXPECT_EQ(std::get<String>(author.value()), "test@example.com");
    
    auto created = schema->getMetadata("created");
    ASSERT_TRUE(created.has_value());
    EXPECT_EQ(std::get<Int>(created.value()), 1234567890);
    
    auto tags = schema->getMetadata("tags");
    ASSERT_TRUE(tags.has_value());
    const Array* tags_array = std::get_if<Array>(&tags.value());
    ASSERT_NE(tags_array, nullptr);
    EXPECT_EQ(tags_array->size(), 2);
}

TEST_F(SchemaVersioningTest, SchemaBuilder) {
    auto schema = SchemaBuilder("builder_test")
        .version(2, 1, 3)
        .description("Built with fluent API")
        .field("id", "int")
        .field("name", "string")
        .optionalField("nickname", "string", String(""))
        .withDescription("nickname", "User's preferred nickname")
        .withConstraint("id", Map{{"min", Int(1)}})
        .evolutionStrategy(EvolutionStrategy::ADDITIVE)
        .metadata("domain", String("example.com"))
        .build();
    
    EXPECT_EQ(schema->getName(), "builder_test");
    EXPECT_EQ(schema->getVersion(), SchemaVersion(2, 1, 3));
    EXPECT_EQ(schema->getDescription(), "Built with fluent API");
    EXPECT_EQ(schema->getEvolutionStrategy(), EvolutionStrategy::ADDITIVE);
    
    auto fields = schema->getFields();
    EXPECT_EQ(fields.size(), 3);
    
    auto nickname_field = schema->getField("nickname");
    ASSERT_TRUE(nickname_field.has_value());
    EXPECT_FALSE(nickname_field->required);
    EXPECT_TRUE(nickname_field->description.has_value());
    EXPECT_EQ(nickname_field->description.value(), "User's preferred nickname");
}

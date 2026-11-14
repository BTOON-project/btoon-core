//  ██████╗ ████████╗ ██████╗  ██████╗ ███╗   ██╗
//  ██╔══██╗╚══██╔══╝██╔═══██╗██╔═══██╗████╗  ██║
//  ██████╔╝   ██║   ██║   ██║██║   ██║██╔██╗ ██║
//  ██╔══██╗   ██║   ██║   ██║██║   ██║██║╚██╗██║
//  ██████╔╝   ██║   ╚██████╔╝╚██████╔╝██║ ╚████║
//  ╚═════╝    ╚═╝    ╚═════╝  ╚═════╝ ╚═╝  ╚═══╝
//
//  BTOON Core
//  https://btoon.net & https://github.com/BTOON-project/btoon-core
//
// SPDX-FileCopyrightText: 2025 Alvar Laigna <https://alvarlaigna.com>
// SPDX-License-Identifier: MIT
/**
 * @file schema.h
 * @brief Header file for the BTOON Schema class with versioning support.
 */
#ifndef BTOON_SCHEMA_H
#define BTOON_SCHEMA_H

#include "btoon.h"
#include <memory>
#include <string>
#include <vector>
#include <optional>
#include <functional>
#include <unordered_map>

namespace btoon {

// Forward declarations
class SchemaImpl;
class SchemaRegistry;

/**
 * @brief Schema version representation
 */
struct SchemaVersion {
    uint32_t major;
    uint32_t minor;
    uint32_t patch;
    
    SchemaVersion(uint32_t maj = 1, uint32_t min = 0, uint32_t pat = 0)
        : major(maj), minor(min), patch(pat) {}
    
    std::string toString() const;
    static SchemaVersion fromString(const std::string& str);
    
    bool operator==(const SchemaVersion& other) const {
        return major == other.major && minor == other.minor && patch == other.patch;
    }
    
    bool operator<(const SchemaVersion& other) const {
        if (major != other.major) return major < other.major;
        if (minor != other.minor) return minor < other.minor;
        return patch < other.patch;
    }
    
    bool operator>(const SchemaVersion& other) const {
        return other < *this;
    }
    
    bool operator<=(const SchemaVersion& other) const {
        return !(*this > other);
    }
    
    bool operator>=(const SchemaVersion& other) const {
        return !(*this < other);
    }
};

/**
 * @brief Schema field definition
 */
struct SchemaField {
    std::string name;
    std::string type;  // "string", "int", "uint", "float", "bool", "binary", "array", "map"
    bool required = true;
    std::optional<Value> default_value;
    std::optional<std::string> description;
    std::optional<Value> constraints;  // min, max, pattern, enum, etc.
};

/**
 * @brief Schema migration function type
 */
using MigrationFunction = std::function<Value(const Value&)>;

/**
 * @brief Schema evolution strategy: which newer versions Schema::isCompatibleWith() accepts.
 *
 * A field matches when its name, type, `required` flag and constraints are equal.
 */
enum class EvolutionStrategy {
    /// No changes: the newer schema has exactly the same fields.
    STRICT,
    /// Only new optional fields: every existing field matches, and every added field is
    /// optional.
    ADDITIVE,
    /// Data written for the old schema (with only the fields it lists) validates under
    /// the newer one. Allowed:
    /// adding optional fields, removing fields (unless the newer schema is STRICT, which
    /// rejects unknown fields), widening a type ("int" or "uint" to "number" or "bigint",
    /// "float" to "number", anything to "any"), making a field optional, and dropping its
    /// constraints. Not allowed: a required field the old schema doesn't require, another
    /// type change, or changed or added constraints.
    BACKWARD_COMPATIBLE,
    /// Any change; data is converted with migrations (Schema::addMigration).
    FLEXIBLE
};

/**
 * @brief Main Schema class with versioning support
 */
class Schema {
public:
    // Constructors
    Schema();
    Schema(const Value& schema_definition);
    Schema(const std::string& schema_name, const SchemaVersion& version, 
           const std::vector<SchemaField>& fields);
    ~Schema();
    
    // Move semantics
    Schema(Schema&&) noexcept;
    Schema& operator=(Schema&&) noexcept;
    
    // Delete copy semantics (use shared_ptr if needed)
    Schema(const Schema&) = delete;
    Schema& operator=(const Schema&) = delete;
    
    // Validation
    bool validate(const Value& value) const;
    std::vector<std::string> validateWithErrors(const Value& value) const;
    
    // Version management
    SchemaVersion getVersion() const;
    std::string getName() const;
    void setVersion(const SchemaVersion& version);
    
    // Field management
    void addField(const SchemaField& field);
    void removeField(const std::string& field_name);
    std::optional<SchemaField> getField(const std::string& field_name) const;
    std::vector<SchemaField> getFields() const;
    
    // Evolution and compatibility
    /// True if `newer` is an allowed evolution of this schema under this schema's
    /// EvolutionStrategy (see EvolutionStrategy). Names and versions aren't compared.
    bool isCompatibleWith(const Schema& newer) const;
    /// True if data can be migrated to `target`'s version: the versions are equal, or a
    /// migration from this version to that one was added with addMigration(). Doesn't run
    /// the migration.
    bool canMigrateTo(const Schema& target) const;
    void setEvolutionStrategy(EvolutionStrategy strategy);
    EvolutionStrategy getEvolutionStrategy() const;
    
    // Migration
    void addMigration(const SchemaVersion& from_version, 
                      const SchemaVersion& to_version,
                      MigrationFunction migration);
    /// Returns `value` unchanged if `target_version` is this schema's version, the result
    /// of the migration from this version to `target_version` if one was added, and
    /// std::nullopt otherwise. Exceptions thrown by the migration function propagate.
    std::optional<Value> migrate(const Value& value, const SchemaVersion& target_version) const;
    
    // Serialization
    Value toValue() const;
    static Schema fromValue(const Value& value);
    
    // Schema comparison
    std::vector<std::string> diff(const Schema& other) const;
    
    // Metadata
    void setDescription(const std::string& description);
    std::string getDescription() const;
    void setMetadata(const std::string& key, const Value& value);
    std::optional<Value> getMetadata(const std::string& key) const;

private:
    std::unique_ptr<SchemaImpl> pimpl_;
};

/**
 * @brief Schema Registry for managing multiple schema versions
 */
class SchemaRegistry {
public:
    SchemaRegistry();
    ~SchemaRegistry();
    
    // Move semantics
    SchemaRegistry(SchemaRegistry&&) noexcept;
    SchemaRegistry& operator=(SchemaRegistry&&) noexcept;
    
    // Delete copy semantics
    SchemaRegistry(const SchemaRegistry&) = delete;
    SchemaRegistry& operator=(const SchemaRegistry&) = delete;
    
    // Registration. A null schema throws BtoonException (invalid_argument).
    void registerSchema(const std::string& name, std::shared_ptr<Schema> schema);
    void registerSchema(std::shared_ptr<Schema> schema);  // Uses schema's name
    
    // Retrieval
    std::shared_ptr<Schema> getSchema(const std::string& name) const;
    std::shared_ptr<Schema> getSchema(const std::string& name, const SchemaVersion& version) const;
    std::vector<SchemaVersion> getVersions(const std::string& name) const;
    std::shared_ptr<Schema> getLatestSchema(const std::string& name) const;
    
    // Validation with auto-detection
    bool validate(const Value& value) const;
    
    // Management
    void removeSchema(const std::string& name);
    void removeSchema(const std::string& name, const SchemaVersion& version);
    void clear();
    
    // Persistence: encode() toValue() to store a registry, and fromValue() to load it.
    Value toValue() const;
    static SchemaRegistry fromValue(const Value& value);

private:
    class RegistryImpl;
    std::unique_ptr<RegistryImpl> pimpl_;
};

/**
 * @brief Schema builder for fluent API
 */
class SchemaBuilder {
public:
    SchemaBuilder(const std::string& name);
    
    SchemaBuilder& version(uint32_t major, uint32_t minor = 0, uint32_t patch = 0);
    SchemaBuilder& version(const SchemaVersion& v);
    SchemaBuilder& description(const std::string& desc);
    
    SchemaBuilder& field(const std::string& name, const std::string& type);
    SchemaBuilder& field(const SchemaField& field);
    SchemaBuilder& optionalField(const std::string& name, const std::string& type, 
                                 const Value& default_value = Nil{});
    
    SchemaBuilder& withConstraint(const std::string& field_name, const Value& constraint);
    SchemaBuilder& withDescription(const std::string& field_name, const std::string& desc);
    
    SchemaBuilder& evolutionStrategy(EvolutionStrategy strategy);
    SchemaBuilder& metadata(const std::string& key, const Value& value);
    
    std::shared_ptr<Schema> build();

private:
    std::string name_;
    SchemaVersion version_;
    std::string description_;
    std::vector<SchemaField> fields_;
    EvolutionStrategy strategy_ = EvolutionStrategy::BACKWARD_COMPATIBLE;
    std::unordered_map<std::string, Value> metadata_;
};

/**
 * @brief Predefined schema types for common use cases
 */
namespace schemas {
    std::shared_ptr<Schema> createTimeSeries();
    std::shared_ptr<Schema> createKeyValue();
    std::shared_ptr<Schema> createDocument();
    std::shared_ptr<Schema> createTable(const std::vector<SchemaField>& columns);
}

/**
 * @brief Schema inference options
 */
struct InferenceOptions {
    bool infer_constraints = true;      // Infer min/max, pattern, enum
    bool strict_types = false;          // If false, allow type variations (e.g., int/uint)
    size_t sample_size = 1000;          // Max number of samples to analyze
    double required_threshold = 0.95;   // Field presence threshold for required
    size_t max_enum_values = 10;        // Max unique values to consider as enum
    bool infer_patterns = true;         // Try to detect string patterns
    bool merge_numeric_types = true;    // Treat int/uint/float as compatible
};

/**
 * @brief Schema inference engine
 */
class SchemaInferrer {
public:
    SchemaInferrer(const InferenceOptions& options = InferenceOptions{});
    ~SchemaInferrer();
    
    // Move semantics
    SchemaInferrer(SchemaInferrer&&) noexcept;
    SchemaInferrer& operator=(SchemaInferrer&&) noexcept;
    
    // Delete copy semantics
    SchemaInferrer(const SchemaInferrer&) = delete;
    SchemaInferrer& operator=(const SchemaInferrer&) = delete;
    
    /**
     * @brief Infer a schema from a map, or from an array of maps (see inferFromArray()).
     *
     * A schema describes maps, so any other value throws BtoonException
     * (invalid_argument): a schema inferred from it couldn't validate it.
     */
    Schema infer(const Value& value, const std::string& name = "InferredSchema");
    
    /**
     * @brief Infer the schema of each element of an array of maps (rows).
     *
     * An empty array gives a schema without fields. An element that isn't a map throws
     * BtoonException (invalid_argument).
     */
    Schema inferFromArray(const Array& array, const std::string& name = "InferredSchema");
    
    /**
     * @brief Merge multiple inferred schemas into one that accepts what each accepts.
     *
     * Field types widen as in inference ("number", or "any"). Constraints are merged too:
     * the smaller minimum and the larger maximum, the union of enums; a constraint that
     * only some schemas have, or that differs otherwise, is dropped.
     */
    Schema merge(std::vector<Schema>&& schemas, const std::string& name = "MergedSchema");
    
    /**
     * @brief Get inference statistics
     */
    struct Statistics {
        size_t samples_analyzed;
        size_t fields_discovered;
        size_t optional_fields;
        size_t enum_fields;
        std::unordered_map<std::string, std::string> field_types;
        std::unordered_map<std::string, double> field_presence_ratio;
    };
    
    Statistics getStatistics() const;
    
private:
    std::unique_ptr<class SchemaInferrerImpl> pimpl_;
};

/**
 * @brief Convenience function to infer schema from data
 */
Schema inferSchema(const Value& value, const InferenceOptions& options = InferenceOptions{});

} // namespace btoon

#endif // BTOON_SCHEMA_H
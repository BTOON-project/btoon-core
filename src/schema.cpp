#include "btoon/schema.h"
#include <sstream>
#include <algorithm>
#include <regex>
#include <fstream>
#include <set>
#include <limits>

namespace btoon {

// ===== SchemaVersion Implementation =====

std::string SchemaVersion::toString() const {
    return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
}

SchemaVersion SchemaVersion::fromString(const std::string& str) {
    std::regex version_regex(R"((\d+)\.(\d+)\.(\d+))");
    std::smatch match;
    
    if (std::regex_match(str, match, version_regex)) {
        return SchemaVersion(
            static_cast<uint32_t>(std::stoul(match[1].str())),
            static_cast<uint32_t>(std::stoul(match[2].str())),
            static_cast<uint32_t>(std::stoul(match[3].str()))
        );
    }
    
    throw BtoonException("Invalid version string: " + str);
}

// ===== SchemaImpl Implementation =====

class SchemaImpl {
public:
    SchemaImpl() : version_(1, 0, 0), evolution_strategy_(EvolutionStrategy::BACKWARD_COMPATIBLE) {}
    
    SchemaImpl(const Value& schema_definition) : SchemaImpl() {
        parseFromValue(schema_definition);
    }
    
    SchemaImpl(const std::string& name, const SchemaVersion& version, 
               const std::vector<SchemaField>& fields)
        : name_(name), version_(version), fields_(fields),
          evolution_strategy_(EvolutionStrategy::BACKWARD_COMPATIBLE) {}

    // Validation
    bool validate(const Value& value) const {
        auto errors = validateWithErrors(value);
        return errors.empty();
    }
    
    std::vector<std::string> validateWithErrors(const Value& value) const {
        std::vector<std::string> errors;
        
        const Map* value_map = std::get_if<Map>(&value);
        if (!value_map) {
            errors.push_back("Value must be a map");
            return errors;
        }
        
        // Check required fields
        for (const auto& field : fields_) {
            if (field.required) {
                auto it = value_map->find(field.name);
                if (it == value_map->end()) {
                    errors.push_back("Missing required field: " + field.name);
                    continue;
                }
                
                // Type validation
                if (!validateType(it->second, field.type)) {
                    errors.push_back("Invalid type for field '" + field.name + 
                                    "': expected " + field.type);
                }
                
                // Constraint validation
                if (field.constraints.has_value()) {
                    if (!validateConstraints(it->second, field.constraints.value())) {
                        errors.push_back("Constraint violation for field: " + field.name);
                    }
                }
            }
        }
        
        // Check for unknown fields if strategy is strict
        if (evolution_strategy_ == EvolutionStrategy::STRICT) {
            for (const auto& [key, _] : *value_map) {
                bool found = false;
                for (const auto& field : fields_) {
                    if (field.name == key) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    errors.push_back("Unknown field: " + key);
                }
            }
        }
        
        return errors;
    }
    
    // Version management
    SchemaVersion getVersion() const { return version_; }
    void setVersion(const SchemaVersion& version) { version_ = version; }
    std::string getName() const { return name_; }
    
    // Field management
    void addField(const SchemaField& field) {
        fields_.push_back(field);
    }
    
    void removeField(const std::string& field_name) {
        fields_.erase(
            std::remove_if(fields_.begin(), fields_.end(),
                [&](const SchemaField& f) { return f.name == field_name; }),
            fields_.end()
        );
    }
    
    std::optional<SchemaField> getField(const std::string& field_name) const {
        auto it = std::find_if(fields_.begin(), fields_.end(),
            [&](const SchemaField& f) { return f.name == field_name; });
        
        if (it != fields_.end()) {
            return *it;
        }
        return std::nullopt;
    }
    
    std::vector<SchemaField> getFields() const {
        return fields_;
    }
    
    // Evolution and compatibility
    bool isCompatibleWith(const Schema& other) const {
        // Check based on evolution strategy
        switch (evolution_strategy_) {
            case EvolutionStrategy::STRICT:
                return false;  // No changes allowed
                
            case EvolutionStrategy::ADDITIVE:
                // Only new optional fields allowed
                for (const auto& field : fields_) {
                    if (field.required) {
                        auto other_field = other.getField(field.name);
                        if (!other_field || other_field->type != field.type) {
                            return false;
                        }
                    }
                }
                return true;
                
            case EvolutionStrategy::BACKWARD_COMPATIBLE:
                // Can read old data
                for (const auto& field : fields_) {
                    if (field.required && !field.default_value.has_value()) {
                        auto other_field = other.getField(field.name);
                        if (!other_field) {
                            return false;
                        }
                    }
                }
                return true;
                
            case EvolutionStrategy::FLEXIBLE:
                // Always compatible with migrations
                return true;
        }
        return false;
    }
    
    void setEvolutionStrategy(EvolutionStrategy strategy) {
        evolution_strategy_ = strategy;
    }
    
    EvolutionStrategy getEvolutionStrategy() const {
        return evolution_strategy_;
    }
    
    // Migration
    void addMigration(const SchemaVersion& from_version, 
                      const SchemaVersion& to_version,
                      MigrationFunction migration) {
        migrations_[{from_version, to_version}] = migration;
    }
    
    std::optional<Value> migrate(const Value& value, const SchemaVersion& target_version) const {
        // If already at target version, return as-is
        if (version_ == target_version) {
            return value;
        }
        
        // Find migration path
        auto key = std::make_pair(version_, target_version);
        auto it = migrations_.find(key);
        
        if (it != migrations_.end()) {
            return it->second(value);
        }
        
        // No direct migration found
        return std::nullopt;
    }
    
    // Serialization
    Value toValue() const {
        Map schema_map;
        schema_map["$schema"] = String("btoon/schema/v1");
        schema_map["name"] = String(name_);
        schema_map["version"] = String(version_.toString());
        
        if (!description_.empty()) {
            schema_map["description"] = String(description_);
        }
        
        Array fields_array;
        for (const auto& field : fields_) {
            Map field_map;
            field_map["name"] = String(field.name);
            field_map["type"] = String(field.type);
            field_map["required"] = Bool(field.required);
            
            if (field.default_value.has_value()) {
                field_map["default"] = field.default_value.value();
            }
            if (field.description.has_value()) {
                field_map["description"] = String(field.description.value());
            }
            if (field.constraints.has_value()) {
                field_map["constraints"] = field.constraints.value();
            }
            
            fields_array.push_back(field_map);
        }
        schema_map["fields"] = fields_array;
        
        schema_map["evolution_strategy"] = String(evolutionStrategyToString(evolution_strategy_));
        
        if (!metadata_.empty()) {
            Map metadata_map;
            for (const auto& [key, value] : metadata_) {
                metadata_map[key] = value;
            }
            schema_map["metadata"] = metadata_map;
        }
        
        return schema_map;
    }
    
    void parseFromValue(const Value& value) {
        const Map* schema_map = std::get_if<Map>(&value);
        if (!schema_map) {
            throw BtoonException("Schema definition must be a map");
        }
        
        // Parse name
        if (schema_map->count("name")) {
            const String* name = std::get_if<String>(&schema_map->at("name"));
            if (name) {
                name_ = *name;
            }
        }
        
        // Parse version
        if (schema_map->count("version")) {
            const String* version_str = std::get_if<String>(&schema_map->at("version"));
            if (version_str) {
                version_ = SchemaVersion::fromString(*version_str);
            }
        }
        
        // Parse description
        if (schema_map->count("description")) {
            const String* desc = std::get_if<String>(&schema_map->at("description"));
            if (desc) {
                description_ = *desc;
            }
        }
        
        // Parse fields
        if (schema_map->count("fields")) {
            const Array* fields_array = std::get_if<Array>(&schema_map->at("fields"));
            if (fields_array) {
                for (const auto& field_value : *fields_array) {
                    const Map* field_map = std::get_if<Map>(&field_value);
                    if (field_map) {
                        SchemaField field;
                        
                        if (field_map->count("name")) {
                            const String* name = std::get_if<String>(&field_map->at("name"));
                            if (name) field.name = *name;
                        }
                        
                        if (field_map->count("type")) {
                            const String* type = std::get_if<String>(&field_map->at("type"));
                            if (type) field.type = *type;
                        }
                        
                        if (field_map->count("required")) {
                            const Bool* required = std::get_if<Bool>(&field_map->at("required"));
                            if (required) field.required = *required;
                        }
                        
                        if (field_map->count("default")) {
                            field.default_value = field_map->at("default");
                        }
                        
                        if (field_map->count("description")) {
                            const String* desc = std::get_if<String>(&field_map->at("description"));
                            if (desc) field.description = *desc;
                        }
                        
                        if (field_map->count("constraints")) {
                            field.constraints = field_map->at("constraints");
                        }
                        
                        fields_.push_back(field);
                    }
                }
            }
        }
        
        // Parse evolution strategy
        if (schema_map->count("evolution_strategy")) {
            const String* strategy = std::get_if<String>(&schema_map->at("evolution_strategy"));
            if (strategy) {
                evolution_strategy_ = stringToEvolutionStrategy(*strategy);
            }
        }
        
        // Parse metadata
        if (schema_map->count("metadata")) {
            const Map* metadata_map = std::get_if<Map>(&schema_map->at("metadata"));
            if (metadata_map) {
                for (const auto& [key, value] : *metadata_map) {
                    metadata_[key] = value;
                }
            }
        }
    }
    
    // Schema comparison
    std::vector<std::string> diff(const Schema& other) const {
        std::vector<std::string> differences;
        
        // Compare version
        if (version_ != other.getVersion()) {
            differences.push_back("Version changed: " + version_.toString() + 
                                 " -> " + other.getVersion().toString());
        }
        
        // Compare fields
        auto other_fields = other.getFields();
        
        // Find added fields
        for (const auto& other_field : other_fields) {
            bool found = false;
            for (const auto& field : fields_) {
                if (field.name == other_field.name) {
                    found = true;
                    // Check for type changes
                    if (field.type != other_field.type) {
                        differences.push_back("Field '" + field.name + 
                                            "' type changed: " + field.type + 
                                            " -> " + other_field.type);
                    }
                    // Check for required changes
                    if (field.required != other_field.required) {
                        differences.push_back("Field '" + field.name + 
                                            "' required changed: " + 
                                            std::to_string(field.required) + 
                                            " -> " + std::to_string(other_field.required));
                    }
                    break;
                }
            }
            if (!found) {
                differences.push_back("Field added: " + other_field.name);
            }
        }
        
        // Find removed fields
        for (const auto& field : fields_) {
            bool found = false;
            for (const auto& other_field : other_fields) {
                if (field.name == other_field.name) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                differences.push_back("Field removed: " + field.name);
            }
        }
        
        return differences;
    }
    
    // Metadata
    void setDescription(const std::string& description) {
        description_ = description;
    }
    
    std::string getDescription() const {
        return description_;
    }
    
    void setMetadata(const std::string& key, const Value& value) {
        metadata_[key] = value;
    }
    
    std::optional<Value> getMetadata(const std::string& key) const {
        auto it = metadata_.find(key);
        if (it != metadata_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

private:
    std::string name_;
    SchemaVersion version_;
    std::vector<SchemaField> fields_;
    std::string description_;
    EvolutionStrategy evolution_strategy_;
    std::unordered_map<std::string, Value> metadata_;
    
    // Migration functions: (from_version, to_version) -> migration_function
    using MigrationKey = std::pair<SchemaVersion, SchemaVersion>;
    struct MigrationKeyHash {
        std::size_t operator()(const MigrationKey& k) const {
            return std::hash<std::string>()(k.first.toString() + "->" + k.second.toString());
        }
    };
    std::unordered_map<MigrationKey, MigrationFunction, MigrationKeyHash> migrations_;
    
    bool validateType(const Value& value, const std::string& expected_type) const {
        if (expected_type == "string") {
            return std::holds_alternative<String>(value);
        } else if (expected_type == "int") {
            return std::holds_alternative<Int>(value);
        } else if (expected_type == "uint") {
            return std::holds_alternative<Uint>(value);
        } else if (expected_type == "float") {
            return std::holds_alternative<Float>(value);
        } else if (expected_type == "bool") {
            return std::holds_alternative<Bool>(value);
        } else if (expected_type == "binary") {
            return std::holds_alternative<Binary>(value);
        } else if (expected_type == "array") {
            return std::holds_alternative<Array>(value);
        } else if (expected_type == "map") {
            return std::holds_alternative<Map>(value);
        } else if (expected_type == "nil") {
            return std::holds_alternative<Nil>(value);
        } else if (expected_type == "any") {
            return true;  // Any type is valid
        }
        return false;
    }
    
    bool validateConstraints(const Value& value, const Value& constraints) const {
        const Map* constraint_map = std::get_if<Map>(&constraints);
        if (!constraint_map) {
            return true;  // No constraints
        }
        
        // Check min/max for numeric types
        if (constraint_map->count("min")) {
            if (const Int* int_val = std::get_if<Int>(&value)) {
                const Int* min_val = std::get_if<Int>(&constraint_map->at("min"));
                if (min_val && *int_val < *min_val) {
                    return false;
                }
            }
            if (const Float* float_val = std::get_if<Float>(&value)) {
                const Float* min_val = std::get_if<Float>(&constraint_map->at("min"));
                if (min_val && *float_val < *min_val) {
                    return false;
                }
            }
        }
        
        if (constraint_map->count("max")) {
            if (const Int* int_val = std::get_if<Int>(&value)) {
                const Int* max_val = std::get_if<Int>(&constraint_map->at("max"));
                if (max_val && *int_val > *max_val) {
                    return false;
                }
            }
            if (const Float* float_val = std::get_if<Float>(&value)) {
                const Float* max_val = std::get_if<Float>(&constraint_map->at("max"));
                if (max_val && *float_val > *max_val) {
                    return false;
                }
            }
        }
        
        // Check pattern for strings
        if (constraint_map->count("pattern")) {
            if (const String* str_val = std::get_if<String>(&value)) {
                const String* pattern = std::get_if<String>(&constraint_map->at("pattern"));
                if (pattern) {
                    std::regex re(*pattern);
                    if (!std::regex_match(*str_val, re)) {
                        return false;
                    }
                }
            }
        }
        
        // Check enum values
        if (constraint_map->count("enum")) {
            const Array* enum_values = std::get_if<Array>(&constraint_map->at("enum"));
            if (enum_values) {
                bool found = false;
                for (const auto& enum_val : *enum_values) {
                    if (value == enum_val) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    return false;
                }
            }
        }
        
        return true;
    }
    
    static std::string evolutionStrategyToString(EvolutionStrategy strategy) {
        switch (strategy) {
            case EvolutionStrategy::STRICT: return "strict";
            case EvolutionStrategy::ADDITIVE: return "additive";
            case EvolutionStrategy::BACKWARD_COMPATIBLE: return "backward_compatible";
            case EvolutionStrategy::FLEXIBLE: return "flexible";
            default: return "backward_compatible";
        }
    }
    
    static EvolutionStrategy stringToEvolutionStrategy(const std::string& str) {
        if (str == "strict") return EvolutionStrategy::STRICT;
        if (str == "additive") return EvolutionStrategy::ADDITIVE;
        if (str == "backward_compatible") return EvolutionStrategy::BACKWARD_COMPATIBLE;
        if (str == "flexible") return EvolutionStrategy::FLEXIBLE;
        return EvolutionStrategy::BACKWARD_COMPATIBLE;
    }
};

// ===== Schema Implementation =====

Schema::Schema() : pimpl_(std::make_unique<SchemaImpl>()) {}

Schema::Schema(const Value& schema_definition)
    : pimpl_(std::make_unique<SchemaImpl>(schema_definition)) {}

Schema::Schema(const std::string& schema_name, const SchemaVersion& version, 
               const std::vector<SchemaField>& fields)
    : pimpl_(std::make_unique<SchemaImpl>(schema_name, version, fields)) {}

Schema::~Schema() = default;
Schema::Schema(Schema&&) noexcept = default;
Schema& Schema::operator=(Schema&&) noexcept = default;

bool Schema::validate(const Value& value) const {
    return pimpl_->validate(value);
}

std::vector<std::string> Schema::validateWithErrors(const Value& value) const {
    return pimpl_->validateWithErrors(value);
}

SchemaVersion Schema::getVersion() const {
    return pimpl_->getVersion();
}

std::string Schema::getName() const {
    return pimpl_->getName();
}

void Schema::setVersion(const SchemaVersion& version) {
    pimpl_->setVersion(version);
}

void Schema::addField(const SchemaField& field) {
    pimpl_->addField(field);
}

void Schema::removeField(const std::string& field_name) {
    pimpl_->removeField(field_name);
}

std::optional<SchemaField> Schema::getField(const std::string& field_name) const {
    return pimpl_->getField(field_name);
}

std::vector<SchemaField> Schema::getFields() const {
    return pimpl_->getFields();
}

bool Schema::isCompatibleWith(const Schema& other) const {
    return pimpl_->isCompatibleWith(other);
}

bool Schema::canMigrateTo(const Schema& target) const {
    return pimpl_->migrate(Map{}, target.getVersion()).has_value();
}

void Schema::setEvolutionStrategy(EvolutionStrategy strategy) {
    pimpl_->setEvolutionStrategy(strategy);
}

EvolutionStrategy Schema::getEvolutionStrategy() const {
    return pimpl_->getEvolutionStrategy();
}

void Schema::addMigration(const SchemaVersion& from_version, 
                          const SchemaVersion& to_version,
                          MigrationFunction migration) {
    pimpl_->addMigration(from_version, to_version, migration);
}

std::optional<Value> Schema::migrate(const Value& value, const SchemaVersion& target_version) const {
    return pimpl_->migrate(value, target_version);
}

Value Schema::toValue() const {
    return pimpl_->toValue();
}

Schema Schema::fromValue(const Value& value) {
    return Schema(value);
}

std::vector<std::string> Schema::diff(const Schema& other) const {
    return pimpl_->diff(other);
}

void Schema::setDescription(const std::string& description) {
    pimpl_->setDescription(description);
}

std::string Schema::getDescription() const {
    return pimpl_->getDescription();
}

void Schema::setMetadata(const std::string& key, const Value& value) {
    pimpl_->setMetadata(key, value);
}

std::optional<Value> Schema::getMetadata(const std::string& key) const {
    return pimpl_->getMetadata(key);
}

// ===== SchemaRegistry Implementation =====

class SchemaRegistry::RegistryImpl {
public:
    void registerSchema(const std::string& name, std::shared_ptr<Schema> schema) {
        schemas_[name][schema->getVersion()] = schema;
    }
    
    std::shared_ptr<Schema> getSchema(const std::string& name) const {
        auto it = schemas_.find(name);
        if (it != schemas_.end() && !it->second.empty()) {
            // Return latest version
            return it->second.rbegin()->second;
        }
        return nullptr;
    }
    
    std::shared_ptr<Schema> getSchema(const std::string& name, const SchemaVersion& version) const {
        auto it = schemas_.find(name);
        if (it != schemas_.end()) {
            auto ver_it = it->second.find(version);
            if (ver_it != it->second.end()) {
                return ver_it->second;
            }
        }
        return nullptr;
    }
    
    std::vector<SchemaVersion> getVersions(const std::string& name) const {
        std::vector<SchemaVersion> versions;
        auto it = schemas_.find(name);
        if (it != schemas_.end()) {
            for (const auto& [version, _] : it->second) {
                versions.push_back(version);
            }
        }
        return versions;
    }
    
    std::shared_ptr<Schema> getLatestSchema(const std::string& name) const {
        return getSchema(name);
    }
    
    bool validate(const Value& value) const {
        // Try to auto-detect schema
        const Map* value_map = std::get_if<Map>(&value);
        if (value_map && value_map->count("$schema")) {
            const String* schema_name = std::get_if<String>(&value_map->at("$schema"));
            if (schema_name) {
                auto schema = getSchema(*schema_name);
                if (schema) {
                    return schema->validate(value);
                }
            }
        }
        
        // Try all schemas
        for (const auto& [name, versions] : schemas_) {
            for (const auto& [version, schema] : versions) {
                if (schema->validate(value)) {
                    return true;
                }
            }
        }
        
        return false;
    }
    
    void removeSchema(const std::string& name) {
        schemas_.erase(name);
    }
    
    void removeSchema(const std::string& name, const SchemaVersion& version) {
        auto it = schemas_.find(name);
        if (it != schemas_.end()) {
            it->second.erase(version);
            if (it->second.empty()) {
                schemas_.erase(it);
            }
        }
    }
    
    void clear() {
        schemas_.clear();
    }
    
    Value toValue() const {
        Map registry_map;
        registry_map["$type"] = String("schema_registry");
        
        Array schemas_array;
        for (const auto& [name, versions] : schemas_) {
            for (const auto& [version, schema] : versions) {
                schemas_array.push_back(schema->toValue());
            }
        }
        registry_map["schemas"] = schemas_array;
        
        return registry_map;
    }
    
    void parseFromValue(const Value& value) {
        const Map* registry_map = std::get_if<Map>(&value);
        if (!registry_map) {
            throw BtoonException("Registry must be a map");
        }
        
        if (registry_map->count("schemas")) {
            const Array* schemas_array = std::get_if<Array>(&registry_map->at("schemas"));
            if (schemas_array) {
                for (const auto& schema_value : *schemas_array) {
                    auto schema = std::make_shared<Schema>(Schema::fromValue(schema_value));
                    registerSchema(schema->getName(), schema);
                }
            }
        }
    }

private:
    // name -> (version -> schema)
    std::unordered_map<std::string, std::map<SchemaVersion, std::shared_ptr<Schema>>> schemas_;
};

SchemaRegistry::SchemaRegistry() : pimpl_(std::make_unique<RegistryImpl>()) {}
SchemaRegistry::~SchemaRegistry() = default;
SchemaRegistry::SchemaRegistry(SchemaRegistry&&) noexcept = default;
SchemaRegistry& SchemaRegistry::operator=(SchemaRegistry&&) noexcept = default;

void SchemaRegistry::registerSchema(const std::string& name, std::shared_ptr<Schema> schema) {
    pimpl_->registerSchema(name, schema);
}

void SchemaRegistry::registerSchema(std::shared_ptr<Schema> schema) {
    pimpl_->registerSchema(schema->getName(), schema);
}

std::shared_ptr<Schema> SchemaRegistry::getSchema(const std::string& name) const {
    return pimpl_->getSchema(name);
}

std::shared_ptr<Schema> SchemaRegistry::getSchema(const std::string& name, const SchemaVersion& version) const {
    return pimpl_->getSchema(name, version);
}

std::vector<SchemaVersion> SchemaRegistry::getVersions(const std::string& name) const {
    return pimpl_->getVersions(name);
}

std::shared_ptr<Schema> SchemaRegistry::getLatestSchema(const std::string& name) const {
    return pimpl_->getLatestSchema(name);
}

bool SchemaRegistry::validate(const Value& value) const {
    return pimpl_->validate(value);
}

void SchemaRegistry::removeSchema(const std::string& name) {
    pimpl_->removeSchema(name);
}

void SchemaRegistry::removeSchema(const std::string& name, const SchemaVersion& version) {
    pimpl_->removeSchema(name, version);
}

void SchemaRegistry::clear() {
    pimpl_->clear();
}

Value SchemaRegistry::toValue() const {
    return pimpl_->toValue();
}

SchemaRegistry SchemaRegistry::fromValue(const Value& value) {
    SchemaRegistry registry;
    registry.pimpl_->parseFromValue(value);
    return registry;
}

// ===== SchemaBuilder Implementation =====

SchemaBuilder::SchemaBuilder(const std::string& name) 
    : name_(name), version_(1, 0, 0) {}

SchemaBuilder& SchemaBuilder::version(uint32_t major, uint32_t minor, uint32_t patch) {
    version_ = SchemaVersion(major, minor, patch);
    return *this;
}

SchemaBuilder& SchemaBuilder::version(const SchemaVersion& v) {
    version_ = v;
    return *this;
}

SchemaBuilder& SchemaBuilder::description(const std::string& desc) {
    description_ = desc;
    return *this;
}

SchemaBuilder& SchemaBuilder::field(const std::string& name, const std::string& type) {
    fields_.push_back({name, type, true, std::nullopt, std::nullopt, std::nullopt});
    return *this;
}

SchemaBuilder& SchemaBuilder::field(const SchemaField& field) {
    fields_.push_back(field);
    return *this;
}

SchemaBuilder& SchemaBuilder::optionalField(const std::string& name, const std::string& type, 
                                           const Value& default_value) {
    fields_.push_back({name, type, false, default_value, std::nullopt, std::nullopt});
    return *this;
}

SchemaBuilder& SchemaBuilder::withConstraint(const std::string& field_name, const Value& constraint) {
    auto it = std::find_if(fields_.begin(), fields_.end(),
        [&](const SchemaField& f) { return f.name == field_name; });
    
    if (it != fields_.end()) {
        it->constraints = constraint;
    }
    return *this;
}

SchemaBuilder& SchemaBuilder::withDescription(const std::string& field_name, const std::string& desc) {
    auto it = std::find_if(fields_.begin(), fields_.end(),
        [&](const SchemaField& f) { return f.name == field_name; });
    
    if (it != fields_.end()) {
        it->description = desc;
    }
    return *this;
}

SchemaBuilder& SchemaBuilder::evolutionStrategy(EvolutionStrategy strategy) {
    strategy_ = strategy;
    return *this;
}

SchemaBuilder& SchemaBuilder::metadata(const std::string& key, const Value& value) {
    metadata_[key] = value;
    return *this;
}

std::shared_ptr<Schema> SchemaBuilder::build() {
    auto schema = std::make_shared<Schema>(name_, version_, fields_);
    schema->setDescription(description_);
    schema->setEvolutionStrategy(strategy_);
    
    for (const auto& [key, value] : metadata_) {
        schema->setMetadata(key, value);
    }
    
    return schema;
}

// ===== Predefined schemas =====

namespace schemas {
    std::shared_ptr<Schema> createTimeSeries() {
        return SchemaBuilder("time_series")
            .version(1, 0, 0)
            .description("Time series data schema")
            .field("timestamp", "int")
            .field("value", "float")
            .optionalField("tags", "map", Map{})
            .withConstraint("timestamp", Map{{"min", Int(0)}})
            .build();
    }
    
    std::shared_ptr<Schema> createKeyValue() {
        return SchemaBuilder("key_value")
            .version(1, 0, 0)
            .description("Key-value store schema")
            .field("key", "string")
            .field("value", "any")
            .optionalField("ttl", "int", Int(0))
            .optionalField("metadata", "map", Map{})
            .build();
    }
    
    std::shared_ptr<Schema> createDocument() {
        return SchemaBuilder("document")
            .version(1, 0, 0)
            .description("Document store schema")
            .field("id", "string")
            .field("content", "map")
            .optionalField("created_at", "int")
            .optionalField("updated_at", "int")
            .optionalField("version", "int", Int(1))
            .build();
    }
    
    std::shared_ptr<Schema> createTable(const std::vector<SchemaField>& columns) {
        SchemaBuilder builder("table");
        builder.version(1, 0, 0)
               .description("Table schema with custom columns");
        
        for (const auto& column : columns) {
            builder.field(column);
        }
        
        return builder.build();
    }
}

// Schema Inference Implementation
class SchemaInferrerImpl {
public:
    InferenceOptions options;
    SchemaInferrer::Statistics stats;
    
    SchemaInferrerImpl(const InferenceOptions& opts) : options(opts), stats{} {}
    
    std::string inferType(const Value& value) const {
        return std::visit([this](auto&& arg) -> std::string {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, Nil>) return "nil";
            else if constexpr (std::is_same_v<T, Bool>) return "bool";
            else if constexpr (std::is_same_v<T, Int>) return options.merge_numeric_types ? "number" : "int";
            else if constexpr (std::is_same_v<T, Uint>) return options.merge_numeric_types ? "number" : "uint";
            else if constexpr (std::is_same_v<T, Float>) return options.merge_numeric_types ? "number" : "float";
            else if constexpr (std::is_same_v<T, String>) return "string";
            else if constexpr (std::is_same_v<T, Binary>) return "binary";
            else if constexpr (std::is_same_v<T, Array>) return "array";
            else if constexpr (std::is_same_v<T, Map>) return "map";
            else if constexpr (std::is_same_v<T, Extension>) return "extension";
            else if constexpr (std::is_same_v<T, Timestamp>) return "timestamp";
            else if constexpr (std::is_same_v<T, Date>) return "date";
            else if constexpr (std::is_same_v<T, DateTime>) return "datetime";
            else if constexpr (std::is_same_v<T, BigInt>) return "bigint";
            else return "unknown";
        }, value);
    }
    
    std::optional<Value> inferConstraints(const std::vector<Value>& values, const std::string& type) {
        if (!options.infer_constraints || values.empty()) {
            return std::nullopt;
        }
        
        Map constraints;
        
        if (type == "string") {
            size_t min_len = std::numeric_limits<size_t>::max();
            size_t max_len = 0;
            std::set<std::string> unique_values;
            
            for (const auto& v : values) {
                if (auto* s = std::get_if<String>(&v)) {
                    min_len = std::min(min_len, s->size());
                    max_len = std::max(max_len, s->size());
                    unique_values.insert(*s);
                }
            }
            
            if (min_len != std::numeric_limits<size_t>::max()) {
                constraints["minLength"] = static_cast<Uint>(min_len);
                constraints["maxLength"] = static_cast<Uint>(max_len);
            }
            
            // Infer enum if few unique values
            if (unique_values.size() <= options.max_enum_values && unique_values.size() > 0) {
                Array enum_values;
                for (const auto& s : unique_values) {
                    enum_values.push_back(s);
                }
                constraints["enum"] = enum_values;
            }
            
            // TODO: Infer patterns if options.infer_patterns is true
            
        } else if (type == "int" || type == "uint" || type == "number") {
            double min_val = std::numeric_limits<double>::max();
            double max_val = std::numeric_limits<double>::lowest();
            
            for (const auto& v : values) {
                double val = 0;
                if (auto* i = std::get_if<Int>(&v)) {
                    val = static_cast<double>(*i);
                } else if (auto* u = std::get_if<Uint>(&v)) {
                    val = static_cast<double>(*u);
                } else if (auto* f = std::get_if<Float>(&v)) {
                    val = *f;
                }
                min_val = std::min(min_val, val);
                max_val = std::max(max_val, val);
            }
            
            if (min_val != std::numeric_limits<double>::max()) {
                if (type == "int") {
                    constraints["min"] = static_cast<Int>(min_val);
                    constraints["max"] = static_cast<Int>(max_val);
                } else if (type == "uint") {
                    constraints["min"] = static_cast<Uint>(min_val);
                    constraints["max"] = static_cast<Uint>(max_val);
                } else {
                    constraints["min"] = static_cast<Float>(min_val);
                    constraints["max"] = static_cast<Float>(max_val);
                }
            }
        } else if (type == "array") {
            size_t min_items = std::numeric_limits<size_t>::max();
            size_t max_items = 0;
            
            for (const auto& v : values) {
                if (auto* a = std::get_if<Array>(&v)) {
                    min_items = std::min(min_items, a->size());
                    max_items = std::max(max_items, a->size());
                }
            }
            
            if (min_items != std::numeric_limits<size_t>::max()) {
                constraints["minItems"] = static_cast<Uint>(min_items);
                constraints["maxItems"] = static_cast<Uint>(max_items);
            }
        }
        
        return constraints.empty() ? std::nullopt : std::optional<Value>(constraints);
    }
    
    Schema inferFromMap(const Map& map, const std::string& name) {
        SchemaBuilder builder(name);
        builder.version(1, 0, 0);
        
        for (const auto& [key, value] : map) {
            std::string field_type = inferType(value);
            SchemaField field{key, field_type, true, std::nullopt, std::nullopt, std::nullopt};
            
            // Infer constraints for single value
            if (options.infer_constraints) {
                field.constraints = inferConstraints({value}, field_type);
            }
            
            builder.field(field);
            stats.fields_discovered++;
        }
        
        stats.samples_analyzed = 1;
        return std::move(*builder.build());
    }
    
    Schema inferFromArrayOfMaps(const Array& array, const std::string& name) {
        if (array.empty()) {
            return Schema(name, SchemaVersion{1, 0, 0}, {});
        }
        
        // Collect field information
        std::unordered_map<std::string, std::vector<Value>> field_values;
        std::unordered_map<std::string, size_t> field_occurrence;
        std::unordered_map<std::string, std::string> field_types;
        
        size_t sample_count = 0;
        for (const auto& item : array) {
            if (sample_count >= options.sample_size) break;
            
            const Map* map = std::get_if<Map>(&item);
            if (!map) continue;
            
            sample_count++;
            
            for (const auto& [key, value] : *map) {
                field_values[key].push_back(value);
                field_occurrence[key]++;
                
                // Infer type, merging if necessary
                std::string new_type = inferType(value);
                if (field_types.find(key) == field_types.end()) {
                    field_types[key] = new_type;
                } else if (field_types[key] != new_type) {
                    // Handle type conflicts
                    if (options.merge_numeric_types &&
                        ((field_types[key] == "int" || field_types[key] == "uint" || field_types[key] == "float" || field_types[key] == "number") &&
                         (new_type == "int" || new_type == "uint" || new_type == "float"))) {
                        field_types[key] = "number";
                    } else if (!options.strict_types) {
                        field_types[key] = "any";  // Generic type for mixed types
                    }
                }
            }
        }
        
        // Build schema
        SchemaBuilder builder(name);
        builder.version(1, 0, 0);
        
        for (const auto& [field_name, values] : field_values) {
            double presence_ratio = static_cast<double>(field_occurrence[field_name]) / sample_count;
            bool required = presence_ratio >= options.required_threshold;
            
            SchemaField field{
                field_name,
                field_types[field_name],
                required,
                std::nullopt,
                std::nullopt,
                inferConstraints(values, field_types[field_name])
            };
            
            builder.field(field);
            
            // Update statistics
            stats.field_types[field_name] = field_types[field_name];
            stats.field_presence_ratio[field_name] = presence_ratio;
            if (!required) stats.optional_fields++;
            if (field.constraints.has_value()) {
                const Map* constraints = std::get_if<Map>(&field.constraints.value());
                if (constraints && constraints->find("enum") != constraints->end()) {
                    stats.enum_fields++;
                }
            }
        }
        
        stats.samples_analyzed = sample_count;
        stats.fields_discovered = field_values.size();
        
        return std::move(*builder.build());
    }
};

SchemaInferrer::SchemaInferrer(const InferenceOptions& options)
    : pimpl_(std::make_unique<SchemaInferrerImpl>(options)) {}

SchemaInferrer::~SchemaInferrer() = default;

SchemaInferrer::SchemaInferrer(SchemaInferrer&&) noexcept = default;
SchemaInferrer& SchemaInferrer::operator=(SchemaInferrer&&) noexcept = default;

Schema SchemaInferrer::infer(const Value& value, const std::string& name) {
    if (const Map* map = std::get_if<Map>(&value)) {
        return pimpl_->inferFromMap(*map, name);
    } else if (const Array* array = std::get_if<Array>(&value)) {
        return inferFromArray(*array, name);
    } else {
        // For primitive values, create a single-field schema
        SchemaBuilder builder(name);
        builder.version(1, 0, 0);
        builder.field("value", pimpl_->inferType(value));
        pimpl_->stats.samples_analyzed = 1;
        pimpl_->stats.fields_discovered = 1;
        return std::move(*builder.build());
    }
}

Schema SchemaInferrer::inferFromArray(const Array& array, const std::string& name) {
    if (array.empty()) {
        return Schema(name, SchemaVersion{1, 0, 0}, {});
    }
    
    // Check if it's an array of maps (tabular data)
    bool all_maps = true;
    for (const auto& item : array) {
        if (!std::holds_alternative<Map>(item)) {
            all_maps = false;
            break;
        }
    }
    
    if (all_maps) {
        return pimpl_->inferFromArrayOfMaps(array, name);
    } else {
        // Infer schema for array of primitives or mixed types
        std::set<std::string> item_types;
        std::vector<Value> sample_values;
        
        size_t count = 0;
        for (const auto& item : array) {
            if (count >= pimpl_->options.sample_size) break;
            item_types.insert(pimpl_->inferType(item));
            sample_values.push_back(item);
            count++;
        }
        
        SchemaBuilder builder(name);
        builder.version(1, 0, 0);
        
        // Create a schema with an "items" field representing the array element type
        std::string item_type = item_types.size() == 1 ? *item_types.begin() : "any";
        SchemaField items_field{"items", item_type, true, std::nullopt, std::nullopt, 
                               pimpl_->inferConstraints(sample_values, item_type)};
        builder.field(items_field);
        
        pimpl_->stats.samples_analyzed = count;
        pimpl_->stats.fields_discovered = 1;
        
        return std::move(*builder.build());
    }
}

Schema SchemaInferrer::merge(std::vector<Schema>&& schemas, const std::string& name) {
    if (schemas.empty()) {
        return Schema(name, SchemaVersion{1, 0, 0}, {});
    }
    
    if (schemas.size() == 1) {
        // Return a copy of the single schema with a new name
        SchemaBuilder builder(name);
        builder.version(schemas[0].getVersion());
        for (const auto& field : schemas[0].getFields()) {
            builder.field(field);
        }
        return std::move(*builder.build());
    }
    
    // Merge fields from all schemas
    std::unordered_map<std::string, SchemaField> merged_fields;
    std::unordered_map<std::string, size_t> field_count;
    
    for (const auto& schema : schemas) {
        for (const auto& field : schema.getFields()) {
            field_count[field.name]++;
            
            if (merged_fields.find(field.name) == merged_fields.end()) {
                merged_fields[field.name] = field;
            } else {
                // Merge field definitions
                auto& existing = merged_fields[field.name];
                
                // Make field optional if not present in all schemas
                if (field_count[field.name] < schemas.size()) {
                    existing.required = false;
                }
                
                // Merge types if different
                if (existing.type != field.type) {
                    if (pimpl_->options.merge_numeric_types &&
                        ((existing.type == "int" || existing.type == "uint" || existing.type == "float" || existing.type == "number") &&
                         (field.type == "int" || field.type == "uint" || field.type == "float"))) {
                        existing.type = "number";
                    } else {
                        existing.type = "any";
                    }
                }
                
                // TODO: Merge constraints intelligently
            }
        }
    }
    
    // Build merged schema
    SchemaBuilder builder(name);
    builder.version(1, 0, 0);
    
    for (const auto& [field_name, field] : merged_fields) {
        // Make field optional if not in all schemas
        SchemaField merged_field = field;
        if (field_count[field_name] < schemas.size()) {
            merged_field.required = false;
        }
        builder.field(merged_field);
    }
    
    return std::move(*builder.build());
}

SchemaInferrer::Statistics SchemaInferrer::getStatistics() const {
    return pimpl_->stats;
}

// Convenience function
Schema inferSchema(const Value& value, const InferenceOptions& options) {
    SchemaInferrer inferrer(options);
    return inferrer.infer(value);
}

} // namespace btoon
#include "btoon/schema.h"
#include <stdexcept>

namespace btoon {

class SchemaImpl {
public:
    SchemaImpl(const Value& schema_definition) : schema_(schema_definition) {}

    bool validate(const Value& value) const {
        // This is a simplified implementation. A real implementation would
        // recursively traverse the schema and the value to check for type
        // and structure conformance.
        if (const auto* schema_map = std::get_if<Map>(&schema_)) {
            if (const auto* value_map = std::get_if<Map>(&value)) {
                for (const auto& [key, schema_type_val] : *schema_map) {
                    if (const auto* schema_type_str = std::get_if<String>(&schema_type_val)) {
                        auto it = value_map->find(key);
                        if (it == value_map->end()) {
                            return false; // Key not found
                        }
                        // This is a very basic type check
                        if (*schema_type_str == "string" && !std::holds_alternative<String>(it->second)) {
                            return false;
                        }
                        if (*schema_type_str == "int" && !std::holds_alternative<Int>(it->second)) {
                            return false;
                        }
                    }
                }
                return true;
            }
        }
        return false;
    }

private:
    Value schema_;
};

Schema::Schema(const Value& schema_definition)
    : pimpl_(std::make_unique<SchemaImpl>(schema_definition)) {}

Schema::~Schema() = default;

bool Schema::validate(const Value& value) const {
    return pimpl_->validate(value);
}

} // namespace btoon
/**
 * @file schema.h
 * @brief Header file for the BTOON Schema class.
 */
#ifndef BTOON_SCHEMA_H
#define BTOON_SCHEMA_H

#include "btoon.h"
#include <memory>

namespace btoon {

class SchemaImpl; // Pimpl idiom

class Schema {
public:
    Schema(const Value& schema_definition);
    ~Schema();

    bool validate(const Value& value) const;

private:
    std::unique_ptr<SchemaImpl> pimpl_;
};

} // namespace btoon

#endif // BTOON_SCHEMA_H
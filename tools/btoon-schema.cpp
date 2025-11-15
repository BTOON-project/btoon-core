/**
 * btoon-schema: Schema compiler and management tool for BTOON
 * 
 * Commands:
 *   validate   - Validate a schema definition
 *   compile    - Compile schema to binary format
 *   generate   - Generate code from schema
 *   infer      - Infer schema from data
 *   migrate    - Apply schema migration to data
 *   diff       - Compare two schema versions
 *   doc        - Generate documentation from schema
 */

#include "btoon/btoon.h"
#include "btoon/schema.h"
#include "btoon/encoder.h"
#include "btoon/decoder.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <cstring>

namespace fs = std::filesystem;
using json = nlohmann::json;
using namespace btoon;

// Forward declarations
void print_usage(const std::string& program_name);
int cmd_validate(int argc, char* argv[]);
int cmd_compile(int argc, char* argv[]);
int cmd_generate(int argc, char* argv[]);
int cmd_infer(int argc, char* argv[]);
int cmd_migrate(int argc, char* argv[]);
int cmd_diff(int argc, char* argv[]);
int cmd_doc(int argc, char* argv[]);

// Utility functions
std::vector<uint8_t> read_file(const std::string& path);
void write_file(const std::string& path, const std::vector<uint8_t>& data);
void write_file(const std::string& path, const std::string& content);
Value load_data_file(const std::string& path);
Schema load_schema_file(const std::string& path);
std::string value_to_json_string(const Value& v, bool pretty = true);
Value json_to_value(const json& j);

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }
    
    std::string command = argv[1];
    
    try {
        if (command == "validate") {
            return cmd_validate(argc - 1, argv + 1);
        } else if (command == "compile") {
            return cmd_compile(argc - 1, argv + 1);
        } else if (command == "generate" || command == "gen") {
            return cmd_generate(argc - 1, argv + 1);
        } else if (command == "infer") {
            return cmd_infer(argc - 1, argv + 1);
        } else if (command == "migrate") {
            return cmd_migrate(argc - 1, argv + 1);
        } else if (command == "diff") {
            return cmd_diff(argc - 1, argv + 1);
        } else if (command == "doc") {
            return cmd_doc(argc - 1, argv + 1);
        } else if (command == "--help" || command == "-h") {
            print_usage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown command: " << command << std::endl;
            print_usage(argv[0]);
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}

void print_usage(const std::string& program_name) {
    std::cout << "BTOON Schema Compiler v0.0.1" << std::endl;
    std::cout << "Usage: " << program_name << " <command> [options]" << std::endl;
    std::cout << std::endl;
    std::cout << "Commands:" << std::endl;
    std::cout << "  validate <schema_file>           Validate a schema definition" << std::endl;
    std::cout << "  compile <schema_file> [-o output] Compile schema to binary format" << std::endl;
    std::cout << "  generate <schema_file> -l <lang>  Generate code from schema" << std::endl;
    std::cout << "  infer <data_file> [-o output]     Infer schema from data" << std::endl;
    std::cout << "  migrate <data> <old> <new>        Migrate data between schemas" << std::endl;
    std::cout << "  diff <schema1> <schema2>          Compare two schema versions" << std::endl;
    std::cout << "  doc <schema_file> [-o output]     Generate documentation" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -o, --output <file>  Output file (default: stdout)" << std::endl;
    std::cout << "  -l, --language <lang> Target language (cpp, python, go, rust, typescript)" << std::endl;
    std::cout << "  -f, --format <fmt>   Output format (json, yaml, binary, markdown)" << std::endl;
    std::cout << "  -v, --verbose        Verbose output" << std::endl;
    std::cout << "  -h, --help           Show this help message" << std::endl;
}

int cmd_validate(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: btoon-schema validate <schema_file>" << std::endl;
        return 1;
    }
    
    std::string schema_file = argv[1];
    
    try {
        Schema schema = load_schema_file(schema_file);
        std::cout << "✓ Schema '" << schema.getName() 
                  << "' v" << schema.getVersion().toString() 
                  << " is valid" << std::endl;
        
        // Print schema details
        std::cout << "\nSchema Details:" << std::endl;
        std::cout << "  Name: " << schema.getName() << std::endl;
        std::cout << "  Version: " << schema.getVersion().toString() << std::endl;
        std::cout << "  Description: " << schema.getDescription() << std::endl;
        std::cout << "  Fields: " << schema.getFields().size() << std::endl;
        
        for (const auto& field : schema.getFields()) {
            std::cout << "    - " << field.name 
                      << " (" << field.type << ")"
                      << (field.required ? " [required]" : " [optional]") 
                      << std::endl;
            if (field.description.has_value()) {
                std::cout << "      Description: " << field.description.value() << std::endl;
            }
        }
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "✗ Schema validation failed: " << e.what() << std::endl;
        return 1;
    }
}

int cmd_compile(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: btoon-schema compile <schema_file> [-o output_file]" << std::endl;
        return 1;
    }
    
    std::string schema_file = argv[1];
    std::string output_file;
    
    // Parse options
    for (int i = 2; i < argc; i++) {
        if (std::string(argv[i]) == "-o" || std::string(argv[i]) == "--output") {
            if (i + 1 < argc) {
                output_file = argv[++i];
            }
        }
    }
    
    // Default output filename
    if (output_file.empty()) {
        output_file = fs::path(schema_file).stem().string() + ".bsch";
    }
    
    Schema schema = load_schema_file(schema_file);
    
    // Compile schema to binary BTOON format
    Value schema_value = schema.toValue();
    EncodeOptions opts;
    opts.compress = true;
    opts.compression_algorithm = CompressionAlgorithm::ZLIB;
    
    auto encoded = encode(schema_value, opts);
    write_file(output_file, encoded);
    
    std::cout << "✓ Compiled schema to: " << output_file 
              << " (" << encoded.size() << " bytes)" << std::endl;
    
    return 0;
}

int cmd_generate(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: btoon-schema generate <schema_file> -l <language> [-o output_file]" << std::endl;
        std::cerr << "Languages: cpp, python, go, rust, typescript" << std::endl;
        return 1;
    }
    
    std::string schema_file = argv[1];
    std::string language;
    std::string output_file;
    
    // Parse options
    for (int i = 2; i < argc; i++) {
        if (std::string(argv[i]) == "-l" || std::string(argv[i]) == "--language") {
            if (i + 1 < argc) {
                language = argv[++i];
            }
        } else if (std::string(argv[i]) == "-o" || std::string(argv[i]) == "--output") {
            if (i + 1 < argc) {
                output_file = argv[++i];
            }
        }
    }
    
    if (language.empty()) {
        std::cerr << "Error: Language must be specified with -l option" << std::endl;
        return 1;
    }
    
    Schema schema = load_schema_file(schema_file);
    std::stringstream code;
    
    if (language == "cpp" || language == "c++") {
        // Generate C++ code
        if (output_file.empty()) {
            output_file = fs::path(schema_file).stem().string() + ".h";
        }
        
        code << "// Generated from " << schema_file << " by btoon-schema" << std::endl;
        code << "#pragma once" << std::endl;
        code << "#include <btoon/btoon.h>" << std::endl;
        code << "#include <optional>" << std::endl;
        code << "#include <string>" << std::endl;
        code << std::endl;
        
        std::string class_name = schema.getName();
        // Capitalize first letter
        if (!class_name.empty()) {
            class_name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(class_name[0])));
        }
        
        code << "namespace btoon {" << std::endl;
        code << std::endl;
        code << "class " << class_name << " {" << std::endl;
        code << "public:" << std::endl;
        
        // Generate member variables
        for (const auto& field : schema.getFields()) {
            std::string cpp_type;
            if (field.type == "string") cpp_type = "std::string";
            else if (field.type == "int") cpp_type = "int64_t";
            else if (field.type == "uint") cpp_type = "uint64_t";
            else if (field.type == "float" || field.type == "number") cpp_type = "double";
            else if (field.type == "bool") cpp_type = "bool";
            else if (field.type == "binary") cpp_type = "std::vector<uint8_t>";
            else if (field.type == "array") cpp_type = "btoon::Array";
            else if (field.type == "map") cpp_type = "btoon::Map";
            else cpp_type = "btoon::Value";
            
            if (!field.required) {
                cpp_type = "std::optional<" + cpp_type + ">";
            }
            
            code << "    " << cpp_type << " " << field.name << ";" << std::endl;
        }
        
        code << std::endl;
        code << "    // Convert to BTOON Value" << std::endl;
        code << "    btoon::Value toValue() const {" << std::endl;
        code << "        btoon::Map m;" << std::endl;
        
        for (const auto& field : schema.getFields()) {
            if (field.required) {
                code << "        m[\"" << field.name << "\"] = " << field.name << ";" << std::endl;
            } else {
                code << "        if (" << field.name << ".has_value()) {" << std::endl;
                code << "            m[\"" << field.name << "\"] = " << field.name << ".value();" << std::endl;
                code << "        }" << std::endl;
            }
        }
        
        code << "        return m;" << std::endl;
        code << "    }" << std::endl;
        code << std::endl;
        
        code << "    // Create from BTOON Value" << std::endl;
        code << "    static " << class_name << " fromValue(const btoon::Value& v) {" << std::endl;
        code << "        " << class_name << " result;" << std::endl;
        code << "        const auto* map = std::get_if<btoon::Map>(&v);" << std::endl;
        code << "        if (!map) throw std::runtime_error(\"Expected Map\");" << std::endl;
        code << std::endl;
        
        for (const auto& field : schema.getFields()) {
            code << "        if (auto it = map->find(\"" << field.name << "\"); it != map->end()) {" << std::endl;
            
            std::string extract_code;
            if (field.type == "string") {
                extract_code = "std::get<btoon::String>(it->second)";
            } else if (field.type == "int") {
                extract_code = "std::get<btoon::Int>(it->second)";
            } else if (field.type == "uint") {
                extract_code = "std::get<btoon::Uint>(it->second)";
            } else if (field.type == "float" || field.type == "number") {
                extract_code = "std::get<btoon::Float>(it->second)";
            } else if (field.type == "bool") {
                extract_code = "std::get<btoon::Bool>(it->second)";
            } else if (field.type == "binary") {
                extract_code = "std::get<btoon::Binary>(it->second)";
            } else if (field.type == "array") {
                extract_code = "std::get<btoon::Array>(it->second)";
            } else if (field.type == "map") {
                extract_code = "std::get<btoon::Map>(it->second)";
            } else {
                extract_code = "it->second";
            }
            
            if (field.required) {
                code << "            result." << field.name << " = " << extract_code << ";" << std::endl;
            } else {
                code << "            result." << field.name << " = " << extract_code << ";" << std::endl;
            }
            
            code << "        }";
            if (field.required) {
                code << " else {" << std::endl;
                code << "            throw std::runtime_error(\"Missing required field: " << field.name << "\");" << std::endl;
                code << "        }" << std::endl;
            } else {
                code << std::endl;
            }
        }
        
        code << "        return result;" << std::endl;
        code << "    }" << std::endl;
        code << "};" << std::endl;
        code << std::endl;
        code << "} // namespace btoon" << std::endl;
        
    } else if (language == "python" || language == "py") {
        // Generate Python code
        if (output_file.empty()) {
            output_file = fs::path(schema_file).stem().string() + ".py";
        }
        
        code << "# Generated from " << schema_file << " by btoon-schema" << std::endl;
        code << "from typing import Optional, Any, Dict, List" << std::endl;
        code << "from dataclasses import dataclass, field" << std::endl;
        code << "import btoon" << std::endl;
        code << std::endl;
        
        std::string class_name = schema.getName();
        // Capitalize first letter
        if (!class_name.empty()) {
            class_name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(class_name[0])));
        }
        
        code << "@dataclass" << std::endl;
        code << "class " << class_name << ":" << std::endl;
        code << "    \"\"\"" << schema.getDescription() << "\"\"\"" << std::endl;
        
        for (const auto& field_def : schema.getFields()) {
            std::string py_type;
            if (field_def.type == "string") py_type = "str";
            else if (field_def.type == "int") py_type = "int";
            else if (field_def.type == "uint") py_type = "int";
            else if (field_def.type == "float" || field_def.type == "number") py_type = "float";
            else if (field_def.type == "bool") py_type = "bool";
            else if (field_def.type == "binary") py_type = "bytes";
            else if (field_def.type == "array") py_type = "List[Any]";
            else if (field_def.type == "map") py_type = "Dict[str, Any]";
            else py_type = "Any";
            
            if (!field_def.required) {
                py_type = "Optional[" + py_type + "]";
            }
            
            code << "    " << field_def.name << ": " << py_type;
            
            if (!field_def.required && field_def.default_value.has_value()) {
                code << " = None";
            } else if (!field_def.required) {
                code << " = None";
            }
            
            if (field_def.description.has_value()) {
                code << "  # " << field_def.description.value();
            }
            code << std::endl;
        }
        
        code << std::endl;
        code << "    def to_dict(self) -> dict:" << std::endl;
        code << "        \"\"\"Convert to dictionary\"\"\"" << std::endl;
        code << "        result = {}" << std::endl;
        
        for (const auto& field_def : schema.getFields()) {
            if (field_def.required) {
                code << "        result['" << field_def.name << "'] = self." << field_def.name << std::endl;
            } else {
                code << "        if self." << field_def.name << " is not None:" << std::endl;
                code << "            result['" << field_def.name << "'] = self." << field_def.name << std::endl;
            }
        }
        
        code << "        return result" << std::endl;
        code << std::endl;
        
        code << "    @classmethod" << std::endl;
        code << "    def from_dict(cls, data: dict):" << std::endl;
        code << "        \"\"\"Create from dictionary\"\"\"" << std::endl;
        code << "        return cls(" << std::endl;
        
        bool first = true;
        for (const auto& field_def : schema.getFields()) {
            if (!first) code << "," << std::endl;
            code << "            " << field_def.name << "=data.get('" << field_def.name << "')";
            first = false;
        }
        
        code << std::endl;
        code << "        )" << std::endl;
        
    } else if (language == "typescript" || language == "ts") {
        // Generate TypeScript code
        if (output_file.empty()) {
            output_file = fs::path(schema_file).stem().string() + ".ts";
        }
        
        code << "// Generated from " << schema_file << " by btoon-schema" << std::endl;
        code << std::endl;
        
        std::string interface_name = schema.getName();
        // Capitalize first letter
        if (!interface_name.empty()) {
            interface_name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(interface_name[0])));
        }
        
        code << "export interface " << interface_name << " {" << std::endl;
        
        for (const auto& field : schema.getFields()) {
            std::string ts_type;
            if (field.type == "string") ts_type = "string";
            else if (field.type == "int" || field.type == "uint") ts_type = "number";
            else if (field.type == "float" || field.type == "number") ts_type = "number";
            else if (field.type == "bool") ts_type = "boolean";
            else if (field.type == "binary") ts_type = "Uint8Array";
            else if (field.type == "array") ts_type = "any[]";
            else if (field.type == "map") ts_type = "Record<string, any>";
            else ts_type = "any";
            
            code << "    " << field.name;
            if (!field.required) code << "?";
            code << ": " << ts_type << ";";
            
            if (field.description.has_value()) {
                code << " // " << field.description.value();
            }
            code << std::endl;
        }
        
        code << "}" << std::endl;
        
    } else {
        std::cerr << "Error: Unsupported language: " << language << std::endl;
        return 1;
    }
    
    if (!output_file.empty()) {
        write_file(output_file, code.str());
        std::cout << "✓ Generated " << language << " code to: " << output_file << std::endl;
    } else {
        std::cout << code.str();
    }
    
    return 0;
}

int cmd_infer(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: btoon-schema infer <data_file> [-o output_file]" << std::endl;
        return 1;
    }
    
    std::string data_file = argv[1];
    std::string output_file;
    bool verbose = false;
    
    // Parse options
    for (int i = 2; i < argc; i++) {
        if (std::string(argv[i]) == "-o" || std::string(argv[i]) == "--output") {
            if (i + 1 < argc) {
                output_file = argv[++i];
            }
        } else if (std::string(argv[i]) == "-v" || std::string(argv[i]) == "--verbose") {
            verbose = true;
        }
    }
    
    // Load and analyze data
    Value data = load_data_file(data_file);
    
    // Configure inference options
    InferenceOptions opts;
    opts.infer_constraints = true;
    opts.infer_patterns = true;
    opts.max_enum_values = 10;
    
    // Infer schema
    SchemaInferrer inferrer(opts);
    Schema schema = inferrer.infer(data, fs::path(data_file).stem().string());
    
    // Get statistics
    auto stats = inferrer.getStatistics();
    
    if (verbose) {
        std::cout << "Inference Statistics:" << std::endl;
        std::cout << "  Samples analyzed: " << stats.samples_analyzed << std::endl;
        std::cout << "  Fields discovered: " << stats.fields_discovered << std::endl;
        std::cout << "  Optional fields: " << stats.optional_fields << std::endl;
        std::cout << "  Enum fields: " << stats.enum_fields << std::endl;
        std::cout << std::endl;
    }
    
    // Convert schema to JSON for output
    Value schema_value = schema.toValue();
    std::string schema_json = value_to_json_string(schema_value);
    
    if (!output_file.empty()) {
        write_file(output_file, schema_json);
        std::cout << "✓ Inferred schema saved to: " << output_file << std::endl;
    } else {
        std::cout << schema_json << std::endl;
    }
    
    return 0;
}

int cmd_migrate(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: btoon-schema migrate <data_file> <old_schema> <new_schema> [-o output]" << std::endl;
        return 1;
    }
    
    std::string data_file = argv[1];
    std::string old_schema_file = argv[2];
    std::string new_schema_file = argv[3];
    std::string output_file;
    
    // Parse options
    for (int i = 4; i < argc; i++) {
        if (std::string(argv[i]) == "-o" || std::string(argv[i]) == "--output") {
            if (i + 1 < argc) {
                output_file = argv[++i];
            }
        }
    }
    
    // Load schemas and data
    Schema old_schema = load_schema_file(old_schema_file);
    Schema new_schema = load_schema_file(new_schema_file);
    Value data = load_data_file(data_file);
    
    // Check if migration is possible
    if (!old_schema.canMigrateTo(new_schema)) {
        std::cerr << "Error: Cannot migrate from " << old_schema.getName() 
                  << " v" << old_schema.getVersion().toString()
                  << " to " << new_schema.getName()
                  << " v" << new_schema.getVersion().toString() << std::endl;
        
        // Show differences
        auto diffs = old_schema.diff(new_schema);
        if (!diffs.empty()) {
            std::cerr << "\nDifferences:" << std::endl;
            for (const auto& diff : diffs) {
                std::cerr << "  - " << diff << std::endl;
            }
        }
        
        return 1;
    }
    
    // Perform migration
    auto migrated = old_schema.migrate(data, new_schema.getVersion());
    
    if (!migrated.has_value()) {
        std::cerr << "Error: Migration failed" << std::endl;
        return 1;
    }
    
    // Validate migrated data
    if (!new_schema.validate(migrated.value())) {
        std::cerr << "Error: Migrated data does not validate against new schema" << std::endl;
        auto errors = new_schema.validateWithErrors(migrated.value());
        for (const auto& error : errors) {
            std::cerr << "  - " << error << std::endl;
        }
        return 1;
    }
    
    // Save migrated data
    auto encoded = encode(migrated.value());
    
    if (!output_file.empty()) {
        write_file(output_file, encoded);
        std::cout << "✓ Migrated data saved to: " << output_file << std::endl;
    } else {
        write_file(fs::path(data_file).stem().string() + "_migrated.btoon", encoded);
        std::cout << "✓ Migrated data saved to: " 
                  << fs::path(data_file).stem().string() + "_migrated.btoon" << std::endl;
    }
    
    return 0;
}

int cmd_diff(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: btoon-schema diff <schema1> <schema2>" << std::endl;
        return 1;
    }
    
    std::string schema1_file = argv[1];
    std::string schema2_file = argv[2];
    
    Schema schema1 = load_schema_file(schema1_file);
    Schema schema2 = load_schema_file(schema2_file);
    
    std::cout << "Comparing schemas:" << std::endl;
    std::cout << "  Schema 1: " << schema1.getName() << " v" << schema1.getVersion().toString() << std::endl;
    std::cout << "  Schema 2: " << schema2.getName() << " v" << schema2.getVersion().toString() << std::endl;
    std::cout << std::endl;
    
    auto diffs = schema1.diff(schema2);
    
    if (diffs.empty()) {
        std::cout << "✓ Schemas are identical" << std::endl;
    } else {
        std::cout << "Differences found:" << std::endl;
        for (const auto& diff : diffs) {
            std::cout << "  • " << diff << std::endl;
        }
        
        // Check compatibility
        std::cout << std::endl;
        if (schema1.isCompatibleWith(schema2)) {
            std::cout << "✓ Schemas are compatible" << std::endl;
        } else {
            std::cout << "✗ Schemas are incompatible" << std::endl;
        }
        
        if (schema1.canMigrateTo(schema2)) {
            std::cout << "✓ Migration from v" << schema1.getVersion().toString() 
                      << " to v" << schema2.getVersion().toString() << " is possible" << std::endl;
        } else {
            std::cout << "✗ Direct migration is not available" << std::endl;
        }
    }
    
    return 0;
}

int cmd_doc(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: btoon-schema doc <schema_file> [-o output_file] [-f format]" << std::endl;
        return 1;
    }
    
    std::string schema_file = argv[1];
    std::string output_file;
    std::string format = "markdown";
    
    // Parse options
    for (int i = 2; i < argc; i++) {
        if (std::string(argv[i]) == "-o" || std::string(argv[i]) == "--output") {
            if (i + 1 < argc) {
                output_file = argv[++i];
            }
        } else if (std::string(argv[i]) == "-f" || std::string(argv[i]) == "--format") {
            if (i + 1 < argc) {
                format = argv[++i];
            }
        }
    }
    
    Schema schema = load_schema_file(schema_file);
    std::stringstream doc;
    
    if (format == "markdown" || format == "md") {
        // Generate Markdown documentation
        doc << "# " << schema.getName() << " Schema Documentation" << std::endl;
        doc << std::endl;
        doc << "**Version:** " << schema.getVersion().toString() << std::endl;
        doc << std::endl;
        
        if (!schema.getDescription().empty()) {
            doc << schema.getDescription() << std::endl;
            doc << std::endl;
        }
        
        doc << "## Fields" << std::endl;
        doc << std::endl;
        doc << "| Field | Type | Required | Description |" << std::endl;
        doc << "|-------|------|----------|-------------|" << std::endl;
        
        for (const auto& field : schema.getFields()) {
            doc << "| `" << field.name << "` ";
            doc << "| " << field.type << " ";
            doc << "| " << (field.required ? "Yes" : "No") << " ";
            doc << "| " << (field.description.has_value() ? field.description.value() : "-") << " ";
            doc << "|" << std::endl;
        }
        
        doc << std::endl;
        doc << "## Field Details" << std::endl;
        doc << std::endl;
        
        for (const auto& field : schema.getFields()) {
            doc << "### " << field.name << std::endl;
            doc << std::endl;
            doc << "- **Type:** `" << field.type << "`" << std::endl;
            doc << "- **Required:** " << (field.required ? "Yes" : "No") << std::endl;
            
            if (field.description.has_value()) {
                doc << "- **Description:** " << field.description.value() << std::endl;
            }
            
            if (field.default_value.has_value()) {
                doc << "- **Default:** `" << value_to_json_string(field.default_value.value(), false) << "`" << std::endl;
            }
            
            if (field.constraints.has_value()) {
                doc << "- **Constraints:**" << std::endl;
                auto constraints_json = value_to_json_string(field.constraints.value(), true);
                doc << "  ```json" << std::endl;
                doc << "  " << constraints_json << std::endl;
                doc << "  ```" << std::endl;
            }
            
            doc << std::endl;
        }
        
        doc << "## Schema Evolution" << std::endl;
        doc << std::endl;
        doc << "- **Evolution Strategy:** ";
        switch (schema.getEvolutionStrategy()) {
            case EvolutionStrategy::STRICT:
                doc << "Strict (no changes allowed)";
                break;
            case EvolutionStrategy::ADDITIVE:
                doc << "Additive (only new optional fields)";
                break;
            case EvolutionStrategy::BACKWARD_COMPATIBLE:
                doc << "Backward Compatible";
                break;
            case EvolutionStrategy::FLEXIBLE:
                doc << "Flexible (any changes with migrations)";
                break;
        }
        doc << std::endl;
        
    } else if (format == "html") {
        // Generate HTML documentation
        doc << "<!DOCTYPE html>" << std::endl;
        doc << "<html><head>" << std::endl;
        doc << "<title>" << schema.getName() << " Schema</title>" << std::endl;
        doc << "<style>" << std::endl;
        doc << "body { font-family: Arial, sans-serif; margin: 40px; }" << std::endl;
        doc << "table { border-collapse: collapse; width: 100%; }" << std::endl;
        doc << "th, td { border: 1px solid #ddd; padding: 8px; text-align: left; }" << std::endl;
        doc << "th { background-color: #f2f2f2; }" << std::endl;
        doc << ".required { color: red; }" << std::endl;
        doc << ".optional { color: green; }" << std::endl;
        doc << "</style>" << std::endl;
        doc << "</head><body>" << std::endl;
        
        doc << "<h1>" << schema.getName() << " Schema</h1>" << std::endl;
        doc << "<p><strong>Version:</strong> " << schema.getVersion().toString() << "</p>" << std::endl;
        
        if (!schema.getDescription().empty()) {
            doc << "<p>" << schema.getDescription() << "</p>" << std::endl;
        }
        
        doc << "<h2>Fields</h2>" << std::endl;
        doc << "<table>" << std::endl;
        doc << "<tr><th>Field</th><th>Type</th><th>Required</th><th>Description</th></tr>" << std::endl;
        
        for (const auto& field : schema.getFields()) {
            doc << "<tr>" << std::endl;
            doc << "<td><code>" << field.name << "</code></td>" << std::endl;
            doc << "<td>" << field.type << "</td>" << std::endl;
            doc << "<td class=\"" << (field.required ? "required" : "optional") << "\">" 
                << (field.required ? "Yes" : "No") << "</td>" << std::endl;
            doc << "<td>" << (field.description.has_value() ? field.description.value() : "") << "</td>" << std::endl;
            doc << "</tr>" << std::endl;
        }
        
        doc << "</table>" << std::endl;
        doc << "</body></html>" << std::endl;
        
    } else {
        std::cerr << "Error: Unsupported format: " << format << std::endl;
        return 1;
    }
    
    if (!output_file.empty()) {
        write_file(output_file, doc.str());
        std::cout << "✓ Documentation saved to: " << output_file << std::endl;
    } else {
        std::cout << doc.str();
    }
    
    return 0;
}

// Utility function implementations
std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Cannot open file: " + path);
    }
    
    file.seekg(0, std::ios::end);
    size_t size = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);
    
    std::vector<uint8_t> buffer(size);
    file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(size));
    
    return buffer;
}

void write_file(const std::string& path, const std::vector<uint8_t>& data) {
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Cannot create file: " + path);
    }
    
    file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

void write_file(const std::string& path, const std::string& content) {
    std::ofstream file(path);
    if (!file) {
        throw std::runtime_error("Cannot create file: " + path);
    }
    
    file << content;
}

Value load_data_file(const std::string& path) {
    std::string extension = fs::path(path).extension().string();
    
    if (extension == ".json") {
        std::ifstream file(path);
        if (!file) {
            throw std::runtime_error("Cannot open file: " + path);
        }
        
        json j;
        file >> j;
        return json_to_value(j);
    } else {
        // Assume BTOON binary format
        auto buffer = read_file(path);
        return decode(buffer);
    }
}

Schema load_schema_file(const std::string& path) {
    std::string extension = fs::path(path).extension().string();
    
    if (extension == ".json") {
        std::ifstream file(path);
        if (!file) {
            throw std::runtime_error("Cannot open file: " + path);
        }
        
        json j;
        file >> j;
        Value schema_value = json_to_value(j);
        return Schema::fromValue(schema_value);
    } else if (extension == ".bsch") {
        // Binary compiled schema
        auto buffer = read_file(path);
        Value schema_value = decode(buffer);
        return Schema::fromValue(schema_value);
    } else {
        // Try to parse as JSON
        std::ifstream file(path);
        if (!file) {
            throw std::runtime_error("Cannot open file: " + path);
        }
        
        json j;
        file >> j;
        Value schema_value = json_to_value(j);
        return Schema::fromValue(schema_value);
    }
}

std::string value_to_json_string(const Value& v, bool pretty) {
    json j;
    
    std::visit([&j](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, Nil>) {
            j = nullptr;
        } else if constexpr (std::is_same_v<T, Bool>) {
            j = arg;
        } else if constexpr (std::is_same_v<T, Int>) {
            j = arg;
        } else if constexpr (std::is_same_v<T, Uint>) {
            j = arg;
        } else if constexpr (std::is_same_v<T, Float>) {
            j = arg;
        } else if constexpr (std::is_same_v<T, String>) {
            j = arg;
        } else if constexpr (std::is_same_v<T, Binary>) {
            j = json::binary(arg);
        } else if constexpr (std::is_same_v<T, Array>) {
            j = json::array();
            for (size_t i = 0; i < arg.size(); ++i) {
                json item_json;
                // Recursive conversion would be needed here
                j.push_back(item_json);
            }
        } else if constexpr (std::is_same_v<T, Map>) {
            j = json::object();
            for (const auto& [key, value] : arg) {
                // Recursive conversion would be needed here
                j[key] = json();
            }
        }
    }, v);
    
    return pretty ? j.dump(2) : j.dump();
}

Value json_to_value(const json& j) {
    if (j.is_null()) {
        return Nil{};
    } else if (j.is_boolean()) {
        return Bool(j.get<bool>());
    } else if (j.is_number_integer()) {
        if (j.get<int64_t>() < 0) {
            return Int(j.get<int64_t>());
        } else {
            return Uint(j.get<uint64_t>());
        }
    } else if (j.is_number_float()) {
        return Float(j.get<double>());
    } else if (j.is_string()) {
        return String(j.get<std::string>());
    } else if (j.is_binary()) {
        auto bin = j.get_binary();
        return Binary(bin.begin(), bin.end());
    } else if (j.is_array()) {
        Array arr;
        for (const auto& item : j) {
            arr.push_back(json_to_value(item));
        }
        return arr;
    } else if (j.is_object()) {
        Map m;
        for (auto it = j.begin(); it != j.end(); ++it) {
            m[it.key()] = json_to_value(it.value());
        }
        return m;
    }
    
    return Nil{};
}

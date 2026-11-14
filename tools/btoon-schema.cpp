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
#include "json_mapping.h"
#include "tool_io.h"
#include "btoon/schema.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;
using btoon::tools::json;
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
Value load_json_file(const std::string& path);
Value load_data_file(const std::string& path);
Schema load_schema_file(const std::string& path);
std::string value_to_json_string(const Value& v, bool pretty = true);

int main(int argc, char* argv[]) {
    btoon::tools::use_binary_stdio();
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }
    
    std::string command = argv[1];
    int rc = 0;
    
    try {
        if (command == "validate") {
            rc = cmd_validate(argc - 1, argv + 1);
        } else if (command == "compile") {
            rc = cmd_compile(argc - 1, argv + 1);
        } else if (command == "generate" || command == "gen") {
            rc = cmd_generate(argc - 1, argv + 1);
        } else if (command == "infer") {
            rc = cmd_infer(argc - 1, argv + 1);
        } else if (command == "migrate") {
            rc = cmd_migrate(argc - 1, argv + 1);
        } else if (command == "diff") {
            rc = cmd_diff(argc - 1, argv + 1);
        } else if (command == "doc") {
            rc = cmd_doc(argc - 1, argv + 1);
        } else if (command == "--help" || command == "-h") {
            print_usage(argv[0]);
        } else {
            std::cerr << "Unknown command: " << command << std::endl;
            print_usage(argv[0]);
            rc = 1;
        }
    } catch (const BtoonException& e) {
        std::cerr << "Error: " << e.code_name() << ": " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    // Results go to stdout unless -o is given; a failed write (full disk, closed
    // stdout) must not look like success.
    if (!btoon::tools::flush_stdout()) {
        std::cerr << "Error: writing to standard output failed" << std::endl;
        return 1;
    }
    return rc;
}

void print_usage(const std::string& program_name) {
    std::cout << "BTOON Schema Compiler v" << btoon::version() << std::endl;
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
    std::cout << "  -o, --output <file>  Output file (\"-\" for standard output)" << std::endl;
    std::cout << "  -l, --language <lang> generate: cpp, python or typescript" << std::endl;
    std::cout << "  -n, --namespace <ns> generate -l cpp: wrap the class in a namespace" << std::endl;
    std::cout << "  -f, --format <fmt>   doc: markdown or html" << std::endl;
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

// --- Code generation ------------------------------------------------------------------
//
// Field names become identifiers: characters other than [A-Za-z0-9_] turn into '_', a
// leading digit gets a '_' prefix, and keywords (and the generated methods' names) get a
// '_' suffix. The original names stay the map keys. Two fields that end up with the same
// identifier are an error. Before 0.3.0 the generated C++ threw bad_variant_access on
// valid data (an integer in a "uint" or "number" field) and the Python failed at import
// (an optional field before a required one), and names weren't sanitized.

namespace codegen {

const std::set<std::string> kCppKeywords = {
    "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor", "bool", "break", "case", "catch",
    "char", "char8_t", "char16_t", "char32_t", "class", "compl", "concept", "const", "consteval", "constexpr",
    "constinit", "const_cast", "continue", "co_await", "co_return", "co_yield", "decltype", "default", "delete",
    "do", "double", "dynamic_cast", "else", "enum", "explicit", "export", "extern", "false", "float", "for",
    "friend", "goto", "if", "inline", "int", "long", "mutable", "namespace", "new", "noexcept", "not", "not_eq",
    "nullptr", "operator", "or", "or_eq", "private", "protected", "public", "register", "reinterpret_cast",
    "requires", "return", "short", "signed", "sizeof", "static", "static_assert", "static_cast", "struct",
    "switch", "template", "this", "thread_local", "throw", "true", "try", "typedef", "typeid", "typename",
    "union", "unsigned", "using", "virtual", "void", "volatile", "wchar_t", "while", "xor", "xor_eq",
    // Generated members.
    "toValue", "fromValue", "fail"};

const std::set<std::string> kPythonKeywords = {
    "False", "None", "True", "and", "as", "assert", "async", "await", "break", "class", "continue", "def", "del",
    "elif", "else", "except", "finally", "for", "from", "global", "if", "import", "in", "is", "lambda",
    "nonlocal", "not", "or", "pass", "raise", "return", "try", "while", "with", "yield",
    // Generated members, and dataclass __init__'s first parameter.
    "to_dict", "from_dict", "self"};

bool is_alnum(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

std::string identifier(const std::string& name, const std::set<std::string>& reserved) {
    std::string id;
    for (char c : name) id += is_alnum(c) || c == '_' ? c : '_';
    if (id.empty() || (id[0] >= '0' && id[0] <= '9')) id = "_" + id;
    if (reserved.count(id)) id += "_";
    return id;
}

// "user-data" -> "UserData"; a name without letters or digits becomes "Schema".
std::string type_name(const std::string& name, const std::set<std::string>& reserved) {
    std::string out;
    bool upper = true;
    for (char c : name) {
        if (!is_alnum(c)) {
            upper = true;
            continue;
        }
        out += upper && c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
        upper = false;
    }
    if (out.empty()) out = "Schema";
    if (out[0] >= '0' && out[0] <= '9') out = "Schema" + out;
    if (reserved.count(out)) out += "_";
    return out;
}

// One identifier per field, in field order; throws if two fields collide.
std::vector<std::string> field_identifiers(const std::vector<SchemaField>& fields,
                                           const std::set<std::string>& reserved) {
    std::vector<std::string> ids;
    std::map<std::string, std::string> owner;
    for (const auto& f : fields) {
        std::string id = identifier(f.name, reserved);
        auto [it, inserted] = owner.emplace(id, f.name);
        if (!inserted) {
            throw std::runtime_error("fields \"" + it->second + "\" and \"" + f.name +
                                     "\" both become the identifier " + id);
        }
        ids.push_back(id);
    }
    return ids;
}

// Octal escapes never run into a following digit the way \x escapes do.
std::string cpp_string(const std::string& s) {
    std::string out = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += static_cast<char>(c);
        } else if (c < 0x20 || c >= 0x7f) {
            char buf[5];
            std::snprintf(buf, sizeof buf, "\\%03o", c);
            out += buf;
        } else {
            out += static_cast<char>(c);
        }
    }
    return out + "\"";
}

std::string python_string(const std::string& s) {
    std::string out = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += static_cast<char>(c);
        } else if (c < 0x20 || c == 0x7f) {
            char buf[5];
            std::snprintf(buf, sizeof buf, "\\x%02x", c);
            out += buf;
        } else {
            out += static_cast<char>(c);  // UTF-8 passes through: Python source is UTF-8
        }
    }
    return out + "\"";
}

// Descriptions go into line comments: keep them on one line.
std::string one_line(const std::string& s) {
    std::string out;
    for (char c : s) out += (c == '\n' || c == '\r') ? ' ' : c;
    return out;
}

struct CppType {
    std::string type;     // member type
    std::string extract;  // expression of `v` (a const btoon::Value&) giving a pointer or optional, empty if v itself
    std::string what;     // for the error message
};

CppType cpp_type(const std::string& t) {
    if (t == "string") return {"std::string", "std::get_if<btoon::String>(&v)", "a string"};
    if (t == "int") return {"int64_t", "v.as_int64()", "an integer in the int64 range"};
    if (t == "uint") return {"uint64_t", "v.as_uint64()", "a non-negative integer"};
    if (t == "float") return {"double", "std::get_if<btoon::Float>(&v)", "a float"};
    if (t == "number") return {"double", "v.as_double()", "a number"};
    if (t == "bool") return {"bool", "std::get_if<btoon::Bool>(&v)", "a bool"};
    if (t == "binary") return {"btoon::Binary", "std::get_if<btoon::Binary>(&v)", "binary data"};
    if (t == "array") return {"btoon::Array", "std::get_if<btoon::Array>(&v)", "an array"};
    if (t == "map") return {"btoon::Map", "std::get_if<btoon::Map>(&v)", "a map"};
    if (t == "timestamp") return {"btoon::Timestamp", "std::get_if<btoon::Timestamp>(&v)", "a timestamp"};
    if (t == "date") return {"btoon::Date", "std::get_if<btoon::Date>(&v)", "a date"};
    if (t == "datetime") return {"btoon::DateTime", "std::get_if<btoon::DateTime>(&v)", "a datetime"};
    if (t == "extension") return {"btoon::Extension", "std::get_if<btoon::Extension>(&v)", "an extension"};
    if (t == "vector_float") return {"btoon::VectorFloat", "std::get_if<btoon::VectorFloat>(&v)", "a float32 vector"};
    if (t == "vector_double") return {"btoon::VectorDouble", "std::get_if<btoon::VectorDouble>(&v)", "a float64 vector"};
    return {"btoon::Value", "", ""};  // bigint, nil, any: kept as values
}

std::string generate_cpp(const Schema& schema, const std::string& source, const std::string& ns) {
    const auto fields = schema.getFields();
    const auto ids = field_identifiers(fields, kCppKeywords);
    const std::string cls = type_name(schema.getName(), kCppKeywords);
    std::ostringstream code;
    code << "// Generated from " << one_line(source) << " by btoon-schema " << btoon::version() << ". Do not edit.\n"
         << "#pragma once\n\n"
         << "#include <btoon/btoon.h>\n\n"
         << "#include <cstdint>\n#include <optional>\n#include <string>\n#include <utility>\n\n";
    if (!ns.empty()) code << "namespace " << ns << " {\n\n";
    if (!schema.getDescription().empty()) code << "// " << one_line(schema.getDescription()) << "\n";
    code << "struct " << cls << " {\n";
    for (size_t i = 0; i < fields.size(); ++i) {
        const auto t = cpp_type(fields[i].type);
        if (fields[i].description) code << "    // " << one_line(*fields[i].description) << "\n";
        if (fields[i].required) {
            code << "    " << t.type << " " << ids[i] << "{};\n";
        } else {
            code << "    std::optional<" << t.type << "> " << ids[i] << ";\n";
        }
    }
    code << "\n    btoon::Value toValue() const {\n        btoon::Map m;\n";
    for (size_t i = 0; i < fields.size(); ++i) {
        const std::string key = cpp_string(fields[i].name);
        // this->: a field named "m" mustn't resolve to the local map.
        if (fields[i].required) {
            code << "        m.emplace(" << key << ", btoon::Value(this->" << ids[i] << "));\n";
        } else {
            code << "        if (this->" << ids[i] << ") m.emplace(" << key << ", btoon::Value(*this->" << ids[i]
                 << "));\n";
        }
    }
    code << "        return btoon::Value(std::move(m));\n    }\n\n";
    code << "    /// @throws btoon::BtoonException (invalid_argument) if `value` isn't a map, misses a\n"
         << "    /// required field, or holds a field of the wrong type.\n"
         << "    static " << cls << " fromValue(const btoon::Value& value) {\n"
         << "        const auto* map = std::get_if<btoon::Map>(&value);\n"
         << "        if (!map) fail(\"is not a map\");\n"
         << "        " << cls << " out;\n";
    for (size_t i = 0; i < fields.size(); ++i) {
        const auto t = cpp_type(fields[i].type);
        const std::string key = cpp_string(fields[i].name);
        code << "        if (auto it = map->find(" << key << "); it != map->end()) {\n"
             << "            const btoon::Value& v = it->second;\n";
        if (t.extract.empty()) {
            code << "            out." << ids[i] << " = v;\n";
        } else {
            code << "            auto x = " << t.extract << ";\n"
                 << "            if (!x) fail(" << cpp_string("field \"" + fields[i].name + "\" is not " + t.what)
                 << ");\n"
                 << "            out." << ids[i] << " = *x;\n";
        }
        code << "        }";
        if (fields[i].required) {
            code << " else {\n            fail(" << cpp_string("misses the required field \"" + fields[i].name + "\"")
                 << ");\n        }\n";
        } else {
            code << "\n";
        }
    }
    code << "        return out;\n    }\n\n"
         << "private:\n"
         << "    [[noreturn]] static void fail(const char* what) {\n"
         << "        throw btoon::BtoonException(btoon::ErrorCode::invalid_argument, std::string("
         << cpp_string(cls + ": ") << ") + what);\n"
         << "    }\n"
         << "};\n";
    if (!ns.empty()) code << "\n}  // namespace " << ns << "\n";
    return code.str();
}

std::string python_type(const std::string& t) {
    if (t == "string") return "str";
    if (t == "int" || t == "uint" || t == "bigint") return "int";
    if (t == "float" || t == "number") return "float";
    if (t == "bool") return "bool";
    if (t == "binary") return "bytes";
    if (t == "array") return "List[Any]";
    if (t == "map") return "Dict[str, Any]";
    return "Any";
}

std::string generate_python(const Schema& schema, const std::string& source) {
    const auto all = schema.getFields();
    // A dataclass needs the fields with defaults (the optional ones) last.
    std::vector<SchemaField> fields;
    for (const auto& f : all) {
        if (f.required) fields.push_back(f);
    }
    for (const auto& f : all) {
        if (!f.required) fields.push_back(f);
    }
    const auto ids = field_identifiers(fields, kPythonKeywords);
    const std::string cls = type_name(schema.getName(), kPythonKeywords);
    std::ostringstream code;
    code << "# Generated from " << one_line(source) << " by btoon-schema " << btoon::version() << ". Do not edit.\n"
         << "from __future__ import annotations\n\n"
         << "from dataclasses import dataclass\n"
         << "from typing import Any, Dict, List, Optional\n\n\n"
         << "@dataclass\n"
         << "class " << cls << ":\n";
    if (!schema.getDescription().empty()) code << "    " << python_string(schema.getDescription()) << "\n\n";
    for (size_t i = 0; i < fields.size(); ++i) {
        const std::string t = python_type(fields[i].type);
        code << "    " << ids[i] << ": " << (fields[i].required ? t : "Optional[" + t + "]")
             << (fields[i].required ? "" : " = None");
        if (fields[i].description) code << "  # " << one_line(*fields[i].description);
        code << "\n";
    }
    if (!fields.empty()) code << "\n";
    code << "    def to_dict(self) -> Dict[str, Any]:\n"
         << "        result: Dict[str, Any] = {}\n";
    for (size_t i = 0; i < fields.size(); ++i) {
        const std::string key = python_string(fields[i].name);
        if (fields[i].required) {
            code << "        result[" << key << "] = self." << ids[i] << "\n";
        } else {
            code << "        if self." << ids[i] << " is not None:\n"
                 << "            result[" << key << "] = self." << ids[i] << "\n";
        }
    }
    code << "        return result\n\n"
         << "    @classmethod\n"
         << "    def from_dict(cls, data: Dict[str, Any]) -> " << python_string(cls) << ":\n"
         << "        \"\"\"Raises KeyError if a required field is missing.\"\"\"\n"
         << "        return cls(\n";
    for (size_t i = 0; i < fields.size(); ++i) {
        const std::string key = python_string(fields[i].name);
        code << "            " << ids[i] << "=" << (fields[i].required ? "data[" + key + "]" : "data.get(" + key + ")")
             << ",\n";
    }
    code << "        )\n";
    return code.str();
}

std::string typescript_type(const std::string& t) {
    if (t == "string") return "string";
    if (t == "int" || t == "uint" || t == "float" || t == "number") return "number";
    if (t == "bigint") return "bigint | number";
    if (t == "bool") return "boolean";
    if (t == "binary") return "Uint8Array";
    if (t == "array") return "any[]";
    if (t == "map") return "Record<string, any>";
    if (t == "vector_float") return "Float32Array";
    if (t == "vector_double") return "Float64Array";
    return "any";
}

std::string generate_typescript(const Schema& schema, const std::string& source) {
    std::ostringstream code;
    code << "// Generated from " << one_line(source) << " by btoon-schema " << btoon::version() << ". Do not edit.\n\n";
    if (!schema.getDescription().empty()) code << "// " << one_line(schema.getDescription()) << "\n";
    code << "export interface " << type_name(schema.getName(), {}) << " {\n";
    for (const auto& f : schema.getFields()) {
        bool plain = !f.name.empty() && !(f.name[0] >= '0' && f.name[0] <= '9');
        for (char c : f.name) plain = plain && (is_alnum(c) || c == '_' || c == '$');
        // A property name that isn't an identifier is quoted (JSON strings are valid TypeScript).
        code << "    " << (plain ? f.name : json(f.name).dump()) << (f.required ? "" : "?") << ": "
             << typescript_type(f.type) << ";";
        if (f.description) code << " // " << one_line(*f.description);
        code << "\n";
    }
    code << "}\n";
    return code.str();
}

}  // namespace codegen

int cmd_generate(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: btoon-schema generate <schema_file> -l <language> [-o output_file|-] [-n namespace]"
                  << std::endl;
        std::cerr << "Languages: cpp, python, typescript" << std::endl;
        return 1;
    }

    std::string schema_file = argv[1];
    std::string language;
    std::string output_file;
    std::string ns;

    for (int i = 2; i < argc; i++) {
        const std::string a = argv[i];
        if ((a == "-l" || a == "--language") && i + 1 < argc) {
            language = argv[++i];
        } else if ((a == "-o" || a == "--output") && i + 1 < argc) {
            output_file = argv[++i];
        } else if ((a == "-n" || a == "--namespace") && i + 1 < argc) {
            ns = argv[++i];
        } else {
            std::cerr << "Error: unknown or incomplete option: " << a << std::endl;
            return 1;
        }
    }
    if (language.empty()) {
        std::cerr << "Error: Language must be specified with -l option" << std::endl;
        return 1;
    }

    Schema schema = load_schema_file(schema_file);
    const std::string source = fs::path(schema_file).filename().string();
    std::string code;
    std::string extension;
    if (language == "cpp" || language == "c++") {
        if (!ns.empty() && ns != codegen::identifier(ns, codegen::kCppKeywords)) {
            std::cerr << "Error: the namespace must be a C++ identifier: " << ns << std::endl;
            return 1;
        }
        code = codegen::generate_cpp(schema, source, ns);
        extension = ".h";
    } else if (language == "python" || language == "py") {
        code = codegen::generate_python(schema, source);
        extension = ".py";
    } else if (language == "typescript" || language == "ts") {
        code = codegen::generate_typescript(schema, source);
        extension = ".ts";
    } else {
        std::cerr << "Error: Unsupported language: " << language << " (cpp, python, typescript)" << std::endl;
        return 1;
    }

    if (output_file.empty()) output_file = fs::path(schema_file).stem().string() + extension;
    if (output_file == "-") {
        std::cout << code;
    } else {
        write_file(output_file, code);
        std::cout << "✓ Generated " << language << " code to: " << output_file << std::endl;
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
    
    // The command line can't load migration functions, so data moves between versions
    // unchanged when the new schema is a compatible evolution of the old one (see
    // EvolutionStrategy), and is then validated against the new schema.
    const bool compatible = !old_schema.canMigrateTo(new_schema) && old_schema.isCompatibleWith(new_schema);
    if (!old_schema.canMigrateTo(new_schema) && !compatible) {
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
    auto migrated = compatible ? std::optional<Value>(data) : old_schema.migrate(data, new_schema.getVersion());
    if (compatible) {
        std::cout << "The schemas are compatible: the data is carried over unchanged" << std::endl;
    }
    
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
    const std::string bytes = btoon::tools::read_file(path);  // also works for pipes
    return {bytes.begin(), bytes.end()};
}

void write_file(const std::string& path, const std::vector<uint8_t>& data) {
    btoon::tools::write_file(path, std::string_view(reinterpret_cast<const char*>(data.data()), data.size()));
}

void write_file(const std::string& path, const std::string& content) {
    btoon::tools::write_file(path, content);  // throws if the write fails
}

// JSON goes through the shared lossless mapping (tools/json_mapping.h): the parser
// is depth-limited, and uint64, bigint, timestamps etc. keep their values.
Value load_json_file(const std::string& path) {
    return btoon::tools::from_json(btoon::tools::parse_json(btoon::tools::read_file(path)));
}

Value load_data_file(const std::string& path) {
    if (fs::path(path).extension().string() == ".json") return load_json_file(path);
    // Otherwise assume BTOON.
    return decode(read_file(path));
}

Schema load_schema_file(const std::string& path) {
    if (fs::path(path).extension().string() == ".bsch") {
        // Binary compiled schema
        return Schema::fromValue(decode(read_file(path)));
    }
    // JSON (whatever the extension)
    return Schema::fromValue(load_json_file(path));
}

std::string value_to_json_string(const Value& v, bool pretty) {
    auto j = btoon::tools::to_json(v);  // recursive, lossless (tools/json_mapping.h)
    return pretty ? j.dump(2) : j.dump();
}

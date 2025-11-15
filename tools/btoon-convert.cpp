/**
 * btoon-convert: Universal format converter for BTOON
 * 
 * Converts between BTOON and various data formats:
 *   - JSON
 *   - YAML
 *   - MessagePack
 *   - CBOR
 *   - CSV (for tabular data)
 *   - XML
 * 
 * Features:
 *   - Auto-detection of input format
 *   - Batch conversion
 *   - Schema validation
 *   - Streaming for large files
 *   - Pretty printing
 */

#include "btoon/btoon.h"
#include "btoon/encoder.h"
#include "btoon/decoder.h"
#include "btoon/schema.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <vector>
#include <map>
#include <algorithm>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <cstring>
#include <cctype>

namespace fs = std::filesystem;
using json = nlohmann::json;
using namespace btoon;

// Supported formats
enum class Format {
    AUTO,      // Auto-detect
    BTOON,     // Binary BTOON
    JSON,      // JSON
    YAML,      // YAML
    MSGPACK,   // MessagePack
    CBOR,      // CBOR
    CSV,       // CSV (tabular data only)
    XML,       // XML
    UNKNOWN
};

// Conversion options
struct ConvertOptions {
    Format input_format = Format::AUTO;
    Format output_format = Format::BTOON;
    bool pretty = false;
    bool validate = false;
    std::string schema_file;
    bool compress = false;
    CompressionAlgorithm compression_algo = CompressionAlgorithm::ZLIB;
    bool streaming = false;
    size_t batch_size = 100;
    bool verbose = false;
    bool force = false;
    std::string delimiter = ",";  // For CSV
    bool has_header = true;  // For CSV
};

// Forward declarations
void print_usage(const std::string& program_name);
Format detect_format(const std::string& filename);
Format parse_format(const std::string& str);
std::string format_to_string(Format fmt);
Value read_input(const std::string& filename, Format fmt, const ConvertOptions& opts);
void write_output(const std::string& filename, const Value& data, Format fmt, const ConvertOptions& opts);
Value read_json(std::istream& input);
void write_json(std::ostream& output, const Value& data, bool pretty);
Value read_csv(std::istream& input, const ConvertOptions& opts);
void write_csv(std::ostream& output, const Value& data, const ConvertOptions& opts);
Value read_yaml(std::istream& input);
void write_yaml(std::ostream& output, const Value& data, bool pretty);
Value read_msgpack(std::istream& input);
void write_msgpack(std::ostream& output, const Value& data);
Value read_cbor(std::istream& input);
void write_cbor(std::ostream& output, const Value& data);
Value read_xml(std::istream& input);
void write_xml(std::ostream& output, const Value& data, bool pretty);
Value json_to_value(const json& j);
json value_to_json(const Value& v);
bool validate_with_schema(const Value& data, const std::string& schema_file);
void convert_batch(const std::vector<std::string>& input_files, 
                  const std::string& output_dir,
                  const ConvertOptions& opts);
void convert_streaming(const std::string& input_file, 
                      const std::string& output_file,
                      const ConvertOptions& opts);

int main(int argc, char* argv[]) {
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }
    
    ConvertOptions opts;
    std::vector<std::string> input_files;
    std::string output_file;
    std::string output_dir;
    bool batch_mode = false;
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "-i" || arg == "--input-format") {
            if (i + 1 < argc) {
                opts.input_format = parse_format(argv[++i]);
                if (opts.input_format == Format::UNKNOWN) {
                    std::cerr << "Unknown input format: " << argv[i] << std::endl;
                    return 1;
                }
            }
        } else if (arg == "-o" || arg == "--output-format") {
            if (i + 1 < argc) {
                opts.output_format = parse_format(argv[++i]);
                if (opts.output_format == Format::UNKNOWN) {
                    std::cerr << "Unknown output format: " << argv[i] << std::endl;
                    return 1;
                }
            }
        } else if (arg == "-p" || arg == "--pretty") {
            opts.pretty = true;
        } else if (arg == "-s" || arg == "--schema") {
            if (i + 1 < argc) {
                opts.schema_file = argv[++i];
                opts.validate = true;
            }
        } else if (arg == "-c" || arg == "--compress") {
            opts.compress = true;
        } else if (arg == "--compression-algo") {
            if (i + 1 < argc) {
                std::string algo = argv[++i];
                if (algo == "zlib") opts.compression_algo = CompressionAlgorithm::ZLIB;
                else if (algo == "lz4") opts.compression_algo = CompressionAlgorithm::LZ4;
                else if (algo == "zstd") opts.compression_algo = CompressionAlgorithm::ZSTD;
                else {
                    std::cerr << "Unknown compression algorithm: " << algo << std::endl;
                    return 1;
                }
            }
        } else if (arg == "--streaming") {
            opts.streaming = true;
        } else if (arg == "--batch") {
            batch_mode = true;
        } else if (arg == "--batch-size") {
            if (i + 1 < argc) {
                opts.batch_size = std::stoull(argv[++i]);
            }
        } else if (arg == "-v" || arg == "--verbose") {
            opts.verbose = true;
        } else if (arg == "-f" || arg == "--force") {
            opts.force = true;
        } else if (arg == "--csv-delimiter") {
            if (i + 1 < argc) {
                opts.delimiter = argv[++i];
            }
        } else if (arg == "--csv-no-header") {
            opts.has_header = false;
        } else if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg[0] != '-') {
            // Input file or directory
            if (batch_mode || fs::is_directory(arg)) {
                if (fs::is_directory(arg)) {
                    // Add all files in directory
                    for (const auto& entry : fs::directory_iterator(arg)) {
                        if (entry.is_regular_file()) {
                            input_files.push_back(entry.path().string());
                        }
                    }
                } else {
                    input_files.push_back(arg);
                }
            } else {
                if (input_files.empty()) {
                    input_files.push_back(arg);
                } else {
                    // Last non-option argument is output
                    if (batch_mode) {
                        output_dir = arg;
                    } else {
                        output_file = arg;
                    }
                }
            }
        }
    }
    
    if (input_files.empty()) {
        std::cerr << "Error: No input files specified" << std::endl;
        print_usage(argv[0]);
        return 1;
    }
    
    try {
        if (batch_mode) {
            // Batch conversion
            if (output_dir.empty()) {
                output_dir = ".";
            }
            convert_batch(input_files, output_dir, opts);
        } else if (opts.streaming) {
            // Streaming conversion (single file)
            if (input_files.size() != 1) {
                std::cerr << "Error: Streaming mode requires exactly one input file" << std::endl;
                return 1;
            }
            convert_streaming(input_files[0], output_file, opts);
        } else {
            // Single file conversion
            if (input_files.size() != 1) {
                std::cerr << "Error: Single mode requires exactly one input file" << std::endl;
                return 1;
            }
            
            std::string input_file = input_files[0];
            
            // Auto-detect format if needed
            if (opts.input_format == Format::AUTO) {
                opts.input_format = detect_format(input_file);
                if (opts.verbose) {
                    std::cout << "Detected input format: " 
                              << format_to_string(opts.input_format) << std::endl;
                }
            }
            
            // Read input
            Value data = read_input(input_file, opts.input_format, opts);
            
            // Validate if schema provided
            if (opts.validate && !opts.schema_file.empty()) {
                if (!validate_with_schema(data, opts.schema_file)) {
                    if (!opts.force) {
                        std::cerr << "Error: Data validation failed" << std::endl;
                        return 1;
                    } else {
                        std::cerr << "Warning: Data validation failed, continuing anyway" << std::endl;
                    }
                }
            }
            
            // Determine output file if not specified
            if (output_file.empty()) {
                std::string base = fs::path(input_file).stem().string();
                std::string ext;
                switch (opts.output_format) {
                    case Format::BTOON: ext = ".btoon"; break;
                    case Format::JSON: ext = ".json"; break;
                    case Format::YAML: ext = ".yaml"; break;
                    case Format::MSGPACK: ext = ".msgpack"; break;
                    case Format::CBOR: ext = ".cbor"; break;
                    case Format::CSV: ext = ".csv"; break;
                    case Format::XML: ext = ".xml"; break;
                    default: ext = ".out"; break;
                }
                output_file = base + ext;
            }
            
            // Write output
            write_output(output_file, data, opts.output_format, opts);
            
            if (opts.verbose) {
                std::cout << "✓ Converted " << input_file 
                          << " to " << output_file 
                          << " (" << format_to_string(opts.output_format) << ")" 
                          << std::endl;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}

void print_usage(const std::string& program_name) {
    std::cout << "BTOON Format Converter v0.0.1" << std::endl;
    std::cout << "Usage: " << program_name << " [options] <input> [output]" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -i, --input-format <fmt>   Input format (auto, btoon, json, yaml, msgpack, cbor, csv, xml)" << std::endl;
    std::cout << "  -o, --output-format <fmt>  Output format (default: btoon)" << std::endl;
    std::cout << "  -p, --pretty               Pretty print output (where applicable)" << std::endl;
    std::cout << "  -s, --schema <file>        Validate with schema" << std::endl;
    std::cout << "  -c, --compress             Compress output (BTOON only)" << std::endl;
    std::cout << "  --compression-algo <algo>  Compression algorithm (zlib, lz4, zstd)" << std::endl;
    std::cout << "  --streaming                Enable streaming mode for large files" << std::endl;
    std::cout << "  --batch                    Batch conversion mode" << std::endl;
    std::cout << "  --batch-size <n>           Batch size for streaming (default: 100)" << std::endl;
    std::cout << "  -v, --verbose              Verbose output" << std::endl;
    std::cout << "  -f, --force                Continue on validation errors" << std::endl;
    std::cout << "  --csv-delimiter <delim>    CSV delimiter (default: ,)" << std::endl;
    std::cout << "  --csv-no-header            CSV has no header row" << std::endl;
    std::cout << "  -h, --help                 Show this help message" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  " << program_name << " data.json data.btoon" << std::endl;
    std::cout << "  " << program_name << " -i json -o yaml data.json data.yaml" << std::endl;
    std::cout << "  " << program_name << " -o json -p data.btoon data.json" << std::endl;
    std::cout << "  " << program_name << " --batch -o btoon *.json output_dir/" << std::endl;
    std::cout << "  " << program_name << " -s schema.json -o btoon data.json" << std::endl;
}

Format detect_format(const std::string& filename) {
    std::string ext = fs::path(filename).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    if (ext == ".btoon" || ext == ".bto") return Format::BTOON;
    if (ext == ".json") return Format::JSON;
    if (ext == ".yaml" || ext == ".yml") return Format::YAML;
    if (ext == ".msgpack" || ext == ".mp") return Format::MSGPACK;
    if (ext == ".cbor") return Format::CBOR;
    if (ext == ".csv") return Format::CSV;
    if (ext == ".xml") return Format::XML;
    
    // Try to detect by content
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        return Format::UNKNOWN;
    }
    
    char magic[4];
    file.read(magic, 4);
    file.close();
    
    // Check for JSON
    if (magic[0] == '{' || magic[0] == '[') {
        return Format::JSON;
    }
    
    // Check for XML
    if (magic[0] == '<') {
        return Format::XML;
    }
    
    // Check for BTOON compression header
    if (magic[0] == 0x42 && magic[1] == 0x54 && magic[2] == 0x4F && magic[3] == 0x4E) {
        return Format::BTOON;
    }
    
    // Default to BTOON for binary data
    return Format::BTOON;
}

Format parse_format(const std::string& str) {
    std::string lower = str;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    
    if (lower == "auto") return Format::AUTO;
    if (lower == "btoon" || lower == "bto") return Format::BTOON;
    if (lower == "json") return Format::JSON;
    if (lower == "yaml" || lower == "yml") return Format::YAML;
    if (lower == "msgpack" || lower == "mp") return Format::MSGPACK;
    if (lower == "cbor") return Format::CBOR;
    if (lower == "csv") return Format::CSV;
    if (lower == "xml") return Format::XML;
    
    return Format::UNKNOWN;
}

std::string format_to_string(Format fmt) {
    switch (fmt) {
        case Format::AUTO: return "auto";
        case Format::BTOON: return "btoon";
        case Format::JSON: return "json";
        case Format::YAML: return "yaml";
        case Format::MSGPACK: return "msgpack";
        case Format::CBOR: return "cbor";
        case Format::CSV: return "csv";
        case Format::XML: return "xml";
        default: return "unknown";
    }
}

Value read_input(const std::string& filename, Format fmt, const ConvertOptions& opts) {
    if (fmt == Format::BTOON) {
        std::ifstream file(filename, std::ios::binary);
        if (!file) {
            throw std::runtime_error("Cannot open file: " + filename);
        }
        
        file.seekg(0, std::ios::end);
        size_t size = static_cast<size_t>(file.tellg());
        file.seekg(0, std::ios::beg);
        
        std::vector<uint8_t> buffer(size);
        file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(size));
        
        DecodeOptions decode_opts;
        decode_opts.auto_decompress = true;
        return decode(buffer, decode_opts);
    } else if (fmt == Format::JSON) {
        std::ifstream file(filename);
        if (!file) {
            throw std::runtime_error("Cannot open file: " + filename);
        }
        return read_json(file);
    } else if (fmt == Format::CSV) {
        std::ifstream file(filename);
        if (!file) {
            throw std::runtime_error("Cannot open file: " + filename);
        }
        return read_csv(file, opts);
    } else if (fmt == Format::YAML) {
        std::ifstream file(filename);
        if (!file) {
            throw std::runtime_error("Cannot open file: " + filename);
        }
        return read_yaml(file);
    } else if (fmt == Format::MSGPACK) {
        std::ifstream file(filename, std::ios::binary);
        if (!file) {
            throw std::runtime_error("Cannot open file: " + filename);
        }
        return read_msgpack(file);
    } else if (fmt == Format::CBOR) {
        std::ifstream file(filename, std::ios::binary);
        if (!file) {
            throw std::runtime_error("Cannot open file: " + filename);
        }
        return read_cbor(file);
    } else if (fmt == Format::XML) {
        std::ifstream file(filename);
        if (!file) {
            throw std::runtime_error("Cannot open file: " + filename);
        }
        return read_xml(file);
    } else {
        throw std::runtime_error("Unsupported input format");
    }
}

void write_output(const std::string& filename, const Value& data, Format fmt, const ConvertOptions& opts) {
    if (fmt == Format::BTOON) {
        EncodeOptions encode_opts;
        encode_opts.compress = opts.compress;
        encode_opts.compression_algorithm = opts.compression_algo;
        
        auto encoded = encode(data, encode_opts);
        
        std::ofstream file(filename, std::ios::binary);
        if (!file) {
            throw std::runtime_error("Cannot create file: " + filename);
        }
        
        file.write(reinterpret_cast<const char*>(encoded.data()), static_cast<std::streamsize>(encoded.size()));
    } else if (fmt == Format::JSON) {
        std::ofstream file(filename);
        if (!file) {
            throw std::runtime_error("Cannot create file: " + filename);
        }
        write_json(file, data, opts.pretty);
    } else if (fmt == Format::CSV) {
        std::ofstream file(filename);
        if (!file) {
            throw std::runtime_error("Cannot create file: " + filename);
        }
        write_csv(file, data, opts);
    } else if (fmt == Format::YAML) {
        std::ofstream file(filename);
        if (!file) {
            throw std::runtime_error("Cannot create file: " + filename);
        }
        write_yaml(file, data, opts.pretty);
    } else if (fmt == Format::MSGPACK) {
        std::ofstream file(filename, std::ios::binary);
        if (!file) {
            throw std::runtime_error("Cannot create file: " + filename);
        }
        write_msgpack(file, data);
    } else if (fmt == Format::CBOR) {
        std::ofstream file(filename, std::ios::binary);
        if (!file) {
            throw std::runtime_error("Cannot create file: " + filename);
        }
        write_cbor(file, data);
    } else if (fmt == Format::XML) {
        std::ofstream file(filename);
        if (!file) {
            throw std::runtime_error("Cannot create file: " + filename);
        }
        write_xml(file, data, opts.pretty);
    } else {
        throw std::runtime_error("Unsupported output format");
    }
}

Value read_json(std::istream& input) {
    json j;
    input >> j;
    return json_to_value(j);
}

void write_json(std::ostream& output, const Value& data, bool pretty) {
    json j = value_to_json(data);
    if (pretty) {
        output << j.dump(2);
    } else {
        output << j.dump();
    }
    output << std::endl;
}

Value read_csv(std::istream& input, const ConvertOptions& opts) {
    Array result;
    std::string line;
    std::vector<std::string> headers;
    
    // Read header if present
    if (opts.has_header && std::getline(input, line)) {
        std::stringstream ss(line);
        std::string field;
        while (std::getline(ss, field, opts.delimiter[0])) {
            // Trim whitespace
            field.erase(0, field.find_first_not_of(" \t\r\n"));
            field.erase(field.find_last_not_of(" \t\r\n") + 1);
            headers.push_back(field);
        }
    }
    
    // Read data rows
    while (std::getline(input, line)) {
        if (line.empty()) continue;
        
        Map row;
        std::stringstream ss(line);
        std::string field;
        size_t col = 0;
        
        while (std::getline(ss, field, opts.delimiter[0])) {
            // Trim whitespace
            field.erase(0, field.find_first_not_of(" \t\r\n"));
            field.erase(field.find_last_not_of(" \t\r\n") + 1);
            
            // Try to parse as number
            Value value;
            char* end;
            
            // Try integer
            long long int_val = std::strtoll(field.c_str(), &end, 10);
            if (end == field.c_str() + field.length()) {
                value = Int(int_val);
            } else {
                // Try float
                double float_val = std::strtod(field.c_str(), &end);
                if (end == field.c_str() + field.length()) {
                    value = Float(float_val);
                } else {
                    // String
                    value = String(field);
                }
            }
            
            if (!headers.empty() && col < headers.size()) {
                row[headers[col]] = value;
            } else {
                row["col" + std::to_string(col)] = value;
            }
            
            col++;
        }
        
        result.push_back(row);
    }
    
    return result;
}

void write_csv(std::ostream& output, const Value& data, const ConvertOptions& opts) {
    const Array* arr = std::get_if<Array>(&data);
    if (!arr || arr->empty()) {
        return;
    }
    
    // Get headers from first row
    const Map* first_row = std::get_if<Map>(&(*arr)[0]);
    if (!first_row) {
        throw std::runtime_error("CSV export requires array of maps");
    }
    
    std::vector<std::string> headers;
    for (const auto& [key, _] : *first_row) {
        headers.push_back(key);
    }
    
    // Write header
    if (opts.has_header) {
        for (size_t i = 0; i < headers.size(); i++) {
            if (i > 0) output << opts.delimiter;
            output << headers[i];
        }
        output << std::endl;
    }
    
    // Write data
    for (const auto& row_val : *arr) {
        const Map* row = std::get_if<Map>(&row_val);
        if (!row) continue;
        
        for (size_t i = 0; i < headers.size(); i++) {
            if (i > 0) output << opts.delimiter;
            
            auto it = row->find(headers[i]);
            if (it != row->end()) {
                std::visit([&output](auto&& arg) {
                    using T = std::decay_t<decltype(arg)>;
                    if constexpr (std::is_same_v<T, String>) {
                        // Escape quotes in strings
                        output << "\"" << arg << "\"";
                    } else if constexpr (std::is_same_v<T, Int> || 
                                       std::is_same_v<T, Uint> ||
                                       std::is_same_v<T, Float> ||
                                       std::is_same_v<T, Bool>) {
                        output << arg;
                    } else {
                        output << "";
                    }
                }, it->second);
            }
        }
        output << std::endl;
    }
}

// Simplified implementations for other formats
// In production, these would use proper libraries

Value read_yaml(std::istream& input) {
    // Simplified: treat as JSON for now
    // In production, use a proper YAML library
    return read_json(input);
}

void write_yaml(std::ostream& output, const Value& data, bool pretty) {
    // Simplified: output as JSON
    // In production, use a proper YAML library
    write_json(output, data, pretty);
}

Value read_msgpack(std::istream& input) {
    // Read entire file
    input.seekg(0, std::ios::end);
    size_t size = static_cast<size_t>(input.tellg());
    input.seekg(0, std::ios::beg);
    
    std::vector<uint8_t> buffer(size);
    input.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(size));
    
    // BTOON is MessagePack-compatible, so we can decode directly
    return decode(buffer);
}

void write_msgpack(std::ostream& output, const Value& data) {
    // BTOON is MessagePack-compatible
    auto encoded = encode(data);
    output.write(reinterpret_cast<const char*>(encoded.data()), static_cast<std::streamsize>(encoded.size()));
}

Value read_cbor(std::istream& input) {
    // Simplified: for now, throw not implemented
    throw std::runtime_error("CBOR input not yet implemented");
}

void write_cbor(std::ostream& output, const Value& data) {
    // Simplified: for now, throw not implemented
    throw std::runtime_error("CBOR output not yet implemented");
}

Value read_xml(std::istream& input) {
    // Simplified: for now, throw not implemented
    throw std::runtime_error("XML input not yet implemented");
}

void write_xml(std::ostream& output, const Value& data, bool pretty) {
    // Simplified XML output
    output << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" << std::endl;
    output << "<root>" << std::endl;
    
    std::function<void(const Value&, int)> write_value = [&](const Value& v, int indent) {
        std::string ind(static_cast<size_t>(indent * 2), ' ');
        
        std::visit([&](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, Nil>) {
                output << ind << "<null/>" << std::endl;
            } else if constexpr (std::is_same_v<T, Bool>) {
                output << ind << "<bool>" << (arg ? "true" : "false") << "</bool>" << std::endl;
            } else if constexpr (std::is_same_v<T, Int>) {
                output << ind << "<int>" << arg << "</int>" << std::endl;
            } else if constexpr (std::is_same_v<T, Uint>) {
                output << ind << "<uint>" << arg << "</uint>" << std::endl;
            } else if constexpr (std::is_same_v<T, Float>) {
                output << ind << "<float>" << arg << "</float>" << std::endl;
            } else if constexpr (std::is_same_v<T, String>) {
                output << ind << "<string>" << arg << "</string>" << std::endl;
            } else if constexpr (std::is_same_v<T, Array>) {
                output << ind << "<array>" << std::endl;
                for (const auto& item : arg) {
                    write_value(item, indent + 1);
                }
                output << ind << "</array>" << std::endl;
            } else if constexpr (std::is_same_v<T, Map>) {
                output << ind << "<map>" << std::endl;
                for (const auto& [key, value] : arg) {
                    output << ind << "  <entry key=\"" << key << "\">" << std::endl;
                    write_value(value, indent + 2);
                    output << ind << "  </entry>" << std::endl;
                }
                output << ind << "</map>" << std::endl;
            }
        }, v);
    };
    
    write_value(data, 1);
    output << "</root>" << std::endl;
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

json value_to_json(const Value& v) {
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
            for (const auto& item : arg) {
                j.push_back(value_to_json(item));
            }
        } else if constexpr (std::is_same_v<T, Map>) {
            j = json::object();
            for (const auto& [key, value] : arg) {
                j[key] = value_to_json(value);
            }
        } else if constexpr (std::is_same_v<T, Timestamp>) {
            j = arg.seconds;
        } else if constexpr (std::is_same_v<T, Extension>) {
            // Convert extension to hex string
            std::stringstream ss;
            ss << std::hex;
            for (uint8_t byte : arg.data) {
                ss << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
            }
            j = ss.str();
        }
    }, v);
    
    return j;
}

bool validate_with_schema(const Value& data, const std::string& schema_file) {
    try {
        // Load schema
        std::ifstream file(schema_file);
        if (!file) {
            std::cerr << "Cannot open schema file: " << schema_file << std::endl;
            return false;
        }
        
        json j;
        file >> j;
        Value schema_value = json_to_value(j);
        Schema schema = Schema::fromValue(schema_value);
        
        // Validate data
        if (schema.validate(data)) {
            std::cout << "✓ Data validates against schema" << std::endl;
            return true;
        } else {
            auto errors = schema.validateWithErrors(data);
            std::cerr << "✗ Validation errors:" << std::endl;
            for (const auto& error : errors) {
                std::cerr << "  - " << error << std::endl;
            }
            return false;
        }
    } catch (const std::exception& e) {
        std::cerr << "Schema validation error: " << e.what() << std::endl;
        return false;
    }
}

void convert_batch(const std::vector<std::string>& input_files, 
                  const std::string& output_dir,
                  const ConvertOptions& opts) {
    
    if (!fs::exists(output_dir)) {
        fs::create_directories(output_dir);
    }
    
    size_t converted = 0;
    size_t failed = 0;
    
    for (const auto& input_file : input_files) {
        try {
            ConvertOptions file_opts = opts;
            
            // Auto-detect format for each file
            if (file_opts.input_format == Format::AUTO) {
                file_opts.input_format = detect_format(input_file);
            }
            
            // Read input
            Value data = read_input(input_file, file_opts.input_format, file_opts);
            
            // Validate if needed
            if (file_opts.validate && !file_opts.schema_file.empty()) {
                if (!validate_with_schema(data, file_opts.schema_file)) {
                    if (!file_opts.force) {
                        std::cerr << "Skipping " << input_file << " due to validation errors" << std::endl;
                        failed++;
                        continue;
                    }
                }
            }
            
            // Generate output filename
            std::string base = fs::path(input_file).stem().string();
            std::string ext;
            switch (file_opts.output_format) {
                case Format::BTOON: ext = ".btoon"; break;
                case Format::JSON: ext = ".json"; break;
                case Format::YAML: ext = ".yaml"; break;
                case Format::MSGPACK: ext = ".msgpack"; break;
                case Format::CBOR: ext = ".cbor"; break;
                case Format::CSV: ext = ".csv"; break;
                case Format::XML: ext = ".xml"; break;
                default: ext = ".out"; break;
            }
            
            std::string output_file = fs::path(output_dir) / (base + ext);
            
            // Write output
            write_output(output_file, data, file_opts.output_format, file_opts);
            
            converted++;
            if (opts.verbose) {
                std::cout << "✓ " << input_file << " -> " << output_file << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "✗ Failed to convert " << input_file << ": " << e.what() << std::endl;
            failed++;
        }
    }
    
    std::cout << "\nBatch conversion complete:" << std::endl;
    std::cout << "  Converted: " << converted << " files" << std::endl;
    std::cout << "  Failed: " << failed << " files" << std::endl;
}

void convert_streaming(const std::string& input_file, 
                      const std::string& output_file,
                      const ConvertOptions& opts) {
    // Simplified streaming implementation
    // In production, this would process data in chunks
    std::cout << "Streaming conversion not fully implemented yet" << std::endl;
    std::cout << "Falling back to regular conversion" << std::endl;
    
    ConvertOptions non_streaming_opts = opts;
    non_streaming_opts.streaming = false;
    
    Value data = read_input(input_file, non_streaming_opts.input_format, non_streaming_opts);
    write_output(output_file, data, non_streaming_opts.output_format, non_streaming_opts);
}

/**
 * btoon-convert: convert between BTOON and other data formats.
 *
 *   Input:  BTOON, JSON, YAML (JSON-compatible YAML only), MessagePack, CSV
 *   Output: BTOON, JSON, YAML (written as JSON, which is valid YAML 1.2), MessagePack,
 *           CSV, XML
 *
 * JSON and YAML use the lossless mapping in json_mapping.h, the same one `btoon` uses,
 * so BTOON -> JSON -> BTOON is byte-identical. MessagePack output is plain MessagePack
 * (no tabular encoding). CSV follows RFC 4180; see read_csv() and write_csv().
 */

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "btoon/btoon.h"
#include "btoon/schema.h"
#include "json_mapping.h"
#include "tool_io.h"

namespace fs = std::filesystem;
using namespace btoon;
using btoon::tools::json;

namespace {

enum class Format { AUTO, BTOON, JSON, YAML, MSGPACK, CBOR, CSV, XML, UNKNOWN };

struct ConvertOptions {
    Format input_format = Format::AUTO;
    Format output_format = Format::BTOON;
    bool pretty = false;
    bool validate = false;
    std::string schema_file;
    bool compress = false;
    CompressionAlgorithm compression_algo = CompressionAlgorithm::ZLIB;
    bool verbose = false;
    bool force = false;
    char delimiter = ',';     // CSV
    bool has_header = true;   // CSV
};

/// Bad command line: exit status 2.
struct UsageError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

void print_usage(std::ostream& out, const std::string& program) {
    out << "BTOON Format Converter v" << btoon::version() << "\n"
        << "Usage: " << program << " [options] <input> [output]\n"
        << "       " << program << " --batch [options] <input>... <output-dir>\n\n"
        << "Options:\n"
        << "  -i, --input-format <fmt>   auto (default), btoon, json, yaml, msgpack, csv\n"
        << "  -o, --output-format <fmt>  btoon (default), json, yaml, msgpack, csv, xml\n"
        << "  -p, --pretty               Indent JSON and YAML output\n"
        << "  -s, --schema <file>        Validate the data against a schema (JSON) first\n"
        << "  -f, --force                Convert even if the data doesn't match the schema\n"
        << "  -c, --compress             Compress BTOON output\n"
        << "  --compression-algo <algo>  zlib (default), lz4, zstd, brotli, snappy\n"
        << "  --batch                    Convert several files (or directories of files);\n"
        << "                             the last argument is the output directory\n"
        << "  --batch-size <n>           Accepted for compatibility; no effect\n"
        << "  --streaming                Accepted for compatibility; no effect\n"
        << "  --csv-delimiter <c>        CSV field delimiter (default ','; 'tab' or '\\t' for a tab)\n"
        << "  --csv-no-header            CSV without a header row (columns are col0, col1, ...)\n"
        << "  -v, --verbose              Report progress on standard error\n"
        << "  -h, --help                 Show this help\n\n"
        << "'-' means standard input or output. Without an output name, the output is written\n"
        << "to <input name>.<format> in the current directory.\n\n"
        << "JSON and YAML use the lossless mapping of `btoon` (see btoon(1)). YAML input must\n"
        << "be JSON-compatible; YAML output is JSON. MessagePack output has no tabular encoding.\n"
        << "CSV follows RFC 4180. On input, quoted fields are strings; unquoted fields that are\n"
        << "JSON numbers, true or false become numbers and booleans; an empty unquoted field is\n"
        << "null; anything else is a string. On output, strings are always quoted, lines end\n"
        << "in CRLF, and values CSV can't hold (arrays, maps, binary, timestamps, ...) are\n"
        << "written as their JSON text.\n\n"
        << "Exit status: 0 on success, 1 on a conversion, validation or I/O error, 2 on a\n"
        << "usage error.\n\n"
        << "Examples:\n"
        << "  " << program << " data.json data.btoon\n"
        << "  " << program << " -o json -p data.btoon data.json\n"
        << "  " << program << " --batch -o btoon in/*.json out/\n"
        << "  " << program << " -s schema.json -o btoon data.json\n";
}

Format parse_format(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (s == "auto") return Format::AUTO;
    if (s == "btoon" || s == "bto") return Format::BTOON;
    if (s == "json") return Format::JSON;
    if (s == "yaml" || s == "yml") return Format::YAML;
    if (s == "msgpack" || s == "mp") return Format::MSGPACK;
    if (s == "cbor") return Format::CBOR;
    if (s == "csv") return Format::CSV;
    if (s == "xml") return Format::XML;
    return Format::UNKNOWN;
}

std::string format_name(Format fmt) {
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

std::string format_extension(Format fmt) {
    switch (fmt) {
        case Format::BTOON: return ".btoon";
        case Format::JSON: return ".json";
        case Format::YAML: return ".yaml";
        case Format::MSGPACK: return ".msgpack";
        case Format::CBOR: return ".cbor";
        case Format::CSV: return ".csv";
        case Format::XML: return ".xml";
        default: return ".out";
    }
}

Format detect_format(const std::string& filename, std::string_view content) {
    std::string ext = filename == "-" ? "" : fs::path(filename).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext == ".btoon" || ext == ".bto") return Format::BTOON;
    if (ext == ".json") return Format::JSON;
    if (ext == ".yaml" || ext == ".yml") return Format::YAML;
    if (ext == ".msgpack" || ext == ".mp") return Format::MSGPACK;
    if (ext == ".cbor") return Format::CBOR;
    if (ext == ".csv") return Format::CSV;
    if (ext == ".xml") return Format::XML;

    // Sniff the content: text that starts with { or [ is JSON, < is XML.
    size_t i = content.substr(0, 3) == "\xEF\xBB\xBF" ? 3 : 0;
    while (i < content.size() && (content[i] == ' ' || content[i] == '\t' || content[i] == '\r' || content[i] == '\n')) ++i;
    if (i < content.size() && (content[i] == '{' || content[i] == '[')) return Format::JSON;
    if (i < content.size() && content[i] == '<') return Format::XML;
    return Format::BTOON;  // binary; BTOON and MessagePack decode the same way
}

size_t parse_count(const std::string& option, const std::string& text) {
    size_t v = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), v);
    if (text.empty() || ec != std::errc() || end != text.data() + text.size() || v == 0) {
        throw UsageError(option + " needs a positive integer, not '" + text + "'");
    }
    return v;
}

char parse_delimiter(const std::string& text) {
    if (text == "tab" || text == "\\t") return '\t';
    const bool ok = text.size() == 1 && static_cast<unsigned char>(text[0]) < 0x80 &&
                    !std::isalnum(static_cast<unsigned char>(text[0])) &&
                    std::string_view("\".+-\r\n").find(text[0]) == std::string_view::npos;
    // Letters, digits and . + - appear in unquoted numbers and booleans, so they can't delimit.
    if (!ok) throw UsageError("--csv-delimiter needs one ASCII character other than a letter, digit, quote, '.', '+', '-' or line break");
    return text[0];
}

CompressionAlgorithm parse_algorithm(const std::string& name) {
    if (name == "zlib") return CompressionAlgorithm::ZLIB;
    if (name == "lz4") return CompressionAlgorithm::LZ4;
    if (name == "zstd") return CompressionAlgorithm::ZSTD;
    if (name == "brotli") return CompressionAlgorithm::BROTLI;
    if (name == "snappy") return CompressionAlgorithm::SNAPPY;
    throw UsageError("unknown compression algorithm: " + name);
}

std::span<const uint8_t> bytes_of(std::string_view s) { return {reinterpret_cast<const uint8_t*>(s.data()), s.size()}; }

/// Shortest text that reads back as the same double; NaN/Infinity/-Infinity otherwise.
std::string format_number(double d) {
    if (std::isnan(d)) return "NaN";
    if (std::isinf(d)) return d > 0 ? "Infinity" : "-Infinity";
    return json(d).dump();
}

// --- CSV (RFC 4180) ------------------------------------------------------------------

struct CsvField {
    std::string text;
    bool quoted = false;
};

struct CsvRecord {
    std::vector<CsvField> fields;
    size_t line = 0;
};

/// Splits CSV text into records. Quoted fields may contain the delimiter, line breaks
/// and doubled quotes. Lines end in CRLF, LF or CR. Blank lines are skipped.
std::vector<CsvRecord> parse_csv(std::string_view in, char delim) {
    std::vector<CsvRecord> records;
    size_t i = in.substr(0, 3) == "\xEF\xBB\xBF" ? 3 : 0;  // UTF-8 byte order mark
    size_t line = 1;
    if (i >= in.size()) return records;
    CsvRecord record{{}, line};
    auto at_break = [&](size_t k) { return k < in.size() && (in[k] == '\r' || in[k] == '\n'); };
    while (true) {
        CsvField field;
        if (i < in.size() && in[i] == '"') {
            field.quoted = true;
            const size_t first_line = line;
            ++i;
            while (true) {
                if (i >= in.size()) throw std::runtime_error("CSV line " + std::to_string(first_line) + ": unterminated quoted field");
                const char c = in[i++];
                if (c == '"') {
                    if (i < in.size() && in[i] == '"') {
                        field.text += '"';
                        ++i;
                        continue;
                    }
                    break;
                }
                if (c == '\n') ++line;
                field.text += c;
            }
            if (i < in.size() && in[i] != delim && !at_break(i)) {
                throw std::runtime_error("CSV line " + std::to_string(line) + ": unexpected text after a closing quote");
            }
        } else {
            const size_t start = i;
            while (i < in.size() && in[i] != delim && !at_break(i)) ++i;
            field.text.assign(in.substr(start, i - start));
        }
        record.fields.push_back(std::move(field));
        if (i < in.size() && in[i] == delim) {
            ++i;
            continue;
        }
        const bool blank = record.fields.size() == 1 && !record.fields[0].quoted && record.fields[0].text.empty();
        if (!blank) records.push_back(std::move(record));
        if (i >= in.size()) break;
        if (in[i] == '\r' && i + 1 < in.size() && in[i + 1] == '\n') ++i;
        ++i;
        ++line;
        if (i >= in.size()) break;
        record = CsvRecord{{}, line};
    }
    return records;
}

/// Strict JSON number syntax: -?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][+-]?[0-9]+)?
bool is_json_number(std::string_view s) {
    size_t i = 0;
    auto digits = [&]() {
        const size_t start = i;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
        return i > start;
    };
    if (i < s.size() && s[i] == '-') ++i;
    if (i < s.size() && s[i] == '0') {
        ++i;
    } else if (!(i < s.size() && s[i] >= '1' && s[i] <= '9') || !digits()) {
        return false;
    }
    if (i < s.size() && s[i] == '.') {
        ++i;
        if (!digits()) return false;
    }
    if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
        ++i;
        if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
        if (!digits()) return false;
    }
    return i == s.size();
}

Value csv_value(const CsvField& field) {
    if (field.quoted) return field.text;
    if (field.text.empty()) return nullptr;
    if (field.text == "true") return true;
    if (field.text == "false") return false;
    if (is_json_number(field.text)) {
        try {
            return tools::from_json(tools::parse_json(field.text));  // integers beyond 64 bits become bigints
        } catch (const std::exception&) {
            // Out of the double range (1e400): keep the text.
        }
    }
    return field.text;
}

Value read_csv(std::string_view text, const ConvertOptions& opts) {
    const auto records = parse_csv(text, opts.delimiter);
    Array rows;
    std::vector<std::string> header;
    size_t first = 0;
    if (opts.has_header) {
        if (records.empty()) return rows;
        std::set<std::string> seen;
        for (const auto& f : records[0].fields) {
            if (!seen.insert(f.text).second) throw std::runtime_error("CSV header repeats the column name '" + f.text + "'");
            header.push_back(f.text);
        }
        first = 1;
    }
    rows.reserve(records.size() - first);
    for (size_t r = first; r < records.size(); ++r) {
        const auto& record = records[r];
        if (opts.has_header && record.fields.size() != header.size()) {
            throw std::runtime_error("CSV line " + std::to_string(record.line) + " has " +
                                     std::to_string(record.fields.size()) + " fields; the header has " +
                                     std::to_string(header.size()));
        }
        Map row;
        for (size_t c = 0; c < record.fields.size(); ++c) {
            row.emplace(opts.has_header ? header[c] : "col" + std::to_string(c), csv_value(record.fields[c]));
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

std::string csv_quote(std::string_view s) {
    std::string out = "\"";
    for (char c : s) out += c == '"' ? std::string("\"\"") : std::string(1, c);
    return out + "\"";
}

std::string csv_cell(const Value& v) {
    switch (v.index()) {
        case 0: return "";
        case 1: return std::get<Bool>(v) ? "true" : "false";
        case 2: return std::to_string(std::get<Int>(v));
        case 3: return std::to_string(std::get<Uint>(v));
        case 4: return format_number(std::get<Float>(v));
        case 5: return csv_quote(std::get<String>(v));
        case 13: {  // decimal bigints stay numbers (they read back as bigints)
            std::string s = tools::bigint_to_string(std::get<BigInt>(v));
            if (s.find('x') == std::string::npos) return s;
            break;
        }
        default: break;
    }
    return csv_quote(tools::to_json(v).dump());
}

/// An array of maps becomes one row per map. The columns are the union of all keys,
/// in key order; a missing key is an empty (null) field.
std::string write_csv(const Value& data, const ConvertOptions& opts) {
    const Array* rows = std::get_if<Array>(&data);
    if (!rows) throw std::runtime_error(std::string("CSV output needs an array of maps, not a ") + data.type_name());
    std::set<std::string> keys;
    for (size_t r = 0; r < rows->size(); ++r) {
        const Map* row = std::get_if<Map>(&(*rows)[r]);
        if (!row) {
            throw std::runtime_error("CSV output needs an array of maps; element " + std::to_string(r) + " is a " +
                                     (*rows)[r].type_name());
        }
        for (const auto& [k, _] : *row) keys.insert(k);
    }
    const std::vector<std::string> header(keys.begin(), keys.end());
    const char d = opts.delimiter;
    std::string out;
    if (opts.has_header && !header.empty()) {
        for (size_t i = 0; i < header.size(); ++i) {
            if (i) out += d;
            const std::string& h = header[i];
            const bool quote = h.empty() || h.find_first_of(std::string{d, '"', '\r', '\n'}) != std::string::npos;
            out += quote ? csv_quote(h) : h;
        }
        out += "\r\n";
    }
    for (const auto& row_value : *rows) {
        const Map& row = std::get<Map>(row_value);
        for (size_t i = 0; i < header.size(); ++i) {
            if (i) out += d;
            const auto it = row.find(header[i]);
            if (it != row.end()) out += csv_cell(it->second);
        }
        out += "\r\n";
    }
    return out;
}

// --- XML (output only) -----------------------------------------------------------------

/// Appends `s` escaped for XML text or an attribute value. Fails on invalid UTF-8 and on
/// characters XML 1.0 can't contain at all (most C0 controls, U+FFFE, U+FFFF).
void xml_escape(std::string& out, std::string_view s, bool attribute) {
    for (size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        size_t len = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
        if (len == 0 || i + len > s.size()) throw std::runtime_error("XML output: string isn't valid UTF-8");
        uint32_t cp = len == 1 ? c : len == 2 ? (c & 0x1Fu) : len == 3 ? (c & 0x0Fu) : (c & 0x07u);
        for (size_t k = 1; k < len; ++k) {
            const auto cc = static_cast<unsigned char>(s[i + k]);
            if ((cc & 0xC0) != 0x80) throw std::runtime_error("XML output: string isn't valid UTF-8");
            cp = (cp << 6) | (cc & 0x3Fu);
        }
        if ((len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) || (len == 4 && (cp < 0x10000 || cp > 0x10FFFF)) ||
            (cp >= 0xD800 && cp <= 0xDFFF)) {
            throw std::runtime_error("XML output: string isn't valid UTF-8");
        }
        const bool allowed = cp == 0x9 || cp == 0xA || cp == 0xD || (cp >= 0x20 && cp <= 0xD7FF) ||
                             (cp >= 0xE000 && cp <= 0xFFFD) || cp >= 0x10000;
        if (!allowed) {
            char buf[16];
            std::snprintf(buf, sizeof buf, "U+%04X", static_cast<unsigned>(cp));
            throw std::runtime_error(std::string("XML 1.0 can't represent the character ") + buf);
        }
        switch (cp) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += attribute ? "&quot;" : "\""; break;
            case '\'': out += attribute ? "&apos;" : "'"; break;
            case '\r': out += "&#13;"; break;  // parsers would turn a literal CR into LF
            case '\n': out += attribute ? "&#10;" : "\n"; break;
            case '\t': out += attribute ? "&#9;" : "\t"; break;
            default: out.append(s.substr(i, len));
        }
        i += len;
    }
}

std::string write_xml(const Value& data) {
    std::string out = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<root>\n";
    std::function<void(const Value&, size_t)> emit = [&](const Value& v, size_t indent) {
        const std::string ind(indent * 2, ' ');
        auto element = [&](const std::string& tag, const std::string& text) {
            out += ind + "<" + tag + ">" + text + "</" + tag + ">\n";
        };
        switch (v.index()) {
            case 0: out += ind + "<null/>\n"; break;
            case 1: element("bool", std::get<Bool>(v) ? "true" : "false"); break;
            case 2: element("int", std::to_string(std::get<Int>(v))); break;
            case 3: element("uint", std::to_string(std::get<Uint>(v))); break;
            case 4: element("float", format_number(std::get<Float>(v))); break;
            case 5: {
                std::string text;
                xml_escape(text, std::get<String>(v), false);
                element("string", text);
                break;
            }
            case 6: element("binary", tools::base64_encode(std::get<Binary>(v))); break;
            case 7:
                out += ind + "<array>\n";
                for (const auto& item : std::get<Array>(v)) emit(item, indent + 1);
                out += ind + "</array>\n";
                break;
            case 8:
                out += ind + "<map>\n";
                for (const auto& [key, value] : std::get<Map>(v)) {
                    std::string k;
                    xml_escape(k, key, true);
                    out += ind + "  <entry key=\"" + k + "\">\n";
                    emit(value, indent + 2);
                    out += ind + "  </entry>\n";
                }
                out += ind + "</map>\n";
                break;
            case 9: {
                const auto& e = std::get<Extension>(v);
                out += ind + "<ext type=\"" + std::to_string(int(e.type)) + "\">" + tools::base64_encode(e.data) + "</ext>\n";
                break;
            }
            case 10: element("timestamp", tools::format_rfc3339(std::get<Timestamp>(v))); break;
            case 11: element("date", std::to_string(std::get<Date>(v).milliseconds)); break;
            case 12: element("datetime", std::to_string(std::get<DateTime>(v).nanoseconds)); break;
            case 13: element("bigint", tools::bigint_to_string(std::get<BigInt>(v))); break;
            case 14:
            case 15: {
                // Same decision as the JSON mapping: base64 when NaN payloads must survive.
                const json j = tools::to_json(v);
                const std::string tag = v.index() == 14 ? "f32" : "f64";
                const json& body = j.at("$" + tag);
                if (body.is_string()) {
                    out += ind + "<" + tag + " encoding=\"base64\">" + body.get<std::string>() + "</" + tag + ">\n";
                    break;
                }
                out += ind + "<" + tag + ">\n";
                for (const auto& e : body) {
                    out += ind + "  <float>" + (e.is_object() ? e.at("$float").get<std::string>() : format_number(e.get<double>())) + "</float>\n";
                }
                out += ind + "</" + tag + ">\n";
                break;
            }
            default: throw std::runtime_error("XML output: unknown value type");
        }
    };
    emit(data, 1);
    out += "</root>\n";
    return out;
}

// --- reading and writing ---------------------------------------------------------------

Value read_input(const std::string& content, Format fmt, const ConvertOptions& opts) {
    switch (fmt) {
        case Format::BTOON:
        case Format::MSGPACK:  // BTOON is a superset of MessagePack
            return decode(bytes_of(content));
        case Format::JSON: return tools::from_json(tools::parse_json(content));
        case Format::YAML: {
            json j;
            try {
                j = tools::parse_json(content);
            } catch (const BtoonException&) {
                throw;
            } catch (const std::exception& e) {
                throw std::runtime_error(std::string("YAML input must be JSON-compatible (no YAML parser is bundled): ") + e.what());
            }
            return tools::from_json(j);
        }
        case Format::CSV: return read_csv(content, opts);
        case Format::CBOR: throw std::runtime_error("CBOR input isn't supported");
        case Format::XML: throw std::runtime_error("XML input isn't supported");
        default: throw std::runtime_error("unknown input format");
    }
}

std::string render_output(const Value& data, Format fmt, const ConvertOptions& opts) {
    switch (fmt) {
        case Format::BTOON:
        case Format::MSGPACK: {
            EncodeOptions eo;
            if (fmt == Format::MSGPACK) {
                eo.auto_tabular = false;  // plain MessagePack
            } else {
                eo.compress = opts.compress;
                eo.compression_algorithm = opts.compression_algo;
            }
            const auto bytes = encode(data, eo);
            return std::string(bytes.begin(), bytes.end());
        }
        case Format::JSON:
        case Format::YAML: return tools::to_json(data).dump(opts.pretty ? 2 : -1) + "\n";
        case Format::CSV: return write_csv(data, opts);
        case Format::XML: return write_xml(data);
        case Format::CBOR: throw std::runtime_error("CBOR output isn't supported");
        default: throw std::runtime_error("unknown output format");
    }
}

bool validate_with_schema(const Value& data, const std::string& schema_file) {
    const Value schema_value = tools::from_json(tools::parse_json(tools::read_file(schema_file)));
    Schema schema = Schema::fromValue(schema_value);
    if (schema.validate(data)) {
        std::cerr << "✓ Data validates against schema\n";
        return true;
    }
    std::cerr << "✗ Validation errors:\n";
    for (const auto& error : schema.validateWithErrors(data)) std::cerr << "  - " << error << "\n";
    return false;
}

/// Reads, validates and converts one file. Returns false if validation failed (and
/// --force wasn't given).
bool convert_file(const std::string& input, const std::string& output, ConvertOptions opts) {
    const std::string content = tools::read_file(input);
    if (opts.input_format == Format::AUTO) {
        opts.input_format = detect_format(input, content);
        if (opts.verbose) std::cerr << "Detected input format: " << format_name(opts.input_format) << "\n";
    }
    const Value data = read_input(content, opts.input_format, opts);
    if (opts.validate && !validate_with_schema(data, opts.schema_file)) {
        if (!opts.force) return false;
        std::cerr << "Warning: data validation failed, continuing anyway (--force)\n";
    }
    tools::write_file(output, render_output(data, opts.output_format, opts));
    if (opts.verbose) std::cerr << "✓ Converted " << input << " to " << output << " (" << format_name(opts.output_format) << ")\n";
    return true;
}

int run_single(const std::vector<std::string>& positional, const ConvertOptions& opts) {
    if (positional.empty()) throw UsageError("no input file");
    if (positional.size() > 2) throw UsageError("too many arguments (use --batch to convert several files)");
    const std::string& input = positional[0];
    if (input != "-" && fs::is_directory(input)) throw UsageError(input + " is a directory (use --batch)");
    std::string output;
    if (positional.size() == 2) {
        output = positional[1];
    } else if (input == "-") {
        output = "-";
    } else {
        output = fs::path(input).stem().string() + format_extension(opts.output_format);
    }
    if (!convert_file(input, output, opts)) {
        std::cerr << "btoon-convert: data validation failed\n";
        return 1;
    }
    return 0;
}

int run_batch(const std::vector<std::string>& positional, const ConvertOptions& opts) {
    if (positional.size() < 2) throw UsageError("--batch needs input files and an output directory (the last argument)");
    const fs::path output_dir = positional.back();
    if (fs::exists(output_dir) && !fs::is_directory(output_dir)) {
        throw UsageError("the output directory " + output_dir.string() + " is not a directory");
    }
    std::vector<std::string> inputs;
    for (size_t k = 0; k + 1 < positional.size(); ++k) {
        const std::string& p = positional[k];
        if (p == "-") throw UsageError("--batch reads files, not standard input");
        if (fs::is_directory(p)) {
            std::vector<std::string> files;
            for (const auto& entry : fs::directory_iterator(p)) {
                if (entry.is_regular_file()) files.push_back(entry.path().string());
            }
            std::sort(files.begin(), files.end());
            inputs.insert(inputs.end(), files.begin(), files.end());
        } else {
            inputs.push_back(p);
        }
    }
    if (inputs.empty()) throw UsageError("no input files");

    // Name every output first, so that two inputs can't overwrite each other's output.
    std::map<std::string, std::string> claimed;  // output -> input
    std::vector<std::pair<std::string, std::string>> plan;
    for (const auto& in : inputs) {
        const std::string out = (output_dir / (fs::path(in).stem().string() + format_extension(opts.output_format))).string();
        const auto [it, fresh] = claimed.emplace(out, in);
        if (!fresh) throw UsageError(it->second + " and " + in + " would both be written to " + out);
        plan.emplace_back(in, out);
    }
    fs::create_directories(output_dir);

    size_t converted = 0, failed = 0;
    for (const auto& [in, out] : plan) {
        try {
            if (convert_file(in, out, opts)) {
                ++converted;
            } else {
                std::cerr << "Skipping " << in << " due to validation errors\n";
                ++failed;
            }
        } catch (const std::exception& e) {
            std::cerr << "✗ Failed to convert " << in << ": " << e.what() << "\n";
            ++failed;
        }
    }
    std::cout << "Batch conversion complete:\n"
              << "  Converted: " << converted << " files\n"
              << "  Failed: " << failed << " files\n";
    return failed ? 1 : 0;
}

int run(int argc, char* argv[]) {
    const std::string program = argc > 0 ? argv[0] : "btoon-convert";
    if (argc < 2) {
        print_usage(std::cerr, program);
        return 2;
    }
    ConvertOptions opts;
    std::vector<std::string> positional;
    bool batch_mode = false;
    for (int i = 1; i < argc; i++) {
        const std::string arg = argv[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= argc) throw UsageError(arg + " needs a value");
            return argv[++i];
        };
        if (arg == "-i" || arg == "--input-format") {
            const std::string v = value();
            opts.input_format = parse_format(v);
            if (opts.input_format == Format::UNKNOWN) throw UsageError("unknown input format: " + v);
        } else if (arg == "-o" || arg == "--output-format") {
            const std::string v = value();
            opts.output_format = parse_format(v);
            if (opts.output_format == Format::UNKNOWN || opts.output_format == Format::AUTO) {
                throw UsageError("unknown output format: " + v);
            }
        } else if (arg == "-p" || arg == "--pretty") {
            opts.pretty = true;
        } else if (arg == "-s" || arg == "--schema") {
            opts.schema_file = value();
            opts.validate = true;
        } else if (arg == "-c" || arg == "--compress") {
            opts.compress = true;
        } else if (arg == "--compression-algo") {
            opts.compression_algo = parse_algorithm(value());
        } else if (arg == "--streaming") {
            // Accepted for compatibility: files are converted in memory.
        } else if (arg == "--batch") {
            batch_mode = true;
        } else if (arg == "--batch-size") {
            parse_count(arg, value());  // accepted for compatibility; unused
        } else if (arg == "-v" || arg == "--verbose") {
            opts.verbose = true;
        } else if (arg == "-f" || arg == "--force") {
            opts.force = true;
        } else if (arg == "--csv-delimiter") {
            opts.delimiter = parse_delimiter(value());
        } else if (arg == "--csv-no-header") {
            opts.has_header = false;
        } else if (arg == "-h" || arg == "--help") {
            print_usage(std::cout, program);
            return 0;
        } else if (arg.size() > 1 && arg[0] == '-') {
            throw UsageError("unknown option: " + arg);
        } else {
            positional.push_back(arg);
        }
    }
    return batch_mode ? run_batch(positional, opts) : run_single(positional, opts);
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        const int rc = run(argc, argv);
        if (!tools::flush_stdout()) {
            std::cerr << "btoon-convert: error writing to standard output\n";
            return 1;
        }
        return rc;
    } catch (const UsageError& e) {
        std::cerr << "btoon-convert: " << e.what() << "\n(run 'btoon-convert --help' for usage)\n";
        return 2;
    } catch (const BtoonException& e) {
        std::cerr << "btoon-convert: " << e.code_name() << ": " << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "btoon-convert: " << e.what() << "\n";
        return 1;
    }
}

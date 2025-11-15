#include "btoon/btoon.h"
#include "nlohmann/json.hpp"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>

using namespace btoon;
using json = nlohmann::basic_json<std::map>;

// --- Conversion Functions ---

Value json_to_btoon(const json& j);

Map json_object_to_btoon_map(const json& j) {
    Map map;
    for (auto& [key, val] : j.items()) {
        map[key] = json_to_btoon(val);
    }
    return map;
}

Array json_array_to_btoon_array(const json& j) {
    Array arr;
    for (auto& item : j) {
        arr.push_back(json_to_btoon(item));
    }
    return arr;
}

Value json_to_btoon(const json& j) {
    if (j.is_null()) return Nil();
    if (j.is_boolean()) return Bool(j.get<bool>());
    if (j.is_number_integer()) return Int(j.get<int64_t>());
    if (j.is_number_unsigned()) return Uint(j.get<uint64_t>());
    if (j.is_number_float()) return Float(j.get<double>());
    if (j.is_string()) return String(j.get<std::string>());
    if (j.is_binary()) return Binary(j.get<std::vector<uint8_t>>());
    if (j.is_array()) return json_array_to_btoon_array(j);
    if (j.is_object()) return json_object_to_btoon_map(j);
    return Nil();
}

json btoon_to_json(const Value& v) {
    return std::visit([](auto&& arg) -> json {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, Nil>) return nullptr;
        if constexpr (std::is_same_v<T, Bool>) return arg;
        if constexpr (std::is_same_v<T, Int>) return arg;
        if constexpr (std::is_same_v<T, Uint>) return arg;
        if constexpr (std::is_same_v<T, Float>) return arg;
        if constexpr (std::is_same_v<T, String>) return arg;
        if constexpr (std::is_same_v<T, Binary>) return arg;
        if constexpr (std::is_same_v<T, Array>) {
            json j = json::array();
            for (const auto& item : arg) {
                j.push_back(btoon_to_json(item));
            }
            return j;
        }
        if constexpr (std::is_same_v<T, Map>) {
            json j = json::object();
            for (const auto& [key, val] : arg) {
                j[key] = btoon_to_json(val);
            }
            return j;
        }
        return nullptr;
    }, v);
}

// --- Main CLI Logic ---

void printUsage(const char* name) {
    std::cerr << "Usage: btoon [command] <input> <output> [options]" << std::endl;
    std::cerr << "Commands:" << std::endl;
    std::cerr << "  encode <input> <output>    Encode JSON to BTOON" << std::endl;
    std::cerr << "  decode <input> <output>    Decode BTOON to JSON" << std::endl;
    std::cerr << "Options:" << std::endl;
    std::cerr << "  --compress    Enable compression (default: zlib)" << std::endl;
    std::cerr << "  --version     Print version information" << std::endl;
    std::cerr << "  --help        Print this usage information" << std::endl;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    std::string command = argv[1];

    if (command == "--version" || command == "-v") {
        std::cout << "btoon version " << btoon::version() << std::endl;
        return 0;
    }

    if (command == "--help" || command == "-h") {
        printUsage(argv[0]);
        return 0;
    }

    if (argc < 4) {
        printUsage(argv[0]);
        return 1;
    }

    std::string input_file = argv[2];
    std::string output_file = argv[3];
    bool compress = (argc > 4 && std::string(argv[4]) == "--compress");

    try {
        if (command == "encode") {
            std::ifstream ifs(input_file);
            json j = json::parse(ifs);
            Value v = json_to_btoon(j);
            
            EncodeOptions opts;
            opts.compress = compress;
            
            auto encoded = encode(v, opts);
            
            std::ofstream ofs(output_file, std::ios::binary);
            ofs.write((const char*)encoded.data(), static_cast<std::streamsize>(encoded.size()));
            
            std::cout << "Encoded " << input_file << " to " << output_file << std::endl;

        } else if (command == "decode") {
            std::ifstream ifs(input_file, std::ios::binary);
            std::vector<uint8_t> buffer((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
            
            DecodeOptions opts;
            opts.auto_decompress = compress; // Use same flag for simplicity
            
            Value v = decode(buffer, opts);
            json j = btoon_to_json(v);
            
            std::ofstream ofs(output_file);
            ofs << j.dump(2);

            std::cout << "Decoded " << input_file << " to " << output_file << std::endl;
        } else {
            printUsage(argv[0]);
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}


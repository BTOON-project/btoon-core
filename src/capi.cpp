#include "btoon/capi.h"
#include "btoon/btoon.h"
#include <vector>
#include <string>
#include <cstring>

// --- Helper Functions ---

// Convert C++ btoon::Value to C btoon_value_t
// NOTE: This is a deep copy. The caller is responsible for freeing the memory
// using btoon_value_destroy.
static btoon_value_t* to_c_value(const btoon::Value& cpp_value) {
    btoon_value_t* c_value = new btoon_value_t();
    
    std::visit([&](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, btoon::Nil>) {
            c_value->type = BTOON_TYPE_NIL;
        } else if constexpr (std::is_same_v<T, btoon::Bool>) {
            c_value->type = BTOON_TYPE_BOOL;
            c_value->as.b = arg;
        } else if constexpr (std::is_same_v<T, btoon::Int>) {
            c_value->type = BTOON_TYPE_INT;
            c_value->as.i = arg;
        } else if constexpr (std::is_same_v<T, btoon::Uint>) {
            c_value->type = BTOON_TYPE_UINT;
            c_value->as.u = arg;
        } else if constexpr (std::is_same_v<T, btoon::Float>) {
            c_value->type = BTOON_TYPE_FLOAT;
            c_value->as.f = arg;
        } else if constexpr (std::is_same_v<T, btoon::String>) {
            c_value->type = BTOON_TYPE_STRING;
            char* str = new char[arg.length() + 1];
            std::memcpy(str, arg.c_str(), arg.length());
            str[arg.length()] = '\0';
            c_value->as.s.ptr = str;
            c_value->as.s.len = arg.length();
        } else if constexpr (std::is_same_v<T, btoon::Binary>) {
            c_value->type = BTOON_TYPE_BINARY;
            uint8_t* bin_data = new uint8_t[arg.size()];
            std::memcpy(bin_data, arg.data(), arg.size());
            c_value->as.bin.ptr = bin_data;
            c_value->as.bin.len = arg.size();
        } else if constexpr (std::is_same_v<T, btoon::Array>) {
            c_value->type = BTOON_TYPE_ARRAY;
            c_value->as.a.len = arg.size();
            c_value->as.a.elements = new btoon_value_t*[arg.size()];
            for (size_t i = 0; i < arg.size(); ++i) {
                c_value->as.a.elements[i] = to_c_value(arg[i]);
            }
        } else if constexpr (std::is_same_v<T, btoon::Map>) {
            c_value->type = BTOON_TYPE_MAP;
            c_value->as.m.len = arg.size();
            c_value->as.m.keys = new btoon_string_t[arg.size()];
            c_value->as.m.values = new btoon_value_t*[arg.size()];
            size_t i = 0;
            for (const auto& [key, val] : arg) {
                char* str = new char[key.length() + 1];
                std::memcpy(str, key.c_str(), key.length());
                str[key.length()] = '\0';
                c_value->as.m.keys[i] = {str, key.length()};
                c_value->as.m.values[i] = to_c_value(val);
                i++;
            }
        } else if constexpr (std::is_same_v<T, btoon::Extension>) {
            c_value->type = BTOON_TYPE_EXTENSION;
            c_value->as.ext.type = arg.type;
            uint8_t* ext_data = new uint8_t[arg.data.size()];
            std::memcpy(ext_data, arg.data.data(), arg.data.size());
            c_value->as.ext.ptr = ext_data;
            c_value->as.ext.len = arg.data.size();
        } else if constexpr (std::is_same_v<T, btoon::Timestamp>) {
            c_value->type = BTOON_TYPE_TIMESTAMP;
            c_value->as.ts.seconds = arg.seconds;
        } else if constexpr (std::is_same_v<T, btoon::Date>) {
            c_value->type = BTOON_TYPE_DATE;
            c_value->as.date.milliseconds = arg.milliseconds;
        } else if constexpr (std::is_same_v<T, btoon::DateTime>) {
            c_value->type = BTOON_TYPE_DATETIME;
            c_value->as.dt.nanoseconds = arg.nanoseconds;
        } else if constexpr (std::is_same_v<T, btoon::BigInt>) {
            c_value->type = BTOON_TYPE_BIGINT;
            uint8_t* bi_bytes = new uint8_t[arg.bytes.size()];
            std::memcpy(bi_bytes, arg.bytes.data(), arg.bytes.size());
            c_value->as.bi.ptr = bi_bytes;
            c_value->as.bi.len = arg.bytes.size();
        } else if constexpr (std::is_same_v<T, btoon::VectorFloat>) {
            c_value->type = BTOON_TYPE_VECTOR_FLOAT;
            c_value->as.vf.len = arg.data.size();
            float* vf_data = new float[arg.data.size()];
            std::memcpy(vf_data, arg.data.data(), arg.data.size() * sizeof(float));
            c_value->as.vf.ptr = vf_data;
        } else if constexpr (std::is_same_v<T, btoon::VectorDouble>) {
            c_value->type = BTOON_TYPE_VECTOR_DOUBLE;
            c_value->as.vd.len = arg.data.size();
            double* vd_data = new double[arg.data.size()];
            std::memcpy(vd_data, arg.data.data(), arg.data.size() * sizeof(double));
            c_value->as.vd.ptr = vd_data;
        } else {
            c_value->type = BTOON_TYPE_NIL; // Fallback for unsupported types
        }
    }, cpp_value);

    return c_value;
}

static btoon::Value to_cpp_value(const btoon_value_t* c_value) {
    if (!c_value) {
        return btoon::Nil();
    }
    switch (c_value->type) {
        case BTOON_TYPE_NIL:
            return btoon::Nil();
        case BTOON_TYPE_BOOL:
            return c_value->as.b;
        case BTOON_TYPE_INT:
            return c_value->as.i;
        case BTOON_TYPE_UINT:
            return c_value->as.u;
        case BTOON_TYPE_FLOAT:
            return c_value->as.f;
        case BTOON_TYPE_STRING:
            return std::string(c_value->as.s.ptr, c_value->as.s.len);
        case BTOON_TYPE_BINARY:
            return btoon::Binary(c_value->as.bin.ptr, c_value->as.bin.ptr + c_value->as.bin.len);
        case BTOON_TYPE_ARRAY: {
            btoon::Array arr;
            arr.reserve(c_value->as.a.len);
            for (size_t i = 0; i < c_value->as.a.len; ++i) {
                arr.push_back(to_cpp_value(c_value->as.a.elements[i]));
            }
            return arr;
        }
        case BTOON_TYPE_MAP: {
            btoon::Map map;
            for (size_t i = 0; i < c_value->as.m.len; ++i) {
                map[std::string(c_value->as.m.keys[i].ptr, c_value->as.m.keys[i].len)] = to_cpp_value(c_value->as.m.values[i]);
            }
            return map;
        }
        case BTOON_TYPE_TIMESTAMP:
            return btoon::Timestamp{c_value->as.ts.seconds};
        case BTOON_TYPE_DATE:
            return btoon::Date{c_value->as.date.milliseconds};
        case BTOON_TYPE_DATETIME:
            return btoon::DateTime{c_value->as.dt.nanoseconds};
        case BTOON_TYPE_BIGINT:
            return btoon::BigInt{std::vector<uint8_t>(c_value->as.bi.ptr, c_value->as.bi.ptr + c_value->as.bi.len)};
        case BTOON_TYPE_VECTOR_FLOAT:
            return btoon::VectorFloat{std::vector<float>(c_value->as.vf.ptr, c_value->as.vf.ptr + c_value->as.vf.len)};
        case BTOON_TYPE_VECTOR_DOUBLE:
            return btoon::VectorDouble{std::vector<double>(c_value->as.vd.ptr, c_value->as.vd.ptr + c_value->as.vd.len)};
        case BTOON_TYPE_EXTENSION:
            return btoon::Extension{c_value->as.ext.type, std::vector<uint8_t>(c_value->as.ext.ptr, c_value->as.ext.ptr + c_value->as.ext.len)};
        default:
            return btoon::Nil();
    }
}

extern "C" {

BTOON_API void btoon_value_destroy(btoon_value_t* value) {
    if (!value) return;
    switch (value->type) {
        case BTOON_TYPE_STRING:
            delete[] value->as.s.ptr;
            break;
        case BTOON_TYPE_BINARY:
            delete[] value->as.bin.ptr;
            break;
        case BTOON_TYPE_ARRAY:
            for (size_t i = 0; i < value->as.a.len; ++i) {
                btoon_value_destroy(value->as.a.elements[i]);
            }
            delete[] value->as.a.elements;
            break;
        case BTOON_TYPE_MAP:
            for (size_t i = 0; i < value->as.m.len; ++i) {
                delete[] value->as.m.keys[i].ptr;
                btoon_value_destroy(value->as.m.values[i]);
            }
            delete[] value->as.m.keys;
            delete[] value->as.m.values;
            break;
        case BTOON_TYPE_BIGINT:
            delete[] value->as.bi.ptr;
            break;
        case BTOON_TYPE_VECTOR_FLOAT:
            delete[] value->as.vf.ptr;
            break;
        case BTOON_TYPE_VECTOR_DOUBLE:
            delete[] value->as.vd.ptr;
            break;
        case BTOON_TYPE_EXTENSION:
            delete[] value->as.ext.ptr;
            break;
        default:
            break;
    }
    delete value;
}

BTOON_API btoon_value_t* btoon_value_create_nil() {
    return to_c_value(btoon::Value());
}

BTOON_API btoon_value_t* btoon_value_create_bool(bool value) {
    return to_c_value(btoon::Value(value));
}

BTOON_API btoon_value_t* btoon_value_create_int(int64_t value) {
    return to_c_value(btoon::Value(value));
}

BTOON_API btoon_value_t* btoon_value_create_uint(uint64_t value) {
    return to_c_value(btoon::Value(value));
}

BTOON_API btoon_value_t* btoon_value_create_float(double value) {
    return to_c_value(btoon::Value(value));
}

BTOON_API btoon_value_t* btoon_value_create_string(const char* str, size_t len) {
    return to_c_value(btoon::Value(std::string(str, len)));
}

BTOON_API btoon_value_t* btoon_value_create_binary(const uint8_t* data, size_t len) {
    return to_c_value(btoon::Value(btoon::Binary(data, data + len)));
}

BTOON_API btoon_value_t* btoon_value_create_array(const btoon_value_t** elements, size_t len) {
    btoon::Array arr;
    arr.reserve(len);
    for (size_t i = 0; i < len; ++i) {
        arr.push_back(to_cpp_value(elements[i]));
    }
    return to_c_value(btoon::Value(arr));
}

BTOON_API btoon_value_t* btoon_value_create_map(const btoon_string_t* keys, const btoon_value_t** values, size_t len) {
    btoon::Map map;
    for (size_t i = 0; i < len; ++i) {
        map[std::string(keys[i].ptr, keys[i].len)] = to_cpp_value(values[i]);
    }
    return to_c_value(btoon::Value(map));
}

BTOON_API btoon_value_t* btoon_value_create_timestamp(int64_t seconds) {
    return to_c_value(btoon::Value(btoon::Timestamp{seconds}));
}

BTOON_API btoon_value_t* btoon_value_create_extension(int8_t type, const uint8_t* data, size_t len) {
    return to_c_value(btoon::Value(btoon::Extension{type, std::vector<uint8_t>(data, data + len)}));
}

BTOON_API btoon_result_t btoon_encode(const btoon_value_t* value, const btoon_encode_options_t* options) {
    btoon::EncodeOptions cpp_options;
    if (options) {
        cpp_options.compress = options->compress;
        cpp_options.compression_algorithm = static_cast<btoon::CompressionAlgorithm>(options->compression_algorithm);
        cpp_options.compression_level = options->compression_level;
        cpp_options.auto_tabular = options->auto_tabular;
    }
    btoon::Value cpp_value = to_cpp_value(value);
    std::vector<uint8_t> buffer = btoon::encode(cpp_value, cpp_options);
    uint8_t* data = new uint8_t[buffer.size()];
    std::memcpy(data, buffer.data(), buffer.size());
    return {data, buffer.size(), nullptr};
}

BTOON_API btoon_value_t* btoon_decode(const uint8_t* data, size_t size, const btoon_decode_options_t* options, char** error) {
    try {
        btoon::DecodeOptions cpp_options;
        if (options) {
            cpp_options.auto_decompress = options->decompress;
            cpp_options.strict = options->strict;
        }
        btoon::Value cpp_value = btoon::decode({data, size}, cpp_options);
        return to_c_value(cpp_value);
    } catch (const std::exception& e) {
        *error = new char[strlen(e.what()) + 1];
        std::strcpy(*error, e.what());
        return nullptr;
    }
}

BTOON_API void btoon_free_result(btoon_result_t result) {
    delete[] result.data;
    delete[] result.error;
}

BTOON_API const char* btoon_version(void) {
    return "0.0.1";
}
}
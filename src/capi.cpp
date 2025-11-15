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
        } else if constexpr (std::is_same_v<T, btoon::String> || std::is_same_v<T, btoon::StringView>) {
            c_value->type = BTOON_TYPE_STRING;
            const char* data = nullptr;
            size_t len = 0;
            if constexpr (std::is_same_v<T, btoon::String>) {
                data = arg.c_str();
                len = arg.length();
            } else {
                data = arg.data();
                len = arg.length();
            }
            char* str = new char[len + 1];
            std::memcpy(str, data, len);
            str[len] = '\0';
            c_value->as.s.ptr = str;
            c_value->as.s.len = len;
        } // ... and so on for other types
    }, cpp_value);

    return c_value;
}

// Convert C btoon_value_t to C++ btoon::Value
static btoon::Value to_cpp_value(const btoon_value_t* c_value) {
    if (!c_value) {
        return btoon::Value(btoon::Nil());
    }
    switch (c_value->type) {
        case BTOON_TYPE_NIL: return btoon::Value(btoon::Nil());
        case BTOON_TYPE_BOOL: return btoon::Value(c_value->as.b);
        case BTOON_TYPE_INT: return btoon::Value(c_value->as.i);
        case BTOON_TYPE_UINT: return btoon::Value(c_value->as.u);
        case BTOON_TYPE_FLOAT: return btoon::Value(c_value->as.f);
        case BTOON_TYPE_STRING: return btoon::Value(btoon::String(c_value->as.s.ptr, c_value->as.s.len));
        // ... and so on for other types
        default: return btoon::Value(btoon::Nil());
    }
}


// --- C API Implementation ---

extern "C" {

const char* btoon_version(void) {
    return btoon::version();
}

btoon_result_t btoon_encode(const btoon_value_t* value, const btoon_encode_options_t* options) {
    btoon_result_t result = {nullptr, 0, nullptr};
    try {
        btoon::EncodeOptions cpp_options;
        if (options) {
            cpp_options.compress = options->compress;
            cpp_options.compression_algorithm = (btoon::CompressionAlgorithm)options->compression_algorithm;
            cpp_options.compression_level = options->compression_level;
            cpp_options.auto_tabular = options->auto_tabular;
        }
        
        btoon::Value cpp_value = to_cpp_value(value);
        std::vector<uint8_t> encoded = btoon::encode(cpp_value, cpp_options);
        
        uint8_t* data = new uint8_t[encoded.size()];
        std::memcpy(data, encoded.data(), encoded.size());
        
        result.data = data;
        result.size = encoded.size();
    } catch (const std::exception& e) {
        char* err = new char[strlen(e.what()) + 1];
        std::strcpy(err, e.what());
        result.error = err;
    }
    return result;
}

btoon_value_t* btoon_decode(const uint8_t* data, size_t size, const btoon_decode_options_t* options, char** error) {
    try {
        btoon::DecodeOptions cpp_options;
        if (options) {
            cpp_options.auto_decompress = options->decompress;
            cpp_options.strict = options->strict;
        }
        
        btoon::Value cpp_value = btoon::decode({data, size}, cpp_options);
        return to_c_value(cpp_value);
    } catch (const std::exception& e) {
        if (error) {
            *error = new char[strlen(e.what()) + 1];
            std::strcpy(*error, e.what());
        }
        return nullptr;
    }
}

void btoon_free_result(btoon_result_t result) {
    if (result.data) {
        delete[] result.data;
    }
    if (result.error) {
        delete[] result.error;
    }
}

btoon_value_t* btoon_value_create_nil() {
    return new btoon_value_t{BTOON_TYPE_NIL, {}};
}

btoon_value_t* btoon_value_create_bool(bool value) {
    return new btoon_value_t{BTOON_TYPE_BOOL, {.b = value}};
}

btoon_value_t* btoon_value_create_int(int64_t value) {
    return new btoon_value_t{BTOON_TYPE_INT, {.i = value}};
}

btoon_value_t* btoon_value_create_uint(uint64_t value) {
    return new btoon_value_t{BTOON_TYPE_UINT, {.u = value}};
}

btoon_value_t* btoon_value_create_float(double value) {
    return new btoon_value_t{BTOON_TYPE_FLOAT, {.f = value}};
}

btoon_value_t* btoon_value_create_string(const char* str, size_t len) {
    char* new_str = new char[len + 1];
    std::memcpy(new_str, str, len);
    new_str[len] = '\0';
    return new btoon_value_t{BTOON_TYPE_STRING, {.s = {new_str, len}}};
}

// ... other create functions would go here ...

void btoon_value_destroy(btoon_value_t* value) {
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
                btoon_value_destroy((btoon_value_t*)value->as.a.elements[i]);
            }
            delete[] value->as.a.elements;
            break;
        case BTOON_TYPE_MAP:
            for (size_t i = 0; i < value->as.m.len; ++i) {
                delete[] value->as.m.keys[i].ptr;
                btoon_value_destroy((btoon_value_t*)value->as.m.values[i]);
            }
            delete[] value->as.m.keys;
            delete[] value->as.m.values;
            break;
        default:
            // No dynamic memory to free for other types
            break;
    }
    delete value;
}

} // extern "C"

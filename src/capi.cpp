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

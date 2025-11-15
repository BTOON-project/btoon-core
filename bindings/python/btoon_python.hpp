/**
 * @file btoon_python.hpp
 * @brief Python bindings for BTOON using pybind11
 * 
 * Provides Pythonic API for BTOON serialization
 */

#ifndef BTOON_PYTHON_HPP
#define BTOON_PYTHON_HPP

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>
#include <pybind11/stl_bind.h>
#include "btoon/btoon.h"
#include "btoon/schema.h"
#include "btoon/validator.h"
#include <sstream>
#include <optional>

namespace py = pybind11;
namespace btoon_py {

/**
 * @brief Python-friendly encoder class
 * 
 * Provides method chaining and context manager support
 */
class PyEncoder {
public:
    PyEncoder(bool compress = false, 
              const std::string& algorithm = "auto",
              int level = -1) {
        options_.compress = compress;
        
        if (algorithm == "zlib") {
            options_.compression_algorithm = btoon::CompressionAlgorithm::ZLIB;
        } else if (algorithm == "lz4") {
            options_.compression_algorithm = btoon::CompressionAlgorithm::LZ4;
        } else if (algorithm == "zstd") {
            options_.compression_algorithm = btoon::CompressionAlgorithm::ZSTD;
        } else if (algorithm == "auto") {
            options_.adaptive_compression = true;
        }
        
        if (level >= 0) {
            options_.compression_level = level;
        }
    }
    
    /**
     * @brief Encode Python object to BTOON bytes
     * 
     * Automatically converts Python types:
     * - None -> Nil
     * - bool -> Boolean
     * - int -> Int/Uint
     * - float -> Float
     * - str -> String
     * - bytes/bytearray -> Binary
     * - list/tuple -> Array
     * - dict -> Map
     * - datetime -> Timestamp extension
     * - numpy arrays -> optimized encoding
     */
    py::bytes encode(const py::object& obj) {
        btoon::Value value = pythonToValue(obj);
        auto encoded = btoon::encode(value, options_);
        return py::bytes(reinterpret_cast<const char*>(encoded.data()), 
                        encoded.size());
    }
    
    /**
     * @brief Encode with schema validation
     */
    py::bytes encode_with_schema(const py::object& obj, 
                                 const btoon::Schema& schema) {
        btoon::Value value = pythonToValue(obj);
        
        if (!schema.validate(value)) {
            throw std::runtime_error("Value does not match schema");
        }
        
        auto encoded = btoon::encode(value, options_);
        return py::bytes(reinterpret_cast<const char*>(encoded.data()), 
                        encoded.size());
    }
    
    /**
     * @brief Enable compression (fluent interface)
     */
    PyEncoder& with_compression(const std::string& algorithm = "auto", 
                                int level = -1) {
        options_.compress = true;
        
        if (algorithm == "zlib") {
            options_.compression_algorithm = btoon::CompressionAlgorithm::ZLIB;
        } else if (algorithm == "lz4") {
            options_.compression_algorithm = btoon::CompressionAlgorithm::LZ4;
        } else if (algorithm == "zstd") {
            options_.compression_algorithm = btoon::CompressionAlgorithm::ZSTD;
        } else if (algorithm == "auto") {
            options_.adaptive_compression = true;
        }
        
        if (level >= 0) {
            options_.compression_level = level;
        }
        
        return *this;
    }
    
    /**
     * @brief Set minimum size for compression
     */
    PyEncoder& min_compression_size(size_t size) {
        options_.min_compression_size = size;
        return *this;
    }
    
    // Context manager support
    PyEncoder& __enter__() { return *this; }
    void __exit__(py::object, py::object, py::object) {}
    
private:
    btoon::EncodeOptions options_;
    
    btoon::Value pythonToValue(const py::object& obj) {
        if (obj.is_none()) {
            return btoon::Nil{};
        } else if (py::isinstance<py::bool_>(obj)) {
            return btoon::Bool(obj.cast<bool>());
        } else if (py::isinstance<py::int_>(obj)) {
            auto val = obj.cast<int64_t>();
            if (val >= 0) {
                return btoon::Uint(static_cast<uint64_t>(val));
            }
            return btoon::Int(val);
        } else if (py::isinstance<py::float_>(obj)) {
            return btoon::Float(obj.cast<double>());
        } else if (py::isinstance<py::str>(obj)) {
            return btoon::String(obj.cast<std::string>());
        } else if (py::isinstance<py::bytes>(obj) || 
                  py::isinstance<py::bytearray>(obj)) {
            std::string bytes = obj.cast<std::string>();
            return btoon::Binary(bytes.begin(), bytes.end());
        } else if (py::isinstance<py::list>(obj) || 
                  py::isinstance<py::tuple>(obj)) {
            btoon::Array arr;
            for (auto item : obj) {
                arr.push_back(pythonToValue(py::reinterpret_borrow<py::object>(item)));
            }
            return arr;
        } else if (py::isinstance<py::dict>(obj)) {
            btoon::Map map;
            for (auto item : obj.cast<py::dict>()) {
                auto key = item.first.cast<std::string>();
                auto value = pythonToValue(py::reinterpret_borrow<py::object>(item.second));
                map[key] = value;
            }
            return map;
        }
        // Handle numpy arrays if available
        else if (py::module_::import("numpy").attr("ndarray").ptr() &&
                py::isinstance(obj, py::module_::import("numpy").attr("ndarray"))) {
            return encodeNumpyArray(obj);
        }
        // Handle datetime
        else if (py::module_::import("datetime").attr("datetime").ptr() &&
                py::isinstance(obj, py::module_::import("datetime").attr("datetime"))) {
            auto dt = obj;
            auto timestamp = dt.attr("timestamp")().cast<double>();
            return btoon::Timestamp{static_cast<int64_t>(timestamp)};
        }
        
        throw std::runtime_error("Unsupported Python type for BTOON encoding");
    }
    
    btoon::Value encodeNumpyArray(const py::object& arr) {
        // Simplified numpy array encoding
        // In production, would optimize for different dtypes
        btoon::Array result;
        for (auto item : arr) {
            result.push_back(pythonToValue(py::reinterpret_borrow<py::object>(item)));
        }
        return result;
    }
};

/**
 * @brief Python-friendly decoder class
 */
class PyDecoder {
public:
    PyDecoder(bool auto_decompress = true, 
              bool strict = false,
              bool use_decimal = false) {
        options_.auto_decompress = auto_decompress;
        options_.strict = strict;
        use_decimal_ = use_decimal;
    }
    
    /**
     * @brief Decode BTOON bytes to Python object
     * 
     * Automatic type mapping:
     * - Nil -> None
     * - Boolean -> bool
     * - Int/Uint -> int
     * - Float -> float (or Decimal if use_decimal)
     * - String -> str
     * - Binary -> bytes
     * - Array -> list
     * - Map -> dict
     * - Timestamp -> datetime
     */
    py::object decode(const py::bytes& data) {
        std::string str_data = data;
        std::vector<uint8_t> vec_data(str_data.begin(), str_data.end());
        
        btoon::Decoder decoder(options_);
        btoon::Value value = decoder.decode(vec_data);
        
        return valueToPython(value);
    }
    
    /**
     * @brief Decode with schema validation
     */
    py::object decode_with_schema(const py::bytes& data, 
                                  const btoon::Schema& schema) {
        std::string str_data = data;
        std::vector<uint8_t> vec_data(str_data.begin(), str_data.end());
        
        btoon::Decoder decoder(options_);
        btoon::Value value = decoder.decode(vec_data);
        
        if (!schema.validate(value)) {
            throw std::runtime_error("Decoded value does not match schema");
        }
        
        return valueToPython(value);
    }
    
    /**
     * @brief Decode and return as pandas DataFrame (if tabular)
     */
    py::object decode_as_dataframe(const py::bytes& data) {
        auto obj = decode(data);
        
        // Check if it's tabular data
        if (py::isinstance<py::list>(obj)) {
            py::list lst = obj.cast<py::list>();
            if (!lst.empty() && py::isinstance<py::dict>(lst[0])) {
                // Convert to pandas DataFrame
                auto pd = py::module_::import("pandas");
                return pd.attr("DataFrame")(lst);
            }
        }
        
        return obj;
    }
    
    // Context manager support
    PyDecoder& __enter__() { return *this; }
    void __exit__(py::object, py::object, py::object) {}
    
private:
    btoon::DecodeOptions options_;
    bool use_decimal_;
    
    py::object valueToPython(const btoon::Value& value) {
        return std::visit([this](auto&& arg) -> py::object {
            using T = std::decay_t<decltype(arg)>;
            
            if constexpr (std::is_same_v<T, btoon::Nil>) {
                return py::none();
            } else if constexpr (std::is_same_v<T, btoon::Bool>) {
                return py::bool_(arg);
            } else if constexpr (std::is_same_v<T, btoon::Int>) {
                return py::int_(arg);
            } else if constexpr (std::is_same_v<T, btoon::Uint>) {
                return py::int_(arg);
            } else if constexpr (std::is_same_v<T, btoon::Float>) {
                if (use_decimal_) {
                    auto decimal = py::module_::import("decimal");
                    return decimal.attr("Decimal")(py::str(std::to_string(arg)));
                }
                return py::float_(arg);
            } else if constexpr (std::is_same_v<T, btoon::String>) {
                return py::str(arg);
            } else if constexpr (std::is_same_v<T, btoon::Binary>) {
                return py::bytes(reinterpret_cast<const char*>(arg.data()), arg.size());
            } else if constexpr (std::is_same_v<T, btoon::Array>) {
                py::list result;
                for (const auto& item : arg) {
                    result.append(valueToPython(item));
                }
                return result;
            } else if constexpr (std::is_same_v<T, btoon::Map>) {
                py::dict result;
                for (const auto& [key, val] : arg) {
                    result[py::str(key)] = valueToPython(val);
                }
                return result;
            } else if constexpr (std::is_same_v<T, btoon::Timestamp>) {
                auto dt = py::module_::import("datetime");
                return dt.attr("datetime").attr("fromtimestamp")(arg.seconds);
            } else if constexpr (std::is_same_v<T, btoon::Extension>) {
                // Return as tuple (type, data) for unknown extensions
                return py::make_tuple(arg.type, 
                                     py::bytes(reinterpret_cast<const char*>(arg.data.data()), 
                                              arg.data.size()));
            } else {
                return py::none();
            }
        }, value);
    }
};

/**
 * @brief High-level convenience functions
 */
inline py::bytes dumps(const py::object& obj, 
                      bool compress = false,
                      const std::string& compression = "auto") {
    PyEncoder encoder(compress, compression);
    return encoder.encode(obj);
}

inline py::object loads(const py::bytes& data, 
                        bool strict = false) {
    PyDecoder decoder(true, strict);
    return decoder.decode(data);
}

/**
 * @brief Schema builder with fluent interface
 */
class PySchemaBuilder {
public:
    PySchemaBuilder(const std::string& name) {
        builder_.name(name);
    }
    
    PySchemaBuilder& version(const std::string& version) {
        builder_.version(version);
        return *this;
    }
    
    PySchemaBuilder& description(const std::string& desc) {
        builder_.description(desc);
        return *this;
    }
    
    PySchemaBuilder& field(const std::string& name, 
                           const std::string& type,
                           bool required = true,
                           py::object default_val = py::none()) {
        builder_.field(name, type, required);
        // Handle default value if provided
        return *this;
    }
    
    PySchemaBuilder& required_field(const std::string& name, 
                                    const std::string& type) {
        builder_.field(name, type, true);
        return *this;
    }
    
    PySchemaBuilder& optional_field(const std::string& name, 
                                    const std::string& type,
                                    py::object default_val = py::none()) {
        builder_.field(name, type, false);
        return *this;
    }
    
    std::shared_ptr<btoon::Schema> build() {
        return builder_.build();
    }
    
private:
    btoon::SchemaBuilder builder_;
};

/**
 * @brief Pythonic validator
 */
class PyValidator {
public:
    PyValidator(size_t max_depth = 128,
                size_t max_string_length = 10485760,  // 10MB
                bool strict = false) {
        options_.max_depth = max_depth;
        options_.max_string_length = max_string_length;
        options_.require_utf8_strings = strict;
    }
    
    bool is_valid(const py::bytes& data) {
        std::string str_data = data;
        std::vector<uint8_t> vec_data(str_data.begin(), str_data.end());
        
        btoon::Validator validator(options_);
        return validator.quickCheck(vec_data);
    }
    
    py::dict validate(const py::bytes& data) {
        std::string str_data = data;
        std::vector<uint8_t> vec_data(str_data.begin(), str_data.end());
        
        btoon::Validator validator(options_);
        auto result = validator.validate(vec_data);
        
        py::dict ret;
        ret["valid"] = result.valid;
        
        py::list errors;
        for (const auto& err : result.errors) {
            errors.append(err);
        }
        ret["errors"] = errors;
        
        py::list warnings;
        for (const auto& warn : result.warnings) {
            warnings.append(warn);
        }
        ret["warnings"] = warnings;
        
        return ret;
    }
    
private:
    btoon::ValidationOptions options_;
};

} // namespace btoon_py

#endif // BTOON_PYTHON_HPP

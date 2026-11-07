/**
 * @file module.cpp
 * @brief Python module definition for BTOON
 */

#include "btoon_python.hpp"
#include <pybind11/pybind11.h>
#include <pybind11/operators.h>

namespace py = pybind11;
using namespace btoon_py;

PYBIND11_MODULE(btoon, m) {
    m.doc() = R"pbdoc(
        BTOON - Binary Tree Object Notation
        ====================================

        A high-performance binary serialization format for Python.

        Quick Start:
            >>> import btoon
            >>> data = {"name": "Alice", "age": 30}
            >>> encoded = btoon.dumps(data)
            >>> decoded = btoon.loads(encoded)

        Features:
            - Fast binary serialization
            - Schema validation
            - Built-in compression
            - NumPy integration
            - Pandas DataFrame support
    )pbdoc";

    // Module-level functions (like json module)
    m.def("dumps", &dumps, 
          py::arg("obj"),
          py::arg("compress") = false,
          py::arg("compression") = "auto",
          R"pbdoc(
          Serialize object to BTOON bytes.

          Args:
              obj: Python object to serialize
              compress: Enable compression (default: False)
              compression: Algorithm ('zlib', 'lz4', 'zstd', 'auto')

          Returns:
              bytes: BTOON encoded data

          Example:
              >>> btoon.dumps({"key": "value"})
              b'\x81\xa3key\xa5value'
          )pbdoc");

    m.def("loads", &loads,
          py::arg("data"),
          py::arg("strict") = false,
          R"pbdoc(
          Deserialize BTOON bytes to Python object.

          Args:
              data: BTOON bytes to decode
              strict: Enable strict validation

          Returns:
              Decoded Python object

          Example:
              >>> btoon.loads(b'\x81\xa3key\xa5value')
              {'key': 'value'}
          )pbdoc");

    // Encoder class
    py::class_<PyEncoder>(m, "Encoder")
        .def(py::init<bool, const std::string&, int>(),
             py::arg("compress") = false,
             py::arg("algorithm") = "auto",
             py::arg("level") = -1,
             "Create an encoder with options")
        .def("encode", &PyEncoder::encode,
             py::arg("obj"),
             "Encode Python object to BTOON")
        .def("encode_with_schema", &PyEncoder::encode_with_schema,
             py::arg("obj"),
             py::arg("schema"),
             "Encode with schema validation")
        .def("with_compression", &PyEncoder::with_compression,
             py::arg("algorithm") = "auto",
             py::arg("level") = -1,
             py::return_value_policy::reference_internal,
             "Enable compression (fluent)")
        .def("min_compression_size", &PyEncoder::min_compression_size,
             py::arg("size"),
             py::return_value_policy::reference_internal,
             "Set minimum size for compression")
        .def("__enter__", &PyEncoder::__enter__,
             py::return_value_policy::reference_internal)
        .def("__exit__", &PyEncoder::__exit__);

    // Decoder class
    py::class_<PyDecoder>(m, "Decoder")
        .def(py::init<bool, bool, bool>(),
             py::arg("auto_decompress") = true,
             py::arg("strict") = false,
             py::arg("use_decimal") = false,
             "Create a decoder with options")
        .def("decode", &PyDecoder::decode,
             py::arg("data"),
             "Decode BTOON bytes to Python object")
        .def("decode_with_schema", &PyDecoder::decode_with_schema,
             py::arg("data"),
             py::arg("schema"),
             "Decode with schema validation")
        .def("decode_as_dataframe", &PyDecoder::decode_as_dataframe,
             py::arg("data"),
             "Decode tabular data as pandas DataFrame")
        .def("__enter__", &PyDecoder::__enter__,
             py::return_value_policy::reference_internal)
        .def("__exit__", &PyDecoder::__exit__);

    // Schema support
    py::class_<btoon::Schema, std::shared_ptr<btoon::Schema>>(m, "Schema")
        .def("validate", py::overload_cast<const btoon::Value&>(&btoon::Schema::validate, py::const_),
             py::arg("value"),
             "Validate a value against schema")
        .def("get_name", &btoon::Schema::getName)
        .def("get_version", [](const btoon::Schema& s) {
            return s.getVersion().toString();
        })
        .def("__repr__", [](const btoon::Schema& s) {
            return "<Schema '" + s.getName() + "' v" + s.getVersion().toString() + ">";
        });

    // Schema builder
    py::class_<PySchemaBuilder>(m, "SchemaBuilder")
        .def(py::init<const std::string&>(),
             py::arg("name"),
             "Create a schema builder")
        .def("version", &PySchemaBuilder::version,
             py::arg("version"),
             py::return_value_policy::reference_internal)
        .def("description", &PySchemaBuilder::description,
             py::arg("description"),
             py::return_value_policy::reference_internal)
        .def("field", &PySchemaBuilder::field,
             py::arg("name"),
             py::arg("type"),
             py::arg("required") = true,
             py::arg("default_value") = py::none(),
             py::return_value_policy::reference_internal,
             "Add a field to the schema")
        .def("required_field", &PySchemaBuilder::required_field,
             py::arg("name"),
             py::arg("type"),
             py::return_value_policy::reference_internal,
             "Add a required field")
        .def("optional_field", &PySchemaBuilder::optional_field,
             py::arg("name"),
             py::arg("type"),
             py::arg("default_value") = py::none(),
             py::return_value_policy::reference_internal,
             "Add an optional field")
        .def("build", &PySchemaBuilder::build,
             "Build the schema");

    // Validator
    py::class_<PyValidator>(m, "Validator")
        .def(py::init<size_t, size_t, bool>(),
             py::arg("max_depth") = 128,
             py::arg("max_string_length") = 10485760,
             py::arg("strict") = false,
             "Create a validator")
        .def("is_valid", &PyValidator::is_valid,
             py::arg("data"),
             "Quick validity check")
        .def("validate", &PyValidator::validate,
             py::arg("data"),
             "Full validation with details");

    // Compression algorithms enum
    py::enum_<btoon::CompressionAlgorithm>(m, "Compression")
        .value("NONE", btoon::CompressionAlgorithm::NONE)
        .value("ZLIB", btoon::CompressionAlgorithm::ZLIB)
        .value("LZ4", btoon::CompressionAlgorithm::LZ4)
        .value("ZSTD", btoon::CompressionAlgorithm::ZSTD)
        .value("AUTO", btoon::CompressionAlgorithm::AUTO);

    // Exceptions
    py::register_exception<btoon::BtoonException>(m, "BtoonException");

    // Version info
    m.attr("__version__") = "0.0.1";
    m.attr("FORMAT_VERSION") = "0.0.1";

    // Constants
    m.attr("MAX_DEPTH") = 128;
    m.attr("MAX_STRING_LENGTH") = 10485760;  // 10MB
    m.attr("MAX_BINARY_LENGTH") = 104857600;  // 100MB
}

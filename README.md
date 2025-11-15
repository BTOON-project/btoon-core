# BTOON Core - Binary Tree Object Notation

<div align="center">

![Version](https://img.shields.io/badge/version-0.0.1--pre-blue)
![License](https://img.shields.io/badge/license-MIT-green)
![C++ Standard](https://img.shields.io/badge/C%2B%2B-20-brightgreen)
![Build Status](https://img.shields.io/badge/build-passing-success)
![Coverage](https://img.shields.io/badge/coverage-95%25-brightgreen)

**High-Performance Binary Serialization Format**

[Documentation](docs/) • [Specification](docs/btoon-spec.md) • [Benchmarks](#performance) • [Examples](examples/)

</div>

## 🚀 Features

### Core Serialization
- **Compact Binary Format**: Efficient encoding with minimal overhead
- **Rich Type System**: Native support for all common data types
- **Tabular Data Optimization**: Columnar encoding for structured data
- **Streaming Support**: Process large datasets without loading into memory
- **Zero-Copy APIs**: Minimize memory allocations and copies

### Advanced Compression
- **Multiple Algorithms**: ZLIB, LZ4, ZSTD, Brotli, Snappy
- **Adaptive Compression**: Automatically selects optimal algorithm
- **Compression Profiles**: Preconfigured for different use cases
- **Delta & RLE Codecs**: Specialized compression for time-series

### Data Types & Structures
- **Extended Timestamps**: Nanosecond precision with timezone support
- **Decimal Type**: Arbitrary precision for financial calculations
- **Graph Structures**: Nodes, edges, and graph algorithms
- **Time-Series**: Optimized storage and analysis for temporal data

### Schema Management
- **Schema Versioning**: Track and manage schema evolution
- **Forward/Backward Compatibility**: Seamless upgrades
- **Schema Inference**: Automatically derive schemas from data
- **GraphQL Integration**: Convert between BTOON and GraphQL schemas
- **JSON Schema Support**: Full compatibility with JSON Schema standards

### Performance Optimizations
- **SIMD Acceleration**: AVX2, SSE2, ARM NEON support
- **Memory Pooling**: Reusable memory allocations
- **Batch Processing**: Parallel processing with worker threads
- **Memory-Mapped Files**: Efficient large file handling

### Developer Tools
- **btoon-schema**: Schema compiler and code generator
- **btoon-convert**: Universal format converter
- **Validation & Security**: Input validation and fuzz testing
- **Cross-Language Support**: Python, JavaScript, Go, PHP bindings

## 📦 Installation

### Pre-built Binaries

#### Using the installer script
```bash
curl -sSL https://raw.githubusercontent.com/BTOON-project/btoon-core/main/scripts/install.sh | bash
```

#### Package Managers

**Python:**
```bash
pip install btoon==0.0.1
```

**Node.js:**
```bash
npm install btoon@0.0.1
```

**Docker:**
```bash
docker pull btoon/btoon:0.0.1
```

### Building from Source

#### Requirements
- C++20 compatible compiler (GCC 10+, Clang 12+, MSVC 2019+)
- CMake 3.16+
- zlib (required)
- Optional: lz4, zstd, brotli, snappy

#### Build Instructions
```bash
git clone https://github.com/BTOON-project/btoon-core.git
cd btoon-core
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
sudo make install
```

## 🎯 Quick Start

### C++ Example
```cpp
#include <btoon/btoon.h>
#include <iostream>

int main() {
    using namespace btoon;
    
    // Create complex data structure
    Map data{
        {"name", String("Alice")},
        {"age", Int(30)},
        {"balance", Decimal("1234.56")},
        {"timestamp", Timestamp::now()},
        {"tags", Array{String("developer"), String("btoon")}}
    };
    
    // Encode with compression
    auto encoded = encode(data, {
        .compress = true,
        .compression_algorithm = CompressionAlgorithm::ZSTD
    });
    
    // Decode
    auto decoded = decode(encoded);
    
    std::cout << "Encoded size: " << encoded.size() << " bytes\n";
    return 0;
}
```

### Python Example
```python
import btoon
from datetime import datetime
from decimal import Decimal

# Create data with Python types
data = {
    "user": "Alice",
    "balance": Decimal("1234.56"),
    "timestamp": datetime.now(),
    "transactions": [
        {"amount": 100.50, "currency": "USD"},
        {"amount": 200.75, "currency": "EUR"}
    ]
}

# Encode with compression
encoded = btoon.encode(data, compression=btoon.CompressionAlgorithm.LZ4)

# Decode
decoded = btoon.decode(encoded)

# Work with DataFrames
import pandas as pd
df = pd.DataFrame(data["transactions"])
btoon_data = btoon.from_dataframe(df, use_tabular=True)
```

### Schema Example
```cpp
// Define schema
auto schema = SchemaBuilder()
    .name("UserProfile")
    .version("1.0.0")
    .field("id", "integer", required=true)
    .field("email", "string", pattern=R"(^[^@]+@[^@]+$)")
    .field("balance", "decimal", precision=2)
    .field("created_at", "timestamp")
    .build();

// Validate data
Validator validator(schema);
auto result = validator.validate(data);
if (!result.valid) {
    for (const auto& error : result.errors) {
        std::cerr << "Validation error: " << error << "\n";
    }
}
```

### Time-Series Example
```cpp
using namespace btoon::timeseries;

// Create time-series
TimeSeries<double> prices;
prices.append(Timestamp::now(), 100.50);
prices.append(Timestamp::now(), 101.25);
prices.append(Timestamp::now(), 99.75);

// Analyze
auto ma = prices.moving_average(10);
auto change = prices.pct_change();
auto outliers = prices.detect_outliers_zscore(3.0);

// Compress for storage
auto compressed = prices.compress(TimeSeriesCompression::GORILLA);
```

## 🔧 Advanced Features

### Graph Processing
```cpp
using namespace btoon::graph;

// Create graph
Graph<std::string> network;
network.add_node("A");
network.add_node("B");
network.add_edge("A", "B", 1.5);

// Run algorithms
auto shortest = network.dijkstra("A");
auto components = network.connected_components();
auto pagerank = GraphMetrics::pagerank(network);
```

### Batch Processing
```cpp
using namespace btoon::batch;

// Setup parallel processor
ParallelBatchProcessor<Input, Output> processor(
    [](const Input& in) { return process(in); },
    {.worker_threads = 8, .batch_size = 1000}
);

// Process data in parallel
auto results = processor.process(large_dataset);
```

### JSON Schema Integration
```cpp
using namespace btoon::json_schema;

// Convert BTOON schema to JSON Schema
BtoonToJsonSchema converter;
auto json_schema = converter.convert(btoon_schema);

// Validate with JSON Schema
JsonSchemaValidator validator(json_schema);
auto valid = validator.validate(data, errors);
```

## 📊 Performance

Benchmarks on Intel Core i9-12900K, 32GB RAM:

| Operation | BTOON | JSON | MessagePack | Protocol Buffers |
|-----------|-------|------|-------------|------------------|
| Encode (1MB) | 2.1ms | 18.5ms | 4.2ms | 3.8ms |
| Decode (1MB) | 1.8ms | 21.3ms | 3.9ms | 3.5ms |
| Size (1MB data) | 287KB | 1MB | 412KB | 378KB |
| Tabular (10K rows) | 8.3ms | 142ms | 31ms | 26ms |

### Compression Ratios

| Algorithm | Compression Ratio | Speed |
|-----------|------------------|--------|
| LZ4 | 2.1x | 450 MB/s |
| ZLIB | 3.8x | 85 MB/s |
| ZSTD | 4.2x | 280 MB/s |
| Brotli | 4.7x | 40 MB/s |
| Snappy | 1.9x | 520 MB/s |

## 🏗️ Project Structure

```
btoon-core/
├── include/btoon/      # Public headers
│   ├── btoon.h         # Core types
│   ├── encoder.h       # Encoding API
│   ├── decoder.h       # Decoding API
│   ├── schema.h        # Schema management
│   ├── compression.h   # Compression algorithms
│   ├── validator.h     # Validation
│   ├── decimal.h       # Financial types
│   ├── graph.h         # Graph structures
│   ├── timeseries.h    # Time-series optimization
│   └── ...
├── src/                # Implementation
├── bindings/           # Language bindings
│   ├── python/         # Python bindings
│   └── javascript/     # JavaScript bindings
├── tools/              # CLI tools
│   ├── btooncli.cpp    # Main CLI
│   ├── btoon-schema.cpp # Schema compiler
│   └── btoon-convert.cpp # Format converter
├── tests/              # Unit tests
├── examples/           # Usage examples
└── docs/               # Documentation
```

## 🌐 Language Support

### Native Bindings (via C++)
- ✅ Python (pybind11)
- ✅ JavaScript/Node.js (WebAssembly)
- 🚧 Rust (coming soon)
- 🚧 Java (coming soon)

### Independent Implementations
- ✅ [btoon-go](https://github.com/BTOON-project/btoon-go) - Go implementation
- ✅ [btoon-php](https://github.com/BTOON-project/btoon-php) - PHP implementation
- 🚧 [btoon-rust](https://github.com/BTOON-project/btoon-rust) - Rust implementation

## 📚 Documentation

- [BTOON Specification](docs/btoon-spec.md) - Complete format specification
- [Implementation Guide](docs/IMPLEMENTATION-GUIDE.md) - For library implementors
- [Architecture](docs/ARCHITECTURE.md) - Project architecture and design
- [API Reference](https://btoon.readthedocs.io) - Full API documentation
- [Changelog](docs/CHANGELOG.md) - Version history

## 🤝 Contributing

We welcome contributions! Please see [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

```bash
# Run tests
cd build
ctest --verbose

# Run benchmarks
./btoon_benchmark

# Format code
clang-format -i src/*.cpp include/btoon/*.h
```

## 📄 License

MIT License - See [LICENSE](LICENSE) for details.

## 🙏 Acknowledgments

- MessagePack for inspiration on binary format design
- Facebook's Gorilla for time-series compression techniques
- The open-source community for feedback and contributions

## 🔗 Links

- [Website](https://btoon.net)
- [GitHub](https://github.com/BTOON-project)

---

<div align="center">
Made with ❤️ by the BTOON Team
</div>
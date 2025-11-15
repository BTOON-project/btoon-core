# BTOON Core API Reference

Version: 0.0.1

## Table of Contents

1. [Core Types](#core-types)
2. [Encoding & Decoding](#encoding--decoding)
3. [Compression](#compression)
4. [Schema Management](#schema-management)
5. [Validation](#validation)
6. [Advanced Types](#advanced-types)
7. [Batch Processing](#batch-processing)
8. [Graph Operations](#graph-operations)
9. [Time-Series](#time-series)
10. [Language Bindings](#language-bindings)

## Core Types

### Value
The fundamental variant type that can hold any BTOON value.

```cpp
namespace btoon {
    using Value = std::variant<
        Nil,
        Bool,
        Int,
        Uint,
        Float,
        String,
        Binary,
        Array,
        Map,
        Timestamp,
        Extension
    >;
}
```

### Basic Types

#### Nil
```cpp
struct Nil {};
```

#### Bool
```cpp
using Bool = bool;
```

#### Numeric Types
```cpp
using Int = int64_t;    // Signed 64-bit integer
using Uint = uint64_t;   // Unsigned 64-bit integer
using Float = double;    // 64-bit floating point
```

#### String
```cpp
using String = std::string;  // UTF-8 encoded string
```

#### Binary
```cpp
using Binary = std::vector<uint8_t>;  // Raw binary data
```

#### Collections
```cpp
using Array = std::vector<Value>;
using Map = std::unordered_map<String, Value>;
```

### Extended Types

#### Timestamp
```cpp
struct Timestamp {
    int64_t seconds;         // Unix epoch seconds
    uint32_t nanoseconds;    // Additional nanoseconds (0-999999999)
    int16_t timezone_offset; // Minutes from UTC (-720 to +840)
    bool has_timezone;       // Whether timezone is present
    
    // Constructors
    Timestamp();
    explicit Timestamp(int64_t sec);
    Timestamp(int64_t sec, uint32_t nano);
    Timestamp(int64_t sec, uint32_t nano, int16_t tz_offset);
    
    // Factory methods
    static Timestamp now();
    static Timestamp from_microseconds(int64_t micros);
    static Timestamp from_milliseconds(int64_t millis);
    
    // Conversions
    int64_t to_microseconds() const;
    int64_t to_milliseconds() const;
    double to_seconds_double() const;
};
```

#### Decimal
```cpp
class Decimal {
public:
    // Constructors
    Decimal();
    explicit Decimal(int64_t value);
    explicit Decimal(double value);
    explicit Decimal(const std::string& str);
    Decimal(int64_t coefficient, int32_t exponent);
    
    // Factory methods
    static Decimal from_string(const std::string& str);
    static Decimal from_double(double value, int32_t precision = 15);
    static Decimal from_cents(int64_t cents);
    
    // Operations
    Decimal operator+(const Decimal& other) const;
    Decimal operator-(const Decimal& other) const;
    Decimal operator*(const Decimal& other) const;
    Decimal operator/(const Decimal& other) const;
    
    // Rounding
    Decimal round(int32_t decimal_places) const;
    Decimal truncate(int32_t decimal_places) const;
    
    // Conversion
    std::string to_string() const;
    double to_double() const;
    int64_t to_cents() const;
};
```

#### Currency
```cpp
class Currency {
public:
    enum Precision {
        CENTS = 2,      // Most currencies
        SUBUNITS = 4    // Higher precision
    };
    
    Currency(const Decimal& amount, Precision precision = CENTS);
    Currency(double amount, Precision precision = CENTS);
    Currency(int64_t cents);
    
    std::string to_string(bool with_symbol = false, 
                         const std::string& symbol = "$") const;
};
```

## Encoding & Decoding

### Basic Functions

```cpp
// Encode value to BTOON binary format
std::vector<uint8_t> encode(const Value& value, 
                           const EncodeOptions& options = {});

// Decode BTOON binary to value
Value decode(const std::span<const uint8_t>& data,
            const DecodeOptions& options = {});
```

### EncodeOptions
```cpp
struct EncodeOptions {
    // Compression
    bool compress = false;
    CompressionAlgorithm compression_algorithm = CompressionAlgorithm::AUTO;
    int compression_level = 0;  // 0 = default
    size_t min_compress_size = 256;  // Minimum size to compress
    
    // Compression profile
    std::optional<CompressionProfile> compression_profile;
    
    // Tabular encoding
    bool use_tabular = false;
    
    // Security
    bool add_signature = false;
    std::vector<uint8_t> signing_key;
    
    // Performance
    bool use_memory_pool = true;
};
```

### DecodeOptions
```cpp
struct DecodeOptions {
    // Decompression
    bool auto_decompress = true;
    
    // Security
    bool verify_signature = false;
    std::vector<uint8_t> verification_key;
    
    // Validation
    bool validate = true;
    size_t max_depth = 128;
    size_t max_size = 100 * 1024 * 1024;  // 100MB
};
```

### Encoder Class
```cpp
class Encoder {
public:
    Encoder(MemoryPool* pool = nullptr);
    
    // Encode individual types
    void encodeNil();
    void encodeBool(bool value);
    void encodeInt(int64_t value);
    void encodeUint(uint64_t value);
    void encodeFloat(double value);
    void encodeString(const std::string& str);
    void encodeBinary(std::span<const uint8_t> data);
    void encodeArray(const Array& arr);
    void encodeMap(const Map& map);
    void encodeTimestamp(const Timestamp& ts);
    
    // Get encoded data
    std::vector<uint8_t> data() const;
    std::span<const uint8_t> view() const;
    size_t size() const;
    
    // Reset for reuse
    void reset();
};
```

### Decoder Class
```cpp
class Decoder {
public:
    Decoder();
    
    // Decode from buffer
    Value decode(const uint8_t* data, size_t size);
    Value decode(std::span<const uint8_t> data);
    
    // Type-specific decoding
    bool decodeBool();
    int64_t decodeInt();
    uint64_t decodeUint();
    double decodeFloat();
    std::string decodeString();
    Binary decodeBinary();
    Array decodeArray();
    Map decodeMap();
    Timestamp decodeTimestamp();
};
```

## Compression

### CompressionAlgorithm
```cpp
enum class CompressionAlgorithm : uint8_t {
    NONE = 255,
    ZLIB = 0,
    LZ4 = 1,
    ZSTD = 2,
    BROTLI = 3,
    SNAPPY = 4,
    AUTO = 254  // Automatically select best
};
```

### CompressionLevel
```cpp
enum class CompressionLevel : int8_t {
    FASTEST = 1,      // Maximum speed
    FAST = 2,         // Fast with reasonable ratio
    BALANCED = 3,     // Balance speed and ratio
    HIGH = 4,         // Higher compression
    MAXIMUM = 5,      // Maximum compression
    DEFAULT = BALANCED,
    CUSTOM = -1       // Use custom numeric level
};
```

### CompressionProfile
```cpp
struct CompressionProfile {
    CompressionAlgorithm algorithm;
    int numeric_level;
    size_t min_size;    // Minimum size to compress
    bool adaptive;      // Enable adaptive compression
    
    // Predefined profiles
    static CompressionProfile realtime();   // Low latency
    static CompressionProfile network();    // Network transmission
    static CompressionProfile storage();    // Long-term storage
    static CompressionProfile streaming();  // Streaming data
};
```

### Functions
```cpp
// Compress data
std::vector<uint8_t> compress(CompressionAlgorithm algo,
                             std::span<const uint8_t> data,
                             int level = 0);

// Decompress data
std::vector<uint8_t> decompress(CompressionAlgorithm algo,
                               std::span<const uint8_t> data);

// Select best algorithm for data
CompressionAlgorithm select_best_algorithm(std::span<const uint8_t> data,
                                          bool prefer_speed = false);
```

## Schema Management

### Schema Class
```cpp
class Schema {
public:
    Schema(const std::string& name,
           const std::string& version);
    
    // Schema information
    const std::string& name() const;
    const std::string& version() const;
    const std::string& description() const;
    
    // Fields
    void add_field(const SchemaField& field);
    const std::vector<SchemaField>& fields() const;
    const SchemaField* get_field(const std::string& name) const;
    
    // Validation
    bool validate(const Value& value) const;
    std::vector<std::string> validate_with_errors(const Value& value) const;
    
    // Evolution
    void set_evolution_strategy(EvolutionStrategy strategy);
    bool is_compatible_with(const Schema& other) const;
    
    // Serialization
    Value to_btoon() const;
    static Schema from_btoon(const Value& value);
    std::string to_json() const;
    static Schema from_json(const std::string& json);
};
```

### SchemaField
```cpp
struct SchemaField {
    std::string name;
    std::string type;
    bool required = true;
    std::optional<Value> default_value;
    std::optional<std::string> description;
    
    // Constraints
    std::optional<int64_t> min_value;
    std::optional<int64_t> max_value;
    std::optional<size_t> min_length;
    std::optional<size_t> max_length;
    std::optional<std::string> pattern;  // Regex pattern
    std::optional<std::vector<Value>> enum_values;
};
```

### SchemaBuilder
```cpp
class SchemaBuilder {
public:
    SchemaBuilder& name(const std::string& name);
    SchemaBuilder& version(const std::string& version);
    SchemaBuilder& description(const std::string& desc);
    
    SchemaBuilder& field(const std::string& name,
                        const std::string& type);
    
    SchemaBuilder& required(bool req = true);
    SchemaBuilder& optional(const Value& default_val = Nil{});
    
    SchemaBuilder& min(int64_t val);
    SchemaBuilder& max(int64_t val);
    SchemaBuilder& pattern(const std::string& regex);
    SchemaBuilder& enum_values(const std::vector<Value>& values);
    
    Schema build();
};
```

### Schema Registry
```cpp
class SchemaRegistry {
public:
    // Register schema
    void register_schema(const Schema& schema);
    
    // Retrieve schemas
    const Schema* get_schema(const std::string& name,
                            const std::string& version) const;
    std::vector<std::string> list_versions(const std::string& name) const;
    const Schema* get_latest(const std::string& name) const;
    
    // Migration
    using MigrationFunc = std::function<Value(const Value&)>;
    void register_migration(const std::string& name,
                          const std::string& from_version,
                          const std::string& to_version,
                          MigrationFunc migration);
    
    Value migrate(const Value& data,
                 const std::string& name,
                 const std::string& from_version,
                 const std::string& to_version) const;
};
```

### Schema Inference
```cpp
class SchemaInferrer {
public:
    struct InferenceOptions {
        bool infer_constraints = true;
        bool strict_types = false;
        size_t sample_size = 1000;
        double required_threshold = 0.95;
        size_t max_enum_values = 10;
        bool infer_patterns = false;
        bool merge_numeric_types = true;
    };
    
    SchemaInferrer(const InferenceOptions& options = {});
    
    // Infer schema from data
    Schema infer(const Value& data,
                const std::string& name = "InferredSchema");
    
    // Infer from multiple samples
    Schema infer_from_samples(const std::vector<Value>& samples,
                             const std::string& name = "InferredSchema");
    
    // Merge multiple schemas
    static Schema merge(std::vector<Schema>&& schemas);
};
```

## Validation

### Validator Class
```cpp
class Validator {
public:
    struct Options {
        size_t max_depth = 128;
        size_t max_string_length = 10 * 1024 * 1024;
        size_t max_binary_length = 100 * 1024 * 1024;
        size_t max_array_length = 1000000;
        size_t max_map_size = 1000000;
        bool require_utf8_strings = true;
        bool check_cycles = true;
        bool collect_stats = false;
    };
    
    Validator(const Options& options = {});
    Validator(const Schema& schema, const Options& options = {});
    
    // Validation
    ValidationResult validate(const Value& value) const;
    ValidationResult validate(std::span<const uint8_t> data) const;
    
    // Type validation
    bool is_valid_type(const Value& value, const std::string& type) const;
};
```

### ValidationResult
```cpp
struct ValidationResult {
    bool valid;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    
    struct Stats {
        size_t max_depth_seen;
        size_t total_nodes;
        size_t total_bytes;
        std::unordered_map<std::string, size_t> type_counts;
    };
    
    std::optional<Stats> stats;
};
```

## Advanced Types

### Graph Operations

```cpp
namespace btoon::graph {

template<typename NodeId, typename NodeData, typename Weight>
class Graph {
public:
    // Node operations
    void add_node(const NodeId& id, const NodeData& data = {});
    void remove_node(const NodeId& id);
    bool has_node(const NodeId& id) const;
    
    // Edge operations
    void add_edge(const NodeId& from, const NodeId& to, Weight weight = 1);
    void remove_edge(const NodeId& from, const NodeId& to);
    bool has_edge(const NodeId& from, const NodeId& to) const;
    
    // Algorithms
    std::vector<NodeId> topological_sort() const;
    std::unordered_map<NodeId, Weight> dijkstra(const NodeId& source) const;
    std::vector<std::vector<NodeId>> connected_components() const;
    
    // Minimum spanning tree
    std::vector<Edge<NodeId, Weight>> kruskal_mst() const;
    std::vector<Edge<NodeId, Weight>> prim_mst(const NodeId& start) const;
    
    // Metrics
    double clustering_coefficient(const NodeId& node) const;
    size_t diameter() const;
    double density() const;
    
    // Serialization
    Value to_btoon() const;
    static Graph from_btoon(const Value& value);
};

// Property graph for complex relationships
class PropertyGraph {
public:
    struct PropertyNode {
        std::string id;
        std::string label;
        std::unordered_map<std::string, Value> properties;
    };
    
    struct PropertyEdge {
        std::string id;
        std::string from;
        std::string to;
        std::string type;
        std::unordered_map<std::string, Value> properties;
    };
    
    // CRUD operations
    void add_node(const PropertyNode& node);
    void add_edge(const PropertyEdge& edge);
    
    // Query operations
    std::vector<PropertyNode> query_nodes(
        const std::unordered_map<std::string, Value>& criteria) const;
    std::vector<PropertyEdge> query_edges(
        const std::unordered_map<std::string, Value>& criteria) const;
};

} // namespace btoon::graph
```

### Time-Series

```cpp
namespace btoon::timeseries {

template<typename T>
class TimeSeries {
public:
    // Data insertion
    void append(const Timestamp& timestamp, const T& value);
    void append_batch(const std::vector<DataPoint<T>>& points);
    
    // Time range queries
    std::vector<DataPoint<T>> range(const Timestamp& start,
                                   const Timestamp& end) const;
    
    // Aggregation
    T aggregate(AggregationFunction func,
               const Timestamp& start = {},
               const Timestamp& end = {}) const;
    
    // Resampling
    TimeSeries<T> resample(std::chrono::milliseconds interval,
                          AggregationFunction func = AggregationFunction::MEAN) const;
    
    // Downsampling
    TimeSeries<T> downsample(size_t target_points,
                            DownsamplingMethod method = DownsamplingMethod::LTTB) const;
    
    // Moving window operations
    TimeSeries<T> moving_average(size_t window_size) const;
    TimeSeries<double> moving_stddev(size_t window_size) const;
    
    // Transformations
    TimeSeries<T> diff(size_t lag = 1) const;
    TimeSeries<double> pct_change() const;
    
    // Statistics
    T mean() const;
    T median() const;
    double stddev() const;
    double correlation(const TimeSeries<T>& other) const;
    
    // Outlier detection
    std::vector<size_t> detect_outliers_zscore(double threshold = 3.0) const;
    
    // Compression
    std::vector<uint8_t> compress(TimeSeriesCompression method = TimeSeriesCompression::GORILLA) const;
    static TimeSeries<T> decompress(const std::vector<uint8_t>& data,
                                   TimeSeriesCompression method);
};

// Circular buffer for streaming
template<typename T>
class CircularTimeSeries {
public:
    CircularTimeSeries(size_t max_size);
    
    void append(const Timestamp& timestamp, const T& value);
    std::vector<DataPoint<T>> latest(size_t n) const;
    
    // Real-time statistics
    T current_mean() const;
    double current_stddev() const;
    bool is_anomaly(const T& value, double threshold = 3.0) const;
};

} // namespace btoon::timeseries
```

## Batch Processing

```cpp
namespace btoon::batch {

// Parallel batch processor
template<typename Input, typename Output>
class ParallelBatchProcessor {
public:
    using ProcessFunc = std::function<Output(const Input&)>;
    
    ParallelBatchProcessor(ProcessFunc func,
                         const BatchOptions& options = {});
    
    std::vector<Output> process(const std::vector<Input>& items);
    
    void set_error_handler(
        std::function<void(const Input&, const std::exception&)> handler);
    
    const BatchStatistics& statistics() const;
};

// Map-Reduce processor
template<typename Key, typename Value, typename Result>
class MapReduceProcessor {
public:
    using MapFunc = std::function<std::pair<Key, Value>(const btoon::Value&)>;
    using ReduceFunc = std::function<Result(const Key&, const std::vector<Value>&)>;
    
    MapReduceProcessor(MapFunc mapper, ReduceFunc reducer,
                      const BatchOptions& options = {});
    
    std::unordered_map<Key, Result> process(const std::vector<btoon::Value>& data);
};

// Window-based processor
template<typename T>
class WindowProcessor {
public:
    enum WindowType {
        TUMBLING,   // Non-overlapping
        SLIDING,    // Overlapping
        SESSION     // Gap-based
    };
    
    WindowProcessor(WindowType type, size_t window_size,
                   size_t slide_interval = 0);
    
    template<typename ProcessFunc>
    void process(const std::vector<T>& items, ProcessFunc func);
};

} // namespace btoon::batch
```

## Language Bindings

### Python API

```python
import btoon

# Basic encoding/decoding
encoded = btoon.encode(data, compression=btoon.CompressionAlgorithm.LZ4)
decoded = btoon.decode(encoded)

# Schema management
schema = btoon.SchemaBuilder() \
    .name("User") \
    .field("id", "integer", required=True) \
    .field("email", "string", pattern=r"^[^@]+@[^@]+$") \
    .build()

# Validation
validator = btoon.Validator(schema)
result = validator.validate(data)

# Time-series
ts = btoon.TimeSeries()
ts.append(btoon.Timestamp.now(), 100.5)
ma = ts.moving_average(10)

# DataFrames
import pandas as pd
df = pd.DataFrame(data)
btoon_data = btoon.from_dataframe(df, use_tabular=True)

# Async streaming
async with btoon.async_stream('localhost', 8888, 'w') as stream:
    await stream.write({'key': 'value'})

# Context managers
with btoon.open_btoon('data.btoon', 'w', compression=btoon.CompressionAlgorithm.ZSTD) as f:
    f.write(data)
```

### JavaScript API

```javascript
const btoon = require('btoon');

// Basic encoding/decoding
const encoded = btoon.encode(data, {
    compression: btoon.CompressionAlgorithm.LZ4
});
const decoded = btoon.decode(encoded);

// Schema validation
const schema = new btoon.SchemaBuilder()
    .name("User")
    .field("id", "integer", { required: true })
    .field("email", "string", { pattern: "^[^@]+@[^@]+$" })
    .build();

const validator = new btoon.Validator(schema);
const result = validator.validate(data);

// Streaming
const stream = new btoon.StreamDecoder();
stream.on('data', (value) => {
    console.log('Received:', value);
});

// TypeScript support
import { Value, Timestamp, encode, decode } from 'btoon';

const timestamp: Timestamp = Timestamp.now();
const encoded: Uint8Array = encode(data);
```

### C API (FFI)

```c
#include <btoon/capi.h>

// Create value
btoon_value_t value = btoon_create_map();
btoon_map_insert(value, "key", btoon_create_string("value"));

// Encode
size_t size;
uint8_t* encoded = btoon_encode(value, &size, NULL);

// Decode
btoon_value_t decoded = btoon_decode(encoded, size, NULL);

// Cleanup
btoon_value_free(value);
btoon_value_free(decoded);
free(encoded);

// Zero-copy operations
btoon_memory_view_t view = btoon_mmap_file("data.btoon");
const uint8_t* data = btoon_memory_view_data(view);
size_t data_size = btoon_memory_view_size(view);
btoon_munmap_file(view);
```

## Error Handling

### BtoonException
```cpp
class BtoonException : public std::runtime_error {
public:
    explicit BtoonException(const std::string& message);
    
    // Error codes
    enum ErrorCode {
        INVALID_FORMAT,
        BUFFER_OVERFLOW,
        UNSUPPORTED_TYPE,
        COMPRESSION_ERROR,
        VALIDATION_ERROR,
        SCHEMA_ERROR
    };
    
    ErrorCode code() const;
};
```

### Error Handling Example
```cpp
try {
    auto value = decode(data);
} catch (const BtoonException& e) {
    std::cerr << "BTOON error: " << e.what() << std::endl;
    if (e.code() == BtoonException::INVALID_FORMAT) {
        // Handle format error
    }
} catch (const std::exception& e) {
    std::cerr << "General error: " << e.what() << std::endl;
}
```

## Performance Tips

### Memory Management
```cpp
// Use memory pool for repeated operations
MemoryPool pool(1024 * 1024);  // 1MB pool
Encoder encoder(&pool);

for (const auto& item : large_dataset) {
    encoder.reset();
    encoder.encode(item);
    process(encoder.data());
}
```

### Batch Operations
```cpp
// Process in parallel
ParallelBatchProcessor<Input, Output> processor(
    [](const Input& in) { return transform(in); },
    {.worker_threads = 8, .batch_size = 1000}
);

auto results = processor.process(data);
```

### Compression Selection
```cpp
// Let BTOON choose optimal compression
EncodeOptions options;
options.compress = true;
options.compression_algorithm = CompressionAlgorithm::AUTO;
options.compression_profile = CompressionProfile::network();

auto encoded = encode(value, options);
```

### Zero-Copy Operations
```cpp
// Use memory-mapped files
auto mmap = MemoryMappedFile::open("large_file.btoon");
MemoryView view = mmap->view();

// Decode without copying strings/binary
ZeroCopyDecoder decoder;
ValueView decoded = decoder.decode(view);
```

## Thread Safety

- **Encoder/Decoder**: Not thread-safe. Create one per thread.
- **MemoryPool**: Thread-safe with internal locking.
- **Schema/SchemaRegistry**: Read-only operations are thread-safe.
- **ParallelBatchProcessor**: Thread-safe, handles internal threading.
- **Value types**: Immutable operations are thread-safe.

## Best Practices

1. **Use appropriate compression**: 
   - LZ4 for real-time data
   - ZSTD for network transmission
   - Brotli for long-term storage

2. **Enable tabular encoding for structured data**:
   ```cpp
   EncodeOptions opts;
   opts.use_tabular = true;
   auto encoded = encode(table_data, opts);
   ```

3. **Validate untrusted input**:
   ```cpp
   Validator validator({.max_depth = 50, .max_size = 10*1024*1024});
   auto result = validator.validate(untrusted_data);
   if (!result.valid) {
       // Handle validation errors
   }
   ```

4. **Use schema versioning**:
   ```cpp
   SchemaRegistry registry;
   registry.register_schema(schema_v1);
   registry.register_schema(schema_v2);
   registry.register_migration("User", "1.0", "2.0", migrate_func);
   ```

5. **Profile your use case**:
   ```cpp
   // Test different compression algorithms
   for (auto algo : {LZ4, ZSTD, BROTLI}) {
       auto start = std::chrono::high_resolution_clock::now();
       auto encoded = compress(algo, data);
       auto end = std::chrono::high_resolution_clock::now();
       // Log timing and size
   }
   ```

---

For more examples and detailed documentation, visit:
- [GitHub Repository](https://github.com/BTOON-project/btoon-core)
- [Online Documentation](https://btoon.readthedocs.io)

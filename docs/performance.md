# BTOON Performance Tuning Guide

BTOON is designed for high performance out of the box, but there are several ways you can tune its behavior to achieve optimal speed and efficiency for your specific use case. This guide covers key areas for performance tuning.

## 1. Choosing the Right Data Types

The types you choose for your data can have a significant impact on both the size of the encoded output and the speed of encoding/decoding.

*   **Integers:** Use the smallest integer type that can safely hold your data. For example, if you know a value will always be a small positive number, `btoon::Uint` might be more efficient than `btoon::Int`.
*   **Strings vs. Binary:** If you are storing arbitrary byte data (like an image), use `btoon::Binary` instead of `btoon::String`. This avoids any potential overhead from UTF-8 validation.
*   **Floating-Point Numbers:** While BTOON supports `double` precision floats, consider if `float` is sufficient for your needs. `VectorFloat` can be more compact than `VectorDouble`.

## 2. Leveraging the Tabular Data Format

BTOON's automatic tabular data optimization is one of its most powerful features for reducing data size, which in turn improves transmission and processing speed.

### How it Works

When you encode an array of maps (objects) that have the same keys, BTOON can switch to a columnar layout. Instead of storing the keys for every object, it stores the schema once and then packs the values for each column together.

**To ensure the tabular format is used:**
*   Structure your data as a `btoon::Array` of `btoon::Map`s.
*   Ensure all maps in the array have the **exact same set of keys**.
*   Keep the `auto_tabular` option in `EncodeOptions` enabled (it is `true` by default).

### When to Use It

The tabular format is most effective when you have a large number of objects with a consistent structure. Examples include:
*   Rows from a database query
*   Time-series data points
*   A list of users, products, or other entities

## 3. Compression Settings

BTOON supports several compression algorithms. The right choice depends on the trade-off you want to make between compression ratio and speed.

*   **No Compression:** For very small payloads or in CPU-bound applications where compression overhead is a concern, disabling compression might be fastest.
*   **LZ4:** This is a very fast compression algorithm that provides a moderate compression ratio. It's a great choice for real-time applications where speed is paramount.
*   **ZLIB:** This algorithm generally provides a better compression ratio than LZ4 but is slower. It's suitable for scenarios where data size is more critical than raw encoding speed, such as for data archival or transmission over slow networks.
*   **ZSTD:** Zstandard offers a wide range of compression levels, providing a flexible trade-off between speed and ratio. At lower levels, it can be as fast as LZ4 but with better compression. At higher levels, it can approach the compression ratio of ZLIB but with faster speeds.

### Tuning Compression Level

For algorithms that support it (like ZLIB and ZSTD), you can adjust the `compression_level`.
*   A **lower level** (e.g., 1) will be faster but offer less compression.
*   A **higher level** (e.g., 9 for ZLIB, 22 for ZSTD) will be slower but provide a better compression ratio.

Experiment with different algorithms and levels on your typical data to find the best balance for your application.

## 4. Memory Management

BTOON's decoder returns owning data types (e.g., `btoon::String`, `btoon::Binary`, `btoon::VectorFloat`). This design ensures memory safety by preventing dangling pointers, as the decoded `btoon::Value` manages the lifetime of its data.

While this is safer and simpler to use, be mindful of memory usage when decoding very large BTOON objects, as the entire data structure will be loaded into memory.

For applications that need to process extremely large datasets that may not fit in memory, consider a streaming approach. *Note: The BTOON streaming API is currently under development.*

## 5. Future Optimizations

The BTOON project is continuously working on performance improvements. Keep an eye on the following upcoming features:

*   **SIMD Optimizations:** We are expanding the use of SIMD (Single Instruction, Multiple Data) instructions to accelerate more encoding and decoding operations.
*   **Streaming API:** A full-featured streaming API will allow you to process large BTOON files or network streams without loading the entire dataset into memory.
*   **Custom Memory Allocators:** The ability to provide a custom memory pool/allocator will give you fine-grained control over memory usage, which can be beneficial in long-running or memory-constrained applications.

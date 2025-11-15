# BTOON Tutorial

Welcome to the BTOON tutorial! This guide will walk you through the basics of using the BTOON library to encode and decode data in your C++ applications.

## 1. Introduction

BTOON is a high-performance binary serialization format that extends MessagePack. It's designed to be fast, efficient, and easy to use, making it ideal for applications where performance and a small footprint are critical.

This tutorial will cover:
*   Including the BTOON header
*   Encoding and decoding basic data types (integers, strings, booleans)
*   Working with complex types like arrays and maps
*   Using the tabular format for efficient storage of structured data
*   Enabling compression and security features

## 2. Getting Started

To start using BTOON, you need to include the main header file in your C++ source file:

```cpp
#include <btoon/btoon.h>
```

All the core functionalities are available within the `btoon` namespace.

## 3. Basic Encoding and Decoding

Let's start with a simple example of encoding and decoding a `btoon::Value`. The `btoon::Value` is a `std::variant` that can hold any of the supported BTOON types.

### Example: Encoding and Decoding an Integer

Here's how you can encode an integer and then decode it back:

```cpp
#include <iostream>
#include "btoon/btoon.h"

int main() {
    // 1. Create a btoon::Value holding an integer
    btoon::Value my_value = btoon::Int(42);

    // 2. Encode the value into a byte vector
    std::vector<uint8_t> encoded_data = btoon::encode(my_value);

    // 3. Decode the byte vector back into a btoon::Value
    btoon::Value decoded_value = btoon::decode(encoded_data);

    // 4. Access the decoded value
    if (std::holds_alternative<btoon::Int>(decoded_value)) {
        int64_t result = std::get<btoon::Int>(decoded_value);
        std::cout << "Decoded integer: " << result << std::endl;
    }

    return 0;
}
```

### Other Basic Types

You can work with other basic types in a similar way:

*   **String:** `btoon::Value my_string = btoon::String("Hello, BTOON!");`
*   **Boolean:** `btoon::Value my_bool = btoon::Bool(true);`
*   **Nil (null):** `btoon::Value my_nil = btoon::Nil();`
*   **Float:** `btoon::Value my_float = btoon::Float(3.14159);`

## 4. Complex Types: Arrays and Maps

BTOON makes it easy to work with nested data structures like arrays and maps.

### Example: An Array of Mixed Types

```cpp
#include "btoon/btoon.h"

void array_example() {
    btoon::Array my_array = {
        btoon::Int(1),
        btoon::String("two"),
        btoon::Bool(true)
    };

    btoon::Value value = my_array;
    std::vector<uint8_t> encoded = btoon::encode(value);
    btoon::Value decoded = btoon::decode(encoded);

    // You can access array elements from the decoded value
    auto& decoded_arr = std::get<btoon::Array>(decoded);
    // ...
}
```

### Example: A Map with String Keys

```cpp
#include "btoon/btoon.h"

void map_example() {
    btoon::Map my_map = {
        {"key1", btoon::Int(123)},
        {"key2", btoon::String("value")}
    };

    btoon::Value value = my_map;
    std::vector<uint8_t> encoded = btoon::encode(value);
    btoon::Value decoded = btoon::decode(encoded);

    // Access map elements
    auto& decoded_map = std::get<btoon::Map>(decoded);
    // ...
}
```

## 5. Tabular Data

One of BTOON's key features is its efficient tabular data format. If you have an array of uniform objects (maps with the same keys), BTOON can automatically encode it in a columnar format, which can significantly reduce the data size.

This feature is enabled by default.

```cpp
#include "btoon/btoon.h"

void tabular_example() {
    btoon::Array tabular_data = {
        btoon::Map{{"id", btoon::Int(1)}, {"name", btoon::String("Alice")}},
        btoon::Map{{"id", btoon::Int(2)}, {"name", btoon::String("Bob")}}
    };

    btoon::Value value = tabular_data;
    std::vector<uint8_t> encoded = btoon::encode(value); // Encoded as tabular
    btoon::Value decoded = btoon::decode(encoded);
}
```

## 6. Compression and Security

BTOON provides options to compress the encoded data and to sign it for integrity verification.

### Compression

To enable compression, you can pass `EncodeOptions` to the `encode` function.

```cpp
#include "btoon/btoon.h"

void compression_example() {
    btoon::Value value = btoon::String("A long string that will compress well...");

    btoon::EncodeOptions options;
    options.compress = true;
    options.compression_algorithm = btoon::CompressionAlgorithm::ZLIB;
    options.compression_level = 9; // Max compression

    std::vector<uint8_t> compressed_data = btoon::encode(value, options);

    // Decoding is automatic
    btoon::Value decoded = btoon::decode(compressed_data);
}
```

### Security (HMAC Signing)

*Security features are still under development and this API is subject to change.*

You can sign and verify data using HMAC-SHA256.

```cpp
#include "btoon/btoon.h"
#include "btoon/security.h"

void security_example() {
    btoon::Value value = btoon::String("Important data");
    std::vector<uint8_t> key = {0x01, 0x02, 0x03, ...}; // Your secret key

    // Encoding with signing
    btoon::Security enc_security(key);
    btoon::Encoder encoder(enc_security);
    encoder.setSecurityEnabled(true);
    encoder.encode(value);
    auto signed_data = encoder.getBuffer();

    // Decoding with verification
    btoon::Security dec_security(key);
    btoon::Decoder decoder(dec_security);
    decoder.setSecurityEnabled(true);
    btoon::Value decoded = decoder.decode(signed_data);
}
```

This concludes the BTOON tutorial. For more detailed information, please refer to the API documentation.

#!/usr/bin/env python3
"""
BTOON Python Example
Demonstrates idiomatic Python API usage
"""

import btoon
import datetime
from pathlib import Path

def basic_example():
    """Basic encoding and decoding"""
    print("=== Basic Example ===")
    
    # Simple data
    data = {
        "name": "Alice",
        "age": 30,
        "active": True,
        "scores": [95.5, 87.3, 92.1],
        "metadata": {
            "created": datetime.datetime.now(),
            "tags": ["python", "btoon", "example"]
        }
    }
    
    # Encode to BTOON
    encoded = btoon.dumps(data)
    print(f"Encoded size: {len(encoded)} bytes")
    
    # Decode back
    decoded = btoon.loads(encoded)
    print(f"Decoded: {decoded}")
    
    # With compression
    compressed = btoon.dumps(data, compress=True)
    print(f"Compressed size: {len(compressed)} bytes")
    print(f"Compression ratio: {len(encoded)/len(compressed):.2f}x")
    print()


def schema_example():
    """Schema definition and validation"""
    print("=== Schema Example ===")
    
    # Create a schema using builder pattern
    user_schema = (btoon.SchemaBuilder("User")
        .version("0.0.1")
        .description("User profile schema")
        .required_field("id", "int")
        .required_field("username", "string")
        .optional_field("email", "string")
        .optional_field("age", "int")
        .field("active", "boolean", required=False, default_value=True)
        .build()
    )
    
    # Valid data
    user = {
        "id": 1,
        "username": "alice",
        "email": "alice@example.com",
        "age": 30
    }
    
    # Encode with schema validation
    encoder = btoon.Encoder()
    encoded = encoder.encode_with_schema(user, user_schema)
    print(f"Valid user encoded: {len(encoded)} bytes")
    
    # Invalid data (missing required field)
    invalid_user = {
        "username": "bob"
        # Missing required 'id' field
    }
    
    try:
        encoder.encode_with_schema(invalid_user, user_schema)
    except btoon.BtoonException as e:
        print(f"Schema validation failed: {e}")
    print()


def streaming_example():
    """Streaming large datasets"""
    print("=== Streaming Example ===")
    
    # Write streaming data
    output_file = Path("stream_example.btoon")
    
    with output_file.open("wb") as f:
        with btoon.StreamEncoder(f, compress=True) as encoder:
            for i in range(100):
                record = {
                    "id": i,
                    "timestamp": datetime.datetime.now().isoformat(),
                    "value": i * 1.5,
                    "tags": [f"tag{j}" for j in range(3)]
                }
                encoder.write(record)
    
    print(f"Wrote 100 records to {output_file}")
    
    # Read streaming data
    count = 0
    with output_file.open("rb") as f:
        with btoon.StreamDecoder(f) as decoder:
            for record in decoder:
                count += 1
                if count <= 3:  # Show first 3 records
                    print(f"Record {count}: {record}")
    
    print(f"Read {count} records")
    
    # Clean up
    output_file.unlink()
    print()


def context_manager_example():
    """Using context managers for encoding/decoding"""
    print("=== Context Manager Example ===")
    
    # Encoder context manager with options
    with btoon.encoder(compress=True, compression="lz4") as enc:
        data1 = enc.encode({"message": "Hello"})
        data2 = enc.encode({"message": "World"})
        print(f"Encoded 2 messages: {len(data1)} + {len(data2)} bytes")
    
    # Decoder context manager
    with btoon.decoder(strict=True) as dec:
        msg1 = dec.decode(data1)
        msg2 = dec.decode(data2)
        print(f"Decoded: {msg1}, {msg2}")
    print()


def pandas_integration():
    """Integration with pandas DataFrames"""
    print("=== Pandas Integration ===")
    
    try:
        import pandas as pd
        import numpy as np
        
        # Create a DataFrame
        df = pd.DataFrame({
            "id": range(10),
            "value": np.random.randn(10),
            "category": ["A", "B"] * 5
        })
        
        # Convert to records and encode
        records = df.to_dict("records")
        encoded = btoon.dumps(records, compress=True)
        print(f"DataFrame encoded: {len(encoded)} bytes")
        
        # Decode and convert back to DataFrame
        decoder = btoon.Decoder()
        decoded_df = decoder.decode_as_dataframe(encoded)
        print(f"Decoded DataFrame shape: {decoded_df.shape}")
        
    except ImportError:
        print("Pandas not installed, skipping DataFrame example")
    print()


def file_operations():
    """File I/O operations"""
    print("=== File Operations ===")
    
    data = {
        "config": {
            "version": "0.0.1",
            "settings": {
                "debug": False,
                "timeout": 30,
                "endpoints": ["api1.example.com", "api2.example.com"]
            }
        }
    }
    
    # Save to file
    config_file = Path("config.btoon")
    btoon.dump_file(data, config_file, compress=True)
    print(f"Saved to {config_file}")
    
    # Load from file
    loaded = btoon.load_file(config_file)
    print(f"Loaded: {loaded}")
    
    # Validate file
    validation = btoon.validate_file(config_file, strict=True)
    print(f"Validation: {validation}")
    
    # Convert to/from JSON
    json_str = btoon.to_json(btoon.dumps(data), indent=2)
    print(f"As JSON:\n{json_str[:100]}...")
    
    # Clean up
    config_file.unlink()
    print()


def main():
    """Run all examples"""
    print(f"BTOON Python Examples (v{btoon.__version__})")
    print("=" * 40)
    
    basic_example()
    schema_example()
    streaming_example()
    context_manager_example()
    pandas_integration()
    file_operations()
    
    print("All examples completed successfully!")


if __name__ == "__main__":
    main()

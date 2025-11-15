#!/usr/bin/env node
/**
 * BTOON JavaScript/Node.js Example
 * Demonstrates idiomatic JavaScript API usage
 */

const btoon = require('btoon');
const fs = require('fs').promises;
const path = require('path');

/**
 * Basic encoding and decoding
 */
async function basicExample() {
    console.log('=== Basic Example ===');
    
    // Simple data
    const data = {
        name: 'Alice',
        age: 30,
        active: true,
        scores: [95.5, 87.3, 92.1],
        metadata: {
            created: new Date(),
            tags: ['javascript', 'btoon', 'example']
        }
    };
    
    // Encode to BTOON
    const encoded = btoon.encode(data);
    console.log(`Encoded size: ${encoded.length} bytes`);
    
    // Decode back
    const decoded = btoon.decode(encoded);
    console.log('Decoded:', decoded);
    
    // With compression
    const compressed = btoon.encode(data, { compress: true });
    console.log(`Compressed size: ${compressed.length} bytes`);
    console.log(`Compression ratio: ${(encoded.length/compressed.length).toFixed(2)}x`);
    console.log();
}

/**
 * Schema definition and validation
 */
function schemaExample() {
    console.log('=== Schema Example ===');
    
    // Create a schema using builder pattern
    const userSchema = new btoon.SchemaBuilder('User')
        .version('0.0.1')
        .description('User profile schema')
        .requiredField('id', 'int')
        .requiredField('username', 'string')
        .optionalField('email', 'string')
        .optionalField('age', 'int')
        .field('active', 'boolean', false)
        .build();
    
    // Valid data
    const user = {
        id: 1,
        username: 'alice',
        email: 'alice@example.com',
        age: 30
    };
    
    // Encode with schema validation
    const encoder = new btoon.Encoder();
    const encoded = encoder.encodeWithSchema(user, userSchema);
    console.log(`Valid user encoded: ${encoded.length} bytes`);
    
    // Invalid data (missing required field)
    const invalidUser = {
        username: 'bob'
        // Missing required 'id' field
    };
    
    try {
        encoder.encodeWithSchema(invalidUser, userSchema);
    } catch (error) {
        console.log(`Schema validation failed: ${error.message}`);
    }
    console.log();
}

/**
 * Async/Promise based operations
 */
async function asyncExample() {
    console.log('=== Async Example ===');
    
    const data = {
        message: 'Hello from async',
        timestamp: Date.now()
    };
    
    // Simulate async encoding
    const encodeAsync = (value) => {
        return new Promise((resolve) => {
            setTimeout(() => {
                resolve(btoon.encode(value));
            }, 10);
        });
    };
    
    // Simulate async decoding
    const decodeAsync = (buffer) => {
        return new Promise((resolve) => {
            setTimeout(() => {
                resolve(btoon.decode(buffer));
            }, 10);
        });
    };
    
    const encoded = await encodeAsync(data);
    console.log(`Async encoded: ${encoded.length} bytes`);
    
    const decoded = await decodeAsync(encoded);
    console.log('Async decoded:', decoded);
    console.log();
}

/**
 * Streaming example
 */
function streamingExample() {
    console.log('=== Streaming Example ===');
    
    // Create stream encoder
    const encoder = new btoon.StreamEncoder({ compress: true });
    
    // Encode multiple records
    const records = [];
    for (let i = 0; i < 10; i++) {
        const record = {
            id: i,
            timestamp: new Date().toISOString(),
            value: i * 1.5,
            tags: [`tag${i}`, `tag${i+1}`]
        };
        
        const chunk = encoder.write(record);
        records.push(chunk);
        
        if (i < 3) {
            console.log(`Record ${i}: ${chunk.length} bytes`);
        }
    }
    
    console.log(`Encoded ${records.length} records`);
    
    // Decode streaming data
    const decoder = new btoon.StreamDecoder();
    let count = 0;
    
    for (const chunk of records) {
        decoder.feed(chunk);
        while (decoder.hasValue()) {
            const record = decoder.read();
            count++;
            if (count <= 3) {
                console.log(`Decoded record ${count}:`, record);
            }
        }
    }
    
    console.log(`Decoded ${count} records`);
    console.log();
}

/**
 * TypedArray and Buffer handling
 */
function binaryExample() {
    console.log('=== Binary Data Example ===');
    
    // Create binary data
    const buffer = new Uint8Array([1, 2, 3, 4, 5]);
    const float32Array = new Float32Array([1.5, 2.5, 3.5]);
    
    const data = {
        binary: buffer,
        floats: float32Array,
        mixed: {
            text: 'Binary example',
            data: new Uint8Array(16)
        }
    };
    
    // Encode
    const encoded = btoon.encode(data);
    console.log(`Binary data encoded: ${encoded.length} bytes`);
    
    // Decode
    const decoded = btoon.decode(encoded);
    console.log('Decoded binary:', decoded.binary);
    console.log('Decoded floats:', decoded.floats);
    console.log();
}

/**
 * File I/O operations
 */
async function fileOperations() {
    console.log('=== File Operations ===');
    
    const data = {
        config: {
            version: '0.0.1',
            settings: {
                debug: false,
                timeout: 30,
                endpoints: ['api1.example.com', 'api2.example.com']
            }
        }
    };
    
    const configFile = path.join(__dirname, 'config.btoon');
    
    try {
        // Save to file
        const encoded = btoon.encode(data, { compress: true });
        await fs.writeFile(configFile, encoded);
        console.log(`Saved to ${configFile}`);
        
        // Load from file
        const loaded = await fs.readFile(configFile);
        const decoded = btoon.decode(loaded);
        console.log('Loaded:', decoded);
        
        // Validate file
        const validator = new btoon.Validator();
        const validation = validator.validate(loaded);
        console.log('Validation:', validation);
        
        // Convert to/from JSON
        const json = btoon.toJSON(loaded);
        console.log('As JSON:', JSON.stringify(json, null, 2).substring(0, 100) + '...');
        
        // Clean up
        await fs.unlink(configFile);
    } catch (error) {
        console.error('File operation error:', error);
    }
    console.log();
}

/**
 * Custom type handling
 */
function customTypeExample() {
    console.log('=== Custom Type Example ===');
    
    // Custom type encoder for Set
    const customEncoder = (value) => {
        if (value instanceof Set) {
            return {
                type: 100, // Custom type ID
                data: btoon.encode(Array.from(value))
            };
        }
        return undefined;
    };
    
    // Custom type decoder
    const customDecoder = (extension) => {
        if (extension.type === 100) {
            const array = btoon.decode(extension.data);
            return new Set(array);
        }
        return extension;
    };
    
    // Data with custom types
    const data = {
        uniqueTags: new Set(['tag1', 'tag2', 'tag3']),
        regular: 'normal data'
    };
    
    // Encode with custom type handler
    const encoder = new btoon.Encoder({ typeEncoder: customEncoder });
    const encoded = encoder.encode(data);
    console.log(`Custom type encoded: ${encoded.length} bytes`);
    
    // Decode with custom type handler
    const decoder = new btoon.Decoder({ typeDecoder: customDecoder });
    const decoded = decoder.decode(encoded);
    console.log('Decoded Set:', decoded.uniqueTags);
    console.log();
}

/**
 * Performance comparison
 */
function performanceExample() {
    console.log('=== Performance Example ===');
    
    const data = {
        users: Array.from({ length: 100 }, (_, i) => ({
            id: i,
            name: `User ${i}`,
            email: `user${i}@example.com`,
            active: i % 2 === 0,
            score: Math.random() * 100
        }))
    };
    
    // BTOON encoding
    const btoonStart = Date.now();
    const btoonEncoded = btoon.encode(data);
    const btoonTime = Date.now() - btoonStart;
    
    // JSON encoding
    const jsonStart = Date.now();
    const jsonEncoded = JSON.stringify(data);
    const jsonTime = Date.now() - jsonStart;
    
    console.log(`BTOON: ${btoonEncoded.length} bytes in ${btoonTime}ms`);
    console.log(`JSON: ${jsonEncoded.length} bytes in ${jsonTime}ms`);
    console.log(`Size reduction: ${((1 - btoonEncoded.length/jsonEncoded.length) * 100).toFixed(1)}%`);
    console.log(`Speed improvement: ${(jsonTime/btoonTime).toFixed(2)}x`);
    console.log();
}

/**
 * Main function
 */
async function main() {
    console.log(`BTOON JavaScript Examples (v${btoon.version})`);
    console.log('='.repeat(40));
    
    await basicExample();
    schemaExample();
    await asyncExample();
    streamingExample();
    binaryExample();
    await fileOperations();
    customTypeExample();
    performanceExample();
    
    console.log('All examples completed successfully!');
}

// Run examples
if (require.main === module) {
    main().catch(console.error);
}

module.exports = { main };

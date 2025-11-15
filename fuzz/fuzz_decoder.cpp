/**
 * Fuzzing harness for BTOON decoder
 * 
 * Usage with libFuzzer:
 *   clang++ -g -fsanitize=fuzzer,address fuzz_decoder.cpp -o fuzz_decoder
 *   ./fuzz_decoder corpus/
 * 
 * Usage with AFL++:
 *   afl-c++ -g fuzz_decoder.cpp -o fuzz_decoder
 *   afl-fuzz -i corpus/ -o findings/ ./fuzz_decoder
 */

#include "btoon/btoon.h"
#include "btoon/decoder.h"
#include "btoon/validator.h"
#include <cstdint>
#include <cstddef>
#include <vector>
#include <span>

// For libFuzzer
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // Test with various sizes
    if (size == 0 || size > 1024 * 1024) {  // Skip empty or huge inputs
        return 0;
    }
    
    std::span<const uint8_t> input(data, size);
    
    // Test 1: Basic decoding
    try {
        btoon::Decoder decoder;
        btoon::Value value = decoder.decode(input);
        
        // If decode succeeds, try to re-encode
        auto encoded = btoon::encode(value);
        
        // Verify round-trip
        btoon::Value value2 = decoder.decode(encoded);
        
    } catch (const btoon::BtoonException&) {
        // Expected for invalid input
    } catch (const std::exception&) {
        // Also acceptable
    } catch (...) {
        // Unexpected exception type - this could be a bug
        return 1;
    }
    
    // Test 2: Validation
    try {
        btoon::ValidationOptions opts;
        opts.max_depth = 10;  // Restrict for fuzzing
        opts.max_total_size = 100000;
        opts.fast_mode = true;  // Speed up fuzzing
        
        btoon::Validator validator(opts);
        auto result = validator.validate(input);
        
        // If validation passes, decoding should succeed
        if (result.valid) {
            btoon::Decoder decoder;
            btoon::Value value = decoder.decode(input);
        }
        
    } catch (...) {
        // Validator should not throw
        return 1;
    }
    
    // Test 3: Decoder with options
    try {
        btoon::DecodeOptions opts;
        opts.auto_decompress = true;
        opts.strict = true;
        
        btoon::Decoder decoder(opts);
        btoon::Value value = decoder.decode(input);
        
    } catch (const btoon::BtoonException&) {
        // Expected
    } catch (...) {
        // Check for unexpected exceptions
    }
    
    // Test 4: Bounds checking
    try {
        btoon::BoundsChecker checker(input);
        size_t pos = 0;
        
        while (pos < size) {
            auto byte = checker.readByte(pos);
            if (!byte) break;
            
            // Try reading various sizes
            checker.readBytes(pos, 1);
            checker.readBytes(pos, 4);
            checker.readBytes(pos, 100);
            
            if (!checker.inBounds(pos)) break;
        }
        
    } catch (...) {
        // BoundsChecker should not throw
        return 1;
    }
    
    return 0;
}

// For AFL++
#ifdef __AFL_COMPILER
int main(int argc, char** argv) {
    // Read from stdin for AFL
    std::vector<uint8_t> buffer;
    uint8_t byte;
    while (std::cin.read(reinterpret_cast<char*>(&byte), 1)) {
        buffer.push_back(byte);
    }
    
    return LLVMFuzzerTestOneInput(buffer.data(), buffer.size());
}
#endif

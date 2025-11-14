#include "btoon/btoon.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>

// Helper function to print a byte vector in hex format
void print_hex(const std::vector<uint8_t>& data) {
    for (uint8_t byte : data) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') 
                  << static_cast<int>(byte) << " ";
    }
    std::cout << std::dec << "\n";
}

int main() {
    std::cout << "BTOON Basic Example (Unified API)\n";
    std::cout << "Library Version: " << btoon::version() << "\n\n";

    // 1. Construct a complex btoon::Value object
    btoon::Value original_data = btoon::Map{
        {"message", btoon::String("Hello, Unified BTOON!")},
        {"count", btoon::Int(123)},
        {"active", btoon::Bool(true)},
        {"values", btoon::Array{
            btoon::Int(1),
            btoon::Int(2),
            btoon::String("three")
        }}
    };

    std::cout << "--- Original Data ---" << std::endl;
    // Note: Printing the variant requires a visitor, which is too verbose for a simple example.
    // We will verify the contents after decoding.
    std::cout << "A map containing a string, an integer, a boolean, and an array.\n\n";

    try {
        // 2. Encode the Value object
        std::cout << "--- Encoding ---" << std::endl;
        std::vector<uint8_t> encoded_data = btoon::encode(original_data);
        std::cout << "Encoded successfully (" << encoded_data.size() << " bytes):" << std::endl;
        print_hex(encoded_data);
        std::cout << "\n";

        // 3. Decode the binary data back into a Value object
        std::cout << "--- Decoding ---" << std::endl;
        btoon::Value decoded_data = btoon::decode(encoded_data);
        std::cout << "Decoded successfully." << std::endl;

        // 4. Verify the decoded data
        std::cout << "\n--- Verification ---" << std::endl;
        
        auto* map = std::get_if<btoon::Map>(&decoded_data);
        if (!map) {
            throw std::runtime_error("Decoded data is not a map!");
        }

        auto* message = std::get_if<btoon::String>(&(*map)["message"]);
        if (!message || *message != "Hello, Unified BTOON!") {
            throw std::runtime_error("Verification failed: 'message' field is incorrect.");
        }
        std::cout << "Message field: OK" << std::endl;

        auto* count = std::get_if<btoon::Int>(&(*map)["count"]);
        if (!count || *count != 123) {
            throw std::runtime_error("Verification failed: 'count' field is incorrect.");
        }
        std::cout << "Count field: OK" << std::endl;

        auto* active = std::get_if<btoon::Bool>(&(*map)["active"]);
        if (!active || *active != true) {
            throw std::runtime_error("Verification failed: 'active' field is incorrect.");
        }
        std::cout << "Active field: OK" << std::endl;

        std::cout << "\nRound-trip successful!" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "An error occurred: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
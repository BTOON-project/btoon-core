#include "btoon/btoon.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>

int main() {
    std::cout << "BTOON Tabular Optimization Example\n";
    std::cout << "Library Version: " << btoon::version() << "\n\n";

    // 1. Create a vector of uniform maps (a tabular dataset)
    btoon::Array users;
    for (int i = 1; i <= 100; ++i) {
        users.push_back(btoon::Map{
            {"id", btoon::Int(i)},
            {"name", btoon::String("User " + std::to_string(i))},
            {"email", btoon::String("user" + std::to_string(i) + "@example.com")},
            {"active", btoon::Bool(i % 2 == 0)}
        });
    }
    btoon::Value tabular_data = users;

    std::cout << "Created a dataset of " << users.size() << " user records.\n";

    // 2. Verify that the data is indeed tabular
    if (btoon::is_tabular(users)) {
        std::cout << "Dataset is verified as tabular.\n\n";
    } else {
        std::cerr << "Error: Dataset is not tabular, aborting." << std::endl;
        return 1;
    }

    try {
        // 3. Encode the data using standard MessagePack encoding
        btoon::EncodeOptions standard_options;
        standard_options.auto_tabular = false; // Explicitly disable tabular optimization
        
        std::vector<uint8_t> standard_encoded = btoon::encode(tabular_data, standard_options);

        // 4. Encode the same data using BTOON's tabular optimization
        btoon::EncodeOptions tabular_options;
        tabular_options.auto_tabular = true; // Enable tabular optimization (default)

        std::vector<uint8_t> tabular_encoded = btoon::encode(tabular_data, tabular_options);

        // 5. Compare the results
        std::cout << "--- Size Comparison ---\n";
        std::cout << "Standard MessagePack size: " << standard_encoded.size() << " bytes\n";
        std::cout << "BTOON Tabular size:        " << tabular_encoded.size() << " bytes\n\n";

        if (tabular_encoded.size() < standard_encoded.size()) {
            size_t savings = standard_encoded.size() - tabular_encoded.size();
            double percentage = 100.0 * savings / standard_encoded.size();
            std::cout << "Size savings with tabular optimization: " << savings << " bytes ("
                      << std::fixed << std::setprecision(1) << percentage << "%)\n";
        } else {
            std::cout << "Tabular optimization did not result in size savings for this dataset.\n";
        }

        // 6. Verify round-trip for tabular encoding
        btoon::Value decoded_data = btoon::decode(tabular_encoded);
        auto* decoded_array = std::get_if<btoon::Array>(&decoded_data);
        if (decoded_array && decoded_array->size() == users.size()) {
             std::cout << "\nTabular round-trip successful: Decoded " << decoded_array->size() << " records.\n";
        } else {
            throw std::runtime_error("Tabular round-trip failed: Decoded data is not a valid array or size mismatch.");
        }

    } catch (const std::exception& e) {
        std::cerr << "An error occurred: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
#include <cstdint>
#include <cstddef>
#include "btoon/decoder.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size == 0) {
        return 0;
    }
    
    try {
        btoon::Decoder decoder;
        std::vector<uint8_t> buffer(data, data + size);
        decoder.decode(buffer);
    } catch (const btoon::BtoonException&) {
        // Expected for invalid input
    } catch (...) {
        // Unexpected exception
    }
    
    return 0;
}


#include "btoon/btoon.h"
#include <cstdint>
#include <stddef.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    try {
        btoon::decode({Data, Size});
    } catch (const btoon::BtoonException& e) {
        // Ignore exceptions, as the fuzzer will generate invalid data.
    }
    return 0;
}
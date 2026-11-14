// SPDX-FileCopyrightText: 2025 Alvar Laigna <https://alvarlaigna.com>
// SPDX-License-Identifier: MIT
//
// Every public header must compile where min and max are function-like macros, as they
// are after <windows.h> without NOMINMAX. The library builds its own sources with
// NOMINMAX, but that definition is private: consumers don't get it (review 2026-11-13).
//
// The standard and platform headers come first, so the macros only reach btoon's headers.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <stack>
#include <stdbool.h>
#include <stddef.h>
#include <stdexcept>
#include <stdint.h>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>
#if defined(_WIN32) || defined(_WIN64)
#include <intrin.h>
#elif defined(__APPLE__)
#include <libkern/OSByteOrder.h>
#elif defined(__linux__) || defined(__unix__)
#include <endian.h>
#endif

#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))

#include "btoon/btoon.h"
#include "btoon/capi.h"
#include "btoon/compression.h"
#include "btoon/decoder.h"
#include "btoon/delta_codec.h"
#include "btoon/dictionary_codec.h"
#include "btoon/encoder.h"
#include "btoon/memory_pool.h"
#include "btoon/rle_codec.h"
#include "btoon/schema.h"
#include "btoon/security.h"
#include "btoon/stream_decoder.h"
#include "btoon/stream_encoder.h"
#include "btoon/utils.h"
#include "btoon/validator.h"
#include "btoon/zero_copy.h"

#undef min
#undef max

#include <gtest/gtest.h>

TEST(PublicHeaders, CompileWithWindowsMinMaxMacros) {
    // The default arguments are the declarations that used to break: an unparenthesized
    // std::numeric_limits<int64_t>::min() expands the min macro.
    EXPECT_TRUE(btoon::TypeValidator::validateInt(-5));
    EXPECT_TRUE(btoon::TypeValidator::validateUint(5));
    EXPECT_FALSE(btoon::TypeValidator::validateInt(5, 6));
}

// SPDX-FileCopyrightText: 2025 Alvar Laigna <https://alvarlaigna.com>
// SPDX-License-Identifier: MIT
//
// The installed modules around the codec: MemoryPool, the zero-copy buffers and the
// schema registry (review 2026-11-13, fixed in 0.3.0).
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <new>
#include <random>
#include <type_traits>
#include <utility>
#include <vector>

#include "btoon/memory_pool.h"
#include "btoon/schema.h"
#include "btoon/zero_copy.h"

using namespace btoon;

// MemoryMappedFile and SharedMemoryBuffer own their mapping, and the destructor unmaps it.
// They were implicitly copyable, so a copy unmapped the region under the original (and
// freed SharedMemoryBuffer's name twice).
static_assert(!std::is_copy_constructible_v<MemoryMappedFile>);
static_assert(!std::is_copy_assignable_v<MemoryMappedFile>);
static_assert(!std::is_copy_constructible_v<SharedMemoryBuffer>);
static_assert(!std::is_copy_assignable_v<SharedMemoryBuffer>);

TEST(ZeroCopyFixes, OwningClassesAreNotCopyable) {
    // The static_asserts above are the test; this keeps the suite visible in the report.
    SUCCEED();
}

// A pool buffer only went back to the pool if it wasn't empty, and get_buffer() hands out
// empty (reserved) buffers: every buffer leaked from the pool's accounting.
TEST(ZeroCopyFixes, BufferPoolGetsItsBuffersBack) {
    BufferPool pool(4, 64);
    for (int i = 0; i < 3; ++i) {
        auto b = pool.get_buffer(128);
        EXPECT_GE(b.capacity(), 128u);
    }
    auto s = pool.stats();
    EXPECT_EQ(s.buffers_in_use, 0u);
    EXPECT_EQ(s.buffers_cached, 1u);
    EXPECT_EQ(s.cache_misses, 1u);
    EXPECT_EQ(s.cache_hits, 2u);
    {
        auto a = pool.get_buffer(10);
        auto b = std::move(a);  // the moved-from buffer gives nothing back
        EXPECT_EQ(pool.stats().buffers_in_use, 1u);
        auto c = pool.get_buffer(10);
        c = std::move(b);  // assignment returns c's own buffer first
        EXPECT_EQ(pool.stats().buffers_in_use, 1u);
    }
    EXPECT_EQ(pool.stats().buffers_in_use, 0u);

    ZeroCopyEncoder enc;
    auto pooled = enc.encode_pooled(Value(Array{Int(1), String("x")}), pool);
    EXPECT_EQ(std::vector<uint8_t>(pooled.data(), pooled.data() + pooled.size()),
              encode(Value(Array{Int(1), String("x")})));
    uint8_t small[2];
    try {
        enc.encode_into(Value(String("too long")), small, sizeof small);
        ADD_FAILURE() << "encode_into overflowed its buffer";
    } catch (const BtoonException& e) {
        EXPECT_EQ(e.code(), ErrorCode::size_limit);
    }
}

// allocate(SIZE_MAX - 16) rounded its size up past SIZE_MAX, wrapped to a few bytes, and
// returned memory that the next allocation overlapped.
TEST(MemoryPoolFixes, HugeRequestsThrowInsteadOfWrapping) {
    MemoryPool pool;
    EXPECT_THROW(pool.allocate(SIZE_MAX - 16), std::bad_alloc);
    EXPECT_THROW(pool.allocate(SIZE_MAX), std::bad_alloc);
    auto* a = static_cast<uint8_t*>(pool.allocate(64));
    auto* b = static_cast<uint8_t*>(pool.allocate(64));
    EXPECT_TRUE(a + 64 <= b || b + 64 <= a);
    EXPECT_NE(pool.allocate(0), pool.allocate(0)) << "zero-size allocations are distinct";
}

// Free chunks were reused and split at unaligned sizes, so the promised 32-byte alignment
// broke after the first deallocate().
TEST(MemoryPoolFixes, EveryAllocationIsAlignedAndDistinct) {
    auto aligned = [](const void* p) { return reinterpret_cast<uintptr_t>(p) % MemoryPool::SIMD_ALIGNMENT == 0; };
    {
        // The review's sequence.
        MemoryPool pool;
        void* a = pool.allocate(100);
        pool.deallocate(a, 100);
        void* b = pool.allocate(10);
        void* c = pool.allocate(32);
        EXPECT_TRUE(aligned(b));
        EXPECT_TRUE(aligned(c));
    }
    MemoryPool pool(1024);
    std::vector<std::pair<uint8_t*, size_t>> live;
    std::mt19937 rng(3);
    for (int i = 0; i < 3000; ++i) {
        if (!live.empty() && rng() % 3 == 0) {
            const size_t idx = rng() % live.size();
            pool.deallocate(live[idx].first, live[idx].second);
            live.erase(live.begin() + static_cast<std::ptrdiff_t>(idx));
            continue;
        }
        const size_t n = rng() % 300;
        auto* p = static_cast<uint8_t*>(pool.allocate(n));
        ASSERT_TRUE(aligned(p)) << "allocation " << i << " of " << n << " bytes";
        std::memset(p, i & 0xff, n);  // ASan catches a chunk that runs past its block
        live.emplace_back(p, n);
    }
    std::sort(live.begin(), live.end());
    for (size_t i = 1; i < live.size(); ++i) {
        ASSERT_LE(live[i - 1].first + live[i - 1].second, live[i].first) << "live allocations overlap";
    }
}

// Four SchemaRegistry methods were declared in schema.h but never defined, so calling one
// failed to link. They were removed in 0.3.0; every remaining method links and works.
TEST(SchemaRegistryApi, EveryMethodIsDefinedAndWorks) {
    SchemaRegistry registry;
    auto v1 = SchemaBuilder("user").version(1).field("id", "int").build();
    auto v2 = SchemaBuilder("user").version(2).field("id", "int").optionalField("nick", "string").build();
    registry.registerSchema(v1);
    registry.registerSchema("user", v2);
    EXPECT_EQ(registry.getSchema("user"), v2);
    EXPECT_EQ(registry.getSchema("user", SchemaVersion(1)), v1);
    EXPECT_EQ(registry.getLatestSchema("user"), v2);
    EXPECT_EQ(registry.getVersions("user").size(), 2u);
    EXPECT_TRUE(registry.validate(Map{{"id", Int(1)}}));
    EXPECT_FALSE(registry.validate(Map{{"id", String("x")}}));

    const Value stored = registry.toValue();
    SchemaRegistry loaded = SchemaRegistry::fromValue(decode(encode(stored)));
    EXPECT_EQ(loaded.getVersions("user").size(), 2u);
    EXPECT_TRUE(loaded.getSchema("user", SchemaVersion(2))->getField("nick").has_value());

    registry.removeSchema("user", SchemaVersion(2));
    EXPECT_EQ(registry.getLatestSchema("user"), v1);
    registry.removeSchema("user");
    EXPECT_EQ(registry.getSchema("user"), nullptr);
    loaded.clear();
    EXPECT_TRUE(loaded.getVersions("user").empty());
}

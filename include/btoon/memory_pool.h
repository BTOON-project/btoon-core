//  ██████╗ ████████╗ ██████╗  ██████╗ ███╗   ██╗
//  ██╔══██╗╚══██╔══╝██╔═══██╗██╔═══██╗████╗  ██║
//  ██████╔╝   ██║   ██║   ██║██║   ██║██╔██╗ ██║
//  ██╔══██╗   ██║   ██║   ██║██║   ██║██║╚██╗██║
//  ██████╔╝   ██║   ╚██████╔╝╚██████╔╝██║ ╚████║
//  ╚═════╝    ╚═╝    ╚═════╝  ╚═════╝ ╚═╝  ╚═══╝
//
//  BTOON Core
//  https://btoon.net & https://github.com/BTOON-project/btoon-core
//
// SPDX-FileCopyrightText: 2025 Alvar Laigna <https://alvarlaigna.com>
// SPDX-License-Identifier: MIT

#ifndef BTOON_MEMORY_POOL_H
#define BTOON_MEMORY_POOL_H

#include <cstddef>
#include <cstdlib>
#include <vector>
#include <memory>
#include <stack>
#include <cstdint>

namespace btoon {

/**
 * @brief Arena allocator with 32-byte aligned allocations and a free list.
 *
 * - Every pointer it returns is aligned to SIMD_ALIGNMENT (32 bytes, for AVX2).
 * - Sizes are rounded up to that alignment; a size so large that rounding would
 *   overflow throws std::bad_alloc.
 * - Freed chunks are reused through a free list.
 * - Not thread-safe. The library doesn't use it: Encoder and Decoder accept a pool only
 *   for source compatibility and ignore it.
 */
class MemoryPool {
public:
    static constexpr size_t SIMD_ALIGNMENT = 32;  // 32-byte alignment for AVX2
    static constexpr size_t DEFAULT_BLOCK_SIZE = 65536;  // 64KB default block

    explicit MemoryPool(size_t initial_size = DEFAULT_BLOCK_SIZE);
    ~MemoryPool();

    // Non-copyable, non-movable
    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;
    MemoryPool(MemoryPool&&) = delete;
    MemoryPool& operator=(MemoryPool&&) = delete;

    /**
     * @brief Allocate aligned memory from the pool
     * @param size Number of bytes to allocate
     * @return Pointer to allocated memory (aligned to SIMD_ALIGNMENT), distinct from
     *         every other live allocation, also for size 0
     * @throws std::bad_alloc if the memory can't be allocated
     */
    void* allocate(size_t size);
    
    /**
     * @brief Return memory to the pool for reuse
     * @param ptr Pointer returned by allocate()
     * @param size The size passed to allocate()
     */
    void deallocate(void* ptr, size_t size);
    
    /**
     * @brief Get total memory allocated by the pool
     */
    size_t total_allocated() const { return total_allocated_; }
    
    /**
     * @brief Get current memory in use
     */
    size_t current_usage() const { return current_usage_; }
    
    /**
     * @brief Reset the pool (keeps allocated blocks but marks them as free)
     */
    void reset();

private:
    struct Block {
        std::unique_ptr<uint8_t[], decltype(&std::free)> data;
        size_t size;
        
        Block(size_t sz);
    };
    
    struct FreeChunk {
        void* ptr;
        size_t size;
        
        FreeChunk(void* p, size_t s) : ptr(p), size(s) {}
    };
    
    void* allocate_from_block(size_t size);
    void new_block(size_t min_size);
    void* align_ptr(void* ptr, size_t alignment);
    
    std::vector<Block> blocks_;
    std::stack<FreeChunk> free_list_;
    
    uint8_t* current_pos_;
    size_t remaining_;
    size_t block_size_;
    size_t total_allocated_;
    size_t current_usage_;
};

} // namespace btoon

#endif // BTOON_MEMORY_POOL_H
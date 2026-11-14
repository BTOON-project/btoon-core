// SPDX-FileCopyrightText: 2025 Alvar Laigna <https://alvarlaigna.com>
// SPDX-License-Identifier: MIT
#include "btoon/memory_pool.h"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>

namespace btoon {

// Aligned allocation for C++11/14 compatibility
static void* aligned_alloc_compat(size_t alignment, size_t size) {
#if defined(_WIN32)
    return _aligned_malloc(size, alignment);
#elif defined(__APPLE__) || defined(__linux__)
    void* ptr = nullptr;
    if (posix_memalign(&ptr, alignment, size) == 0) {
        return ptr;
    }
    return nullptr;
#else
    // Fallback: over-allocate and manually align
    size_t total = size + alignment - 1 + sizeof(void*);
    void* raw = std::malloc(total);
    if (!raw) return nullptr;
    
    void** aligned = reinterpret_cast<void**>(
        (reinterpret_cast<uintptr_t>(raw) + sizeof(void*) + alignment - 1) & ~(alignment - 1)
    );
    aligned[-1] = raw;  // Store original pointer for freeing
    return aligned;
#endif
}

static void aligned_free_compat(void* ptr) noexcept {
#if defined(_WIN32)
    _aligned_free(ptr);
#elif defined(__APPLE__) || defined(__linux__)
    std::free(ptr);
#else
    if (ptr) {
        void** aligned = reinterpret_cast<void**>(ptr);
        std::free(aligned[-1]);
    }
#endif
}

MemoryPool::Block::Block(size_t sz) 
    : data(static_cast<uint8_t*>(aligned_alloc_compat(SIMD_ALIGNMENT, sz)), 
           &aligned_free_compat),
      size(sz) {
    if (!data) {
        throw std::bad_alloc();
    }
}

MemoryPool::MemoryPool(size_t initial_size)
    : current_pos_(nullptr),
      remaining_(0),
      block_size_(initial_size),
      total_allocated_(0),
      current_usage_(0) {
    new_block(initial_size);
}

MemoryPool::~MemoryPool() {
    // Blocks are automatically freed by unique_ptr
}

namespace {
// Rounds up to a multiple of the alignment, so every chunk handed out, returned and split
// starts on an aligned address. 0 rounds to one unit: each allocation is distinct.
size_t aligned_size(size_t size) {
    if (size > SIZE_MAX - (MemoryPool::SIMD_ALIGNMENT - 1)) throw std::bad_alloc();
    const size_t rounded = (size + MemoryPool::SIMD_ALIGNMENT - 1) & ~(MemoryPool::SIMD_ALIGNMENT - 1);
    return rounded == 0 ? MemoryPool::SIMD_ALIGNMENT : rounded;
}
}  // namespace

void* MemoryPool::allocate(size_t size) {
    // Before 0.3.0 the rounding could wrap (allocate(SIZE_MAX - 16) returned a few bytes
    // that the next allocation overlapped), and chunks were reused and split at unaligned
    // sizes, breaking the 32-byte alignment.
    const size_t need = aligned_size(size);

    // Try to reuse from free list first
    if (!free_list_.empty()) {
        auto it = free_list_.top();
        if (it.size >= need) {
            free_list_.pop();
            current_usage_ += size;

            // If the chunk is significantly larger, split it (at an aligned offset)
            if (it.size - need > 64) {
                void* remaining_ptr = static_cast<uint8_t*>(it.ptr) + need;
                free_list_.push(FreeChunk(remaining_ptr, it.size - need));
            }

            return it.ptr;
        }
    }

    // Allocate from current block
    if (need > remaining_) {
        // If requested size is very large, allocate a dedicated block
        if (need > block_size_ / 2) {
            new_block(need);
        } else {
            new_block(block_size_);
        }
    }

    void* ptr = current_pos_;
    current_pos_ += need;
    remaining_ -= need;
    current_usage_ += size;

    return ptr;
}

void MemoryPool::deallocate(void* ptr, size_t size) {
    if (!ptr) return;

    // Add to free list for reuse, with the size allocate() actually reserved
    free_list_.push(FreeChunk(ptr, aligned_size(size)));
    current_usage_ -= std::min(size, current_usage_);
}

void MemoryPool::new_block(size_t min_size) {
    size_t size = aligned_size(std::max(min_size, block_size_));
    
    // Grow block size for next allocation (up to a limit)
    if (block_size_ < 1024 * 1024) {  // Max 1MB blocks
        block_size_ = std::min(block_size_ * 2, size_t(1024 * 1024));
    }
    
    blocks_.emplace_back(size);
    current_pos_ = blocks_.back().data.get();
    remaining_ = size;
    total_allocated_ += size;
}

void MemoryPool::reset() {
    // Clear free list
    while (!free_list_.empty()) {
        free_list_.pop();
    }
    
    // Reset to first block if available
    if (!blocks_.empty()) {
        current_pos_ = blocks_[0].data.get();
        remaining_ = blocks_[0].size;
        current_usage_ = 0;
        
        // Keep only the first block, free others
        if (blocks_.size() > 1) {
            size_t first_block_size = blocks_[0].size;
            blocks_.erase(blocks_.begin() + 1, blocks_.end());
            total_allocated_ = first_block_size;
        }
    }
}

void* MemoryPool::align_ptr(void* ptr, size_t alignment) {
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    uintptr_t aligned = (addr + alignment - 1) & ~(alignment - 1);
    return reinterpret_cast<void*>(aligned);
}

} // namespace btoon
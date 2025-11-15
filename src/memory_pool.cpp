#include "btoon/memory_pool.h"
#include <cstdlib>

namespace btoon {

MemoryPool::MemoryPool(size_t block_size)
    : block_size_(block_size),
      current_pos_(nullptr),
      remaining_(0) {
    new_block();
}

MemoryPool::~MemoryPool() {
    for (void* block : blocks_) {
        std::free(block);
    }
}

void* MemoryPool::allocate(size_t size) {
    if (size > remaining_) {
        new_block();
        if (size > remaining_) {
            // The requested size is larger than the block size.
            // Allocate a new block just for this allocation.
            void* p = std::malloc(size);
            blocks_.push_back(p);
            return p;
        }
    }

    void* p = current_pos_;
    current_pos_ = static_cast<uint8_t*>(current_pos_) + size;
    remaining_ -= size;
    return p;
}

void MemoryPool::deallocate(void* p, size_t size) {
    // This is a simple pool that doesn't support deallocation.
    // The memory is freed when the pool is destroyed.
    (void)p;
    (void)size;
}

void MemoryPool::new_block() {
    void* p = std::malloc(block_size_);
    if (!p) {
        throw std::bad_alloc();
    }
    blocks_.push_back(p);
    current_pos_ = static_cast<uint8_t*>(p);
    remaining_ = block_size_;
}

} // namespace btoon

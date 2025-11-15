//  ██████╗ ████████╗ ██████╗  ██████╗ ███╗   ██╗
//  ██╔══██╗╚══██╔══╝██╔═══██╗██╔═══██╗████╗  ██║
//  ██████╔╝   ██║   ██║   ██║██║   ██║██╔██╗ ██║
//  ██╔══██╗   ██║   ██║   ██║██║   ██║██║╚██╗██║
//  ██████╔╝   ██║   ╚██████╔╝╚██████╔╝██║ ╚████║
//  ╚═════╝    ╚═╝    ╚═════╝  ╚═════╝ ╚═╝  ╚═══╝
//
//  BTOON Core
//  Version 0.0.1
//  https://btoon.net & https://github.com/BTOON-project/btoon-core
//
// SPDX-FileCopyrightText: 2025 Alvar Laigna <https://alvarlaigna.com>
// SPDX-License-Identifier: MIT

#ifndef BTOON_MEMORY_POOL_H
#define BTOON_MEMORY_POOL_H

#include <cstddef>
#include <vector>

namespace btoon {

/**
 * @brief Memory pool allocator for efficient memory management.
 */
class MemoryPool {
public:
    MemoryPool(size_t initial_size = 1024);
    ~MemoryPool();

    void* allocate(size_t size);
    void deallocate(void* ptr, size_t size);

private:
    struct Block {
        Block* next;
    };
    
    void new_block();

    std::vector<void*> blocks_;
    size_t block_size_;
    uint8_t* current_pos_;
    size_t remaining_;
};

} // namespace btoon

#endif // BTOON_MEMORY_POOL_H

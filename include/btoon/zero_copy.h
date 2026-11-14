// SPDX-FileCopyrightText: 2025 Alvar Laigna <https://alvarlaigna.com>
// SPDX-License-Identifier: MIT
/**
 * @file zero_copy.h
 * @brief Buffers to decode from and encode into without extra copies.
 *
 * MemoryMappedFile maps a file read-only, and SharedMemoryBuffer maps a named shared
 * memory segment; pass `view().span()` to btoon::decode(). BufferPool recycles output
 * buffers, and ZeroCopyEncoder encodes into caller-provided memory. A C API exposes the
 * mappings as opaque views.
 *
 * The decoder that returned views into its input (ZeroCopyDecoder) was unfinished and
 * moved to experimental/ in 0.3.0.
 */

#ifndef BTOON_ZERO_COPY_H
#define BTOON_ZERO_COPY_H

#include "btoon.h"
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <vector>
#include "btoon/capi.h"  // BTOON_API

namespace btoon {

/**
 * @brief Memory view for zero-copy access to BTOON data
 * 
 * Provides a non-owning view into BTOON encoded data that can be
 * shared across language boundaries without copying.
 */
class MemoryView {
public:
    using Deleter = std::function<void(const uint8_t*)>;
    
    /**
     * @brief Create a view from existing memory
     * @param data Pointer to data
     * @param size Size of data
     * @param deleter Optional custom deleter for memory management
     */
    MemoryView(const uint8_t* data, size_t size, Deleter deleter = nullptr)
        : data_(data), size_(size), deleter_(deleter) {}
    
    /**
     * @brief Create a view from a span
     */
    explicit MemoryView(std::span<const uint8_t> span)
        : data_(span.data()), size_(span.size()), deleter_(nullptr) {}
    
    ~MemoryView() {
        if (deleter_ && data_) {
            deleter_(data_);
        }
    }
    
    // Move-only semantics
    MemoryView(MemoryView&& other) noexcept
        : data_(other.data_), size_(other.size_), deleter_(std::move(other.deleter_)) {
        other.data_ = nullptr;
        other.size_ = 0;
    }
    
    MemoryView& operator=(MemoryView&& other) noexcept {
        if (this != &other) {
            if (deleter_ && data_) {
                deleter_(data_);
            }
            data_ = other.data_;
            size_ = other.size_;
            deleter_ = std::move(other.deleter_);
            other.data_ = nullptr;
            other.size_ = 0;
        }
        return *this;
    }
    
    // Disable copy
    MemoryView(const MemoryView&) = delete;
    MemoryView& operator=(const MemoryView&) = delete;
    
    /**
     * @brief Get data pointer
     */
    const uint8_t* data() const { return data_; }
    
    /**
     * @brief Get data size
     */
    size_t size() const { return size_; }
    
    /**
     * @brief Get as span
     */
    std::span<const uint8_t> span() const {
        return std::span<const uint8_t>(data_, size_);
    }
    
    /**
     * @brief Check if view is valid
     */
    bool valid() const { return data_ != nullptr && size_ > 0; }
    
    /**
     * @brief Release ownership without calling deleter
     */
    const uint8_t* release() {
        const uint8_t* ptr = data_;
        data_ = nullptr;
        size_ = 0;
        deleter_ = nullptr;
        return ptr;
    }
    
private:
    const uint8_t* data_;
    size_t size_;
    Deleter deleter_;
};

/**
 * @brief Memory-mapped file for zero-copy file access
 */
class MemoryMappedFile {
public:
    /**
     * @brief Open a file for memory-mapped reading
     */
    static std::unique_ptr<MemoryMappedFile> open(const std::string& path);
    
    /**
     * @brief Get memory view of the file
     */
    MemoryView view() const {
        return MemoryView(data_, size_);
    }
    
    /**
     * @brief Get data pointer
     */
    const uint8_t* data() const { return data_; }
    
    /**
     * @brief Get file size
     */
    size_t size() const { return size_; }
    
    ~MemoryMappedFile();

    // The object owns the mapping, and the destructor unmaps it: a copy would unmap it
    // twice and leave the other object dangling. Hold it in the returned unique_ptr.
    MemoryMappedFile(const MemoryMappedFile&) = delete;
    MemoryMappedFile& operator=(const MemoryMappedFile&) = delete;
    MemoryMappedFile(MemoryMappedFile&&) = delete;
    MemoryMappedFile& operator=(MemoryMappedFile&&) = delete;

private:
    MemoryMappedFile(const uint8_t* data, size_t size, void* handle)
        : data_(data), size_(size), handle_(handle) {}
    
    const uint8_t* data_;
    size_t size_;
    void* handle_;  // Platform-specific handle
};

/**
 * @brief Shared memory buffer for inter-process communication
 */
class SharedMemoryBuffer {
public:
    /**
     * @brief Create (create_new) or open a named shared memory segment.
     *
     * Returns nullptr on failure. New segments are readable and writable by the
     * owner only (POSIX mode 0600). When opening an existing segment, size 0 maps all
     * of it, and a segment smaller than size is refused. The creator removes the
     * name when its buffer is destroyed. Not available on Android, whose C library
     * has no POSIX named shared memory: create() always returns nullptr there.
     */
    static std::unique_ptr<SharedMemoryBuffer> create(
        const std::string& name, 
        size_t size,
        bool create_new = true
    );
    
    /**
     * @brief Get writable data pointer
     */
    uint8_t* data() { return data_; }
    const uint8_t* data() const { return data_; }
    
    /**
     * @brief Get buffer size
     */
    size_t size() const { return size_; }
    
    /**
     * @brief Get memory view for zero-copy access
     */
    MemoryView view() const {
        return MemoryView(data_, size_);
    }
    
    ~SharedMemoryBuffer();

    // Owns the mapping and, for the creator, the segment's name: not copyable or movable.
    SharedMemoryBuffer(const SharedMemoryBuffer&) = delete;
    SharedMemoryBuffer& operator=(const SharedMemoryBuffer&) = delete;
    SharedMemoryBuffer(SharedMemoryBuffer&&) = delete;
    SharedMemoryBuffer& operator=(SharedMemoryBuffer&&) = delete;

private:
    SharedMemoryBuffer(uint8_t* data, size_t size, void* handle)
        : data_(data), size_(size), handle_(handle) {}
    
    uint8_t* data_;
    size_t size_;
    void* handle_;  // Platform-specific handle
};

/**
 * @brief Buffer pool for recycling allocations
 */
class BufferPool {
public:
    /**
     * @brief Buffer handle with automatic return to pool
     */
    class Buffer {
    public:
        Buffer(std::vector<uint8_t>&& data, BufferPool* pool)
            : data_(std::move(data)), pool_(pool) {}
        
        // A buffer goes back to its pool when destroyed, empty or not; a moved-from
        // buffer has no pool. Buffers must not outlive their pool.
        ~Buffer() {
            if (pool_) {
                pool_->return_buffer(std::move(data_));
            }
        }
        
        // Move-only
        Buffer(Buffer&& other) noexcept
            : data_(std::move(other.data_)), pool_(other.pool_) {
            other.pool_ = nullptr;
        }
        
        Buffer& operator=(Buffer&& other) noexcept {
            if (this != &other) {
                if (pool_) {
                    pool_->return_buffer(std::move(data_));
                }
                data_ = std::move(other.data_);
                pool_ = other.pool_;
                other.pool_ = nullptr;
            }
            return *this;
        }
        
        // Disable copy
        Buffer(const Buffer&) = delete;
        Buffer& operator=(const Buffer&) = delete;
        
        uint8_t* data() { return data_.data(); }
        const uint8_t* data() const { return data_.data(); }
        size_t size() const { return data_.size(); }
        size_t capacity() const { return data_.capacity(); }
        
        void resize(size_t size) { data_.resize(size); }
        void clear() { data_.clear(); }
        
        std::vector<uint8_t>& vector() { return data_; }
        const std::vector<uint8_t>& vector() const { return data_; }
        
    private:
        std::vector<uint8_t> data_;
        BufferPool* pool_;
    };
    
    /**
     * @brief Create a buffer pool with specified parameters
     */
    explicit BufferPool(size_t max_buffers = 100, size_t default_size = 4096)
        : max_buffers_(max_buffers), default_size_(default_size) {}
    
    /**
     * @brief Get a buffer from the pool
     */
    Buffer get_buffer(size_t min_size = 0);
    
    /**
     * @brief Clear all cached buffers
     */
    void clear();
    
    /**
     * @brief Get statistics
     */
    struct Stats {
        size_t buffers_in_use;
        size_t buffers_cached;
        size_t total_allocated;
        size_t cache_hits;
        size_t cache_misses;
    };
    
    Stats stats() const;
    
private:
    void return_buffer(std::vector<uint8_t>&& buffer);
    
    mutable std::mutex mutex_;
    std::vector<std::vector<uint8_t>> buffers_;
    size_t max_buffers_;
    size_t default_size_;
    
    // Statistics
    mutable size_t buffers_in_use_ = 0;
    mutable size_t cache_hits_ = 0;
    mutable size_t cache_misses_ = 0;
    
    friend class Buffer;
};

/**
 * @brief Encodes into memory the caller provides (a buffer, a pool buffer, shared memory).
 *
 * The value is encoded canonically (no envelopes) and then copied once into the target.
 */
class ZeroCopyEncoder {
public:
    /**
     * @brief Encode into `buffer`.
     * @return Number of bytes written
     * @throws BtoonException with ErrorCode::size_limit if the encoding doesn't fit
     */
    size_t encode_into(const Value& value, uint8_t* buffer, size_t buffer_size);
    
    /**
     * @brief Encode into a buffer pool buffer
     */
    BufferPool::Buffer encode_pooled(const Value& value, BufferPool& pool);
    
    /**
     * @brief Encode into shared memory
     */
    size_t encode_into_shared(const Value& value, SharedMemoryBuffer& buffer);
};

/**
 * @brief C API for zero-copy operations (for FFI)
 */
extern "C" {

/**
 * @brief Opaque handle for memory view
 */
typedef struct btoon_memory_view* btoon_memory_view_t;

/**
 * @brief Create memory view from data
 */
BTOON_API btoon_memory_view_t btoon_memory_view_create(
    const uint8_t* data,
    size_t size,
    void (*deleter)(const uint8_t*)
);

/**
 * @brief Get data pointer from memory view
 */
BTOON_API const uint8_t* btoon_memory_view_data(btoon_memory_view_t view);

/**
 * @brief Get size from memory view
 */
BTOON_API size_t btoon_memory_view_size(btoon_memory_view_t view);

/**
 * @brief Release memory view
 */
BTOON_API void btoon_memory_view_free(btoon_memory_view_t view);

/**
 * @brief Memory map a file
 */
BTOON_API btoon_memory_view_t btoon_mmap_file(const char* path);

/**
 * @brief Unmap a memory-mapped file
 */
BTOON_API void btoon_munmap_file(btoon_memory_view_t view);

/**
 * @brief Create/open shared memory
 */
BTOON_API btoon_memory_view_t btoon_shm_create(
    const char* name,
    size_t size,
    int create_new
);

/**
 * @brief Close shared memory
 */
BTOON_API void btoon_shm_close(btoon_memory_view_t view);

} // extern "C"

} // namespace btoon

#endif // BTOON_ZERO_COPY_H

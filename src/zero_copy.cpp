#include "btoon/zero_copy.h"
#include "btoon/decoder.h"
#include "btoon/encoder.h"
#include <cstring>
#include <mutex>

#ifdef _WIN32
    #ifndef NOMINMAX
        #define NOMINMAX  // Prevent Windows.h from defining min/max macros
    #endif
    #include <windows.h>
#else
    #include <sys/mman.h>
    #include <sys/stat.h>
    #include <fcntl.h>
    #include <unistd.h>
    #ifdef __linux__
        #include <sys/shm.h>
    #endif
#endif
#include <algorithm>  // For std::max
#include <limits>

namespace btoon {

// ===== MemoryMappedFile Implementation =====

#ifdef _WIN32

std::unique_ptr<MemoryMappedFile> MemoryMappedFile::open(const std::string& path) {
    HANDLE file = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                             nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return nullptr;
    }
    
    LARGE_INTEGER file_size;
    if (!GetFileSizeEx(file, &file_size)) {
        CloseHandle(file);
        return nullptr;
    }
    
    HANDLE mapping = CreateFileMappingA(file, nullptr, PAGE_READONLY, 
                                        file_size.HighPart, file_size.LowPart, nullptr);
    CloseHandle(file);
    
    if (mapping == nullptr) {
        return nullptr;
    }
    
    void* data = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    if (data == nullptr) {
        CloseHandle(mapping);
        return nullptr;
    }
    
    return std::unique_ptr<MemoryMappedFile>(
        new MemoryMappedFile(
            static_cast<const uint8_t*>(data),
            static_cast<size_t>(file_size.QuadPart),
            mapping
        )
    );
}

MemoryMappedFile::~MemoryMappedFile() {
    if (data_) {
        UnmapViewOfFile(const_cast<uint8_t*>(data_));
    }
    if (handle_) {
        CloseHandle(static_cast<HANDLE>(handle_));
    }
}

#else  // POSIX

std::unique_ptr<MemoryMappedFile> MemoryMappedFile::open(const std::string& path) {
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        return nullptr;
    }
    
    struct stat st;
    // Regular files only: devices, FIFOs and the like have no meaningful size.
    if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode) || st.st_size <= 0) {
        close(fd);
        return nullptr;
    }
    
    void* data = mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    
    if (data == MAP_FAILED) {
        return nullptr;
    }
    
    return std::unique_ptr<MemoryMappedFile>(
        new MemoryMappedFile(
            static_cast<const uint8_t*>(data),
            static_cast<size_t>(st.st_size),
            data
        )
    );
}

MemoryMappedFile::~MemoryMappedFile() {
    if (data_ && handle_) {
        munmap(const_cast<uint8_t*>(data_), size_);
    }
}

#endif

// ===== SharedMemoryBuffer Implementation =====

#ifdef _WIN32

std::unique_ptr<SharedMemoryBuffer> SharedMemoryBuffer::create(
    const std::string& name, size_t size, bool create_new) {
    
    HANDLE mapping = create_new 
        ? CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                           static_cast<DWORD>(static_cast<uint64_t>(size) >> 32),
                           static_cast<DWORD>(size & 0xFFFFFFFFu), name.c_str())
        : OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
    
    if (mapping == nullptr) {
        return nullptr;
    }
    
    void* data = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, size);
    if (data == nullptr) {
        CloseHandle(mapping);
        return nullptr;
    }
    if (size == 0) {  // opened with size 0: report the whole mapped region
        MEMORY_BASIC_INFORMATION info{};
        if (VirtualQuery(data, &info, sizeof info) == 0) {
            UnmapViewOfFile(data);
            CloseHandle(mapping);
            return nullptr;
        }
        size = info.RegionSize;
    }
    
    return std::unique_ptr<SharedMemoryBuffer>(
        new SharedMemoryBuffer(
            static_cast<uint8_t*>(data),
            size,
            mapping
        )
    );
}

SharedMemoryBuffer::~SharedMemoryBuffer() {
    if (data_) {
        UnmapViewOfFile(data_);
    }
    if (handle_) {
        CloseHandle(static_cast<HANDLE>(handle_));
    }
}

#elif defined(__ANDROID__)

// Android's C library has no POSIX named shared memory (shm_open/shm_unlink).
// Share an ASharedMemory or memfd_create descriptor between processes instead.
std::unique_ptr<SharedMemoryBuffer> SharedMemoryBuffer::create(
    const std::string& /*name*/, size_t /*size*/, bool /*create_new*/) {
    return nullptr;
}

SharedMemoryBuffer::~SharedMemoryBuffer() {
    if (data_) {
        munmap(data_, size_);
    }
}

#else  // POSIX

std::unique_ptr<SharedMemoryBuffer> SharedMemoryBuffer::create(
    const std::string& name, size_t size, bool create_new) {
    
    int flags = O_RDWR;
    if (create_new) {
        flags |= O_CREAT | O_EXCL;
    }
    
    // Owner-only: other local users must not read or modify the segment.
    int fd = shm_open(name.c_str(), flags, 0600);
    if (fd < 0) {
        return nullptr;
    }
    
    if (create_new) {
        if (size > static_cast<uint64_t>(std::numeric_limits<off_t>::max()) ||
            ftruncate(fd, static_cast<off_t>(size)) < 0) {
            close(fd);
            shm_unlink(name.c_str());
            return nullptr;
        }
    } else {
        // Map no more than the existing segment holds (size 0 means all of it):
        // touching pages past its end would raise SIGBUS.
        struct stat st;
        if (fstat(fd, &st) < 0 || st.st_size <= 0 ||
            (size != 0 && static_cast<uint64_t>(st.st_size) < size)) {
            close(fd);
            return nullptr;
        }
        if (size == 0) {
            size = static_cast<size_t>(st.st_size);
        }
    }
    
    void* data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    
    if (data == MAP_FAILED) {
        if (create_new) {
            shm_unlink(name.c_str());
        }
        return nullptr;
    }
    
    // Only the process that created the segment removes its name on destruction;
    // a process that merely opened it must not unlink it for everyone else.
    auto* owned_name = create_new ? new std::string(name) : nullptr;
    return std::unique_ptr<SharedMemoryBuffer>(
        new SharedMemoryBuffer(
            static_cast<uint8_t*>(data),
            size,
            owned_name
        )
    );
}

SharedMemoryBuffer::~SharedMemoryBuffer() {
    if (data_) {
        munmap(data_, size_);
    }
    if (handle_) {
        auto* name = static_cast<std::string*>(handle_);
        shm_unlink(name->c_str());
        delete name;
    }
}

#endif

// ===== BufferPool Implementation =====

BufferPool::Buffer BufferPool::get_buffer(size_t min_size) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    size_t required_size = std::max(min_size, default_size_);
    
    // Try to find a suitable buffer in the pool
    for (auto it = buffers_.begin(); it != buffers_.end(); ++it) {
        if (it->capacity() >= required_size) {
            std::vector<uint8_t> buffer = std::move(*it);
            buffers_.erase(it);
            buffer.clear();
            buffers_in_use_++;
            cache_hits_++;
            return Buffer(std::move(buffer), this);
        }
    }
    
    // No suitable buffer found, allocate new
    cache_misses_++;
    buffers_in_use_++;
    std::vector<uint8_t> buffer;
    buffer.reserve(required_size);
    return Buffer(std::move(buffer), this);
}

void BufferPool::return_buffer(std::vector<uint8_t>&& buffer) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    buffers_in_use_--;
    
    if (buffers_.size() < max_buffers_) {
        buffer.clear();
        buffers_.push_back(std::move(buffer));
    }
}

void BufferPool::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    buffers_.clear();
}

BufferPool::Stats BufferPool::stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return Stats{
        buffers_in_use_,
        buffers_.size(),
        buffers_in_use_ + buffers_.size(),
        cache_hits_,
        cache_misses_
    };
}

// ===== ZeroCopyEncoder Implementation =====

size_t ZeroCopyEncoder::encode_into(const Value& value, uint8_t* buffer, size_t buffer_size) {
    // Use existing encoder but write directly to provided buffer
    Encoder encoder;
    encoder.encode(value);
    auto encoded = encoder.getBuffer();
    
    if (encoded.size() > buffer_size) {
        throw BtoonException(ErrorCode::size_limit, "buffer too small for the encoding");
    }
    
    std::memcpy(buffer, encoded.data(), encoded.size());
    return encoded.size();
}

BufferPool::Buffer ZeroCopyEncoder::encode_pooled(const Value& value, BufferPool& pool) {
    Encoder encoder;
    encoder.encode(value);
    auto encoded = encoder.getBuffer();
    
    auto buffer = pool.get_buffer(encoded.size());
    buffer.resize(encoded.size());
    std::memcpy(buffer.data(), encoded.data(), encoded.size());
    
    return buffer;
}

size_t ZeroCopyEncoder::encode_into_shared(const Value& value, SharedMemoryBuffer& buffer) {
    return encode_into(value, buffer.data(), buffer.size());
}

// ===== C API Implementation =====

extern "C" {

struct btoon_memory_view {
    MemoryView view;
    // Keep the mapping alive for as long as the view: a view into a mapped file or a
    // shared memory segment must not outlive it.
    std::unique_ptr<MemoryMappedFile> file;
    std::unique_ptr<SharedMemoryBuffer> shm;
};

btoon_memory_view_t btoon_memory_view_create(
    const uint8_t* data,
    size_t size,
    void (*deleter)(const uint8_t*)) {
    
    try {
        return new btoon_memory_view{MemoryView(data, size, deleter), nullptr, nullptr};
    } catch (...) {
        return nullptr;
    }
}

const uint8_t* btoon_memory_view_data(btoon_memory_view_t view) {
    return view ? view->view.data() : nullptr;
}

size_t btoon_memory_view_size(btoon_memory_view_t view) {
    return view ? view->view.size() : 0;
}

void btoon_memory_view_free(btoon_memory_view_t view) {
    delete view;
}

btoon_memory_view_t btoon_mmap_file(const char* path) {
    try {
        auto mmap = MemoryMappedFile::open(path);
        if (!mmap) return nullptr;
        
        MemoryView view = mmap->view();
        return new btoon_memory_view{std::move(view), std::move(mmap), nullptr};
    } catch (...) {
        return nullptr;
    }
}

void btoon_munmap_file(btoon_memory_view_t view) {
    btoon_memory_view_free(view);
}

btoon_memory_view_t btoon_shm_create(
    const char* name,
    size_t size,
    int create_new) {
    
    try {
        auto shm = SharedMemoryBuffer::create(name, size, create_new != 0);
        if (!shm) return nullptr;
        
        MemoryView view = shm->view();
        return new btoon_memory_view{std::move(view), nullptr, std::move(shm)};
    } catch (...) {
        return nullptr;
    }
}

void btoon_shm_close(btoon_memory_view_t view) {
    btoon_memory_view_free(view);
}

} // extern "C"

} // namespace btoon

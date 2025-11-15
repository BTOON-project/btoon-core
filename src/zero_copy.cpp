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

namespace btoon {

// ===== ZeroCopyDecoder Implementation =====

ValueView ZeroCopyDecoder::decode(const MemoryView& view) {
    return decode(view.data(), view.size());
}

ValueView ZeroCopyDecoder::decode(const uint8_t* data, size_t size) {
    if (!data || size == 0) {
        throw BtoonException("Invalid input data");
    }
    
    size_t pos = 0;
    return decodeValue(data, size, pos, 0);
}

ValueView ZeroCopyDecoder::decodeValue(const uint8_t* data, size_t size, 
                                      size_t& pos, size_t depth) {
    if (depth > options_.max_depth) {
        throw BtoonException("Maximum nesting depth exceeded");
    }
    
    if (pos >= size) {
        throw BtoonException("Buffer underflow");
    }
    
    uint8_t type = data[pos++];
    
    // Handle fixed types
    if (type <= 0x7f) {
        return static_cast<Uint>(type);  // Positive fixint
    } else if (type >= 0xe0) {
        return static_cast<Int>(static_cast<int8_t>(type));  // Negative fixint
    }
    
    // Handle other types
    switch (type) {
        case 0xc0: return Nil{};
        case 0xc2: return Bool(false);
        case 0xc3: return Bool(true);
        
        // Strings - return string_view instead of copying
        default:
            break;
    }
    
    // fixstr (0xa0 - 0xbf)
    if (type >= 0xa0 && type <= 0xbf) {
        size_t len = type & 0x1f;
        if (pos + len > size) throw BtoonException("Buffer underflow");
        std::string_view str(reinterpret_cast<const char*>(data + pos), len);
        pos += len;
        return str;
    }
    
    // Other string types
    switch (type) {
        
        case 0xd9: {  // str8
            if (pos + 1 > size) throw BtoonException("Buffer underflow");
            size_t len = data[pos++];
            if (pos + len > size) throw BtoonException("Buffer underflow");
            std::string_view str(reinterpret_cast<const char*>(data + pos), len);
            pos += len;
            return str;
        }
        
        case 0xda: {  // str16
            if (pos + 2 > size) throw BtoonException("Buffer underflow");
            size_t len = (data[pos] << 8) | data[pos + 1];
            pos += 2;
            if (pos + len > size) throw BtoonException("Buffer underflow");
            std::string_view str(reinterpret_cast<const char*>(data + pos), len);
            pos += len;
            return str;
        }
        
        // Binary - return span instead of copying
        case 0xc4: {  // bin8
            if (pos + 1 > size) throw BtoonException("Buffer underflow");
            size_t len = data[pos++];
            if (pos + len > size) throw BtoonException("Buffer underflow");
            std::span<const uint8_t> bin(data + pos, len);
            pos += len;
            return bin;
        }
        
        case 0xc5: {  // bin16
            if (pos + 2 > size) throw BtoonException("Buffer underflow");
            size_t len = (data[pos] << 8) | data[pos + 1];
            pos += 2;
            if (pos + len > size) throw BtoonException("Buffer underflow");
            std::span<const uint8_t> bin(data + pos, len);
            pos += len;
            return bin;
        }
        
        // Simplified - full implementation would handle all other types
        default:
            break;
    }
    
    // fixarray (0x90 - 0x9f)
    if (type >= 0x90 && type <= 0x9f) {
        size_t count = type & 0x0f;
        std::vector<ValueView> arr;
        arr.reserve(count);
        for (size_t i = 0; i < count; i++) {
            arr.push_back(decodeValue(data, size, pos, depth + 1));
        }
        return arr;
    }
    
    // fixmap (0x80 - 0x8f)
    if (type >= 0x80 && type <= 0x8f) {
        size_t count = type & 0x0f;
        std::unordered_map<std::string_view, ValueView> map;
        for (size_t i = 0; i < count; i++) {
            auto key = decodeValue(data, size, pos, depth + 1);
            auto value = decodeValue(data, size, pos, depth + 1);
            if (auto* str_key = std::get_if<std::string_view>(&key)) {
                map[*str_key] = std::move(value);
            } else {
                throw BtoonException("Map key must be string");
            }
        }
        return map;
    }
    
    throw BtoonException("Unsupported type in zero-copy decoder");
}

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
    if (fstat(fd, &st) < 0) {
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
                           0, static_cast<DWORD>(size), name.c_str())
        : OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
    
    if (mapping == nullptr) {
        return nullptr;
    }
    
    void* data = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, size);
    if (data == nullptr) {
        CloseHandle(mapping);
        return nullptr;
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

#else  // POSIX

std::unique_ptr<SharedMemoryBuffer> SharedMemoryBuffer::create(
    const std::string& name, size_t size, bool create_new) {
    
    int flags = O_RDWR;
    if (create_new) {
        flags |= O_CREAT | O_EXCL;
    }
    
    int fd = shm_open(name.c_str(), flags, 0666);
    if (fd < 0) {
        return nullptr;
    }
    
    if (create_new && ftruncate(fd, size) < 0) {
        close(fd);
        shm_unlink(name.c_str());
        return nullptr;
    }
    
    void* data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    
    if (data == MAP_FAILED) {
        if (create_new) {
            shm_unlink(name.c_str());
        }
        return nullptr;
    }
    
    auto* name_copy = new std::string(name);
    return std::unique_ptr<SharedMemoryBuffer>(
        new SharedMemoryBuffer(
            static_cast<uint8_t*>(data),
            size,
            name_copy
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
        throw BtoonException("Buffer too small for encoding");
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
};

btoon_memory_view_t btoon_memory_view_create(
    const uint8_t* data,
    size_t size,
    void (*deleter)(const uint8_t*)) {
    
    try {
        auto* view = new btoon_memory_view{
            MemoryView(data, size, deleter)
        };
        return view;
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
        
        auto* view = new btoon_memory_view{
            mmap->view()
        };
        // Note: Need to manage mmap lifetime properly in production
        return view;
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
        
        auto* view = new btoon_memory_view{
            shm->view()
        };
        // Note: Need to manage shm lifetime properly in production
        return view;
    } catch (...) {
        return nullptr;
    }
}

void btoon_shm_close(btoon_memory_view_t view) {
    btoon_memory_view_free(view);
}

} // extern "C"

} // namespace btoon

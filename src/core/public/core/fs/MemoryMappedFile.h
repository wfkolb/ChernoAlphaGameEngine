#pragma once
#include <cstdint>
#include <span>

namespace engine::core::fs {

// Read-only memory-mapped file. The file is mapped until the object is destroyed.
class MemoryMappedFile {
public:
    static MemoryMappedFile open(const class Path& path);

    MemoryMappedFile() = default;
    ~MemoryMappedFile();
    MemoryMappedFile(MemoryMappedFile&&) noexcept;
    MemoryMappedFile& operator=(MemoryMappedFile&&) noexcept;
    MemoryMappedFile(const MemoryMappedFile&) = delete;
    MemoryMappedFile& operator=(const MemoryMappedFile&) = delete;

    bool                     isOpen() const;
    std::span<const uint8_t> data()   const;
    uint64_t                 size()   const;
    void                     close();

private:
    void*    fileHandle_    = nullptr;  // HANDLE
    void*    mappingHandle_ = nullptr;  // HANDLE to file mapping
    void*    view_          = nullptr;  // MapViewOfFile result
    uint64_t size_          = 0;
};

} // namespace engine::core::fs

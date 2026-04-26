#pragma once
#include <cstdint>
#include <span>

namespace engine::core::fs {

// RAII file handle. Non-copyable, movable.
class FileHandle {
public:
    enum class Mode { Read, Write, ReadWrite };

    static FileHandle open(const class Path& path, Mode mode = Mode::Read);

    FileHandle() = default;
    ~FileHandle();
    FileHandle(FileHandle&&) noexcept;
    FileHandle& operator=(FileHandle&&) noexcept;
    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;

    bool     isOpen() const;
    uint64_t size()   const;
    bool read (std::span<uint8_t> buf) const;  // read buf.size() bytes at current position
    bool write(std::span<const uint8_t> buf);
    bool seek (uint64_t offset);
    void close();

private:
    void* handle_ = nullptr;  // HANDLE stored as void* (no Windows.h in public header)
};

} // namespace engine::core::fs

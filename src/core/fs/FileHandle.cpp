#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "core/fs/FileHandle.h"
#include "core/fs/Path.h"
#include "core/diag/Assert.h"

namespace engine::core::fs {

FileHandle FileHandle::open(const Path& path, Mode mode) {
    DWORD access = 0, share = 0, create = 0;
    switch (mode) {
        case Mode::Read:
            access = GENERIC_READ;
            share  = FILE_SHARE_READ;
            create = OPEN_EXISTING;
            break;
        case Mode::Write:
            access = GENERIC_WRITE;
            share  = 0;
            create = CREATE_ALWAYS;
            break;
        case Mode::ReadWrite:
            access = GENERIC_READ | GENERIC_WRITE;
            share  = 0;
            create = OPEN_ALWAYS;
            break;
    }
    HANDLE h = CreateFileW(path.wstr().c_str(), access, share, nullptr,
                           create, FILE_ATTRIBUTE_NORMAL, nullptr);
    FileHandle fh;
    if (h != INVALID_HANDLE_VALUE) fh.handle_ = h;  // nullptr left on failure
    return fh;
}

FileHandle::~FileHandle() { close(); }

FileHandle::FileHandle(FileHandle&& o) noexcept : handle_(o.handle_) {
    o.handle_ = nullptr;
}

FileHandle& FileHandle::operator=(FileHandle&& o) noexcept {
    if (this != &o) { close(); handle_ = o.handle_; o.handle_ = nullptr; }
    return *this;
}

bool FileHandle::isOpen() const { return handle_ != nullptr; }

uint64_t FileHandle::size() const {
    ENGINE_ASSERT(isOpen(), "size() called on closed FileHandle");
    LARGE_INTEGER sz{};
    GetFileSizeEx(static_cast<HANDLE>(handle_), &sz);
    return static_cast<uint64_t>(sz.QuadPart);
}

bool FileHandle::read(std::span<uint8_t> buf) const {
    ENGINE_ASSERT(isOpen(), "read() called on closed FileHandle");
    DWORD bytesRead = 0;
    return ReadFile(static_cast<HANDLE>(handle_), buf.data(),
                    static_cast<DWORD>(buf.size()), &bytesRead, nullptr)
           && bytesRead == static_cast<DWORD>(buf.size());
}

bool FileHandle::write(std::span<const uint8_t> buf) {
    ENGINE_ASSERT(isOpen(), "write() called on closed FileHandle");
    DWORD written = 0;
    return WriteFile(static_cast<HANDLE>(handle_), buf.data(),
                     static_cast<DWORD>(buf.size()), &written, nullptr)
           && written == static_cast<DWORD>(buf.size());
}

bool FileHandle::seek(uint64_t offset) {
    ENGINE_ASSERT(isOpen(), "seek() called on closed FileHandle");
    LARGE_INTEGER li{};
    li.QuadPart = static_cast<LONGLONG>(offset);
    return SetFilePointerEx(static_cast<HANDLE>(handle_), li, nullptr, FILE_BEGIN) != 0;
}

void FileHandle::close() {
    if (handle_) { CloseHandle(static_cast<HANDLE>(handle_)); handle_ = nullptr; }
}

} // namespace engine::core::fs

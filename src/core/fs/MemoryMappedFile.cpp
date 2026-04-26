#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "core/fs/MemoryMappedFile.h"
#include "core/fs/Path.h"

namespace engine::core::fs {

MemoryMappedFile MemoryMappedFile::open(const Path& path) {
    MemoryMappedFile mmf;

    HANDLE fh = CreateFileW(path.wstr().c_str(), GENERIC_READ, FILE_SHARE_READ,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (fh == INVALID_HANDLE_VALUE) return mmf;

    LARGE_INTEGER sz{};
    GetFileSizeEx(fh, &sz);

    HANDLE mh = CreateFileMappingW(fh, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!mh) { CloseHandle(fh); return mmf; }

    void* view = MapViewOfFile(mh, FILE_MAP_READ, 0, 0, 0);
    if (!view) { CloseHandle(mh); CloseHandle(fh); return mmf; }

    mmf.fileHandle_    = fh;
    mmf.mappingHandle_ = mh;
    mmf.view_          = view;
    mmf.size_          = static_cast<uint64_t>(sz.QuadPart);
    return mmf;
}

MemoryMappedFile::~MemoryMappedFile() { close(); }

MemoryMappedFile::MemoryMappedFile(MemoryMappedFile&& o) noexcept
    : fileHandle_(o.fileHandle_), mappingHandle_(o.mappingHandle_),
      view_(o.view_), size_(o.size_) {
    o.fileHandle_ = o.mappingHandle_ = o.view_ = nullptr;
    o.size_ = 0;
}

MemoryMappedFile& MemoryMappedFile::operator=(MemoryMappedFile&& o) noexcept {
    if (this != &o) {
        close();
        fileHandle_    = o.fileHandle_;
        mappingHandle_ = o.mappingHandle_;
        view_          = o.view_;
        size_          = o.size_;
        o.fileHandle_ = o.mappingHandle_ = o.view_ = nullptr;
        o.size_ = 0;
    }
    return *this;
}

bool MemoryMappedFile::isOpen() const { return view_ != nullptr; }

std::span<const uint8_t> MemoryMappedFile::data() const {
    return { static_cast<const uint8_t*>(view_), size_ };
}

uint64_t MemoryMappedFile::size() const { return size_; }

void MemoryMappedFile::close() {
    if (view_)          { UnmapViewOfFile(view_);                              view_ = nullptr; }
    if (mappingHandle_) { CloseHandle(static_cast<HANDLE>(mappingHandle_)); mappingHandle_ = nullptr; }
    if (fileHandle_)    { CloseHandle(static_cast<HANDLE>(fileHandle_));    fileHandle_    = nullptr; }
    size_ = 0;
}

} // namespace engine::core::fs

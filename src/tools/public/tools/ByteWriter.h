#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace engine::tools {

class ByteWriter {
public:
    void writeU8(uint8_t v);
    void writeU16(uint16_t v);
    void writeU32(uint32_t v);
    void writeU64(uint64_t v);
    void writeF32(float v);
    void writeString(std::string_view s);  // u16 length prefix + raw bytes
    void writeBytes(const void* src, size_t n);

    const std::vector<uint8_t>& data() const noexcept { return data_; }
    size_t size() const noexcept { return data_.size(); }
    void   clear() noexcept { data_.clear(); }

private:
    std::vector<uint8_t> data_;
};

class ByteReader {
public:
    ByteReader(const uint8_t* data, size_t size) noexcept
        : data_(data), size_(size), pos_(0), ok_(true) {}

    uint8_t     readU8();
    uint16_t    readU16();
    uint32_t    readU32();
    uint64_t    readU64();
    float       readF32();
    std::string readString();          // inverse of writeString
    bool        readBytes(void* dst, size_t n);
    bool        skip(size_t n);

    size_t pos()       const noexcept { return pos_; }
    size_t remaining() const noexcept { return pos_ < size_ ? size_ - pos_ : 0; }
    bool   ok()        const noexcept { return ok_; }
    bool   eof()       const noexcept { return pos_ >= size_; }

private:
    const uint8_t* data_;
    size_t         size_;
    size_t         pos_;
    bool           ok_;
};

} // namespace engine::tools

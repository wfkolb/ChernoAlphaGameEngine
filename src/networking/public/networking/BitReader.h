#pragma once
#include <cstddef>
#include <cstdint>

namespace engine::networking {

// Byte-aligned binary deserialization reader. Companion to ByteWriter.
// Sets ok() to false on any overread; subsequent reads return zero.
// Named ByteReader to distinguish from Serializer.h's bit-level BitReader.
class ByteReader {
public:
    ByteReader(const uint8_t* data, size_t bytes) noexcept
        : data_(data), size_(bytes) {}

    bool     readBool();
    uint8_t  readU8();
    uint16_t readU16();
    uint32_t readU32();
    float    readF32();
    bool     readBytes(void* dst, size_t n);
    bool     skip(size_t n);

    bool   ok()  const noexcept { return ok_; }
    size_t pos() const noexcept { return pos_; }

private:
    const uint8_t* data_;
    size_t         size_;
    size_t         pos_  = 0;
    bool           ok_   = true;
};

} // namespace engine::networking

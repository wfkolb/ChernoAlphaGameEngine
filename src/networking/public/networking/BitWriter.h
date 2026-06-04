#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace engine::networking {

// Byte-aligned binary serialization writer. All values are little-endian.
// Named ByteWriter to distinguish from Serializer.h's bit-level BitWriter.
class ByteWriter {
public:
    void writeBool(bool v);
    void writeU8(uint8_t v);
    void writeU16(uint16_t v);
    void writeU32(uint32_t v);
    void writeF32(float v);
    void writeBytes(const void* src, size_t n);

    const uint8_t* data()     const noexcept { return buf_.data(); }
    size_t         byteSize() const noexcept { return buf_.size(); }
    void           reset()    noexcept       { buf_.clear(); }

private:
    std::vector<uint8_t> buf_;
};

} // namespace engine::networking

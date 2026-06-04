#include <networking/BitWriter.h>
#include <networking/BitReader.h>
#include <cstring>

namespace engine::networking {

// ── ByteWriter ────────────────────────────────────────────────────────────────

void ByteWriter::writeBool(bool v) {
    buf_.push_back(v ? 1u : 0u);
}

void ByteWriter::writeU8(uint8_t v) {
    buf_.push_back(v);
}

void ByteWriter::writeU16(uint16_t v) {
    buf_.push_back(static_cast<uint8_t>(v));
    buf_.push_back(static_cast<uint8_t>(v >> 8));
}

void ByteWriter::writeU32(uint32_t v) {
    buf_.push_back(static_cast<uint8_t>(v));
    buf_.push_back(static_cast<uint8_t>(v >>  8));
    buf_.push_back(static_cast<uint8_t>(v >> 16));
    buf_.push_back(static_cast<uint8_t>(v >> 24));
}

void ByteWriter::writeF32(float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    writeU32(bits);
}

void ByteWriter::writeBytes(const void* src, size_t n) {
    const auto* p = static_cast<const uint8_t*>(src);
    buf_.insert(buf_.end(), p, p + n);
}

// ── ByteReader ────────────────────────────────────────────────────────────────

bool ByteReader::readBool() {
    return readU8() != 0u;
}

uint8_t ByteReader::readU8() {
    if (!ok_ || pos_ >= size_) { ok_ = false; return 0u; }
    return data_[pos_++];
}

uint16_t ByteReader::readU16() {
    if (!ok_ || pos_ + 2u > size_) { ok_ = false; return 0u; }
    const uint16_t v =  static_cast<uint16_t>(data_[pos_    ])       |
                       (static_cast<uint16_t>(data_[pos_ + 1u]) << 8);
    pos_ += 2u;
    return v;
}

uint32_t ByteReader::readU32() {
    if (!ok_ || pos_ + 4u > size_) { ok_ = false; return 0u; }
    const uint32_t v =  static_cast<uint32_t>(data_[pos_    ])        |
                       (static_cast<uint32_t>(data_[pos_ + 1u]) <<  8)|
                       (static_cast<uint32_t>(data_[pos_ + 2u]) << 16)|
                       (static_cast<uint32_t>(data_[pos_ + 3u]) << 24);
    pos_ += 4u;
    return v;
}

float ByteReader::readF32() {
    const uint32_t bits = readU32();
    float v;
    std::memcpy(&v, &bits, sizeof(v));
    return v;
}

bool ByteReader::readBytes(void* dst, size_t n) {
    if (!ok_ || pos_ + n > size_) { ok_ = false; return false; }
    std::memcpy(dst, data_ + pos_, n);
    pos_ += n;
    return true;
}

bool ByteReader::skip(size_t n) {
    if (!ok_ || pos_ + n > size_) { ok_ = false; return false; }
    pos_ += n;
    return true;
}

} // namespace engine::networking

#include "tools/ByteWriter.h"

#include <cstring>

namespace engine::tools {

// ── ByteWriter ────────────────────────────────────────────────────────────────

void ByteWriter::writeU8(uint8_t v) {
    data_.push_back(v);
}

void ByteWriter::writeU16(uint16_t v) {
    data_.push_back(static_cast<uint8_t>(v));
    data_.push_back(static_cast<uint8_t>(v >> 8));
}

void ByteWriter::writeU32(uint32_t v) {
    data_.push_back(static_cast<uint8_t>(v));
    data_.push_back(static_cast<uint8_t>(v >>  8));
    data_.push_back(static_cast<uint8_t>(v >> 16));
    data_.push_back(static_cast<uint8_t>(v >> 24));
}

void ByteWriter::writeU64(uint64_t v) {
    writeU32(static_cast<uint32_t>(v));
    writeU32(static_cast<uint32_t>(v >> 32));
}

void ByteWriter::writeF32(float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    writeU32(bits);
}

void ByteWriter::writeString(std::string_view s) {
    writeU16(static_cast<uint16_t>(s.size()));
    writeBytes(s.data(), s.size());
}

void ByteWriter::writeBytes(const void* src, size_t n) {
    const auto* p = static_cast<const uint8_t*>(src);
    data_.insert(data_.end(), p, p + n);
}

// ── ByteReader ────────────────────────────────────────────────────────────────

uint8_t ByteReader::readU8() {
    if (pos_ >= size_) { ok_ = false; return 0; }
    return data_[pos_++];
}

uint16_t ByteReader::readU16() {
    const uint16_t lo = readU8();
    const uint16_t hi = readU8();
    return static_cast<uint16_t>(lo | (hi << 8));
}

uint32_t ByteReader::readU32() {
    uint32_t v = readU8();
    v |= static_cast<uint32_t>(readU8()) <<  8;
    v |= static_cast<uint32_t>(readU8()) << 16;
    v |= static_cast<uint32_t>(readU8()) << 24;
    return v;
}

uint64_t ByteReader::readU64() {
    const uint64_t lo = readU32();
    const uint64_t hi = readU32();
    return lo | (hi << 32);
}

float ByteReader::readF32() {
    const uint32_t bits = readU32();
    float v;
    std::memcpy(&v, &bits, sizeof(v));
    return v;
}

std::string ByteReader::readString() {
    const uint16_t len = readU16();
    if (!ok_) return {};
    if (pos_ + len > size_) { ok_ = false; return {}; }
    std::string s(reinterpret_cast<const char*>(data_ + pos_), len);
    pos_ += len;
    return s;
}

bool ByteReader::readBytes(void* dst, size_t n) {
    if (pos_ + n > size_) { ok_ = false; return false; }
    std::memcpy(dst, data_ + pos_, n);
    pos_ += n;
    return true;
}

bool ByteReader::skip(size_t n) {
    if (pos_ + n > size_) { ok_ = false; return false; }
    pos_ += n;
    return true;
}

} // namespace engine::tools

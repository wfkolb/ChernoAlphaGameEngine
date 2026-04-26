// engine/src/networking/Snapshot.cpp
// ECS snapshot encode / decode — Task #30.

#include <networking/Snapshot.h>
#include <core/diag/Assert.h>
#include <cstring>

namespace engine::networking {

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static void appendVarint(std::vector<uint8_t>& buf, uint32_t v)
{
    do {
        uint8_t b = static_cast<uint8_t>(v & 0x7Fu);
        v >>= 7;
        if (v) b |= 0x80u;
        buf.push_back(b);
    } while (v);
}

// Returns false if data is exhausted before the varint is complete.
static bool readVarint(const uint8_t* data, size_t len, size_t& offset, uint32_t& out)
{
    out = 0;
    for (int shift = 0; shift < 35; shift += 7) {
        if (offset >= len) return false;
        const uint8_t b = data[offset++];
        out |= static_cast<uint32_t>(b & 0x7Fu) << shift;
        if (!(b & 0x80u)) return true;
    }
    return false; // too many continuation bytes
}

// ---------------------------------------------------------------------------
// SnapshotEncoder::encode
// ---------------------------------------------------------------------------

std::vector<uint8_t> SnapshotEncoder::encode(const std::vector<SnapshotEntry>& entries)
{
    ENGINE_ASSERT(entries.size() <= 0xFFFF'FFFFu, "entry count exceeds varint32 range");

    std::vector<uint8_t> buf;
    // Reserve a rough lower bound to reduce reallocations.
    buf.reserve(entries.size() * 16);

    // Entity count.
    appendVarint(buf, static_cast<uint32_t>(entries.size()));

    for (const SnapshotEntry& e : entries) {
        // Entity index.
        appendVarint(buf, e.entity.index);

        // Entity generation (4 bytes, little-endian).
        const uint32_t gen = e.entity.generation;
        buf.push_back(static_cast<uint8_t>( gen        & 0xFFu));
        buf.push_back(static_cast<uint8_t>((gen >>  8) & 0xFFu));
        buf.push_back(static_cast<uint8_t>((gen >> 16) & 0xFFu));
        buf.push_back(static_cast<uint8_t>((gen >> 24) & 0xFFu));

        // ComponentMask: 256-bit bitset stored as 32 bytes, bit i = component id i.
        // We extract 8 bits at a time via operator[] to stay portable across
        // bitset<N> implementations (no to_ulong / to_ullong for N > 64).
        for (int byteIdx = 0; byteIdx < 32; ++byteIdx) {
            uint8_t octet = 0;
            for (int bit = 0; bit < 8; ++bit) {
                if (e.mask[static_cast<size_t>(byteIdx * 8 + bit)]) {
                    octet |= static_cast<uint8_t>(1u << bit);
                }
            }
            buf.push_back(octet);
        }

        // Component data length then raw bytes.
        ENGINE_ASSERT(e.componentData.size() <= 0xFFFF'FFFFu,
                      "componentData exceeds varint32 range");
        appendVarint(buf, static_cast<uint32_t>(e.componentData.size()));
        buf.insert(buf.end(), e.componentData.begin(), e.componentData.end());
    }

    return buf;
}

// ---------------------------------------------------------------------------
// SnapshotDecoder::decode
// ---------------------------------------------------------------------------

std::vector<SnapshotEntry> SnapshotDecoder::decode(std::span<const uint8_t> data)
{
    const uint8_t* const base = data.data();
    const size_t         len  = data.size();
    size_t               pos  = 0;

    std::vector<SnapshotEntry> entries;

    // Entity count.
    uint32_t entityCount = 0;
    if (!readVarint(base, len, pos, entityCount)) return {};

    entries.reserve(entityCount);

    for (uint32_t i = 0; i < entityCount; ++i) {
        SnapshotEntry entry;

        // Entity index.
        uint32_t index = 0;
        if (!readVarint(base, len, pos, index)) return {};
        entry.entity.index = index;

        // Entity generation (4 bytes, little-endian).
        if (pos + 4 > len) return {};
        uint32_t gen = 0;
        gen |= static_cast<uint32_t>(base[pos + 0]);
        gen |= static_cast<uint32_t>(base[pos + 1]) <<  8;
        gen |= static_cast<uint32_t>(base[pos + 2]) << 16;
        gen |= static_cast<uint32_t>(base[pos + 3]) << 24;
        pos += 4;
        entry.entity.generation = gen;

        // ComponentMask: 32 bytes → 256 bits.
        if (pos + 32 > len) return {};
        for (int byteIdx = 0; byteIdx < 32; ++byteIdx) {
            const uint8_t octet = base[pos + static_cast<size_t>(byteIdx)];
            for (int bit = 0; bit < 8; ++bit) {
                if (octet & (1u << bit)) {
                    entry.mask.set(static_cast<size_t>(byteIdx * 8 + bit));
                }
            }
        }
        pos += 32;

        // Component data.
        uint32_t dataSize = 0;
        if (!readVarint(base, len, pos, dataSize)) return {};
        if (pos + dataSize > len) return {};
        entry.componentData.assign(base + pos, base + pos + dataSize);
        pos += dataSize;

        entries.push_back(std::move(entry));
    }

    return entries;
}

} // namespace engine::networking

// SerializerFuzzTests.cpp
//
// Fuzz harness for BitReader and PacketHeader parsing.
//
// Goals
//   1. Feed random byte buffers of varying lengths into BitReader and assert
//      that every read either succeeds cleanly OR sets the overflow flag — no
//      crashes, UB, or silent partial reads that corrupt state.
//   2. Feed partially-valid packets (real header + random payload) into the
//      header parser to confirm robust rejection.
//   3. Exercise edge cases: empty buffer, single byte, exactly-header-sized.

#include <gtest/gtest.h>
#include <networking/Serializer.h>
#include <networking/Packet.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

using namespace engine::networking;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

// Drain several typed values from a BitReader in a fixed pattern.
// Chosen to exercise a variety of bit widths without being pathological.
void drainReader(BitReader& r) {
    (void)r.readU8();
    (void)r.readU16();
    (void)r.readU32();
    (void)r.readVarint();
    (void)r.readBool();
    (void)r.readU8();
    (void)r.readVarint();
    (void)r.readU32();
}

} // namespace

// ---------------------------------------------------------------------------
// Test 1: Random buffers (0–1400 bytes)
// ---------------------------------------------------------------------------

TEST(SerializerFuzz, RandomBuffersNoCrash) {
    constexpr uint32_t kSeed       = 42u;
    constexpr int      kIterations = 10'000;
    constexpr size_t   kMaxLen     = 1400u;

    std::mt19937                    rng(kSeed);
    std::uniform_int_distribution<> lenDist(0, static_cast<int>(kMaxLen));
    std::uniform_int_distribution<> byteDist(0, 255);

    std::cout << "[SerializerFuzz] RandomBuffersNoCrash seed=" << kSeed << "\n";

    std::vector<uint8_t> buf;
    buf.reserve(kMaxLen);

    for (int i = 0; i < kIterations; ++i) {
        const int len = lenDist(rng);
        buf.resize(static_cast<size_t>(len));
        for (auto& b : buf)
            b = static_cast<uint8_t>(byteDist(rng));

        BitReader r(std::span<const uint8_t>(buf.data(), buf.size()));
        drainReader(r);

        // Contract: overflow flag is the only valid outcome of running out of
        // data — the reader must never crash or produce UB regardless of input.
        // If there were enough bits the overflow flag may be false; if not it
        // must be true.  Either way is fine; a crash is not.
        (void)r.overflow(); // exercise the accessor; no assertion on value
    }

    // Reaching here without aborting/crashing is the pass condition.
    SUCCEED();
}

// ---------------------------------------------------------------------------
// Test 2: Partially-valid packets (valid header + random payload)
// ---------------------------------------------------------------------------

TEST(SerializerFuzz, PartiallyValidPacketsNoCrash) {
    constexpr uint32_t kSeed       = 42u;
    constexpr int      kIterations = 1'000;
    constexpr size_t   kMaxPayload = kMaxFragmentPayload; // 1190 bytes

    // Total buffer = 10-byte header + up to kMaxPayload bytes.
    constexpr size_t kBufMax = sizeof(PacketHeader) + kMaxPayload;

    std::mt19937                    rng(kSeed);
    std::uniform_int_distribution<> seqDist(0, 65535);
    std::uniform_int_distribution<> payloadLenDist(0, static_cast<int>(kMaxPayload));
    std::uniform_int_distribution<> byteDist(0, 255);

    std::cout << "[SerializerFuzz] PartiallyValidPacketsNoCrash seed=" << kSeed << "\n";

    std::array<uint8_t, kBufMax> buf{};

    for (int i = 0; i < kIterations; ++i) {
        const int payloadLen = payloadLenDist(rng);
        const size_t totalLen = sizeof(PacketHeader) + static_cast<size_t>(payloadLen);

        // Write a well-formed header.
        PacketHeader hdr{};
        hdr.protocolId  = kProtocolId;
        hdr.flags       = 0u;
        hdr.sequence    = static_cast<uint16_t>(seqDist(rng));
        hdr.ack         = static_cast<uint16_t>(seqDist(rng));
        hdr.ackBitfield = static_cast<uint32_t>(rng());

        writeHeader(buf.data(), hdr);

        // Fill payload bytes with random data.
        for (size_t b = sizeof(PacketHeader); b < totalLen; ++b)
            buf[b] = static_cast<uint8_t>(byteDist(rng));

        // Parse the header back out.
        const PacketHeader parsed = readHeader(buf.data(), static_cast<uint32_t>(totalLen));
        EXPECT_EQ(parsed.protocolId, kProtocolId)
            << "Header round-trip should preserve protocolId";

        // Feed the whole buffer into a BitReader and parse header fields
        // manually, then drain any remaining bits as garbage payload.
        BitReader r(std::span<const uint8_t>(buf.data(), totalLen));
        const uint8_t  pid  = r.readU8();
        const uint8_t  fl   = r.readU8();
        const uint16_t seq  = r.readU16();
        const uint16_t ack  = r.readU16();
        const uint32_t bits = r.readU32();
        (void)pid; (void)fl; (void)seq; (void)ack; (void)bits;

        // Read garbage payload.
        drainReader(r);

        // Must not crash regardless of overflow state.
        (void)r.overflow();
    }

    SUCCEED();
}

// ---------------------------------------------------------------------------
// Test 3: Edge cases
// ---------------------------------------------------------------------------

TEST(SerializerFuzz, EmptyBuffer) {
    BitReader r(std::span<const uint8_t>{});
    EXPECT_EQ(r.readU8(),  0u);
    EXPECT_EQ(r.readU16(), 0u);
    EXPECT_EQ(r.readU32(), 0u);
    EXPECT_TRUE(r.overflow()) << "All reads on empty buffer must set overflow";
}

TEST(SerializerFuzz, SingleByte) {
    const uint8_t data = 0xAB;
    BitReader r(std::span<const uint8_t>(&data, 1));

    // First 8 bits succeed.
    const uint8_t v = r.readU8();
    EXPECT_EQ(v, 0xAB);
    EXPECT_FALSE(r.overflow()) << "Single U8 read from 1-byte buffer should not overflow";

    // Next read must overflow cleanly.
    (void)r.readU8();
    EXPECT_TRUE(r.overflow()) << "Second U8 read from exhausted 1-byte buffer must overflow";
}

TEST(SerializerFuzz, ExactlyHeaderSize) {
    // A 10-byte buffer holds precisely one PacketHeader.
    std::array<uint8_t, sizeof(PacketHeader)> buf{};
    PacketHeader hdr{};
    hdr.protocolId  = kProtocolId;
    hdr.flags       = 0u;
    hdr.sequence    = 42u;
    hdr.ack         = 7u;
    hdr.ackBitfield = 0xDEADBEEFu;
    writeHeader(buf.data(), hdr);

    // readHeader must succeed without touching out-of-bounds memory.
    const PacketHeader parsed = readHeader(buf.data(), static_cast<uint32_t>(buf.size()));
    EXPECT_EQ(parsed.protocolId,  kProtocolId);
    EXPECT_EQ(parsed.sequence,    42u);
    EXPECT_EQ(parsed.ack,         7u);
    EXPECT_EQ(parsed.ackBitfield, 0xDEADBEEFu);

    // BitReader over the 10-byte buffer.
    BitReader r(std::span<const uint8_t>(buf.data(), buf.size()));
    (void)r.readU8();  // protocolId
    (void)r.readU8();  // flags
    (void)r.readU16(); // sequence
    (void)r.readU16(); // ack
    (void)r.readU32(); // ackBitfield

    EXPECT_FALSE(r.overflow()) << "Reading exactly 10 bytes of a 10-byte buffer must not overflow";

    // One more bit must overflow.
    (void)r.readBool();
    EXPECT_TRUE(r.overflow()) << "Reading beyond the 10-byte buffer must set overflow";
}

TEST(SerializerFuzz, ShortHeaderRejected) {
    // readHeader with size < 10 must return a zeroed struct (protocolId == 0).
    std::array<uint8_t, 9> shortBuf{};
    shortBuf[0] = kProtocolId; // looks valid but buffer is too short
    const PacketHeader parsed = readHeader(shortBuf.data(), static_cast<uint32_t>(shortBuf.size()));
    EXPECT_EQ(parsed.protocolId, 0u)
        << "readHeader with size < 10 must return zeroed header";
}

#pragma once
#include <core/math/Vec.h>
#include <core/math/Quat.h>
#include <cstdint>
#include <span>

namespace engine::networking {

    // -----------------------------------------------------------------------
    // BitWriter
    //
    // Packs values into a caller-supplied byte buffer, bit by bit, LSB-first.
    // All multi-byte integers are written in little-endian bit order (i.e. the
    // least-significant bit of each value lands at the lowest bit index in the
    // buffer), consistent with the wire format defined in
    // networking-transport.md §5.
    //
    // Call overflow() after a batch of writes; on overflow all further writes
    // are silently discarded.
    // -----------------------------------------------------------------------

    class BitWriter {
    public:
        explicit BitWriter(std::span<uint8_t> buffer) noexcept;

        // Write the lowest 'numBits' bits of 'value'. numBits must be in [1, 64].
        void writeBits(uint64_t value, int numBits) noexcept;

        void writeBool(bool v)      noexcept { writeBits(v ? 1u : 0u, 1); }
        void writeU8  (uint8_t  v)  noexcept { writeBits(v, 8); }
        void writeU16 (uint16_t v)  noexcept { writeBits(v, 16); }
        void writeU32 (uint32_t v)  noexcept { writeBits(v, 32); }
        void writeI8  (int8_t   v)  noexcept { writeBits(static_cast<uint64_t>(static_cast<uint8_t>(v)),  8); }
        void writeI16 (int16_t  v)  noexcept { writeBits(static_cast<uint64_t>(static_cast<uint16_t>(v)), 16); }

        // Variable-length uint32: 7 data bits per byte, MSB of each byte is a
        // continuation flag (1 = more bytes follow). Encodes small values cheaply.
        void writeVarint(uint32_t v) noexcept;

        // Quaternion: smallest-three encoding — 2-bit index + 3 × 9-bit
        // components = 29 bits; padded to 32 bits (3 zero padding bits).
        // See networking-transport.md §5.3.
        void writeQuatSmallestThree(const core::math::Quat& q) noexcept;

        // Quantized Vec3: each axis independently encoded with 'bitsPerAxis'
        // bits covering [rangeMin, rangeMin + maxVal*precision].
        // invPrecision = 1.0f / precision (e.g. 1000.0f for 1 mm precision).
        // See networking-transport.md §5.4.
        void writeVec3Quantized(const core::math::Vec3& v,
                                float invPrecision,
                                float rangeMin,
                                int   bitsPerAxis) noexcept;

        // Pad bit stream to the next byte boundary (writes 0 bits).
        void align() noexcept;

        uint32_t bitsWritten()  const noexcept { return bitPos_; }
        uint32_t bytesWritten() const noexcept { return (bitPos_ + 7) / 8; }
        bool     overflow()     const noexcept { return overflow_; }

        // View of the bytes actually written so far.
        std::span<const uint8_t> writtenData() const noexcept;

    private:
        std::span<uint8_t> buf_;
        uint32_t           bitPos_{ 0 };
        bool               overflow_{ false };
    };


    // -----------------------------------------------------------------------
    // BitReader
    //
    // Reads values from a read-only byte span, bit by bit, LSB-first.
    // On buffer exhaustion the overflow flag is set; all subsequent reads
    // return 0 / identity (never UB).  Callers MUST check overflow() after
    // deserializing untrusted data.
    // -----------------------------------------------------------------------

    class BitReader {
    public:
        explicit BitReader(std::span<const uint8_t> data) noexcept;

        // Read 'numBits' bits and return them in the low bits of a uint64_t.
        // numBits must be in [1, 64].
        uint64_t readBits(int numBits) noexcept;

        bool     readBool() noexcept { return readBits(1) != 0; }
        uint8_t  readU8()   noexcept { return static_cast<uint8_t> (readBits(8));  }
        uint16_t readU16()  noexcept { return static_cast<uint16_t>(readBits(16)); }
        uint32_t readU32()  noexcept { return static_cast<uint32_t>(readBits(32)); }
        int8_t   readI8()   noexcept { return static_cast<int8_t>  (readU8());     }
        int16_t  readI16()  noexcept { return static_cast<int16_t> (readU16());    }

        uint32_t         readVarint()              noexcept;
        core::math::Quat readQuatSmallestThree()   noexcept;
        core::math::Vec3 readVec3Quantized(float precision, float rangeMin, int bitsPerAxis) noexcept;

        // Advance read position to the next byte boundary.
        void align() noexcept;

        bool     overflow()  const noexcept { return overflow_; }
        uint32_t bitsRead()  const noexcept { return bitPos_; }

    private:
        std::span<const uint8_t> buf_;
        uint32_t                 bitPos_{ 0 };
        bool                     overflow_{ false };
    };

} // namespace engine::networking

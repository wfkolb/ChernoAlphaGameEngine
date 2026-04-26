// engine/src/networking/Serializer.cpp
// BitWriter / BitReader implementation.
// Wire format: LSB-first bit packing — see networking-transport.md §5.

#include <networking/Serializer.h>
#include <core/math/Vec.h>
#include <core/math/Quat.h>
#include <core/math/Constants.h>
#include <cmath>
#include <cstring>
#include <algorithm>

namespace engine::networking {

    using core::math::Vec3;
    using core::math::Quat;
    using core::math::normalize;

    // -------------------------------------------------------------------------
    // Local constants
    // -------------------------------------------------------------------------

    // Range of each kept quaternion component in smallest-three encoding.
    // The three non-largest components are always in [-1/sqrt(2), 1/sqrt(2)].
    static constexpr float kOneOverSqrt2 = 0.70710678f;
    // Scale factor: maps the full [-1/sqrt(2), +1/sqrt(2)] range onto [0, 511].
    static constexpr float kScale511     = 511.0f / (2.0f * kOneOverSqrt2);

    // =========================================================================
    // BitWriter
    // =========================================================================

    BitWriter::BitWriter(std::span<uint8_t> buffer) noexcept
        : buf_(buffer)
        , bitPos_(0)
        , overflow_(false)
    {
        // Zero-initialise the buffer so padding bits are deterministic.
        std::memset(buf_.data(), 0, buf_.size());
    }

    // -------------------------------------------------------------------------
    // writeBits
    // -------------------------------------------------------------------------
    // Write the 'numBits' low-order bits of 'value' into buf_, LSB first,
    // starting at the current bit position.  Uses a simple bit-by-bit loop
    // for clarity and correctness; a word-level optimisation can be layered on
    // later without changing the API.
    void BitWriter::writeBits(uint64_t value, int numBits) noexcept
    {
        for (int i = 0; i < numBits; ++i) {
            // Check that the byte we're about to touch is within the buffer.
            if ((bitPos_ >> 3) >= static_cast<uint32_t>(buf_.size())) {
                overflow_ = true;
                return;
            }
            if ((value >> i) & 1u) {
                buf_[bitPos_ >> 3] |= static_cast<uint8_t>(1u << (bitPos_ & 7u));
            }
            ++bitPos_;
        }
    }

    // -------------------------------------------------------------------------
    // writeVarint
    // -------------------------------------------------------------------------
    // Encodes a uint32_t as a sequence of 8-bit groups.
    // Bits [6:0] of each group carry 7 data bits; bit 7 is a continuation flag
    // (1 = another group follows, 0 = this is the last group).
    void BitWriter::writeVarint(uint32_t v) noexcept
    {
        do {
            uint8_t group = static_cast<uint8_t>(v & 0x7Fu);  // low 7 data bits
            v >>= 7;
            if (v != 0) {
                group |= 0x80u;  // set continuation flag
            }
            writeBits(group, 8);
        } while (v != 0);
    }

    // -------------------------------------------------------------------------
    // writeQuatSmallestThree
    // -------------------------------------------------------------------------
    // Encodes a unit quaternion in 29 bits (2-bit index + 3 × 9-bit components),
    // padded to 32 bits with 3 trailing zero bits.
    // See networking-transport.md §5.3.
    void BitWriter::writeQuatSmallestThree(const Quat& q) noexcept
    {
        // Step 1: normalise.
        Quat nq = normalize(q);

        // Step 2: find the component with the largest absolute value.
        const float comps[4] = { nq.x, nq.y, nq.z, nq.w };
        int largestIdx = 0;
        float largestAbs = 0.0f;
        for (int i = 0; i < 4; ++i) {
            const float a = std::abs(comps[i]);
            if (a > largestAbs) {
                largestAbs = a;
                largestIdx = i;
            }
        }

        // Step 3: ensure the largest component is positive (negate if needed).
        // This lets the decoder reconstruct it with a simple sqrt (always +ve).
        if (comps[largestIdx] < 0.0f) {
            nq = { -nq.x, -nq.y, -nq.z, -nq.w };
        }

        // Refresh the component array after potential negation.
        const float c[4] = { nq.x, nq.y, nq.z, nq.w };

        // Step 4: write the 2-bit index of the largest component.
        writeBits(static_cast<uint64_t>(largestIdx), 2);

        // Step 5: encode and write the three remaining components, in index order.
        for (int i = 0; i < 4; ++i) {
            if (i == largestIdx) continue;

            // Map component from [-1/sqrt(2), 1/sqrt(2)] to [0, 511].
            const uint32_t encoded = static_cast<uint32_t>(
                (c[i] + kOneOverSqrt2) * kScale511 + 0.5f);

            // Clamp to valid 9-bit range (guard against floating-point rounding).
            const uint32_t clamped = std::min(encoded, 511u);

            writeBits(clamped, 9);
        }

        // Step 6: pad to 32 bits (3 zero bits).  The buffer was zeroed at
        // construction so there is nothing to actually write here, but we
        // advance bitPos_ so bytesWritten() / align() behave correctly.
        writeBits(0u, 3);
    }

    // -------------------------------------------------------------------------
    // writeVec3Quantized
    // -------------------------------------------------------------------------
    // Independently quantises each axis of v onto 'bitsPerAxis' bits covering
    // [rangeMin, rangeMin + maxVal * (1/invPrecision)].
    // 'invPrecision' is 1/precision (e.g. 1000 for 1 mm steps).
    void BitWriter::writeVec3Quantized(const Vec3& v,
                                       float invPrecision,
                                       float rangeMin,
                                       int   bitsPerAxis) noexcept
    {
        const uint32_t maxVal = (1u << bitsPerAxis) - 1u;
        const float    fMax   = static_cast<float>(maxVal);
        const float    axes[3] = { v.x, v.y, v.z };

        for (int i = 0; i < 3; ++i) {
            const uint32_t encoded = static_cast<uint32_t>(
                std::clamp((axes[i] - rangeMin) * invPrecision + 0.5f,
                           0.0f, fMax));
            writeBits(encoded, bitsPerAxis);
        }
    }

    // -------------------------------------------------------------------------
    // align
    // -------------------------------------------------------------------------
    // Advance bitPos_ to the next byte boundary by writing zero bits.
    void BitWriter::align() noexcept
    {
        const uint32_t rem = bitPos_ & 7u;  // bits already written in the current byte
        if (rem != 0) {
            writeBits(0u, 8 - static_cast<int>(rem));
        }
    }

    // -------------------------------------------------------------------------
    // writtenData
    // -------------------------------------------------------------------------
    std::span<const uint8_t> BitWriter::writtenData() const noexcept
    {
        return { buf_.data(), bytesWritten() };
    }


    // =========================================================================
    // BitReader
    // =========================================================================

    BitReader::BitReader(std::span<const uint8_t> data) noexcept
        : buf_(data)
        , bitPos_(0)
        , overflow_(false)
    {}

    // -------------------------------------------------------------------------
    // readBits
    // -------------------------------------------------------------------------
    // Read 'numBits' bits, LSB first, from the current bit position.
    // Returns them packed into the low bits of a uint64_t.
    uint64_t BitReader::readBits(int numBits) noexcept
    {
        uint64_t result = 0;
        for (int i = 0; i < numBits; ++i) {
            if ((bitPos_ >> 3) >= static_cast<uint32_t>(buf_.size())) {
                overflow_ = true;
                return 0;
            }
            if (buf_[bitPos_ >> 3] & (1u << (bitPos_ & 7u))) {
                result |= (1ull << i);
            }
            ++bitPos_;
        }
        return result;
    }

    // -------------------------------------------------------------------------
    // readVarint
    // -------------------------------------------------------------------------
    // Mirror of writeVarint: reads 8-bit groups until the continuation bit
    // (bit 7) is 0, or after 5 groups (overflow guard for malformed data).
    uint32_t BitReader::readVarint() noexcept
    {
        uint32_t result = 0;
        int      shift  = 0;

        for (int group = 0; group < 5; ++group) {
            const uint8_t byte = static_cast<uint8_t>(readBits(8));
            result |= static_cast<uint32_t>(byte & 0x7Fu) << shift;
            shift  += 7;
            if ((byte & 0x80u) == 0) {
                break;  // no continuation flag — we're done
            }
        }
        return result;
    }

    // -------------------------------------------------------------------------
    // readQuatSmallestThree
    // -------------------------------------------------------------------------
    // Decodes a quaternion previously written with writeQuatSmallestThree.
    // Returns Quat::identity() on overflow.
    Quat BitReader::readQuatSmallestThree() noexcept
    {
        // Read the 2-bit index of the largest (missing) component.
        const int largestIdx = static_cast<int>(readBits(2));

        // Decode the three kept components (9 bits each).
        float kept[3];
        for (int i = 0; i < 3; ++i) {
            const uint32_t bits = static_cast<uint32_t>(readBits(9));
            kept[i] = (static_cast<float>(bits) / 511.0f) * (2.0f * kOneOverSqrt2) - kOneOverSqrt2;
        }

        // Consume the 3 padding bits written by the encoder.
        readBits(3);

        // Reconstruct the missing (largest) component.
        // It is always positive by encoding convention.
        const float sumSq  = kept[0] * kept[0] + kept[1] * kept[1] + kept[2] * kept[2];
        const float missing = std::sqrt(std::max(0.0f, 1.0f - sumSq));

        // Reassemble into (x, y, z, w) by inserting the missing component at
        // largestIdx and filling the other positions from 'kept[]' in order.
        float c[4];
        int   k = 0;
        for (int i = 0; i < 4; ++i) {
            if (i == largestIdx) {
                c[i] = missing;
            } else {
                c[i] = kept[k++];
            }
        }

        return { c[0], c[1], c[2], c[3] };
    }

    // -------------------------------------------------------------------------
    // readVec3Quantized
    // -------------------------------------------------------------------------
    // Decodes a Vec3 previously written with writeVec3Quantized.
    Vec3 BitReader::readVec3Quantized(float precision,
                                      float rangeMin,
                                      int   bitsPerAxis) noexcept
    {
        float axes[3];
        for (int i = 0; i < 3; ++i) {
            axes[i] = static_cast<float>(readBits(bitsPerAxis)) * precision + rangeMin;
        }
        return { axes[0], axes[1], axes[2] };
    }

    // -------------------------------------------------------------------------
    // align
    // -------------------------------------------------------------------------
    // Advance bitPos_ to the next byte boundary (skip padding bits).
    void BitReader::align() noexcept
    {
        const uint32_t rem = bitPos_ & 7u;
        if (rem != 0) {
            readBits(8 - static_cast<int>(rem));
        }
    }

} // namespace engine::networking

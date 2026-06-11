#pragma once
#include <cstdint>

namespace engine::networking {

    // ---------------------------------------------------------------------------
    // Protocol constants
    // ---------------------------------------------------------------------------

    inline constexpr uint8_t  kProtocolId        = 0xECu;  // "Engine Connection" magic byte
    inline constexpr uint32_t kProtocolVersion    = 1u;
    inline constexpr uint16_t kMaxConnections     = 16u;
    inline constexpr uint32_t kMaxFragmentPayload = 1190u;  // 1200-byte MTU floor - 10-byte header
    inline constexpr uint32_t kDefaultTickRate    = 64u;    // Hz — server simulation tick rate
    inline constexpr uint32_t kTimeoutMs          = 5000u;
    inline constexpr uint32_t kKeepAliveMs        = 1000u;
    inline constexpr uint32_t kRewindWindowTicks  = 13u;   // 13 ticks @ 64 Hz ≈ 200 ms rewind window
    inline constexpr uint32_t kMaxRewindTicks     = 13u;   // hard cap matches rewind window

    // Per reliable-channel resend policy.
    inline constexpr uint32_t kResendTimeoutMs    = 100u;
    inline constexpr uint32_t kMaxResendAttempts  = 8u;

    // Fragment reassembly policy.
    inline constexpr uint32_t kFragmentTimeoutMs  = 2000u;

    // Soft-reconnect window.
    inline constexpr uint32_t kSoftReconnectWindowMs = 30000u;

    // ---------------------------------------------------------------------------
    // Packet flag bits  (stored in the 'flags' byte of PacketHeader)
    // ---------------------------------------------------------------------------

    enum PacketFlags : uint8_t {
        kFlagReliable  = 1u << 0,  // Packet carries at least one reliable message
        kFlagFragment  = 1u << 1,  // This is a fragment of a larger payload
        kFlagLastFrag  = 1u << 2,  // This is the final fragment of its reassembly group
        kFlagHandshake = 1u << 3,  // Handshake / connection-management packet
        // Bits 4-7 are reserved; receivers MUST drop packets with these bits set.
    };

    // ---------------------------------------------------------------------------
    // PacketHeader — fixed 10-byte wire format, all fields little-endian.
    //
    //  Byte 0       : protocolId  (must equal kProtocolId; packets with wrong ID are dropped)
    //  Byte 1       : flags       (PacketFlags bitmask)
    //  Bytes 2–3    : sequence    (uint16_t, sender's outgoing sequence number)
    //  Bytes 4–5    : ack         (uint16_t, highest remote sequence received by sender)
    //  Bytes 6–9    : ackBitfield (uint32_t, bitmask: bit N set => packet (ack - N - 1) was received)
    // ---------------------------------------------------------------------------

#pragma pack(push, 1)
    struct PacketHeader {
        uint8_t  protocolId;   // 1 byte
        uint8_t  flags;        // 1 byte
        uint16_t sequence;     // 2 bytes
        uint16_t ack;          // 2 bytes
        uint32_t ackBitfield;  // 4 bytes
    };
#pragma pack(pop)
    static_assert(sizeof(PacketHeader) == 10,
                  "PacketHeader must be exactly 10 bytes");

    // Read a PacketHeader from a raw byte buffer.
    // Returns a zeroed header if 'size' < 10 (caller must validate protocolId).
    PacketHeader readHeader(const uint8_t* data, uint32_t size);

    // Write a PacketHeader into a raw byte buffer.
    // Caller must ensure 'data' points to at least 10 writable bytes.
    void writeHeader(uint8_t* data, const PacketHeader& h);

} // namespace engine::networking

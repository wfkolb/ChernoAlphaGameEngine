#pragma once
#include <networking/Socket.h>
#include <networking/Endpoint.h>
#include <cstdint>
#include <functional>
#include <span>
#include <unordered_map>
#include <vector>

namespace engine::networking {

// Best-effort, newest-wins-per-entity channel. Sits alongside ReliableChannel
// inside a Session and carries state that supersedes itself every snapshot:
// Health, WeaponState, AnimationState. No ACKs, no retransmission.
//
// Each message is keyed by (netId, componentBit). The receiver tracks the
// highest sequence applied per key; a message whose sequence is older-or-equal
// (modulo 16-bit wraparound) is silently dropped. The sender assigns a
// monotonically increasing 16-bit sequence per key.
//
// Wire layout per message: netId(4) componentBit(1) sequence(2) payloadLen(2) payload(N)
class UnreliableOrderedChannel {
public:
    using ReceiveCallback = std::function<void(
        uint32_t netId, uint8_t componentBit, std::span<const uint8_t> payload)>;

    UnreliableOrderedChannel(Socket socket, Endpoint remote);

    // Send a component update. Stamps the next sequence for (netId, componentBit)
    // and transmits unreliably. Returns false only if the socket would block.
    bool send(uint32_t netId, uint8_t componentBit, std::span<const uint8_t> payload);

    // Drain pending datagrams; invoke onMessage for each accepted (non-stale) one.
    void poll(const ReceiveCallback& onMessage);

    // Receiver-side acceptance test, exposed for testing and reuse. Returns true
    // and records the sequence when it is newer than the last applied for the key;
    // returns false (and records nothing) for stale or duplicate sequences.
    bool acceptSequence(uint32_t netId, uint8_t componentBit, uint16_t sequence);

    // Next sequence the sender will stamp for a key (without consuming it).
    uint16_t peekSendSequence(uint32_t netId, uint8_t componentBit) const;

    const Endpoint& remote() const noexcept { return remote_; }
    void reset() noexcept;

private:
    static constexpr size_t   kMtu        = 1200;
    static constexpr size_t   kHeaderSize = 9;  // netId(4)+bit(1)+seq(2)+len(2)

    // 16-bit sequence "newer than" with wraparound (RFC 1982 style).
    static bool seqNewer(uint16_t a, uint16_t b) noexcept {
        return (a != b) &&
               (static_cast<uint16_t>(a - b) < 0x8000u);
    }

    static uint64_t makeKey(uint32_t netId, uint8_t componentBit) noexcept {
        return (static_cast<uint64_t>(netId) << 8) | componentBit;
    }

    Socket   socket_;
    Endpoint remote_;

    std::unordered_map<uint64_t, uint16_t> sendSeq_;     // next seq to stamp per key
    std::unordered_map<uint64_t, uint16_t> lastApplied_; // highest applied seq per key
    std::unordered_map<uint64_t, bool>     hasApplied_;  // whether key has any applied seq

    uint8_t recvBuf_[kMtu]{};
};

} // namespace engine::networking

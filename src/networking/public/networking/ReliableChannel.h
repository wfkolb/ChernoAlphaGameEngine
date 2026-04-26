#pragma once
#include "networking/Socket.h"
#include "networking/Endpoint.h"
#include "networking/Packet.h"
#include <functional>
#include <span>
#include <vector>
#include <cstdint>

namespace engine::networking {

// Reliable-ordered channel over a single UDP socket.
// Wraps a Socket and adds: sequence numbers, acks, resend queue.
class ReliableChannel {
public:
    using ReceiveCallback = std::function<void(const Endpoint&, std::span<const uint8_t>)>;

    explicit ReliableChannel(Socket socket, Endpoint remoteEndpoint);

    // Send a payload reliably (it will be resent until acked).
    // payload must be <= 1190 bytes (MTU=1200 minus header=10).
    bool send(std::span<const uint8_t> payload);

    // Process incoming packets; invoke callback for each fully-received payload.
    // Also drives resend of unacked outgoing packets.
    void poll(ReceiveCallback onMessage);

    // Returns the remote endpoint this channel is connected to.
    const Endpoint& remote() const { return remote_; }

    // Local endpoint (bound address of the socket).
    Endpoint localEndpoint() const;

private:
    static constexpr size_t   kMtu             = 1200;
    static constexpr size_t   kHeaderSize       = 10;   // PacketHeader
    static constexpr size_t   kMaxPayload       = kMtu - kHeaderSize;
    static constexpr uint64_t kResendWindowMs   = 100;  // resend unacked after 100ms
    static constexpr uint32_t kAckBitfieldBits  = 32;

    struct OutboundEntry {
        uint16_t             sequence;
        std::vector<uint8_t> payload;
        uint64_t             sentTimeMs;  // QueryPerformanceCounter-based ms
    };

    Socket   socket_;
    Endpoint remote_;

    uint16_t sendSequence_    = 0;  // next sequence number to send
    uint16_t recvSequence_    = 0;  // highest received sequence so far
    uint32_t recvAckBitfield_ = 0;  // bitfield of received sequences before recvSequence_

    std::vector<OutboundEntry> resendQueue_;  // unacked outbound packets

    uint8_t recvBuf_[kMtu]{};

    // Returns current time in milliseconds (monotonic).
    static uint64_t nowMs();

    // Update recvSequence_ and recvAckBitfield_ when a packet arrives with given sequence.
    void updateAckState(uint16_t seq);

    // Remove entries from resendQueue_ that have been acked by the remote's ack header.
    void processAcks(uint16_t remoteAck, uint32_t remoteAckBitfield);

    // Resend any outbound entries older than kResendWindowMs that haven't been acked.
    void resendUnacked();
};

} // namespace engine::networking

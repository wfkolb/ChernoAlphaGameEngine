// Must be first â€" ensures Winsock is included before <windows.h>.
#include "WinsockInclude.h"

#include "networking/ReliableChannel.h"
#include <core/diag/Assert.h>
#include <core/time/Clock.h>

#include <algorithm>
#include <cstring>

namespace engine::networking {

// ---------------------------------------------------------------------------
// nowMs â€" monotonic millisecond clock via Clock abstraction
// ---------------------------------------------------------------------------

uint64_t ReliableChannel::nowMs() {
    // Clock::now() returns nanoseconds; divide by 1,000,000 to get milliseconds.
    return engine::core::time::Clock::now() / 1'000'000u;
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

ReliableChannel::ReliableChannel(Socket socket, Endpoint remoteEndpoint)
    : socket_(std::move(socket))
    , remote_(remoteEndpoint)
{
    ENGINE_ASSERT(socket_.isValid(), "ReliableChannel requires a valid socket");
}

// ---------------------------------------------------------------------------
// send
// ---------------------------------------------------------------------------

bool ReliableChannel::send(std::span<const uint8_t> payload) {
    ENGINE_ASSERT(payload.size() <= kMaxPayload, "payload exceeds MTU limit");
    if (payload.size() > kMaxPayload) {
        return false;
    }

    // Build the datagram: 10-byte header + payload.
    uint8_t buf[kMtu];

    PacketHeader hdr{};
    hdr.protocolId  = kProtocolId;
    hdr.flags       = kFlagReliable;
    hdr.sequence    = sendSequence_;
    hdr.ack         = recvSequence_;
    hdr.ackBitfield = recvAckBitfield_;
    writeHeader(buf, hdr);

    std::memcpy(buf + kHeaderSize, payload.data(), payload.size());
    const size_t totalSize = kHeaderSize + payload.size();

    // Enqueue before sending so we can resend if it's lost.
    OutboundEntry entry;
    entry.sequence  = sendSequence_;
    entry.payload.assign(payload.begin(), payload.end());
    entry.sentTimeMs = nowMs();
    resendQueue_.push_back(std::move(entry));

    ++sendSequence_;

    socket_.send(remote_, std::span<const uint8_t>(buf, totalSize));
    return true;
}

// ---------------------------------------------------------------------------
// updateAckState
// ---------------------------------------------------------------------------

void ReliableChannel::updateAckState(uint16_t seq) {
    if (seq == recvSequence_) {
        // Duplicate of the current highest â€" ignore (already tracked).
        return;
    }

    // Sequence numbers wrap at 16 bits; treat difference mod 2^16.
    // We consider seq "newer" if it's within 32767 ahead of recvSequence_.
    const uint16_t diff = static_cast<uint16_t>(seq - recvSequence_);

    if (diff > 0 && diff <= 32767u) {
        // seq is newer than recvSequence_.
        if (diff < kAckBitfieldBits) {
            // Shift bitfield left by diff bits; bit (diff-1) represents the old recvSequence_.
            recvAckBitfield_ = (recvAckBitfield_ << diff) | (1u << (diff - 1));
        } else {
            // Gap is larger than the bitfield can represent â€" clear it entirely.
            recvAckBitfield_ = 0;
        }
        recvSequence_ = seq;
    } else {
        // seq is older than recvSequence_ â€" mark its bit in the bitfield.
        // bit N represents recvSequence_ - N - 1, so seq maps to bit (recvSequence_ - seq - 1).
        const uint16_t age = static_cast<uint16_t>(recvSequence_ - seq);
        if (age >= 1 && age <= kAckBitfieldBits) {
            recvAckBitfield_ |= (1u << (age - 1));
        }
        // If age > kAckBitfieldBits the packet is too old to track; drop silently.
    }
}

// ---------------------------------------------------------------------------
// processAcks
// ---------------------------------------------------------------------------

void ReliableChannel::processAcks(uint16_t remoteAck, uint32_t remoteAckBitfield) {
    // Erase entries whose sequence number is acknowledged by the remote header.
    // Sequence s is acked if:
    //   s == remoteAck, OR
    //   (remoteAck - s - 1) < 32 AND bit (remoteAck - s - 1) of remoteAckBitfield is set.

    resendQueue_.erase(
        std::remove_if(resendQueue_.begin(), resendQueue_.end(),
            [&](const OutboundEntry& e) -> bool {
                if (e.sequence == remoteAck) {
                    return true;
                }
                const uint16_t age = static_cast<uint16_t>(remoteAck - e.sequence);
                if (age >= 1 && age <= kAckBitfieldBits) {
                    return (remoteAckBitfield & (1u << (age - 1))) != 0;
                }
                return false;
            }),
        resendQueue_.end());
}

// ---------------------------------------------------------------------------
// resendUnacked
// ---------------------------------------------------------------------------

void ReliableChannel::resendUnacked() {
    const uint64_t now = nowMs();

    for (OutboundEntry& entry : resendQueue_) {
        if (now - entry.sentTimeMs >= kResendWindowMs) {
            uint8_t buf[kMtu];

            PacketHeader hdr{};
            hdr.protocolId  = kProtocolId;
            hdr.flags       = kFlagReliable;
            hdr.sequence    = entry.sequence;
            hdr.ack         = recvSequence_;
            hdr.ackBitfield = recvAckBitfield_;
            writeHeader(buf, hdr);

            std::memcpy(buf + kHeaderSize, entry.payload.data(), entry.payload.size());
            const size_t totalSize = kHeaderSize + entry.payload.size();

            socket_.send(remote_, std::span<const uint8_t>(buf, totalSize));
            entry.sentTimeMs = now;
        }
    }
}

// ---------------------------------------------------------------------------
// poll
// ---------------------------------------------------------------------------

void ReliableChannel::poll(ReceiveCallback onMessage) {
    resendUnacked();

    Endpoint from{};
    uint32_t bytesReceived = 0;

    while (socket_.recv(from, std::span<uint8_t>(recvBuf_, kMtu), bytesReceived)) {
        if (bytesReceived < static_cast<uint32_t>(kHeaderSize)) {
            continue;  // too short to contain a header
        }

        const PacketHeader hdr = readHeader(recvBuf_, bytesReceived);
        if (hdr.protocolId != kProtocolId) {
            continue;  // wrong protocol; drop
        }

        processAcks(hdr.ack, hdr.ackBitfield);
        updateAckState(hdr.sequence);

        const uint32_t payloadSize = bytesReceived - static_cast<uint32_t>(kHeaderSize);
        if (payloadSize > 0 && onMessage) {
            onMessage(from, std::span<const uint8_t>(recvBuf_ + kHeaderSize, payloadSize));
        }
    }
}

// ---------------------------------------------------------------------------
// localEndpoint
// ---------------------------------------------------------------------------

Endpoint ReliableChannel::localEndpoint() const {
    return socket_.localEndpoint();
}

} // namespace engine::networking

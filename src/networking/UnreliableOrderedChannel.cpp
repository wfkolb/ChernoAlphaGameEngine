// Must be first — ensures Winsock is included before <windows.h>.
#include "WinsockInclude.h"

#include "networking/UnreliableOrderedChannel.h"
#include <core/diag/Assert.h>

#include <cstring>

namespace engine::networking {

UnreliableOrderedChannel::UnreliableOrderedChannel(Socket socket, Endpoint remote)
    : socket_(std::move(socket))
    , remote_(remote)
{
    ENGINE_ASSERT(socket_.isValid(),
                  "UnreliableOrderedChannel requires a valid socket");
}

uint16_t UnreliableOrderedChannel::peekSendSequence(uint32_t netId,
                                                    uint8_t  componentBit) const {
    const auto it = sendSeq_.find(makeKey(netId, componentBit));
    return it == sendSeq_.end() ? 0u : it->second;
}

bool UnreliableOrderedChannel::send(uint32_t netId,
                                    uint8_t  componentBit,
                                    std::span<const uint8_t> payload) {
    const size_t total = kHeaderSize + payload.size();
    if (total > kMtu) {
        ENGINE_ASSERT(false, "UnreliableOrderedChannel payload exceeds MTU");
        return false;
    }

    const uint64_t key = makeKey(netId, componentBit);
    const uint16_t seq = sendSeq_[key]++;  // stamp, then advance (wraps at 16 bits)

    uint8_t buf[kMtu];
    std::memcpy(buf + 0, &netId, 4);
    buf[4] = componentBit;
    std::memcpy(buf + 5, &seq, 2);
    const uint16_t len = static_cast<uint16_t>(payload.size());
    std::memcpy(buf + 7, &len, 2);
    if (!payload.empty())
        std::memcpy(buf + kHeaderSize, payload.data(), payload.size());

    return socket_.send(remote_, std::span<const uint8_t>(buf, total));
}

void UnreliableOrderedChannel::poll(const ReceiveCallback& onMessage) {
    Endpoint from{};
    uint32_t bytes = 0;
    while (socket_.recv(from, std::span<uint8_t>(recvBuf_, kMtu), bytes)) {
        if (bytes < kHeaderSize) continue;

        uint32_t netId = 0;
        uint16_t seq   = 0;
        uint16_t len   = 0;
        std::memcpy(&netId, recvBuf_ + 0, 4);
        const uint8_t componentBit = recvBuf_[4];
        std::memcpy(&seq, recvBuf_ + 5, 2);
        std::memcpy(&len, recvBuf_ + 7, 2);

        if (kHeaderSize + len > bytes) continue;  // truncated / malformed

        // Newest-wins: drop stale or duplicate sequences for this key.
        if (!acceptSequence(netId, componentBit, seq)) continue;

        if (onMessage)
            onMessage(netId, componentBit,
                      std::span<const uint8_t>(recvBuf_ + kHeaderSize, len));
    }
}

bool UnreliableOrderedChannel::acceptSequence(uint32_t netId,
                                              uint8_t  componentBit,
                                              uint16_t sequence) {
    const uint64_t key = makeKey(netId, componentBit);

    const auto it = hasApplied_.find(key);
    const bool seen = it != hasApplied_.end() && it->second;
    if (seen && !seqNewer(sequence, lastApplied_[key]))
        return false;

    lastApplied_[key] = sequence;
    hasApplied_[key]  = true;
    return true;
}

void UnreliableOrderedChannel::reset() noexcept {
    sendSeq_.clear();
    lastApplied_.clear();
    hasApplied_.clear();
}

} // namespace engine::networking

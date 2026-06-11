#pragma once
#include "networking/ReliableChannel.h"
#include <functional>
#include <memory>
#include <span>
#include <utility>
#include <vector>
#include <cstdint>

namespace engine::networking {

// High-level connection session. Wraps a ReliableChannel.
// In v1, only connected (point-to-point) sessions are supported.
class Session {
public:
    using MessageCallback = std::function<void(const Endpoint&, std::span<const uint8_t>)>;

    // Creates two sessions bound to loopback (127.0.0.1), already connected to each other.
    // For testing only. First is the "server", second is the "client".
    static std::pair<Session, Session> createLocalPair(uint16_t basePort = 17777);

    Session() = default;

    // Register a callback invoked by poll() for each received message.
    void onMessage(MessageCallback cb) { onMessage_ = std::move(cb); }

    // Send a message to the remote peer.
    bool send(const Endpoint& to, std::span<const uint8_t> data);

    // Process incoming packets; fires onMessage_ callback for each.
    void poll();

    // The local address this session is bound to.
    Endpoint localEndpoint() const;

    // Set a XOR obfuscation key applied to all outgoing and incoming packets.
    // Both peers must use the same key. Empty key disables obfuscation.
    // NOT cryptographic — a future DTLS layer should replace this entirely.
    void setObfuscationKey(std::span<const uint8_t> key) {
        obfuscationKey_.assign(key.begin(), key.end());
    }

private:
    std::unique_ptr<ReliableChannel> channel_;
    MessageCallback onMessage_;
    std::vector<uint8_t> obfuscationKey_;
};

} // namespace engine::networking

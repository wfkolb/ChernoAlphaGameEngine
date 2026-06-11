// Must be first â€" ensures Winsock is included before <windows.h>.
#include "WinsockInclude.h"

#include "networking/Session.h"
#include "networking/PacketObfuscation.h"
#include <core/diag/Assert.h>

#include <cstring>
#include <vector>

namespace engine::networking {

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

namespace {

    // Build an IPv4-mapped loopback Endpoint for 127.0.0.1 at the given port.
    Endpoint makeLoopbackEndpoint(uint16_t port) noexcept {
        Endpoint ep{};
        ep.port   = port;
        ep.isIPv6 = false;
        // IPv4-mapped form: bytes 0-9 = 0, bytes 10-11 = 0xFF, bytes 12-15 = 127.0.0.1
        ep.addr[10] = 0xFF;
        ep.addr[11] = 0xFF;
        ep.addr[12] = 127;
        ep.addr[13] = 0;
        ep.addr[14] = 0;
        ep.addr[15] = 1;
        return ep;
    }

} // anonymous namespace

// ---------------------------------------------------------------------------
// Session::createLocalPair
// ---------------------------------------------------------------------------

std::pair<Session, Session> Session::createLocalPair(uint16_t basePort) {
    // Create two IPv4-only sockets bound to loopback so that we avoid the
    // dual-stack / IPv4-mapped address mismatch on Windows loopback.
    Socket serverSock = Socket::createUdp(/*dualStack=*/false);
    Socket clientSock = Socket::createUdp(/*dualStack=*/false);

    ENGINE_ASSERT(serverSock.isValid(), "createLocalPair: server socket creation failed");
    ENGINE_ASSERT(clientSock.isValid(), "createLocalPair: client socket creation failed");

    serverSock.bind(basePort);
    clientSock.bind(static_cast<uint16_t>(basePort + 1));

    const Endpoint serverEndpoint = makeLoopbackEndpoint(basePort);
    const Endpoint clientEndpoint = makeLoopbackEndpoint(static_cast<uint16_t>(basePort + 1));

    Session server;
    server.channel_ = std::make_unique<ReliableChannel>(
        std::move(serverSock), clientEndpoint);

    Session client;
    client.channel_ = std::make_unique<ReliableChannel>(
        std::move(clientSock), serverEndpoint);

    return { std::move(server), std::move(client) };
}

// ---------------------------------------------------------------------------
// Session::send
// ---------------------------------------------------------------------------

bool Session::send(const Endpoint& /*to*/, std::span<const uint8_t> data) {
    ENGINE_ASSERT(channel_ != nullptr, "Session::send called on uninitialised session");
    // Point-to-point in v1: the channel already knows its remote; ignore 'to'.
    if (!obfuscationKey_.empty()) {
        std::vector<uint8_t> obfuscated(data.begin(), data.end());
        xorObfuscate(std::span<uint8_t>(obfuscated), obfuscationKey_);
        return channel_->send(obfuscated);
    }
    return channel_->send(data);
}

// ---------------------------------------------------------------------------
// Session::poll
// ---------------------------------------------------------------------------

void Session::poll() {
    ENGINE_ASSERT(channel_ != nullptr, "Session::poll called on uninitialised session");
    if (!obfuscationKey_.empty()) {
        // Deobfuscate received bytes before passing them to the application callback.
        channel_->poll([this](const Endpoint& ep, std::span<const uint8_t> raw) {
            std::vector<uint8_t> deobfuscated(raw.begin(), raw.end());
            xorObfuscate(std::span<uint8_t>(deobfuscated), obfuscationKey_);
            if (onMessage_) onMessage_(ep, deobfuscated);
        });
    } else {
        channel_->poll(onMessage_);
    }
}

// ---------------------------------------------------------------------------
// Session::localEndpoint
// ---------------------------------------------------------------------------

Endpoint Session::localEndpoint() const {
    ENGINE_ASSERT(channel_ != nullptr, "Session::localEndpoint called on uninitialised session");
    return channel_->localEndpoint();
}

} // namespace engine::networking

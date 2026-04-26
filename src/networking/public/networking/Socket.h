#pragma once
#include <core/diag/Assert.h>
#include <networking/Endpoint.h>
#include <cstdint>
#include <span>

namespace engine::networking {

    // RAII non-blocking UDP socket wrapper.
    // No Winsock types appear in this header.
    //
    // Usage:
    //   auto sock = Socket::createUdp();
    //   sock.bind(7777);
    //   sock.send(remote, data);
    //
    //   Endpoint from{};
    //   uint32_t bytes = 0;
    //   sock.recv(from, recvBuf, bytes);
    class Socket {
    public:
        ENGINE_NO_COPY(Socket);

        // Create a non-blocking UDP socket.
        // dualStack=true: AF_INET6 with IPV6_V6ONLY=FALSE (accepts IPv4-mapped connections).
        // dualStack=false: AF_INET only.
        static Socket createUdp(bool dualStack = true);

        // Bind to all interfaces on the given port.
        // port=0: OS assigns an ephemeral port (retrieve with localEndpoint()).
        void bind(uint16_t port = 0);

        // Query the locally bound address and port after bind().
        Endpoint localEndpoint() const;

        // Non-blocking send. Returns false ONLY when the send would block (WSAEWOULDBLOCK).
        // Asserts (debug) or logs+drops (release) on other errors.
        bool send(const Endpoint& to, std::span<const uint8_t> data);

        // Non-blocking receive. Returns false when no data is available (WSAEWOULDBLOCK).
        // On success: fromOut and bytesReceivedOut are populated and true is returned.
        bool recv(Endpoint& fromOut, std::span<uint8_t> buf, uint32_t& bytesReceivedOut);

        bool isValid() const noexcept;

        Socket(Socket&&) noexcept;
        Socket& operator=(Socket&&) noexcept;
        ~Socket(); // calls closesocket() if valid

        Socket() = default; // produces an invalid socket

    private:
        // The socket handle is stored as uintptr_t so that SOCKET (which is
        // a typedef for uintptr_t on Win64) never appears in this public header.
        // kInvalidHandle == INVALID_SOCKET == ~0ull on Win64.
        static constexpr uintptr_t kInvalidHandle = static_cast<uintptr_t>(~0ull);

        uintptr_t handle_{ kInvalidHandle };
        bool      dualStack_{ false };
    };

} // namespace engine::networking

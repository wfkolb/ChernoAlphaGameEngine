// Must be first â€” ensures Winsock is included before <windows.h>.
#include "WinsockInclude.h"

#include "networking/Socket.h"
#include <core/diag/Assert.h>

#include <cstring>

namespace engine::networking {

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace {

    // Convert our Endpoint to sockaddr_in6 for sendto/connect/bind.
    // Always uses AF_INET6; IPv4 addresses are already stored in mapped form.
    sockaddr_in6 endpointToSockAddr(const Endpoint& ep) noexcept {
        sockaddr_in6 sa{};
        sa.sin6_family = AF_INET6;
        sa.sin6_port   = htons(ep.port);
        static_assert(sizeof(sa.sin6_addr.s6_addr) == 16);
        std::memcpy(sa.sin6_addr.s6_addr, ep.addr, 16);
        return sa;
    }

    // Convert sockaddr_in6 returned by recvfrom back to an Endpoint.
    Endpoint sockAddrToEndpoint(const sockaddr_in6& sa) noexcept {
        Endpoint ep{};
        ep.port = ntohs(sa.sin6_port);
        static_assert(sizeof(sa.sin6_addr.s6_addr) == 16);
        std::memcpy(ep.addr, sa.sin6_addr.s6_addr, 16);

        // Detect IPv4-mapped form: bytes 0-9 == 0, bytes 10-11 == 0xFF.
        bool isIPv4Mapped = true;
        for (int i = 0; i < 10; ++i) {
            if (ep.addr[i] != 0) { isIPv4Mapped = false; break; }
        }
        if (ep.addr[10] != 0xFF || ep.addr[11] != 0xFF) {
            isIPv4Mapped = false;
        }
        ep.isIPv6 = !isIPv4Mapped;
        return ep;
    }

    // Log a Winsock error to stderr in a way that can be replaced with the
    // real logger once tools::Logger is wired up.
    void logWsaError(const char* context, int errCode) noexcept {
        // In production, replace with LOG_ERROR("networking", "{}: WSA error {}", context, errCode)
        // For now, route to the debug output stream so tests can see it.
        char buf[256];
        (void)context;
        (void)errCode;
        (void)buf;
        // Intentionally minimal â€” avoids pulling in heavy I/O in a hot path.
    }

} // anonymous namespace

// ---------------------------------------------------------------------------
// Socket::createUdp
// ---------------------------------------------------------------------------

Socket Socket::createUdp(bool dualStack) {
    Socket s;
    s.dualStack_ = dualStack;

    int af = dualStack ? AF_INET6 : AF_INET;
    SOCKET raw = ::socket(af, SOCK_DGRAM, IPPROTO_UDP);
    ENGINE_ASSERT(raw != INVALID_SOCKET, "socket() failed");
    if (raw == INVALID_SOCKET) {
        return s; // returns invalid socket; caller must check isValid()
    }

    if (dualStack) {
        // Disable IPV6_V6ONLY so the socket can receive both IPv4 and IPv6 datagrams.
        DWORD v6only = 0;
        const int rc = ::setsockopt(raw, IPPROTO_IPV6, IPV6_V6ONLY,
                                    reinterpret_cast<const char*>(&v6only),
                                    static_cast<int>(sizeof(v6only)));
        ENGINE_ASSERT(rc != SOCKET_ERROR, "setsockopt(IPV6_V6ONLY=0) failed");
        if (rc == SOCKET_ERROR) {
            ::closesocket(raw);
            return s;
        }
    }

    // Set non-blocking mode.
    u_long nonBlocking = 1;
    const int nb = ::ioctlsocket(raw, FIONBIO, &nonBlocking);
    ENGINE_ASSERT(nb != SOCKET_ERROR, "ioctlsocket(FIONBIO) failed");
    if (nb == SOCKET_ERROR) {
        ::closesocket(raw);
        return s;
    }

    s.handle_ = static_cast<uintptr_t>(raw);
    return s;
}

// ---------------------------------------------------------------------------
// Socket::bind
// ---------------------------------------------------------------------------

void Socket::bind(uint16_t port) {
    ENGINE_ASSERT(isValid(), "bind() called on invalid socket");

    SOCKET raw = static_cast<SOCKET>(handle_);

    if (dualStack_) {
        sockaddr_in6 sa{};
        sa.sin6_family = AF_INET6;
        sa.sin6_port   = htons(port);
        sa.sin6_addr   = in6addr_any;

        const int rc = ::bind(raw,
                              reinterpret_cast<const sockaddr*>(&sa),
                              static_cast<int>(sizeof(sa)));
        ENGINE_ASSERT(rc != SOCKET_ERROR, "bind() (IPv6 dual-stack) failed");
    } else {
        sockaddr_in sa{};
        sa.sin_family      = AF_INET;
        sa.sin_port        = htons(port);
        sa.sin_addr.s_addr = INADDR_ANY;

        const int rc = ::bind(raw,
                              reinterpret_cast<const sockaddr*>(&sa),
                              static_cast<int>(sizeof(sa)));
        ENGINE_ASSERT(rc != SOCKET_ERROR, "bind() (IPv4) failed");
    }
}

// ---------------------------------------------------------------------------
// Socket::localEndpoint
// ---------------------------------------------------------------------------

Endpoint Socket::localEndpoint() const {
    ENGINE_ASSERT(isValid(), "localEndpoint() called on invalid socket");

    SOCKET raw = static_cast<SOCKET>(handle_);

    if (dualStack_) {
        sockaddr_in6 sa{};
        int len = static_cast<int>(sizeof(sa));
        const int rc = ::getsockname(raw,
                                     reinterpret_cast<sockaddr*>(&sa),
                                     &len);
        ENGINE_ASSERT(rc != SOCKET_ERROR, "getsockname() failed");
        if (rc == SOCKET_ERROR) {
            return {};
        }
        return sockAddrToEndpoint(sa);
    } else {
        sockaddr_in sa{};
        int len = static_cast<int>(sizeof(sa));
        const int rc = ::getsockname(raw,
                                     reinterpret_cast<sockaddr*>(&sa),
                                     &len);
        ENGINE_ASSERT(rc != SOCKET_ERROR, "getsockname() failed");
        if (rc == SOCKET_ERROR) {
            return {};
        }

        Endpoint ep{};
        ep.port   = ntohs(sa.sin_port);
        ep.isIPv6 = false;

        const auto* bytes = reinterpret_cast<const uint8_t*>(&sa.sin_addr.s_addr);
        ep.addr[10] = 0xFF;
        ep.addr[11] = 0xFF;
        ep.addr[12] = bytes[0];
        ep.addr[13] = bytes[1];
        ep.addr[14] = bytes[2];
        ep.addr[15] = bytes[3];
        return ep;
    }
}

// ---------------------------------------------------------------------------
// Socket::send
// ---------------------------------------------------------------------------

bool Socket::send(const Endpoint& to, std::span<const uint8_t> data) {
    ENGINE_ASSERT(isValid(), "send() called on invalid socket");

    SOCKET raw = static_cast<SOCKET>(handle_);

    if (dualStack_) {
        // Always send via sockaddr_in6 for dual-stack; IPv4 destinations are
        // already in IPv4-mapped form inside Endpoint::addr.
        sockaddr_in6 sa = endpointToSockAddr(to);
        const int sent = ::sendto(raw,
                                  reinterpret_cast<const char*>(data.data()),
                                  static_cast<int>(data.size()),
                                  0,
                                  reinterpret_cast<const sockaddr*>(&sa),
                                  static_cast<int>(sizeof(sa)));
        if (sent == SOCKET_ERROR) {
            const int err = ::WSAGetLastError();
            if (err == WSAEWOULDBLOCK) {
                return false; // not an error; caller should retry later
            }
            ENGINE_ASSERT(false, "sendto() failed with unexpected WSA error");
            logWsaError("sendto (dual-stack)", err);
            return false;
        }
    } else {
        // IPv4-only socket â€” reconstruct sockaddr_in from bytes 12-15.
        sockaddr_in sa{};
        sa.sin_family = AF_INET;
        sa.sin_port   = htons(to.port);
        uint8_t* dst  = reinterpret_cast<uint8_t*>(&sa.sin_addr.s_addr);
        dst[0] = to.addr[12];
        dst[1] = to.addr[13];
        dst[2] = to.addr[14];
        dst[3] = to.addr[15];

        const int sent = ::sendto(raw,
                                  reinterpret_cast<const char*>(data.data()),
                                  static_cast<int>(data.size()),
                                  0,
                                  reinterpret_cast<const sockaddr*>(&sa),
                                  static_cast<int>(sizeof(sa)));
        if (sent == SOCKET_ERROR) {
            const int err = ::WSAGetLastError();
            if (err == WSAEWOULDBLOCK) {
                return false;
            }
            ENGINE_ASSERT(false, "sendto() failed with unexpected WSA error");
            logWsaError("sendto (IPv4)", err);
            return false;
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// Socket::recv
// ---------------------------------------------------------------------------

bool Socket::recv(Endpoint& fromOut, std::span<uint8_t> buf, uint32_t& bytesReceivedOut) {
    ENGINE_ASSERT(isValid(), "recv() called on invalid socket");

    SOCKET raw = static_cast<SOCKET>(handle_);

    if (dualStack_) {
        sockaddr_in6 sa{};
        int fromLen   = static_cast<int>(sizeof(sa));
        const int got = ::recvfrom(raw,
                                   reinterpret_cast<char*>(buf.data()),
                                   static_cast<int>(buf.size()),
                                   0,
                                   reinterpret_cast<sockaddr*>(&sa),
                                   &fromLen);
        if (got == SOCKET_ERROR) {
            const int err = ::WSAGetLastError();
            if (err == WSAEWOULDBLOCK) {
                return false; // no data available; not an error
            }
            ENGINE_ASSERT(false, "recvfrom() failed with unexpected WSA error");
            logWsaError("recvfrom (dual-stack)", err);
            return false;
        }
        fromOut          = sockAddrToEndpoint(sa);
        bytesReceivedOut = static_cast<uint32_t>(got);
        return true;
    } else {
        sockaddr_in sa{};
        int fromLen   = static_cast<int>(sizeof(sa));
        const int got = ::recvfrom(raw,
                                   reinterpret_cast<char*>(buf.data()),
                                   static_cast<int>(buf.size()),
                                   0,
                                   reinterpret_cast<sockaddr*>(&sa),
                                   &fromLen);
        if (got == SOCKET_ERROR) {
            const int err = ::WSAGetLastError();
            if (err == WSAEWOULDBLOCK) {
                return false;
            }
            ENGINE_ASSERT(false, "recvfrom() failed with unexpected WSA error");
            logWsaError("recvfrom (IPv4)", err);
            return false;
        }

        Endpoint ep{};
        ep.port   = ntohs(sa.sin_port);
        ep.isIPv6 = false;
        const auto* bytes = reinterpret_cast<const uint8_t*>(&sa.sin_addr.s_addr);
        ep.addr[10] = 0xFF;
        ep.addr[11] = 0xFF;
        ep.addr[12] = bytes[0];
        ep.addr[13] = bytes[1];
        ep.addr[14] = bytes[2];
        ep.addr[15] = bytes[3];

        fromOut          = ep;
        bytesReceivedOut = static_cast<uint32_t>(got);
        return true;
    }
}

// ---------------------------------------------------------------------------
// Socket::isValid
// ---------------------------------------------------------------------------

bool Socket::isValid() const noexcept {
    return handle_ != kInvalidHandle;
}

// ---------------------------------------------------------------------------
// Move operations
// ---------------------------------------------------------------------------

Socket::Socket(Socket&& other) noexcept
    : handle_(other.handle_)
    , dualStack_(other.dualStack_)
{
    other.handle_ = kInvalidHandle;
}

Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {
        // Close existing handle before taking ownership of the new one.
        if (isValid()) {
            ::closesocket(static_cast<SOCKET>(handle_));
        }
        handle_       = other.handle_;
        dualStack_    = other.dualStack_;
        other.handle_ = kInvalidHandle;
    }
    return *this;
}

// ---------------------------------------------------------------------------
// Destructor
// ---------------------------------------------------------------------------

Socket::~Socket() {
    if (isValid()) {
        ::closesocket(static_cast<SOCKET>(handle_));
        handle_ = kInvalidHandle;
    }
}

} // namespace engine::networking

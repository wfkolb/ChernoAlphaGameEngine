// Must include WinsockInclude.h first â€” before any <windows.h> inclusion.
#include "WinsockInclude.h"

#include "networking/Endpoint.h"

#include <cstring>
#include <algorithm>

namespace engine::networking {

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace {

    // Write IPv4-mapped IPv6 bytes from a 4-byte IPv4 address.
    // Layout: [0..9] = 0x00, [10..11] = 0xFF, [12..15] = IPv4 octets.
    void storeIPv4Mapped(uint8_t (&dst)[16], const uint8_t* ipv4) noexcept {
        std::memset(dst, 0, 10);
        dst[10] = 0xFF;
        dst[11] = 0xFF;
        dst[12] = ipv4[0];
        dst[13] = ipv4[1];
        dst[14] = ipv4[2];
        dst[15] = ipv4[3];
    }

} // anonymous namespace

// ---------------------------------------------------------------------------
// Endpoint::fromString
// ---------------------------------------------------------------------------

Endpoint Endpoint::fromString(std::string_view addrPort) {
    Endpoint ep{};

    if (addrPort.empty()) {
        return ep; // invalid
    }

    if (addrPort.front() == '[') {
        // IPv6 bracket form: "[addr]:port"
        const auto closeBracket = addrPort.find(']');
        if (closeBracket == std::string_view::npos) {
            return ep; // malformed
        }
        const std::string addrStr(addrPort.substr(1, closeBracket - 1));

        // Expect ":port" after the closing bracket.
        const auto rest = addrPort.substr(closeBracket + 1);
        if (rest.size() < 2 || rest.front() != ':') {
            return ep; // malformed
        }
        const std::string portStr(rest.substr(1));

        int portVal = 0;
        try {
            portVal = std::stoi(portStr);
        } catch (...) {
            return ep;
        }
        if (portVal < 0 || portVal > 65535) {
            return ep;
        }

        // Parse the IPv6 address using inet_pton.
        in6_addr sin6{};
        if (inet_pton(AF_INET6, addrStr.c_str(), &sin6) != 1) {
            return ep;
        }

        static_assert(sizeof(sin6.s6_addr) == 16);
        std::memcpy(ep.addr, sin6.s6_addr, 16);
        ep.port   = static_cast<uint16_t>(portVal);
        ep.isIPv6 = true;

    } else {
        // IPv4 form: "A.B.C.D:port"
        // Split on the LAST colon.
        const auto colonPos = addrPort.rfind(':');
        if (colonPos == std::string_view::npos) {
            return ep; // no port specified
        }
        const std::string addrStr(addrPort.substr(0, colonPos));
        const std::string portStr(addrPort.substr(colonPos + 1));

        int portVal = 0;
        try {
            portVal = std::stoi(portStr);
        } catch (...) {
            return ep;
        }
        if (portVal < 0 || portVal > 65535) {
            return ep;
        }

        // Parse dotted-quad.
        in_addr sin4{};
        if (inet_pton(AF_INET, addrStr.c_str(), &sin4) != 1) {
            return ep;
        }

        // Store as IPv4-mapped IPv6.
        const auto* bytes = reinterpret_cast<const uint8_t*>(&sin4.s_addr);
        storeIPv4Mapped(ep.addr, bytes);
        ep.port   = static_cast<uint16_t>(portVal);
        ep.isIPv6 = false;
    }

    return ep;
}

// ---------------------------------------------------------------------------
// Endpoint::toString
// ---------------------------------------------------------------------------

std::string Endpoint::toString() const {
    char buf[INET6_ADDRSTRLEN] = {};

    if (!isIPv6) {
        // Reconstruct in_addr from bytes 12-15.
        in_addr sin4{};
        uint8_t* dst = reinterpret_cast<uint8_t*>(&sin4.s_addr);
        dst[0] = addr[12];
        dst[1] = addr[13];
        dst[2] = addr[14];
        dst[3] = addr[15];

        inet_ntop(AF_INET, &sin4, buf, sizeof(buf));
        return std::string(buf) + ":" + std::to_string(port);
    } else {
        in6_addr sin6{};
        static_assert(sizeof(sin6.s6_addr) == 16);
        std::memcpy(sin6.s6_addr, addr, 16);

        inet_ntop(AF_INET6, &sin6, buf, sizeof(buf));
        return std::string("[") + buf + "]:" + std::to_string(port);
    }
}

// ---------------------------------------------------------------------------
// Endpoint::isValid
// ---------------------------------------------------------------------------

bool Endpoint::isValid() const noexcept {
    if (port != 0) {
        return true;
    }
    for (uint8_t byte : addr) {
        if (byte != 0) {
            return true;
        }
    }
    return false;
}

} // namespace engine::networking

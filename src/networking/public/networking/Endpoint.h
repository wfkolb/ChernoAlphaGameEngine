#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace engine::networking {

    // Network endpoint (IP address + port). No Winsock types exposed.
    // IPv4 addresses are stored in IPv4-mapped IPv6 form for uniform handling:
    //   bytes 0-9  = 0x00, bytes 10-11 = 0xFF, bytes 12-15 = IPv4 octets.
    struct Endpoint {
        uint8_t  addr[16]{};   // IPv4-mapped IPv6 form; 16 bytes covers both families
        uint16_t port{0};
        bool     isIPv6{false};

        // Parse "192.0.2.1:7777"  (IPv4, port required)
        // Parse "[::1]:7777"      (IPv6 bracket form, port required)
        // Returns a zero Endpoint on parse failure.
        static Endpoint fromString(std::string_view addrPort);

        // Symmetric with fromString:
        //   IPv4 -> "A.B.C.D:port"
        //   IPv6 -> "[hex:hex:...]:port"
        std::string toString() const;

        bool operator==(const Endpoint&) const noexcept = default;

        // Returns false when addr is all zeros AND port is 0.
        bool isValid() const noexcept;
    };

} // namespace engine::networking

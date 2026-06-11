#include "networking/PacketObfuscation.h"

namespace engine::networking {

void xorObfuscate(std::span<uint8_t> packet, std::span<const uint8_t> key) {
    if (key.empty()) return;
    for (size_t i = 0; i < packet.size(); ++i) {
        packet[i] ^= key[i % key.size()];
    }
}

} // namespace engine::networking

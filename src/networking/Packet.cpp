#include "networking/Packet.h"

#include <cstring>

// Windows x64 is always little-endian, matching the wire format defined in
// networking-transport.md §1.  Direct struct memcpy is therefore safe and
// produces no byte-swapping overhead.

namespace engine::networking {

PacketHeader readHeader(const uint8_t* data, uint32_t size) {
    PacketHeader h{};
    if (size < static_cast<uint32_t>(sizeof(PacketHeader))) {
        // Return zeroed header; caller checks protocolId to validate.
        return h;
    }
    // Direct copy: layout matches wire order on little-endian Windows x64.
    std::memcpy(&h, data, sizeof(PacketHeader));
    return h;
}

void writeHeader(uint8_t* data, const PacketHeader& h) {
    // Caller is responsible for ensuring at least sizeof(PacketHeader) bytes
    // are available at 'data'.
    std::memcpy(data, &h, sizeof(PacketHeader));
}

} // namespace engine::networking

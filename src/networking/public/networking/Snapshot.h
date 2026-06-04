#pragma once
#include "networking/Serializer.h"
#include "core/ecs/Entity.h"
#include <vector>
#include <span>
#include <cstdint>

namespace engine::networking {

// Snapshot: a point-in-time image of ECS state for network transmission.
// Format (per networking-architecture.md §6.2):
//   - varint: number of entities
//   - per entity:
//     - varint: entity index
//     - uint32 (LE): entity generation
//     - 32 bytes: full ComponentMask (256-bit bitset, bit i = component id i present)
//     - varint: componentData byte count
//     - raw bytes: packed component data in component-id order

struct SnapshotEntry {
    engine::core::ecs::Entity        entity;
    engine::core::ecs::ComponentMask mask;
    std::vector<uint8_t>             componentData; // packed: all components in component-id order
};

class SnapshotEncoder {
public:
    // Encode entries into a byte buffer.
    static std::vector<uint8_t> encode(const std::vector<SnapshotEntry>& entries);
};

class SnapshotDecoder {
public:
    // Decode bytes produced by SnapshotEncoder::encode.
    // Returns empty on any parse error.
    static std::vector<SnapshotEntry> decode(std::span<const uint8_t> data);
};

// Acknowledgement sent by receiver to inform sender which snapshot was received.
struct SnapshotAck {
    uint32_t seq;
    uint32_t receivedMs;
};

} // namespace engine::networking

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace engine::tools {

// One networked entity captured in a checkpoint. Component data is stored as
// opaque, length-prefixed bytes keyed by component type id so that unknown
// components can be skipped on load (forward compatibility).
struct CheckpointEntity {
    struct Component {
        uint8_t              typeId = 0;
        std::vector<uint8_t> data;
    };
    uint32_t               netId = 0;
    std::vector<Component> components;
};

// A connected player recorded so a resumed server can reserve their slot.
struct CheckpointPlayer {
    uint64_t accountId       = 0;
    uint16_t lastKnownPingMs = 0;
};

// In-memory form of a `.checkpoint` file. The entity block is LZ4-compressed
// on disk and a CRC32 over the post-header payload guards integrity.
struct ServerCheckpoint {
    // Header fields.
    uint16_t    versionMajor  = 1;
    uint16_t    versionMinor  = 0;
    uint32_t    engineVersion = 0;
    uint32_t    gameVersion   = 0;
    uint64_t    tickNumber    = 0;
    double      serverTimeSec = 0.0;
    uint64_t    unixTimestamp = 0;
    std::string mapName;
    std::string modeName;

    // Body.
    std::vector<CheckpointEntity> entities;
    std::vector<uint8_t>          gameModeBlob;
    std::vector<CheckpointPlayer> players;
    std::vector<uint8_t>          gameExtensionData;
};

} // namespace engine::tools

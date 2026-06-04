#pragma once
#include <core/components/Transform.h>
#include <cstdint>
#include <vector>

namespace engine::app {

struct GameModeStateBlob {
    static constexpr size_t kMaxBytes = 256;
    uint8_t data[kMaxBytes] = {};
    uint8_t size             = 0;
};

struct EntityRecord {
    uint32_t                netId         = 0;
    engine::core::Transform transform;
    uint32_t                componentMask = 0;
};

struct SnapshotEvent {
    enum class Type : uint8_t {
        PlayerSpawned,
        PlayerDied,
        RoundStart,
        RoundEnd,
    };
    Type     type   = Type::PlayerSpawned;
    uint32_t param0 = 0;
    uint32_t param1 = 0;
};

struct WorldStateSnapshot {
    uint32_t                    tick              = 0;
    float                       serverTime        = 0.0f;
    std::vector<EntityRecord>   entities;
    GameModeStateBlob           gameModeState;
    std::vector<uint32_t>       destroyedEntities;
    std::vector<SnapshotEvent>  events;
};

} // namespace engine::app

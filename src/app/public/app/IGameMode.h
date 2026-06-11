#pragma once
#include "app/BitStream.h"
#include <core/ecs/Entity.h>
#include <cstdint>
#include <span>

namespace engine::app {

struct GameContext;

class IGameMode {
public:
    virtual ~IGameMode() = default;

    virtual void onRoundStart(GameContext& ctx)                                    = 0;
    virtual void onRoundTick(GameContext& ctx, float dt)                           = 0;
    virtual void onRoundEnd(GameContext& ctx)                                      = 0;
    virtual void onMatchEnd(GameContext& ctx)                                      = 0;
    virtual void onPlayerJoin(GameContext& ctx, uint32_t playerId)                 = 0;
    virtual void onPlayerLeave(GameContext& ctx, uint32_t playerId)                = 0;
    virtual void onPlayerSpawn(GameContext& ctx, uint32_t playerId)                = 0;
    virtual void onPlayerDeath(GameContext& ctx, uint32_t playerId,
                               uint32_t killerPlayerId)                            = 0;
    virtual void serializeState(BitStreamWriter& bsw)   const                     = 0;
    virtual void deserializeState(BitStreamReader& bsr)                            = 0;
    virtual bool evaluateWinCondition(GameContext& ctx)                            = 0;

    // Select a spawn point for a joining player. Default picks the first available.
    // availableSpawns: entity IDs of all SpawnPointComponent entities matching teamId.
    virtual core::ecs::Entity selectSpawnPoint(
        uint32_t                             /*playerId*/,
        uint8_t                              /*teamId*/,
        std::span<const core::ecs::Entity>   availableSpawns)
    {
        return availableSpawns.empty() ? core::ecs::kInvalidEntity : availableSpawns[0];
    }

    // Called by PhysicsWorld end-of-step when an entity enters/exits a trigger volume.
    virtual void onTriggerEnter(core::ecs::Entity /*triggerEntity*/,
                                core::ecs::Entity /*enteringEntity*/) {}
    virtual void onTriggerExit (core::ecs::Entity /*triggerEntity*/,
                                core::ecs::Entity /*leavingEntity*/)  {}
};

} // namespace engine::app

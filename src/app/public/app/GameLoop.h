#pragma once
#include "app/GameContext.h"
#include "app/WorldStateSnapshot.h"
#include <core/input/InputFrame.h>
#include <cstdint>

namespace engine::app    { class IGameMode; class SystemScheduler; enum class TickGroup : uint8_t; }
namespace engine::physics { class PhysicsWorld; }

namespace engine::app {

class GameLoop {
public:
    struct Desc {
        SystemScheduler*        scheduler    = nullptr;
        physics::PhysicsWorld*  physicsWorld = nullptr;
        IGameMode*              gameMode     = nullptr;
    };

    void init(const Desc& desc);

    // Server fixed-tick (call at 64 Hz).
    // Order: PrePhysics → physics step → PostPhysics → GameFixed → gameMode tick
    // Builds a WorldStateSnapshot every 3 ticks (~20 Hz).
    void serverTick(GameContext& ctx, float dt);

    // Client fixed-tick: dispatches PrePhysics → PostPhysics → GameFixed
    // and stamps frame.tick with the current tick counter.
    void clientTick(GameContext& ctx, float dt, engine::core::input::InputFrame& frame);

    uint32_t currentTick() const noexcept { return tick_; }

    // Non-null for one tick after every third serverTick(); clear after consuming.
    const WorldStateSnapshot* pendingSnapshot() const noexcept;
    void                      clearPendingSnapshot() noexcept;

private:
    SystemScheduler*        scheduler_   = nullptr;
    physics::PhysicsWorld*  physics_     = nullptr;
    IGameMode*              gameMode_    = nullptr;
    uint32_t                tick_        = 0;
    bool                    hasSnapshot_ = false;
    WorldStateSnapshot      snapshot_;

    void buildSnapshot(GameContext& ctx);
};

} // namespace engine::app

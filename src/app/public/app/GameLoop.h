#pragma once
#include "app/GameContext.h"
#include "app/WorldStateSnapshot.h"
#include <core/input/InputFrame.h>
#include <networking/InputMessage.h>
#include <networking/Serializer.h>
#include <cstdint>

namespace engine::app    { class IGameMode; class SystemScheduler; enum class TickGroup : uint8_t; }
namespace engine::physics { class PhysicsWorld; }
namespace engine::core   { class EventBus; }

namespace engine::app {

class GameLoop {
public:
    struct Desc {
        SystemScheduler*        scheduler    = nullptr;
        physics::PhysicsWorld*  physicsWorld = nullptr;
        IGameMode*              gameMode     = nullptr;
        core::EventBus*         eventBus     = nullptr;
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

    // Called by Application when a player joins the session.
    // Finds an available spawn point matching teamId, teleports the player entity,
    // and calls IGameMode::onPlayerSpawn().
    void onPlayerJoin(GameContext& ctx, uint32_t playerId, uint8_t teamId = 0);

    // Client: serialize pending input ring (up to kInputRingSize messages) into
    // writer, most recent first.
    void flushInputMessages(networking::BitWriter& writer);

    // Server: apply a received client input to the named player entity.
    void receiveInputMessage(GameContext& ctx,
                             const networking::InputMessage& msg,
                             uint32_t fromPlayerId);

private:
    static constexpr int kInputRingSize = 3;
    networking::InputMessage pendingInputs_[kInputRingSize] {};
    int pendingInputCount_ { 0 };

    SystemScheduler*        scheduler_   = nullptr;
    physics::PhysicsWorld*  physics_     = nullptr;
    IGameMode*              gameMode_    = nullptr;
    core::EventBus*         eventBus_    = nullptr;
    uint32_t                tick_        = 0;
    bool                    hasSnapshot_ = false;
    WorldStateSnapshot      snapshot_;

    void buildSnapshot(GameContext& ctx);
};

} // namespace engine::app

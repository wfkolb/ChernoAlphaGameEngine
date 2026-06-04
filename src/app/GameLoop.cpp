#include "app/GameLoop.h"
#include "app/IGameMode.h"
#include "app/SystemScheduler.h"
#include "app/BitStream.h"
#include <physics/PhysicsWorld.h>
#include <algorithm>
#include <cstring>

namespace engine::app {

void GameLoop::init(const Desc& desc) {
    scheduler_   = desc.scheduler;
    physics_     = desc.physicsWorld;
    gameMode_    = desc.gameMode;
    tick_        = 0u;
    hasSnapshot_ = false;
    snapshot_    = WorldStateSnapshot{};
}

void GameLoop::serverTick(GameContext& ctx, float dt) {
    if (scheduler_) {
        scheduler_->tickGroup(TickGroup::PrePhysics, dt);
    }
    if (physics_) {
        physics_->step(dt);
    }
    if (scheduler_) {
        scheduler_->tickGroup(TickGroup::PostPhysics, dt);
        scheduler_->tickGroup(TickGroup::GameFixed,   dt);
    }
    if (gameMode_) {
        gameMode_->onRoundTick(ctx, dt);
        gameMode_->evaluateWinCondition(ctx);
    }
    ++tick_;

    if (tick_ % 3u == 0u) {
        buildSnapshot(ctx);
        hasSnapshot_ = true;
    }
}

void GameLoop::clientTick(GameContext& ctx, float dt,
                          engine::core::input::InputFrame& frame) {
    if (scheduler_) {
        scheduler_->tickGroup(TickGroup::PrePhysics,  dt);
        scheduler_->tickGroup(TickGroup::PostPhysics, dt);
        scheduler_->tickGroup(TickGroup::GameFixed,   dt);
    }
    frame.tick = tick_;
    ++tick_;
    (void)ctx;
}

const WorldStateSnapshot* GameLoop::pendingSnapshot() const noexcept {
    return hasSnapshot_ ? &snapshot_ : nullptr;
}

void GameLoop::clearPendingSnapshot() noexcept {
    hasSnapshot_ = false;
}

void GameLoop::buildSnapshot(GameContext& ctx) {
    snapshot_            = WorldStateSnapshot{};
    snapshot_.tick       = tick_;
    snapshot_.serverTime = static_cast<float>(tick_) / 64.0f;

    if (gameMode_) {
        BitStreamWriter bsw;
        gameMode_->serializeState(bsw);
        const size_t sz = std::min(bsw.byteSize(), GameModeStateBlob::kMaxBytes);
        std::memcpy(snapshot_.gameModeState.data, bsw.data(), sz);
        snapshot_.gameModeState.size = static_cast<uint8_t>(sz);
    }
    (void)ctx;
}

} // namespace engine::app

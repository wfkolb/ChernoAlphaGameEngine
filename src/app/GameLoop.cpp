#include "app/GameLoop.h"
#include <tools/Profiler.h>
#include "app/IGameMode.h"
#include "app/SystemScheduler.h"
#include "app/BitStream.h"
#include <physics/PhysicsWorld.h>
#include <physics/TriggerEvents.h>
#include <core/EventBus.h>
#include <core/components/SpawnPointComponent.h>
#include <core/components/Transform.h>
#include <core/input/InputReceiverComponent.h>
#include <core/ecs/View.h>
#include <core/ecs/World.h>
#include <networking/InputMessageConvert.h>
#include <networking/InputMessageSerializer.h>
#include <networking/NetworkInputComponent.h>
#include <algorithm>
#include <cstring>
#include <span>
#include <vector>

namespace engine::app {

void GameLoop::init(const Desc& desc) {
    scheduler_   = desc.scheduler;
    physics_     = desc.physicsWorld;
    gameMode_    = desc.gameMode;
    tick_        = 0u;
    hasSnapshot_ = false;
    snapshot_    = WorldStateSnapshot{};

    eventBus_ = desc.eventBus;
    if (eventBus_) {
        eventBus_->subscribe<physics::TriggerEnterEvent>([this](const physics::TriggerEnterEvent& e) {
            if (gameMode_) gameMode_->onTriggerEnter(e.triggerEntity, e.enteringEntity);
        });
        eventBus_->subscribe<physics::TriggerExitEvent>([this](const physics::TriggerExitEvent& e) {
            if (gameMode_) gameMode_->onTriggerExit(e.triggerEntity, e.leavingEntity);
        });
    }
}

void GameLoop::serverTick(GameContext& ctx, float dt) {
    PROFILE_SCOPE("GameLoop::serverTick");
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
    PROFILE_SCOPE("GameLoop::clientTick");
    if (scheduler_) {
        scheduler_->tickGroup(TickGroup::PrePhysics,  dt);
        scheduler_->tickGroup(TickGroup::PostPhysics, dt);
        scheduler_->tickGroup(TickGroup::GameFixed,   dt);
    }
    frame.tick = tick_;
    auto wire = networking::toWire(frame, tick_);
    pendingInputs_[pendingInputCount_ % kInputRingSize] = wire;
    ++pendingInputCount_;
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

void GameLoop::onPlayerJoin(GameContext& ctx, uint32_t playerId, uint8_t teamId) {
    if (!ctx.world) return;

    // Collect available spawn points matching teamId (0 = any).
    std::vector<core::ecs::Entity> candidates;
    core::ecs::View<core::Transform, core::SpawnPointComponent> spawnView(*ctx.world);
    for (auto [entity, tr, sp] : spawnView) {
        if (sp.teamId == 0 || sp.teamId == teamId) {
            candidates.push_back(entity);
        }
    }

    // Exclude spawn points whose exclusion radius overlaps any active player.
    {
        // Collect current player positions.
        std::vector<core::math::Vec3> playerPositions;
        core::ecs::View<core::Transform, core::input::InputReceiverComponent> activeView(*ctx.world);
        for (auto [pe, ptr, recv] : activeView) {
            playerPositions.push_back(ptr.position);
        }

        if (!playerPositions.empty()) {
            std::vector<core::ecs::Entity> filtered;
            filtered.reserve(candidates.size());
            for (core::ecs::Entity cand : candidates) {
                const auto* spawnTr = ctx.world->tryGet<core::Transform>(cand);
                const auto* sp      = ctx.world->tryGet<core::SpawnPointComponent>(cand);
                if (!spawnTr || !sp || sp->radius <= 0.0f) {
                    filtered.push_back(cand);
                    continue;
                }
                const float r   = sp->radius;
                const float rSq = r * r;
                bool occupied = false;
                for (const auto& playerPos : playerPositions) {
                    const core::math::Vec3 diff = spawnTr->position - playerPos;
                    if (core::math::dot(diff, diff) <= rSq) {
                        occupied = true;
                        break;
                    }
                }
                if (!occupied) {
                    filtered.push_back(cand);
                }
            }
            // Fallback: restore full list if all candidates were excluded.
            if (!filtered.empty()) {
                candidates = std::move(filtered);
            }
        }
    }

    // Sort by priority descending.
    std::sort(candidates.begin(), candidates.end(),
              [&](core::ecs::Entity a, core::ecs::Entity b) {
                  auto* spa = ctx.world->tryGet<core::SpawnPointComponent>(a);
                  auto* spb = ctx.world->tryGet<core::SpawnPointComponent>(b);
                  return (spa ? spa->priority : 0u) > (spb ? spb->priority : 0u);
              });

    // Let game mode pick.
    std::span<const core::ecs::Entity> availSpan(candidates);
    const core::ecs::Entity spawnEnt = gameMode_
        ? gameMode_->selectSpawnPoint(playerId, teamId, availSpan)
        : (candidates.empty() ? core::ecs::kInvalidEntity : candidates[0]);

    if (spawnEnt == core::ecs::kInvalidEntity) {
        if (gameMode_) gameMode_->onPlayerSpawn(ctx, playerId);
        return;
    }

    // Find the player entity by iterating entities with InputReceiverComponent.
    // For now, find the first entity whose owner matches playerId.
    // (Full ownership tracking arrives in Phase 9 Wave 4 with NetworkIdentity.)
    if (const core::Transform* spawnTr = ctx.world->tryGet<core::Transform>(spawnEnt)) {
        core::ecs::View<core::Transform, core::input::InputReceiverComponent> playerView(*ctx.world);
        for (auto [pe, ptr, recv] : playerView) {
            if (recv.playerId == playerId) {
                ptr.position = spawnTr->position;
                ptr.rotation = spawnTr->rotation;
                ctx.world->addComponent<core::Transform>(pe, ptr);
                break;
            }
        }
    }

    if (gameMode_) gameMode_->onPlayerSpawn(ctx, playerId);
}

void GameLoop::flushInputMessages(networking::BitWriter& writer) {
    const int count = std::min(pendingInputCount_, kInputRingSize);
    // Write most recent first.  The ring is a circular buffer; the newest entry
    // is at index (pendingInputCount_ - 1) % kInputRingSize.
    for (int i = 0; i < count; ++i) {
        const int idx = (pendingInputCount_ - 1 - i + kInputRingSize * 2) % kInputRingSize;
        networking::writeInputMessage(writer, pendingInputs_[idx]);
    }
}

void GameLoop::receiveInputMessage(GameContext& ctx,
                                   const networking::InputMessage& msg,
                                   uint32_t fromPlayerId) {
    if (!ctx.world) return;

    core::ecs::View<core::input::InputReceiverComponent,
                    networking::NetworkInputComponent> view(*ctx.world);
    for (auto [entity, recv, netInput] : view) {
        if (recv.playerId == static_cast<uint8_t>(fromPlayerId)) {
            netInput.lastNetworkInput = msg;
            return;
        }
    }
}

} // namespace engine::app

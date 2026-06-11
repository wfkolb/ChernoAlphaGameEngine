#ifdef ENGINE_DEVREL

#include "editor/PIEController.h"

#include <core/scene/Scene.h>
#include <core/ecs/World.h>
#include <core/components/SpawnPointComponent.h>
#include <core/components/Transform.h>
#include <core/input/InputReceiverComponent.h>
#include <core/ecs/View.h>
#include <core/Input.h>
#include <rendering/FpsCameraSystem.h>
#include <rendering/Camera.h>

#include <cstring>
#include <vector>

namespace engine::editor {

namespace {
constexpr float kFixedDt = 1.0f / 64.0f;   // server tick rate
constexpr float kMaxStep = 0.25f;          // clamp to avoid spiral-of-death
}

void PIEController::captureSnapshot() {
    snapshot_.clear();
    if (!scene_) return;

    core::ecs::World& world = scene_->world();
    world.forEachEntity([&](core::ecs::Entity e) {
        EntitySnapshot s;
        s.entity = e;

        world.forEachComponentOnEntity(e, [&](core::ecs::ComponentTypeId typeId, void* rawData) {
            const auto& meta = core::ecs::World::getComponentMeta(typeId);
            if (meta.size == 0) return;

            ComponentBlob blob;
            blob.typeId = typeId;
            blob.bytes.assign(static_cast<uint8_t*>(rawData),
                              static_cast<uint8_t*>(rawData) + meta.size);
            s.components.push_back(std::move(blob));
        });

        snapshot_.push_back(std::move(s));
    });
}

void PIEController::restoreSnapshot() {
    if (!scene_) return;

    core::ecs::World& world = scene_->world();

    // Destroy entities that were spawned during PIE (not present in the pre-PIE snapshot).
    std::vector<core::ecs::Entity> toDestroy;
    world.forEachEntity([&](core::ecs::Entity e) {
        for (const EntitySnapshot& s : snapshot_) {
            if (s.entity.index == e.index && s.entity.generation == e.generation)
                return; // found in snapshot — keep it
        }
        toDestroy.push_back(e);
    });
    for (core::ecs::Entity e : toDestroy) {
        world.destroyEntity(e);
    }

    // Restore each snapshotted entity that survived PIE.
    for (const EntitySnapshot& s : snapshot_) {
        if (!world.isAlive(s.entity)) continue;

        // Overwrite existing component data in-place — do NOT call addComponentRaw for
        // components already on the entity, as that triggers an archetype self-move and
        // corrupts rec.row (out-of-bounds write on the next memcpy).
        world.forEachComponentOnEntity(s.entity,
            [&s](core::ecs::ComponentTypeId typeId, void* rawData) {
                for (const ComponentBlob& blob : s.components) {
                    if (blob.typeId != typeId) continue;
                    const auto& meta = core::ecs::World::getComponentMeta(typeId);
                    if (meta.size > 0)
                        std::memcpy(rawData, blob.bytes.data(), meta.size);
                    return;
                }
            });

        // Re-add components that were removed from the entity during PIE.
        for (const ComponentBlob& blob : s.components) {
            if (world.hasComponent(s.entity, blob.typeId)) continue;
            const auto& meta = core::ecs::World::getComponentMeta(blob.typeId);
            if (meta.size == 0) continue;
            world.addComponentRaw(s.entity, blob.typeId, blob.bytes.data(), blob.bytes.size());
        }
    }
    snapshot_.clear();
}

void PIEController::start(core::scene::Scene& scene, core::math::Vec3 fallbackPos) {
    if (state_ != State::Stopped) return;

    scene_            = &scene;
    accumulator_      = 0.0f;
    playTime_         = 0.0f;
    simTick_          = 0;
    captureSnapshot();

    // Place the player entity at the first SpawnPoint, or at fallbackPos if none exists.
    // Uses tryGet<Transform> to update in-place — addComponent<Transform> would assert
    // because FpsCharacter entities already have Transform in their archetype.
    {
        core::ecs::World& world = scene_->world();

        core::math::Vec3 targetPos = fallbackPos;
        core::math::Quat targetRot = core::math::Quat::identity();
        bool hasSpawnPoint = false;

        core::ecs::View<core::Transform, core::SpawnPointComponent> spawnView(world);
        auto spawnIt = spawnView.begin();
        if (spawnIt != spawnView.end()) {
            const auto& [se, sTr, sSp] = *spawnIt;
            targetPos      = sTr.position;
            targetRot      = sTr.rotation;
            hasSpawnPoint  = true;
        }

        // Without a spawn point, fallbackPos is the editor camera's flying position.
        // Zero the Y so the player starts grounded at eyeHeight above the floor plane
        // rather than falling 1-2 m from the camera's elevation.
        if (!hasSpawnPoint) {
            targetPos.y = 0.0f;
        }

        core::ecs::View<core::Transform, core::input::InputReceiverComponent> playerView(world);
        auto playerIt = playerView.begin();
        if (playerIt != playerView.end()) {
            const auto& [pe, ptr, recv] = *playerIt;
            if (core::Transform* tr = world.tryGet<core::Transform>(pe)) {
                float eyeH = 1.7f;
                if (auto* ctrl = world.tryGet<rendering::FpsCameraController>(pe))
                    eyeH = ctrl->eyeHeight;
                tr->position = targetPos + core::math::Vec3{0.f, eyeH, 0.f};
                tr->rotation = targetRot;
            }
        }
    }

    usePlayerCamera_  = true;
    captureMouse_     = true;
    state_ = State::Playing;
}

void PIEController::tick(float dt) {
    if (state_ != State::Playing || !scene_) return;

    if (dt > kMaxStep) dt = kMaxStep;
    accumulator_ += dt;
    playTime_    += dt;

    // Fixed-step the scene simulation — this is the in-process "server" loop.
    // Before each physics/logic step, advance the raw InputSystem so action
    // queries inside game systems see fresh keyboard/mouse state.
    while (accumulator_ >= kFixedDt) {
        core::InputSystem::update();
        // Drive FPS camera controller so WASD/mouse move the player during PIE.
        rendering::FpsCameraSystem{}.tick(scene_->world(), kFixedDt);
        scene_->tick(kFixedDt);
        accumulator_ -= kFixedDt;
        ++simTick_;
    }
}

void PIEController::pause() {
    if (state_ == State::Playing) state_ = State::Paused;
}

void PIEController::resume() {
    if (state_ == State::Paused) state_ = State::Playing;
}

void PIEController::stop() {
    if (state_ == State::Stopped) return;
    usePlayerCamera_ = false;
    captureMouse_    = false;
    restoreSnapshot();
    state_       = State::Stopped;
    scene_       = nullptr;
    accumulator_ = 0.0f;
    playTime_    = 0.0f;
    simTick_     = 0;
}

} // namespace engine::editor

#endif // ENGINE_DEVREL

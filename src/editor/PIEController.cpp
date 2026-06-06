#ifdef ENGINE_DEVREL

#include "editor/PIEController.h"

#include <core/scene/Scene.h>
#include <core/ecs/World.h>

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
    for (const EntitySnapshot& s : snapshot_) {
        if (!world.isAlive(s.entity)) continue;

        for (const ComponentBlob& blob : s.components) {
            const auto& meta = core::ecs::World::getComponentMeta(blob.typeId);
            if (meta.size == 0) continue; // skip unregistered components
            world.addComponentRaw(s.entity, blob.typeId, blob.bytes.data(), blob.bytes.size());
        }
    }
    snapshot_.clear();
}

void PIEController::start(core::scene::Scene& scene) {
    if (state_ != State::Stopped) return;

    scene_            = &scene;
    accumulator_      = 0.0f;
    playTime_         = 0.0f;
    simTick_          = 0;
    captureSnapshot();
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
    while (accumulator_ >= kFixedDt) {
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

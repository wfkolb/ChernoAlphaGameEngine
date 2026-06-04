#ifdef ENGINE_DEVREL

#include "editor/PIEController.h"

#include <core/scene/Scene.h>
#include <core/ecs/World.h>
#include <core/ecs/Name.h>

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
        if (auto* nm = world.tryGet<core::ecs::Name>(e)) {
            s.name = nm->c_str();
        }
        if (auto* tr = world.tryGet<core::Transform>(e)) {
            s.transform    = *tr;
            s.hasTransform = true;
        }
        snapshot_.push_back(std::move(s));
    });
}

void PIEController::restoreSnapshot() {
    if (!scene_) return;

    core::ecs::World& world = scene_->world();
    size_t i = 0;
    world.forEachEntity([&](core::ecs::Entity e) {
        if (i >= snapshot_.size()) return;
        const EntitySnapshot& s = snapshot_[i++];
        if (s.hasTransform) {
            if (auto* tr = world.tryGet<core::Transform>(e)) {
                *tr = s.transform;
            }
        }
    });
    snapshot_.clear();
}

void PIEController::start(core::scene::Scene& scene) {
    if (state_ != State::Stopped) return;

    scene_       = &scene;
    accumulator_ = 0.0f;
    playTime_    = 0.0f;
    simTick_     = 0;
    captureSnapshot();
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
    restoreSnapshot();
    state_       = State::Stopped;
    scene_       = nullptr;
    accumulator_ = 0.0f;
    playTime_    = 0.0f;
    simTick_     = 0;
}

} // namespace engine::editor

#endif // ENGINE_DEVREL

#pragma once
#include <core/ecs/World.h>

namespace engine::rendering {

// ECS system that runs in the Update phase.
// For every entity that has both FpsCameraController (active == true) and
// Transform components, reads InputSystem::state() for mouse deltas and WASD
// keys, integrates yaw/pitch, and writes back Transform::rotation and
// Transform::position.
class FpsCameraSystem {
public:
    void tick(core::ecs::World& world, float dt);
};

} // namespace engine::rendering

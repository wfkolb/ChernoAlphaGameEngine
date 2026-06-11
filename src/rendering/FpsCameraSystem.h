#pragma once
#include <core/ecs/World.h>

namespace engine::rendering {

class FpsCameraSystem {
public:
    void tick(core::ecs::World& world, float dt);
};

} // namespace engine::rendering

#pragma once

#include <core/math/Vec.h>
#include <string>
#include <cstdint>

namespace engine::core::scene {

struct SceneGlobals {
    // Physics
    math::Vec3 gravity        = {0.0f, -9.81f, 0.0f};

    // Rendering
    math::Vec3 ambientLight   = {0.1f, 0.1f, 0.1f};
    math::Vec3 fogColor       = {0.5f, 0.5f, 0.5f};
    float      fogDensity     = 0.0f;

    // Identity
    std::string  sceneName;
    uint32_t     sceneId      = 0;

    // Gameplay
    float        matchTimeLimit = 0.0f;   // seconds; 0 = no limit
    int          maxPlayers     = 0;
    std::string  gameMode;
};

} // namespace engine::core::scene

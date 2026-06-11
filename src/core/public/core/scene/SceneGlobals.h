#pragma once

#include <core/math/Vec.h>
#include <string>
#include <cstdint>

namespace engine::core::scene {

struct SceneGlobals {
    // Physics
    math::Vec3 gravity        = {0.0f, -9.81f, 0.0f};

    // Rendering
    math::Vec3 ambientLight       = {0.1f, 0.1f, 0.1f};
    math::Vec3 fogColor           = {0.5f, 0.5f, 0.5f};
    float      fogDensity         = 0.0f;

    // Shadow
    /// Blend between uniform (0.0) and logarithmic (1.0) CSM cascade splits.
    /// Overrides [render].shadow_split_lambda from engine.toml when a scene is loaded.
    float      shadowSplitLambda  = 0.95f;

    // IBL / Skybox
    // Path to a .easset v4 IBL file (produced by asset_cooker --cubemap).
    // When non-empty, Scene::activate() should call loadIblEasset() and
    // upload the results to the GPU before the first frame renders.
    // TODO Phase 10 R3: hook up IBL load in Scene::activate() (GPU upload side).
    std::string  skyboxAssetPath;

    // Identity
    std::string  sceneName;
    uint32_t     sceneId      = 0;

    // Gameplay
    float        matchTimeLimit = 0.0f;   // seconds; 0 = no limit
    int          maxPlayers     = 0;
    std::string  gameMode;
};

} // namespace engine::core::scene

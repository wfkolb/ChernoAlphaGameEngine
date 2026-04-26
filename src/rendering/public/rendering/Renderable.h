#pragma once

#include <rendering/Mesh.h>
#include <rendering/Material.h>

namespace engine::rendering {

    // ECS component — attach to any entity that should be drawn by the renderer.
    // The culling system iterates View<Transform, Renderable>() each frame and
    // submits visible instances to the draw lists.
    struct Renderable {
        MeshHandle     mesh          { kInvalidMesh };
        MaterialHandle material      { kInvalidMaterial };
        bool           castShadow    { true };
        bool           receiveShadow { true };
    };

} // namespace engine::rendering

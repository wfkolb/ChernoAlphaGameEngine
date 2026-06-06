#pragma once
#ifdef ENGINE_DEVREL

#include <physics/QueryFilter.h>
#include <string>

namespace engine::editor {

// Visual editor for the 16×16 layer collision matrix.
// Save writes collision_layers.toml and reloads QueryFilter.
class CollisionLayerPanel {
public:
    void draw(physics::QueryFilter& filter,
              const std::string& tomlPath,
              bool* open);
};

} // namespace engine::editor

#endif // ENGINE_DEVREL

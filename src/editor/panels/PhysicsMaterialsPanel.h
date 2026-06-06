#pragma once
#ifdef ENGINE_DEVREL

#include <physics/PhysicsMaterialTable.h>
#include <string>

namespace engine::editor {

// Panel for editing physics materials backed by physics_materials.toml.
// Save writes the file and calls PhysicsMaterialTable::load() to hot-reload.
class PhysicsMaterialsPanel {
public:
    void draw(physics::PhysicsMaterialTable& table,
              const std::string& tomlPath,
              bool* open);
};

} // namespace engine::editor

#endif // ENGINE_DEVREL

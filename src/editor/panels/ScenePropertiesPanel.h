#pragma once
#ifdef ENGINE_DEVREL

namespace engine::core::scene { struct SceneGlobals; }

namespace engine::editor {

// Editable view of SceneGlobals (gravity, ambient, fog, game mode, etc.).
// Changes are applied directly to the active scene's globals; callers are
// responsible for marking the scene dirty if a mutation is detected.
class ScenePropertiesPanel {
public:
    // Returns true if any field was modified this frame.
    bool draw(core::scene::SceneGlobals& globals, bool* open);
};

} // namespace engine::editor

#endif // ENGINE_DEVREL

#pragma once

#include <core/scene/Scene.h>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace engine::core::scene {

// Manages a collection of named scenes; at most one is typically active at a time
// (multiple active scenes support Play-in-Editor).
class SceneManager {
public:
    SceneManager()  = default;
    ~SceneManager() = default;

    SceneManager(const SceneManager&)            = delete;
    SceneManager& operator=(const SceneManager&) = delete;
    SceneManager(SceneManager&&)                 = delete;
    SceneManager& operator=(SceneManager&&)      = delete;

    // Create a new scene, load it, and return a pointer.
    // Returns nullptr if a scene with the same name already exists.
    Scene* load(std::string_view name);

    // Deactivate (if needed), unload, and remove the scene.
    void unload(std::string_view name);

    // Activate the named scene. Does nothing if already active.
    // Returns false if the scene is not found.
    bool activate(std::string_view name);

    // Deactivate the named scene. Does nothing if not active.
    void deactivate(std::string_view name);

    // Tick all active scenes.
    void tickActive(float dt);

    // Returns the first active scene (most common single-scene use case), or nullptr.
    Scene* getActive() noexcept;
    const Scene* getActive() const noexcept;

    // Look up by name (regardless of active state).
    Scene* get(std::string_view name) noexcept;
    const Scene* get(std::string_view name) const noexcept;

    // Same as get(); provided for API symmetry with the spec.
    Scene* getByName(std::string_view name) noexcept;

    // Returns all currently active scenes.
    std::vector<Scene*> getAllActive();

    int sceneCount() const noexcept { return static_cast<int>(scenes_.size()); }

private:
    struct Entry {
        std::string         name;
        std::unique_ptr<Scene> scene;
    };

    std::vector<Entry> scenes_;

    Entry* findEntry(std::string_view name) noexcept;
    const Entry* findEntry(std::string_view name) const noexcept;
};

} // namespace engine::core::scene

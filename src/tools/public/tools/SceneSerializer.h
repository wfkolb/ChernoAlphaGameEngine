#pragma once

#include <core/ecs/Entity.h>
#include <core/ecs/EntityFactory.h>
#include <core/ecs/World.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <future>

// Forward declarations — avoid pulling heavy headers into the public API.
namespace engine::core::scene { class Scene; }

namespace engine::tools {

class SceneSerializer {
public:
    // Called by the application at startup to teach the serializer how to
    // reconstruct a specific component type on load.  Components without a
    // registered loader are silently skipped (forward-compatibility).
    using ComponentLoader = std::function<void(
        core::ecs::World& world,
        core::ecs::Entity entity,
        const uint8_t*    data,
        size_t            size
    )>;

    static void registerComponentLoader(core::ecs::ComponentTypeId id,
                                        ComponentLoader             loader);
    static void clearComponentLoaders();

    // Optional: provide an EntityFactory so that entities with a saved
    // archetype name are reconstructed via EntityFactory::spawn() on load,
    // giving them archetype defaults before applying stored component deltas.
    // Pass nullptr to disable (default behaviour: create a bare entity).
    static void setEntityFactory(core::ecs::EntityFactory* factory);
    static void clearEntityFactory();

    // Serialize `scene` to a binary .scene file.  Returns false on I/O error.
    static bool save(const core::scene::Scene& scene,
                     const std::filesystem::path& path);

    // Deserialize a .scene file into `scene`.  The scene must not be loaded.
    // Returns false on I/O or format error.
    static bool load(core::scene::Scene& scene,
                     const std::filesystem::path& path);

    // Non-blocking variant; the caller must keep `scene` alive until the
    // future is resolved.
    static std::future<bool> loadAsync(core::scene::Scene& scene,
                                       const std::filesystem::path& path);

    // Checks that the file has a valid ENGS header and plausible section
    // offsets, without fully deserializing.  Returns false on any error.
    static bool validate(const std::filesystem::path& path);

private:
    static inline std::array<ComponentLoader, 256> loaders_ = {};
    static inline core::ecs::EntityFactory*        factory_ = nullptr;
};

} // namespace engine::tools

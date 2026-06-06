#pragma once

#include <core/ecs/Entity.h>
#include <core/ecs/World.h>
#include <core/ecs/EntityFactory.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace engine::core::ecs { struct SpawnParams; }

namespace engine::tools {

class PrefabSerializer {
public:
    struct ComponentData {
        core::ecs::ComponentTypeId typeId;
        std::vector<uint8_t>       bytes;
    };

    struct EntitySnapshot {
        std::vector<ComponentData> components;
    };

    struct PrefabData {
        std::string                    name;
        std::vector<EntitySnapshot>    entities; // entity 0 = root
    };

    // Capture an entity subtree from a live World into PrefabData.
    // Traverses children via HierarchyComponent.
    // HierarchyComponent parent/child/sibling fields are converted to
    // local indices (generation = 0 means "local index N = entity.index").
    static PrefabData capture(core::ecs::Entity root, core::ecs::World& world);

    // Write PrefabData to disk as a .prefab file (ENGP magic).
    static bool save(const PrefabData& data, const std::filesystem::path& path);

    // Read a .prefab file from disk.
    static std::optional<PrefabData> load(const std::filesystem::path& path);

    // Instantiate a PrefabData into a World. Returns the root EntityId.
    // SpawnParams.position is added as an offset to the root Transform.
    // HierarchyComponent entity references are fixed up to new EntityIds.
    static core::ecs::Entity instantiate(const PrefabData& data,
                                         const core::ecs::SpawnParams& params,
                                         core::ecs::World& world);

    // Returns true if the file has a valid ENGP magic and sane section bounds.
    static bool validate(const std::filesystem::path& path);
};

} // namespace engine::tools

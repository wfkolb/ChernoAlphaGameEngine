#include "core/ecs/EntityFactory.h"
#include <core/log.h>
#include <algorithm>

namespace engine::core::ecs {

void EntityFactory::registerArchetype(std::string name, ArchetypeFn fn) {
    archetypes_.insert_or_assign(std::move(name), std::move(fn));
}

Entity EntityFactory::spawn(const std::string& name, const SpawnParams& params, World& world) const {
    auto it = archetypes_.find(name);
    if (it == archetypes_.end()) {
        LOG_WARN("EntityFactory: unknown archetype '{}'", name);
        return kInvalidEntity;
    }
    Entity e = world.createEntity();
    it->second(e, params, world);
    return e;
}

std::vector<std::string> EntityFactory::registeredArchetypeNames() const {
    std::vector<std::string> names;
    names.reserve(archetypes_.size());
    for (const auto& [name, _] : archetypes_)
        names.push_back(name);
    std::sort(names.begin(), names.end());
    return names;
}

} // namespace engine::core::ecs

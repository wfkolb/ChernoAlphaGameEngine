#include "core/ecs/EntityFactory.h"
#include <core/log.h>

namespace engine::core::ecs {

void EntityFactory::registerArchetype(std::string name, ArchetypeFn fn) {
    archetypes_.emplace(std::move(name), std::move(fn));
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

} // namespace engine::core::ecs

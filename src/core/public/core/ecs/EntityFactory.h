#pragma once
#include <core/ecs/Entity.h>
#include <core/ecs/World.h>
#include <core/math/Vec.h>
#include <core/math/Quat.h>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::core::ecs {

struct SpawnParams {
    math::Vec3  position = math::Vec3::zero();
    math::Quat  rotation = math::Quat::identity();
    Entity      parent   = kInvalidEntity;
    std::string scene;
};

class EntityFactory {
public:
    using ArchetypeFn = std::function<void(Entity, const SpawnParams&, World&)>;

    void   registerArchetype(std::string name, ArchetypeFn fn);
    Entity spawn(const std::string& name, const SpawnParams& params, World& world) const;

    // Returns archetype names sorted alphabetically.
    std::vector<std::string> registeredArchetypeNames() const;

private:
    std::unordered_map<std::string, ArchetypeFn> archetypes_;
};

} // namespace engine::core::ecs

#pragma once
#include <core/ecs/Entity.h>
#include <type_traits>

namespace engine::core { struct Transform; }

namespace engine::core::ecs {

class World;

struct HierarchyComponent {
    static constexpr ComponentTypeId kComponentId = 11;

    Entity parent      = kInvalidEntity;
    Entity firstChild  = kInvalidEntity;
    Entity nextSibling = kInvalidEntity;
    Entity prevSibling = kInvalidEntity;
};

static_assert(std::is_trivially_copyable_v<HierarchyComponent>,
              "HierarchyComponent must be trivially copyable for ECS archetype moves");

// Attach child to parent. Adds HierarchyComponent to parent if absent.
// Appends child as the last sibling in parent's child linked list.
void linkChild(World& w, Entity parent, Entity child);

// Walk up the hierarchy composing local transforms into a world transform.
// ENGINE_ASSERT fires if depth exceeds 8 levels.
core::Transform computeWorldTransform(World& w, Entity e);

} // namespace engine::core::ecs

#include "core/ecs/HierarchyComponent.h"
#include <core/ecs/World.h>
#include <core/components/Transform.h>
#include <core/math/Transform.h>
#include <core/diag/Assert.h>

namespace engine::core::ecs {

void linkChild(World& w, Entity parent, Entity child) {
    if (!w.hasComponent(parent, HierarchyComponent::kComponentId)) {
        w.addComponent<HierarchyComponent>(parent, HierarchyComponent{});
    }

    HierarchyComponent& parentHC = w.get<HierarchyComponent>(parent);
    HierarchyComponent& childHC  = w.get<HierarchyComponent>(child);

    childHC.parent = parent;

    if (parentHC.firstChild == kInvalidEntity) {
        parentHC.firstChild  = child;
        childHC.prevSibling  = kInvalidEntity;
        childHC.nextSibling  = kInvalidEntity;
    } else {
        // Walk to the last sibling and append.
        Entity last = parentHC.firstChild;
        while (true) {
            HierarchyComponent& lastHC = w.get<HierarchyComponent>(last);
            if (lastHC.nextSibling == kInvalidEntity) {
                lastHC.nextSibling  = child;
                childHC.prevSibling = last;
                childHC.nextSibling = kInvalidEntity;
                break;
            }
            last = lastHC.nextSibling;
        }
    }
}

core::Transform computeWorldTransform(World& w, Entity e) {
    constexpr int kMaxDepth = 8;

    // Collect chain from e up to the root (root first in output).
    Entity chain[kMaxDepth + 1];
    int    depth = 0;

    Entity cur = e;
    while (cur != kInvalidEntity && depth <= kMaxDepth) {
        chain[depth++] = cur;
        const auto* hc = w.tryGet<HierarchyComponent>(cur);
        if (!hc || hc->parent == kInvalidEntity) break;
        cur = hc->parent;
    }

    ENGINE_ASSERT(depth <= kMaxDepth,
                  "computeWorldTransform: hierarchy depth exceeds 8");

    auto toMath = [](const core::Transform& t) -> math::Transform {
        return { t.position, t.rotation, t.scale };
    };

    // chain[depth-1] = root, chain[0] = e. Compose top-down.
    const core::Transform* rootTr = w.tryGet<core::Transform>(chain[depth - 1]);
    math::Transform world = rootTr ? toMath(*rootTr) : math::Transform{};

    for (int i = depth - 2; i >= 0; --i) {
        const core::Transform* tr = w.tryGet<core::Transform>(chain[i]);
        const math::Transform local = tr ? toMath(*tr) : math::Transform{};
        world = math::compose(world, local);
    }

    core::Transform result;
    result.position = world.position;
    result.rotation = world.rotation;
    result.scale    = world.scale;
    return result;
}

} // namespace engine::core::ecs

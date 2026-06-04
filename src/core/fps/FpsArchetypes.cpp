#include "core/fps/FpsArchetypes.h"
#include <core/ecs/EntityFactory.h>
#include <core/components/Transform.h>
#include <core/components/Health.h>
#include <core/components/Lifetime.h>
#include <core/components/TeamTag.h>
#include <core/input/InputReceiverComponent.h>

namespace engine::core::fps {

using engine::core::ecs::Entity;
using engine::core::ecs::EntityFactory;
using engine::core::ecs::SpawnParams;
using engine::core::ecs::World;

namespace {

void spawnWithTransform(Entity e, const SpawnParams& p, World& w) {
    w.addComponent<engine::core::Transform>(e, {p.position, p.rotation});
}

} // namespace

void registerFpsArchetypes(EntityFactory& factory) {
    factory.registerArchetype("PlayerEntity",
        [](Entity e, const SpawnParams& p, World& w) {
            w.addComponent<engine::core::Transform>(e, {p.position, p.rotation});
            w.addComponent<engine::core::Health>(e, {});
            w.addComponent<engine::core::TeamTag>(e, {});
            w.addComponent<engine::core::input::InputReceiverComponent>(e, {});
        });

    factory.registerArchetype("WeaponEntity",     spawnWithTransform);
    factory.registerArchetype("StaticPropEntity", spawnWithTransform);
    factory.registerArchetype("TriggerEntity",    spawnWithTransform);
    factory.registerArchetype("SpawnPointEntity", spawnWithTransform);

    factory.registerArchetype("ProjectileEntity",
        [](Entity e, const SpawnParams& p, World& w) {
            w.addComponent<engine::core::Transform>(e, {p.position, p.rotation});
            w.addComponent<engine::core::Lifetime>(e, {});
        });

    factory.registerArchetype("PickupEntity",
        [](Entity e, const SpawnParams& p, World& w) {
            w.addComponent<engine::core::Transform>(e, {p.position, p.rotation});
            w.addComponent<engine::core::Lifetime>(e, {});
        });
}

} // namespace engine::core::fps

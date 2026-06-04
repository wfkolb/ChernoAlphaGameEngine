#pragma once

namespace engine::core::ecs { class EntityFactory; }

namespace engine::core::fps {

// Registers the 7 standard FPS archetypes into the given EntityFactory.
// Archetype names: "PlayerEntity", "WeaponEntity", "ProjectileEntity",
//                 "StaticPropEntity", "TriggerEntity", "SpawnPointEntity",
//                 "PickupEntity"
void registerFpsArchetypes(engine::core::ecs::EntityFactory& factory);

} // namespace engine::core::fps

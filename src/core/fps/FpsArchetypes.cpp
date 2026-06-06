#include "core/fps/FpsArchetypes.h"
#include <core/ecs/EntityFactory.h>
#include <core/ecs/HierarchyComponent.h>
#include <core/components/Transform.h>
#include <core/components/Health.h>
#include <core/components/Lifetime.h>
#include <core/components/TeamTag.h>
#include <core/components/AnimationState.h>
#include <core/components/MeshHandle.h>
#include <core/components/ColliderComponent.h>
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

ColliderComponent makeCapsuleCollider(float radius, float halfHeight) {
    ColliderComponent c{};
    c.shape                    = ColliderComponent::Shape::Capsule;
    c.params.capsule.radius     = radius;
    c.params.capsule.halfHeight = halfHeight;
    return c;
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

    // FpsCharacter: 4-entity hierarchy (root + CameraArm + FPMesh + TPMesh).
    // Physics (RigidBody, CharacterController) and networking (NetworkIdentity)
    // are added by the app layer since this module cannot depend on them.
    factory.registerArchetype("FpsCharacter",
        [](Entity root, const SpawnParams& p, World& w) {
            using ecs::HierarchyComponent;
            using ecs::linkChild;

            // ── Root ─────────────────────────────────────────────────────────
            w.addComponent<engine::core::Transform>(root, {p.position, p.rotation});
            w.addComponent<engine::core::Health>(root,
                engine::core::Health{ 100.f, 100.f, 0.f });
            w.addComponent<engine::core::TeamTag>(root,
                engine::core::TeamTag{ 0xFF });
            w.addComponent<engine::core::input::InputReceiverComponent>(root,
                engine::core::input::InputReceiverComponent{
                    0, 10, false, engine::core::input::FocusGroup::Gameplay });
            w.addComponent<engine::core::AnimationState>(root,
                engine::core::AnimationState{});
            w.addComponent<engine::core::ColliderComponent>(root,
                makeCapsuleCollider(0.35f, 0.525f));
            // Root gets HierarchyComponent so children can be linked.
            w.addComponent<HierarchyComponent>(root, HierarchyComponent{});

            // ── CameraArm ─────────────────────────────────────────────────────
            Entity cameraArm = w.createEntity();
            engine::core::Transform camArmTr{};
            camArmTr.position = {0.f, 1.65f, 0.f};
            w.addComponent<engine::core::Transform>(cameraArm, camArmTr);
            w.addComponent<HierarchyComponent>(cameraArm,
                HierarchyComponent{ root });
            linkChild(w, root, cameraArm);

            // ── FirstPersonMesh ───────────────────────────────────────────────
            Entity fpMesh = w.createEntity();
            engine::core::Transform fpTr{};
            fpTr.position = {0.15f, -0.20f, 0.35f};
            w.addComponent<engine::core::Transform>(fpMesh, fpTr);
            engine::core::MeshHandle fpHandle{};
            strncpy_s(fpHandle.assetPath, sizeof(fpHandle.assetPath),
                      "assets/first_person_arms.easset", _TRUNCATE);
            w.addComponent<engine::core::MeshHandle>(fpMesh, fpHandle);
            w.addComponent<HierarchyComponent>(fpMesh,
                HierarchyComponent{ cameraArm });
            linkChild(w, cameraArm, fpMesh);

            // ── ThirdPersonMesh ───────────────────────────────────────────────
            Entity tpMesh = w.createEntity();
            engine::core::Transform tpTr{};
            tpTr.position = engine::core::math::Vec3::zero();
            w.addComponent<engine::core::Transform>(tpMesh, tpTr);
            engine::core::MeshHandle tpHandle{};
            strncpy_s(tpHandle.assetPath, sizeof(tpHandle.assetPath),
                      "assets/third_person_capsule.easset", _TRUNCATE);
            w.addComponent<engine::core::MeshHandle>(tpMesh, tpHandle);
            w.addComponent<HierarchyComponent>(tpMesh,
                HierarchyComponent{ root });
            linkChild(w, root, tpMesh);
        });
}

} // namespace engine::core::fps

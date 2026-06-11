// Tests for Task #68 — SpawnPoint System
#include <gtest/gtest.h>
#include <core/components/SpawnPointComponent.h>
#include <core/components/TriggerComponent.h>
#include <core/ecs/World.h>
#include <core/components/Transform.h>

using engine::core::SpawnPointComponent;
using engine::core::TriggerComponent;
using engine::core::Transform;
using engine::core::ecs::Entity;
using engine::core::ecs::World;
using engine::core::ecs::kInvalidEntity;

TEST(SpawnPointComponent, DefaultFieldsAreAnyTeam) {
    SpawnPointComponent sp{};
    EXPECT_EQ(sp.teamId, 0u);
    EXPECT_EQ(sp.priority, 0u);
    EXPECT_GT(sp.radius, 0.0f);
}

TEST(SpawnPointComponent, ComponentIdIs14) {
    EXPECT_EQ(SpawnPointComponent::kComponentId, 14u);
}

TEST(SpawnPointComponent, IsTriviallyMovable) {
    EXPECT_TRUE(std::is_trivially_copyable_v<SpawnPointComponent>);
}

TEST(TriggerComponent, ComponentIdIs15) {
    EXPECT_EQ(TriggerComponent::kComponentId, 15u);
}

TEST(TriggerComponent, DefaultShapeIsBox) {
    TriggerComponent tc{};
    EXPECT_EQ(tc.shape, engine::core::ColliderComponent::Shape::Box);
}

TEST(TriggerComponent, IsTriviallyMovable) {
    EXPECT_TRUE(std::is_trivially_copyable_v<TriggerComponent>);
}

TEST(SpawnPointECS, AddAndQuerySpawnPoint) {
    // Register the component — safe to call even if already registered (overwrites slot).
    World::registerComponent<SpawnPointComponent>({
        "SpawnPointComponent", sizeof(SpawnPointComponent),
        alignof(SpawnPointComponent),
        [](void* p){ new(p) SpawnPointComponent{}; }, nullptr, nullptr
    });

    World world;
    Entity e = world.createEntity();
    SpawnPointComponent sp{};
    sp.teamId = 2;
    sp.priority = 5;
    world.addComponent<SpawnPointComponent>(e, sp);

    const auto* got = world.tryGet<SpawnPointComponent>(e);
    ASSERT_NE(got, nullptr);
    EXPECT_EQ(got->teamId, 2u);
    EXPECT_EQ(got->priority, 5u);
}

TEST(SpawnPointECS, AddAndQueryTrigger) {
    // Register the component — safe to call even if already registered.
    World::registerComponent<TriggerComponent>({
        "TriggerComponent", sizeof(TriggerComponent),
        alignof(TriggerComponent),
        [](void* p){ new(p) TriggerComponent{}; }, nullptr, nullptr
    });

    World world;
    Entity e = world.createEntity();
    TriggerComponent tc{};
    tc.eventTag = 42u;
    tc.teamFilter = 1u;
    world.addComponent<TriggerComponent>(e, tc);

    const auto* got = world.tryGet<TriggerComponent>(e);
    ASSERT_NE(got, nullptr);
    EXPECT_EQ(got->eventTag, 42u);
    EXPECT_EQ(got->teamFilter, 1u);
}

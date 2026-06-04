#include <gtest/gtest.h>
#include <core/ecs/EntityFactory.h>
#include <core/ecs/World.h>

using namespace engine::core::ecs;
using namespace engine::core::math;

// High component ID to avoid conflicts with engine or other test components.
struct Marker {
    static constexpr ComponentTypeId kComponentId = 100;
    bool spawned = false;
};

// ── SpawnParams ───────────────────────────────────────────────────────────────

TEST(SpawnParams, DefaultValues) {
    SpawnParams p;
    EXPECT_NEAR(p.position.x, 0.0f, 1e-6f);
    EXPECT_NEAR(p.position.y, 0.0f, 1e-6f);
    EXPECT_NEAR(p.position.z, 0.0f, 1e-6f);
    EXPECT_EQ(p.rotation, Quat::identity());
    EXPECT_EQ(p.parent, kInvalidEntity);
    EXPECT_TRUE(p.scene.empty());
}

// ── EntityFactory ─────────────────────────────────────────────────────────────

TEST(EntityFactory, UnknownArchetypeReturnsInvalid) {
    World world;
    EntityFactory factory;
    Entity e = factory.spawn("nonexistent", {}, world);
    EXPECT_EQ(e, kInvalidEntity);
}

TEST(EntityFactory, SpawnCreatesAliveEntity) {
    World world;
    EntityFactory factory;
    factory.registerArchetype("unit",
        [](Entity e, const SpawnParams&, World& w) {
            w.addComponent<Marker>(e, {true});
        });
    Entity e = factory.spawn("unit", {}, world);
    EXPECT_NE(e, kInvalidEntity);
    EXPECT_TRUE(world.isAlive(e));
}

TEST(EntityFactory, ArchetypeFnSetsComponent) {
    World world;
    EntityFactory factory;
    factory.registerArchetype("unit",
        [](Entity e, const SpawnParams&, World& w) {
            w.addComponent<Marker>(e, {true});
        });
    Entity e = factory.spawn("unit", {}, world);
    ASSERT_NE(world.tryGet<Marker>(e), nullptr);
    EXPECT_TRUE(world.get<Marker>(e).spawned);
}

TEST(EntityFactory, SpawnPassesPosition) {
    World world;
    EntityFactory factory;
    Vec3 captured;
    factory.registerArchetype("pos",
        [&captured](Entity, const SpawnParams& p, World&) { captured = p.position; });
    SpawnParams p;
    p.position = {1.0f, 2.0f, 3.0f};
    factory.spawn("pos", p, world);
    EXPECT_NEAR(captured.x, 1.0f, 1e-6f);
    EXPECT_NEAR(captured.y, 2.0f, 1e-6f);
    EXPECT_NEAR(captured.z, 3.0f, 1e-6f);
}

TEST(EntityFactory, SpawnPassesRotation) {
    World world;
    EntityFactory factory;
    Quat captured;
    factory.registerArchetype("rot",
        [&captured](Entity, const SpawnParams& p, World&) { captured = p.rotation; });
    SpawnParams p;
    p.rotation = {0.0f, 0.707f, 0.0f, 0.707f};
    factory.spawn("rot", p, world);
    EXPECT_NEAR(captured.y, 0.707f, 1e-4f);
    EXPECT_NEAR(captured.w, 0.707f, 1e-4f);
}

TEST(EntityFactory, MultipleArchetypesIndependent) {
    World world;
    EntityFactory factory;
    int a = 0, b = 0;
    factory.registerArchetype("A", [&a](Entity, const SpawnParams&, World&) { ++a; });
    factory.registerArchetype("B", [&b](Entity, const SpawnParams&, World&) { ++b; });
    factory.spawn("A", {}, world);
    factory.spawn("B", {}, world);
    factory.spawn("A", {}, world);
    EXPECT_EQ(a, 2);
    EXPECT_EQ(b, 1);
}

TEST(EntityFactory, SpawnMultipleEntitiesDistinct) {
    World world;
    EntityFactory factory;
    factory.registerArchetype("unit",
        [](Entity e, const SpawnParams&, World& w) { w.addComponent<Marker>(e, {}); });
    Entity e1 = factory.spawn("unit", {}, world);
    Entity e2 = factory.spawn("unit", {}, world);
    EXPECT_NE(e1, kInvalidEntity);
    EXPECT_NE(e2, kInvalidEntity);
    EXPECT_NE(e1, e2);
}

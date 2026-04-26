// tests/core/ecs/WorldTests.cpp
// Unit tests for engine::core::ecs::World (Task #35).

#include <core/ecs/World.h>
#include <gtest/gtest.h>

using namespace engine::core::ecs;

namespace {

struct Position {
    float x, y, z;
    static constexpr ComponentTypeId kComponentId = 0;
};

struct Velocity {
    float vx, vy, vz;
    static constexpr ComponentTypeId kComponentId = 1;
};

struct Health {
    int hp;
    static constexpr ComponentTypeId kComponentId = 2;
};

} // namespace

TEST(WorldTest, createEntityReturnsAlive) {
    World world;
    Entity e = world.createEntity();
    EXPECT_TRUE(world.isAlive(e));
}

TEST(WorldTest, destroyedEntityIsNotAlive) {
    World world;
    Entity e = world.createEntity();
    world.destroyEntity(e);
    EXPECT_FALSE(world.isAlive(e));
}

TEST(WorldTest, stableHandleAcrossArchetypeMove) {
    World world;
    Entity e = world.createEntity();
    const uint32_t savedIndex      = e.index;
    const uint32_t savedGeneration = e.generation;

    world.addComponent<Position>(e, {1.0f, 2.0f, 3.0f});

    EXPECT_TRUE(world.isAlive(e));
    EXPECT_EQ(e.index,      savedIndex);
    EXPECT_EQ(e.generation, savedGeneration);
}

TEST(WorldTest, addRemoveComponentCycleCorrect) {
    World world;
    Entity e = world.createEntity();

    world.addComponent<Position>(e, {1.0f, 2.0f, 3.0f});
    EXPECT_NE(world.tryGet<Position>(e), nullptr);

    world.removeComponent<Position>(e);
    EXPECT_EQ(world.tryGet<Position>(e), nullptr);

    world.addComponent<Velocity>(e, {4.0f, 5.0f, 6.0f});
    EXPECT_NE(world.tryGet<Velocity>(e), nullptr);
    EXPECT_EQ(world.tryGet<Position>(e), nullptr);

    EXPECT_TRUE(world.isAlive(e));
}

TEST(WorldTest, getReturnsNullForMissingComponent) {
    World world;
    Entity e = world.createEntity();
    world.addComponent<Position>(e, {1.0f, 0.0f, 0.0f});

    EXPECT_EQ(world.tryGet<Velocity>(e), nullptr);
    EXPECT_EQ(world.tryGet<Health>(e),   nullptr);
}

TEST(WorldTest, getReturnsNullForDestroyedEntity) {
    World world;
    Entity e = world.createEntity();
    world.addComponent<Position>(e, {1.0f, 2.0f, 3.0f});
    world.destroyEntity(e);

    EXPECT_EQ(world.tryGet<Position>(e), nullptr);
}

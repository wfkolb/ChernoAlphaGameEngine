// tests/core/ecs/CommandBufferTests.cpp
// Unit tests for engine::core::ecs::CommandBuffer (Task #35).

#include <core/ecs/World.h>
#include <core/ecs/CommandBuffer.h>
#include <gtest/gtest.h>

#include <vector>

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

TEST(CommandBufferTest, deferredAddComponentAppliedAfterFlush) {
    World world;
    Entity e = world.createEntity();

    CommandBuffer cb;
    cb.addComponent<Position>(e, {5.0f, 6.0f, 7.0f});

    EXPECT_EQ(world.tryGet<Position>(e), nullptr);

    cb.flush(world);

    Position* p = world.tryGet<Position>(e);
    ASSERT_NE(p, nullptr);
    EXPECT_FLOAT_EQ(p->x, 5.0f);
    EXPECT_FLOAT_EQ(p->y, 6.0f);
    EXPECT_FLOAT_EQ(p->z, 7.0f);
}

TEST(CommandBufferTest, deferredDestroyEntityAppliedAfterFlush) {
    World world;
    Entity e = world.createEntity();

    CommandBuffer cb;
    cb.destroyEntity(e);

    EXPECT_TRUE(world.isAlive(e));

    cb.flush(world);

    EXPECT_FALSE(world.isAlive(e));
}

TEST(CommandBufferTest, commandOrderIsPreservedWithinFlush) {
    World world;

    CommandBuffer cb;
    Entity pending = cb.createEntity();
    cb.addComponent<Position>(pending, {10.0f, 20.0f, 30.0f});
    cb.addComponent<Velocity>(pending, {1.0f, 2.0f, 3.0f});

    cb.flush(world);

    std::vector<Entity> alive;
    world.forEachEntity([&](Entity e) { alive.push_back(e); });

    ASSERT_EQ(alive.size(), 1u);
    Entity real = alive[0];

    EXPECT_TRUE(world.isAlive(real));

    Position* p = world.tryGet<Position>(real);
    ASSERT_NE(p, nullptr);
    EXPECT_FLOAT_EQ(p->x, 10.0f);
    EXPECT_FLOAT_EQ(p->y, 20.0f);
    EXPECT_FLOAT_EQ(p->z, 30.0f);

    Velocity* v = world.tryGet<Velocity>(real);
    ASSERT_NE(v, nullptr);
    EXPECT_FLOAT_EQ(v->vx, 1.0f);
    EXPECT_FLOAT_EQ(v->vy, 2.0f);
    EXPECT_FLOAT_EQ(v->vz, 3.0f);
}

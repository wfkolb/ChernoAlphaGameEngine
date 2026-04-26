// tests/core/ecs/ViewTests.cpp
// Unit tests for engine::core::ecs::View (Task #35).

#include <core/ecs/World.h>
#include <core/ecs/View.h>
#include <gtest/gtest.h>

#include <tuple>
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

TEST(ViewTest, viewIteratesMatchingArchetypes) {
    World world;

    Entity posOnly = world.createEntity();
    world.addComponent<Position>(posOnly, {0.0f, 0.0f, 0.0f});

    Entity posVel1 = world.createEntity();
    world.addComponent<Position>(posVel1, {1.0f, 0.0f, 0.0f});
    world.addComponent<Velocity>(posVel1, {1.0f, 0.0f, 0.0f});

    Entity posVel2 = world.createEntity();
    world.addComponent<Position>(posVel2, {2.0f, 0.0f, 0.0f});
    world.addComponent<Velocity>(posVel2, {2.0f, 0.0f, 0.0f});

    View<Position, Velocity> view(world);

    std::vector<Entity> visited;
    for (auto [e, pos, vel] : view) {
        visited.push_back(e);
    }

    EXPECT_EQ(visited.size(), 2u);
    EXPECT_EQ(std::count(visited.begin(), visited.end(), posOnly), 0);
    EXPECT_EQ(std::count(visited.begin(), visited.end(), posVel1), 1);
    EXPECT_EQ(std::count(visited.begin(), visited.end(), posVel2), 1);
}

TEST(ViewTest, viewExcludesExcludedComponent) {
    // TODO: View does not expose a Without<> exclusion filter. When/if
    // engine::core::ecs::View gains a Without<T...> template parameter,
    // this test should verify that entities possessing the excluded component
    // are skipped during iteration.
    GTEST_SKIP() << "View does not support Without<> exclusion; API needed: View<A, B>::Without<C>";
}

TEST(ViewTest, viewOptionalReturnsNullForMissingComponent) {
    // TODO: View does not expose an Optional<T> wrapper in its iterator.
    // When/if the engine::core::ecs::View iterator gains Optional<T> support
    // (returning T* rather than T&, null when absent), this test should verify
    // that a missing optional component yields nullptr.
    GTEST_SKIP() << "View does not support Optional<>; API needed: View<A, Optional<B>>";
}

TEST(ViewTest, viewIterationOrderIsConsistent) {
    World world;

    Entity e1 = world.createEntity();
    world.addComponent<Position>(e1, {1.0f, 0.0f, 0.0f});
    world.addComponent<Velocity>(e1, {0.0f, 0.0f, 0.0f});

    Entity e2 = world.createEntity();
    world.addComponent<Position>(e2, {2.0f, 0.0f, 0.0f});
    world.addComponent<Velocity>(e2, {0.0f, 0.0f, 0.0f});

    Entity e3 = world.createEntity();
    world.addComponent<Position>(e3, {3.0f, 0.0f, 0.0f});
    world.addComponent<Velocity>(e3, {0.0f, 0.0f, 0.0f});

    View<Position, Velocity> view(world);

    std::vector<Entity> firstPass;
    for (auto [e, pos, vel] : view) {
        firstPass.push_back(e);
    }

    std::vector<Entity> secondPass;
    for (auto [e, pos, vel] : view) {
        secondPass.push_back(e);
    }

    EXPECT_EQ(firstPass, secondPass);
}

TEST(ViewTest, viewIsInvalidatedByNewArchetype) {
    World world;

    Entity e1 = world.createEntity();
    world.addComponent<Position>(e1, {1.0f, 0.0f, 0.0f});
    world.addComponent<Velocity>(e1, {0.0f, 0.0f, 0.0f});

    {
        View<Position, Velocity> viewBefore(world);
        int countBefore = 0;
        for (auto [e, pos, vel] : viewBefore) {
            ++countBefore;
        }
        EXPECT_EQ(countBefore, 1);
    }

    Entity e2 = world.createEntity();
    world.addComponent<Position>(e2, {2.0f, 0.0f, 0.0f});
    world.addComponent<Velocity>(e2, {0.0f, 0.0f, 0.0f});
    world.addComponent<Health>(e2, {100});

    View<Position, Velocity> viewAfter(world);
    int countAfter = 0;
    for (auto [e, pos, vel] : viewAfter) {
        ++countAfter;
    }

    EXPECT_EQ(countAfter, 2);
}

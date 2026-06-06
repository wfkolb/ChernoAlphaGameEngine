#include <gtest/gtest.h>

#include <core/ecs/HierarchyComponent.h>
#include <core/ecs/World.h>
#include <core/components/Transform.h>

#include <type_traits>
#include <cmath>

using namespace engine::core::ecs;
using engine::core::Transform;

static_assert(std::is_trivially_copyable_v<HierarchyComponent>,
              "HierarchyComponent must be trivially copyable");

// ── linkChild ─────────────────────────────────────────────────────────────────

TEST(HierarchyComponentTest, LinkChildSetsParent) {
    World world;
    Entity parent = world.createEntity();
    Entity child  = world.createEntity();
    world.addComponent<HierarchyComponent>(child, HierarchyComponent{});

    linkChild(world, parent, child);

    ASSERT_NE(world.tryGet<HierarchyComponent>(child), nullptr);
    EXPECT_EQ(world.get<HierarchyComponent>(child).parent, parent);
}

TEST(HierarchyComponentTest, LinkChildUpdatesFirstChild) {
    World world;
    Entity parent = world.createEntity();
    Entity child1 = world.createEntity();
    Entity child2 = world.createEntity();
    world.addComponent<HierarchyComponent>(child1, HierarchyComponent{});
    world.addComponent<HierarchyComponent>(child2, HierarchyComponent{});

    linkChild(world, parent, child1);
    linkChild(world, parent, child2);

    const auto& phc = world.get<HierarchyComponent>(parent);
    EXPECT_EQ(phc.firstChild, child1);
}

TEST(HierarchyComponentTest, LinkChildBuildsNextSiblingChain) {
    World world;
    Entity parent = world.createEntity();
    Entity child1 = world.createEntity();
    Entity child2 = world.createEntity();
    Entity child3 = world.createEntity();
    world.addComponent<HierarchyComponent>(child1, HierarchyComponent{});
    world.addComponent<HierarchyComponent>(child2, HierarchyComponent{});
    world.addComponent<HierarchyComponent>(child3, HierarchyComponent{});

    linkChild(world, parent, child1);
    linkChild(world, parent, child2);
    linkChild(world, parent, child3);

    EXPECT_EQ(world.get<HierarchyComponent>(child1).nextSibling, child2);
    EXPECT_EQ(world.get<HierarchyComponent>(child2).nextSibling, child3);
    EXPECT_EQ(world.get<HierarchyComponent>(child3).nextSibling, kInvalidEntity);
}

TEST(HierarchyComponentTest, LinkChildBuildsPrevSiblingChain) {
    World world;
    Entity parent = world.createEntity();
    Entity child1 = world.createEntity();
    Entity child2 = world.createEntity();
    world.addComponent<HierarchyComponent>(child1, HierarchyComponent{});
    world.addComponent<HierarchyComponent>(child2, HierarchyComponent{});

    linkChild(world, parent, child1);
    linkChild(world, parent, child2);

    EXPECT_EQ(world.get<HierarchyComponent>(child1).prevSibling, kInvalidEntity);
    EXPECT_EQ(world.get<HierarchyComponent>(child2).prevSibling, child1);
}

TEST(HierarchyComponentTest, LinkChildAddsHierarchyToParentIfAbsent) {
    World world;
    Entity parent = world.createEntity();
    Entity child  = world.createEntity();
    world.addComponent<HierarchyComponent>(child, HierarchyComponent{});

    EXPECT_FALSE(world.hasComponent(parent, HierarchyComponent::kComponentId));
    linkChild(world, parent, child);
    EXPECT_TRUE(world.hasComponent(parent, HierarchyComponent::kComponentId));
}

// ── computeWorldTransform ─────────────────────────────────────────────────────

TEST(HierarchyComponentTest, ComputeWorldTransformRootIsLocal) {
    World world;
    Entity root = world.createEntity();
    Transform t{};
    t.position = {1.f, 2.f, 3.f};
    world.addComponent<Transform>(root, t);

    const Transform wt = computeWorldTransform(world, root);
    EXPECT_NEAR(wt.position.x, 1.f, 1e-5f);
    EXPECT_NEAR(wt.position.y, 2.f, 1e-5f);
    EXPECT_NEAR(wt.position.z, 3.f, 1e-5f);
}

TEST(HierarchyComponentTest, ComputeWorldTransformAddsParentOffset) {
    World world;
    Entity parent = world.createEntity();
    Entity child  = world.createEntity();

    Transform pt{};
    pt.position = {1.f, 0.f, 0.f};
    world.addComponent<Transform>(parent, pt);

    Transform ct{};
    ct.position = {0.f, 2.f, 0.f};
    world.addComponent<Transform>(child, ct);

    world.addComponent<HierarchyComponent>(child, HierarchyComponent{});
    linkChild(world, parent, child);

    const Transform wt = computeWorldTransform(world, child);
    EXPECT_NEAR(wt.position.x, 1.f, 1e-5f);
    EXPECT_NEAR(wt.position.y, 2.f, 1e-5f);
    EXPECT_NEAR(wt.position.z, 0.f, 1e-5f);
}

TEST(HierarchyComponentTest, ComputeWorldTransformThreeLevels) {
    World world;
    Entity root  = world.createEntity();
    Entity mid   = world.createEntity();
    Entity leaf  = world.createEntity();

    Transform rt{}; rt.position = {1.f, 0.f, 0.f};
    Transform mt{}; mt.position = {0.f, 2.f, 0.f};
    Transform lt{}; lt.position = {0.f, 0.f, 3.f};
    world.addComponent<Transform>(root, rt);
    world.addComponent<Transform>(mid,  mt);
    world.addComponent<Transform>(leaf, lt);

    world.addComponent<HierarchyComponent>(mid,  HierarchyComponent{});
    world.addComponent<HierarchyComponent>(leaf, HierarchyComponent{});
    linkChild(world, root, mid);
    linkChild(world, mid,  leaf);

    const Transform wt = computeWorldTransform(world, leaf);
    EXPECT_NEAR(wt.position.x, 1.f, 1e-5f);
    EXPECT_NEAR(wt.position.y, 2.f, 1e-5f);
    EXPECT_NEAR(wt.position.z, 3.f, 1e-5f);
}

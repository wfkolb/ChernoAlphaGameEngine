#include <gtest/gtest.h>

#include <core/components/ColliderComponent.h>
#include <core/ecs/World.h>

#include <type_traits>
#include <cstring>

using engine::core::ColliderComponent;
using engine::core::ecs::World;
using engine::core::ecs::Entity;

// ColliderComponent must be trivially copyable to live safely in the ECS
// archetype columns (byte-copied during entity moves between archetypes).
static_assert(std::is_trivially_copyable_v<ColliderComponent>,
              "ColliderComponent must be trivially copyable");

// ── Default values ────────────────────────────────────────────────────────────

TEST(ColliderComponentTest, DefaultShapeIsBox) {
    ColliderComponent c{};
    EXPECT_EQ(c.shape, ColliderComponent::Shape::Box);
}

TEST(ColliderComponentTest, DefaultBoxHalfExtents) {
    ColliderComponent c{};
    EXPECT_FLOAT_EQ(c.params.box.halfX, 0.5f);
    EXPECT_FLOAT_EQ(c.params.box.halfY, 0.5f);
    EXPECT_FLOAT_EQ(c.params.box.halfZ, 0.5f);
}

TEST(ColliderComponentTest, DefaultOffsetIsZero) {
    ColliderComponent c{};
    EXPECT_FLOAT_EQ(c.offsetX, 0.0f);
    EXPECT_FLOAT_EQ(c.offsetY, 0.0f);
    EXPECT_FLOAT_EQ(c.offsetZ, 0.0f);
}

TEST(ColliderComponentTest, DefaultLayerAndMaterialAreZero) {
    ColliderComponent c{};
    EXPECT_EQ(c.layerIndex,    0u);
    EXPECT_EQ(c.materialIndex, 0u);
}

TEST(ColliderComponentTest, DefaultIsTriggerFalse) {
    ColliderComponent c{};
    EXPECT_FALSE(c.isTrigger);
}

// ── ECS round-trip ────────────────────────────────────────────────────────────

TEST(ColliderComponentTest, AddAndRetrieveBox) {
    World w;
    Entity e = w.createEntity();

    ColliderComponent box{};
    box.shape          = ColliderComponent::Shape::Box;
    box.params.box     = { 1.0f, 2.0f, 3.0f };
    box.offsetY        = 0.5f;
    box.isTrigger      = false;
    w.addComponent<ColliderComponent>(e, box);

    const ColliderComponent* got = w.tryGet<ColliderComponent>(e);
    ASSERT_NE(got, nullptr);
    EXPECT_EQ(got->shape, ColliderComponent::Shape::Box);
    EXPECT_FLOAT_EQ(got->params.box.halfX, 1.0f);
    EXPECT_FLOAT_EQ(got->params.box.halfY, 2.0f);
    EXPECT_FLOAT_EQ(got->params.box.halfZ, 3.0f);
    EXPECT_FLOAT_EQ(got->offsetY, 0.5f);
}

TEST(ColliderComponentTest, AddAndRetrieveSphere) {
    World w;
    Entity e = w.createEntity();

    ColliderComponent s{};
    s.shape               = ColliderComponent::Shape::Sphere;
    s.params.sphere.radius = 4.0f;
    s.isTrigger           = true;
    w.addComponent<ColliderComponent>(e, s);

    const ColliderComponent* got = w.tryGet<ColliderComponent>(e);
    ASSERT_NE(got, nullptr);
    EXPECT_EQ(got->shape, ColliderComponent::Shape::Sphere);
    EXPECT_FLOAT_EQ(got->params.sphere.radius, 4.0f);
    EXPECT_TRUE(got->isTrigger);
}

TEST(ColliderComponentTest, AddAndRetrieveCapsule) {
    World w;
    Entity e = w.createEntity();

    ColliderComponent cap{};
    cap.shape                    = ColliderComponent::Shape::Capsule;
    cap.params.capsule.radius     = 0.4f;
    cap.params.capsule.halfHeight = 1.2f;
    w.addComponent<ColliderComponent>(e, cap);

    const ColliderComponent* got = w.tryGet<ColliderComponent>(e);
    ASSERT_NE(got, nullptr);
    EXPECT_EQ(got->shape, ColliderComponent::Shape::Capsule);
    EXPECT_FLOAT_EQ(got->params.capsule.radius,     0.4f);
    EXPECT_FLOAT_EQ(got->params.capsule.halfHeight, 1.2f);
}

TEST(ColliderComponentTest, RemoveComponent) {
    World w;
    Entity e = w.createEntity();
    w.addComponent<ColliderComponent>(e, ColliderComponent{});
    ASSERT_NE(w.tryGet<ColliderComponent>(e), nullptr);
    w.removeComponent<ColliderComponent>(e);
    EXPECT_EQ(w.tryGet<ColliderComponent>(e), nullptr);
}

TEST(ColliderComponentTest, TrivialCopyPreservesData) {
    ColliderComponent src{};
    src.shape                = ColliderComponent::Shape::Capsule;
    src.params.capsule.radius = 0.3f;
    src.layerIndex           = 5u;
    src.materialIndex        = 2u;
    src.isTrigger            = true;

    ColliderComponent dst{};
    std::memcpy(&dst, &src, sizeof(dst));

    EXPECT_EQ(dst.shape, ColliderComponent::Shape::Capsule);
    EXPECT_FLOAT_EQ(dst.params.capsule.radius, 0.3f);
    EXPECT_EQ(dst.layerIndex,    5u);
    EXPECT_EQ(dst.materialIndex, 2u);
    EXPECT_TRUE(dst.isTrigger);
}

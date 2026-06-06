#include <gtest/gtest.h>

#include <core/fps/FpsArchetypes.h>
#include <core/ecs/EntityFactory.h>
#include <core/ecs/World.h>
#include <core/ecs/HierarchyComponent.h>
#include <core/components/Transform.h>
#include <core/components/Health.h>
#include <core/components/TeamTag.h>
#include <core/components/AnimationState.h>
#include <core/components/MeshHandle.h>
#include <core/components/ColliderComponent.h>
#include <core/input/InputReceiverComponent.h>

using namespace engine::core::ecs;
using engine::core::Transform;
using engine::core::Health;
using engine::core::TeamTag;
using engine::core::AnimationState;
using engine::core::MeshHandle;
using engine::core::ColliderComponent;
using engine::core::input::InputReceiverComponent;

// Helper: count live entities in the world.
static int countEntities(World& world) {
    int n = 0;
    world.forEachEntity([&](Entity) { ++n; });
    return n;
}

class FpsCharacterTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine::core::fps::registerFpsArchetypes(factory_);
    }

    EntityFactory factory_;
    World         world_;
};

TEST_F(FpsCharacterTest, SpawnCreatesFourEntities) {
    SpawnParams p{};
    const Entity root = factory_.spawn("FpsCharacter", p, world_);
    ASSERT_NE(root, kInvalidEntity);
    EXPECT_EQ(countEntities(world_), 4);
}

TEST_F(FpsCharacterTest, RootHasTransform) {
    const Entity root = factory_.spawn("FpsCharacter", {}, world_);
    EXPECT_NE(world_.tryGet<Transform>(root), nullptr);
}

TEST_F(FpsCharacterTest, RootTransformMatchesSpawnParams) {
    SpawnParams p{};
    p.position = {5.f, 1.f, -3.f};
    const Entity root = factory_.spawn("FpsCharacter", p, world_);
    const auto* t = world_.tryGet<Transform>(root);
    ASSERT_NE(t, nullptr);
    EXPECT_NEAR(t->position.x, 5.f, 1e-5f);
    EXPECT_NEAR(t->position.y, 1.f, 1e-5f);
    EXPECT_NEAR(t->position.z, -3.f, 1e-5f);
}

TEST_F(FpsCharacterTest, RootHasHealthAt100) {
    const Entity root = factory_.spawn("FpsCharacter", {}, world_);
    const auto* h = world_.tryGet<Health>(root);
    ASSERT_NE(h, nullptr);
    EXPECT_FLOAT_EQ(h->currentHp, 100.f);
    EXPECT_FLOAT_EQ(h->maxHp, 100.f);
}

TEST_F(FpsCharacterTest, RootHasTeamTagUnassigned) {
    const Entity root = factory_.spawn("FpsCharacter", {}, world_);
    const auto* t = world_.tryGet<TeamTag>(root);
    ASSERT_NE(t, nullptr);
    EXPECT_EQ(t->teamId, 0xFFu);
}

TEST_F(FpsCharacterTest, RootHasInputReceiverWithPriority10) {
    const Entity root = factory_.spawn("FpsCharacter", {}, world_);
    const auto* ir = world_.tryGet<InputReceiverComponent>(root);
    ASSERT_NE(ir, nullptr);
    EXPECT_EQ(ir->priority, 10u);
    EXPECT_EQ(ir->focusGroup, engine::core::input::FocusGroup::Gameplay);
}

TEST_F(FpsCharacterTest, RootHasAnimationStateIdleByDefault) {
    const Entity root = factory_.spawn("FpsCharacter", {}, world_);
    const auto* a = world_.tryGet<AnimationState>(root);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->currentClip, AnimationState::Clip::Idle);
    EXPECT_FLOAT_EQ(a->clipTimeSeconds, 0.f);
}

TEST_F(FpsCharacterTest, RootHasCapsuleCollider) {
    const Entity root = factory_.spawn("FpsCharacter", {}, world_);
    const auto* c = world_.tryGet<ColliderComponent>(root);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->shape, ColliderComponent::Shape::Capsule);
    EXPECT_NEAR(c->params.capsule.radius,     0.35f, 1e-5f);
    EXPECT_NEAR(c->params.capsule.halfHeight, 0.525f, 1e-5f);
}

TEST_F(FpsCharacterTest, RootHasHierarchyComponentAsRoot) {
    const Entity root = factory_.spawn("FpsCharacter", {}, world_);
    const auto* hc = world_.tryGet<HierarchyComponent>(root);
    ASSERT_NE(hc, nullptr);
    EXPECT_EQ(hc->parent, kInvalidEntity);
}

TEST_F(FpsCharacterTest, RootFirstChildIsCameraArm) {
    const Entity root = factory_.spawn("FpsCharacter", {}, world_);
    const auto* hc = world_.tryGet<HierarchyComponent>(root);
    ASSERT_NE(hc, nullptr);
    const Entity cameraArm = hc->firstChild;
    EXPECT_NE(cameraArm, kInvalidEntity);
    EXPECT_TRUE(world_.isAlive(cameraArm));
}

TEST_F(FpsCharacterTest, CameraArmHasEyeHeightOffset) {
    const Entity root      = factory_.spawn("FpsCharacter", {}, world_);
    const Entity cameraArm = world_.get<HierarchyComponent>(root).firstChild;

    const auto* t = world_.tryGet<Transform>(cameraArm);
    ASSERT_NE(t, nullptr);
    EXPECT_NEAR(t->position.y, 1.65f, 1e-5f);
}

TEST_F(FpsCharacterTest, CameraArmParentIsRoot) {
    const Entity root      = factory_.spawn("FpsCharacter", {}, world_);
    const Entity cameraArm = world_.get<HierarchyComponent>(root).firstChild;

    const auto* hc = world_.tryGet<HierarchyComponent>(cameraArm);
    ASSERT_NE(hc, nullptr);
    EXPECT_EQ(hc->parent, root);
}

TEST_F(FpsCharacterTest, FirstPersonMeshHasMeshHandle) {
    const Entity root      = factory_.spawn("FpsCharacter", {}, world_);
    const Entity cameraArm = world_.get<HierarchyComponent>(root).firstChild;
    const Entity fpMesh    = world_.get<HierarchyComponent>(cameraArm).firstChild;

    EXPECT_NE(fpMesh, kInvalidEntity);
    const auto* m = world_.tryGet<MeshHandle>(fpMesh);
    ASSERT_NE(m, nullptr);
    EXPECT_STRNE(m->assetPath, "");
}

TEST_F(FpsCharacterTest, ThirdPersonMeshHasMeshHandle) {
    const Entity root      = factory_.spawn("FpsCharacter", {}, world_);
    const Entity cameraArm = world_.get<HierarchyComponent>(root).firstChild;
    const Entity tpMesh    = world_.get<HierarchyComponent>(cameraArm).nextSibling;

    EXPECT_NE(tpMesh, kInvalidEntity);
    const auto* m = world_.tryGet<MeshHandle>(tpMesh);
    ASSERT_NE(m, nullptr);
    EXPECT_STRNE(m->assetPath, "");
}

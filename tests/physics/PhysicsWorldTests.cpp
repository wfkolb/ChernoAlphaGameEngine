#include <gtest/gtest.h>
#include <physics/PhysicsWorld.h>
#include <physics/RigidBody.h>
#include <physics/CharacterController.h>
#include <physics/ColliderShape.h>
#include <physics/QueryFilter.h>
#include <core/math/Constants.h>

using namespace engine::physics;
using namespace engine::core::math;
using engine::core::ecs::kInvalidEntity;

// Helper: default ECS transform at a given position.
static engine::core::Transform makeTransform(Vec3 pos, Quat rot = Quat::identity()) {
    engine::core::Transform t;
    t.position = pos;
    t.rotation = rot;
    return t;
}

// Helper: static sphere body.
static BodyId addStaticSphere(PhysicsWorld& pw, Vec3 pos, float r,
                               engine::core::ecs::Entity e = kInvalidEntity) {
    RigidBody rb; rb.type = RigidBodyType::Static;
    Collider  c;  c.shape = SphereShape{r};
    return pw.addRigidBody(e, makeTransform(pos), rb, c);
}

// Helper: static box body.
static BodyId addStaticBox(PhysicsWorld& pw, Vec3 pos, Vec3 halfExtents,
                            engine::core::ecs::Entity e = kInvalidEntity) {
    RigidBody rb; rb.type = RigidBodyType::Static;
    Collider  c;  c.shape = BoxShape{halfExtents};
    return pw.addRigidBody(e, makeTransform(pos), rb, c);
}

// ── Construction ──────────────────────────────────────────────────────────────

TEST(PhysicsWorld, DefaultGravity) {
    PhysicsWorld pw;
    const Vec3 g = pw.getGravity();
    EXPECT_NEAR(g.x, 0.0f,  1e-5f);
    EXPECT_NEAR(g.y, -9.81f, 1e-4f);
    EXPECT_NEAR(g.z, 0.0f,  1e-5f);
}

TEST(PhysicsWorld, SetGravity) {
    PhysicsWorld pw;
    pw.setGravity({0, -20, 0});
    EXPECT_NEAR(pw.getGravity().y, -20.0f, 1e-5f);
}

// ── Body management ───────────────────────────────────────────────────────────

TEST(PhysicsWorld, AddAndGetTransform) {
    PhysicsWorld pw;
    RigidBody rb; rb.type = RigidBodyType::Static;
    Collider  c;  c.shape = SphereShape{1.0f};
    const BodyId id = pw.addRigidBody(kInvalidEntity, makeTransform({3,4,5}), rb, c);
    EXPECT_NE(id, kInvalidBodyId);
    const auto t = pw.getTransform(id);
    EXPECT_NEAR(t.position.x, 3.0f, 1e-5f);
    EXPECT_NEAR(t.position.y, 4.0f, 1e-5f);
    EXPECT_NEAR(t.position.z, 5.0f, 1e-5f);
}

TEST(PhysicsWorld, RemoveBody) {
    PhysicsWorld pw;
    const BodyId id = addStaticSphere(pw, {0,0,0}, 1.0f);
    pw.removeBody(id);
    // After removal the transform returns a default (zero) transform
    const auto t = pw.getTransform(id);
    EXPECT_NEAR(t.position.x, 0.0f, 1e-5f);
}

// ── Static bodies stay still ──────────────────────────────────────────────────

TEST(PhysicsWorld, StaticBodyDoesNotMove) {
    PhysicsWorld pw;
    const BodyId id = addStaticSphere(pw, {0, 5, 0}, 1.0f);
    pw.step(1.0f / 64.0f);
    EXPECT_NEAR(pw.getTransform(id).position.y, 5.0f, 1e-5f);
}

// ── Dynamic body falls under gravity ─────────────────────────────────────────

TEST(PhysicsWorld, DynamicBodyFallsUnderGravity) {
    PhysicsWorld pw;
    RigidBody rb;
    rb.type = RigidBodyType::Dynamic;
    rb.mass = 1.0f;
    Collider c; c.shape = SphereShape{0.5f};
    const BodyId id = pw.addRigidBody(kInvalidEntity, makeTransform({0, 10, 0}), rb, c);
    pw.step(1.0f / 64.0f);
    EXPECT_LT(pw.getTransform(id).position.y, 10.0f);
}

TEST(PhysicsWorld, FreezeYAxisPreventsGravity) {
    PhysicsWorld pw;
    RigidBody rb;
    rb.type        = RigidBodyType::Dynamic;
    rb.mass        = 1.0f;
    rb.freezeFlags = kFreezePosY;
    Collider c; c.shape = SphereShape{0.5f};
    const BodyId id = pw.addRigidBody(kInvalidEntity, makeTransform({0, 5, 0}), rb, c);
    pw.step(1.0f / 64.0f);
    EXPECT_NEAR(pw.getTransform(id).position.y, 5.0f, 1e-4f);
}

// ── Raycast ───────────────────────────────────────────────────────────────────

TEST(PhysicsWorld, RaycastHitsSphere) {
    PhysicsWorld pw;
    addStaticSphere(pw, {0, 0, 5}, 1.0f);
    const RaycastHit hit = pw.raycast({0, 0, 0}, {0, 0, 1}, 100.0f);
    EXPECT_TRUE(hit.hasHit);
    EXPECT_NEAR(hit.distance, 4.0f, 0.01f); // surface at z=4
}

TEST(PhysicsWorld, RaycastMissesSphere) {
    PhysicsWorld pw;
    addStaticSphere(pw, {0, 0, 5}, 1.0f);
    const RaycastHit hit = pw.raycast({0, 0, 0}, {1, 0, 0}, 100.0f);
    EXPECT_FALSE(hit.hasHit);
}

TEST(PhysicsWorld, RaycastHitsBox) {
    PhysicsWorld pw;
    addStaticBox(pw, {0, 0, 5}, {1, 1, 1});
    const RaycastHit hit = pw.raycast({0, 0, 0}, {0, 0, 1}, 100.0f);
    EXPECT_TRUE(hit.hasHit);
    EXPECT_LT(hit.distance, 5.0f); // front face at z=4
}

TEST(PhysicsWorld, RaycastMaxDistRespected) {
    PhysicsWorld pw;
    addStaticSphere(pw, {0, 0, 10}, 1.0f);
    const RaycastHit hit = pw.raycast({0, 0, 0}, {0, 0, 1}, 5.0f);
    EXPECT_FALSE(hit.hasHit);
}

// ── SweepCapsule ─────────────────────────────────────────────────────────────

TEST(PhysicsWorld, SweepCapsuleHitsFloor) {
    PhysicsWorld pw;
    // Floor: box centred at y=-1 with halfExtents {50,1,50}, so top at y=0
    addStaticBox(pw, {0, -1, 0}, {50, 1, 50});
    // Capsule starts at y=5, sweeps down
    const RaycastHit hit = pw.sweepCapsule({0, 5, 0}, {0, -1, 0}, 0.3f, 0.5f, 20.0f);
    EXPECT_TRUE(hit.hasHit);
    EXPECT_GT(hit.distance, 0.0f);
    EXPECT_LT(hit.distance, 20.0f);
}

TEST(PhysicsWorld, SweepCapsuleMisses) {
    PhysicsWorld pw;
    addStaticSphere(pw, {0, 0, 10}, 1.0f);
    // Sweep perpendicular to sphere
    const RaycastHit hit = pw.sweepCapsule({0, 0, 0}, {1, 0, 0}, 0.3f, 0.5f, 5.0f);
    EXPECT_FALSE(hit.hasHit);
}

// ── OverlapSphere ─────────────────────────────────────────────────────────────

TEST(PhysicsWorld, OverlapSphereFindsStaticSphere) {
    PhysicsWorld pw;
    addStaticSphere(pw, {0, 0, 0}, 1.0f);
    engine::core::ecs::Entity results[8];
    const int count = pw.overlapSphere({0, 0, 0}, 2.0f, {}, results, 8);
    EXPECT_GT(count, 0);
}

TEST(PhysicsWorld, OverlapSphereFindsStaticBox) {
    PhysicsWorld pw;
    addStaticBox(pw, {0, 0, 0}, {1, 1, 1});
    engine::core::ecs::Entity results[8];
    const int count = pw.overlapSphere({0, 0, 0}, 1.5f, {}, results, 8);
    EXPECT_GT(count, 0);
}

TEST(PhysicsWorld, OverlapSphereRespectsMaxResults) {
    PhysicsWorld pw;
    for (int i = 0; i < 5; ++i)
        addStaticSphere(pw, {static_cast<float>(i), 0, 0}, 0.4f);
    engine::core::ecs::Entity results[2];
    const int count = pw.overlapSphere({2, 0, 0}, 10.0f, {}, results, 2);
    EXPECT_LE(count, 2);
}

TEST(PhysicsWorld, OverlapSphereEmpty) {
    PhysicsWorld pw;
    addStaticSphere(pw, {100, 0, 0}, 1.0f);
    engine::core::ecs::Entity results[8];
    const int count = pw.overlapSphere({0, 0, 0}, 1.0f, {}, results, 8);
    EXPECT_EQ(count, 0);
}

// ── Character controller ──────────────────────────────────────────────────────

TEST(PhysicsWorld, CharacterControllerAddedSuccessfully) {
    PhysicsWorld pw;
    CharacterController cc;
    const BodyId id = pw.addCharacterController(kInvalidEntity, makeTransform({0,0,0}), cc);
    EXPECT_NE(id, kInvalidBodyId);
}

TEST(PhysicsWorld, CharacterControllerMovesWithDesiredVelocity) {
    PhysicsWorld pw;
    pw.setGravity({0, 0, 0});
    CharacterController cc;
    const BodyId id = pw.addCharacterController(kInvalidEntity, makeTransform({0, 0, 0}), cc);
    pw.setDesiredVelocity(id, {1, 0, 0});
    pw.step(1.0f);
    EXPECT_GT(pw.getTransform(id).position.x, 0.5f);
}

TEST(PhysicsWorld, CharacterControllerGroundedOnFloor) {
    PhysicsWorld pw;
    // Floor just below character
    addStaticBox(pw, {0, -2, 0}, {10, 1, 10});
    CharacterController cc;
    // Place character just above the floor top (y=-1), with total height 1.8, centre at y=-(1 - 0.9)=y=-0.1
    const BodyId id = pw.addCharacterController(kInvalidEntity, makeTransform({0, -0.1f, 0}), cc);
    pw.step(1.0f / 64.0f);
    EXPECT_TRUE(pw.isGrounded(id));
}

// ── QueryFilter ───────────────────────────────────────────────────────────────

TEST(QueryFilter, DefaultAllCollide) {
    QueryFilter f;
    EXPECT_TRUE(f.collides(0, 0));
    EXPECT_TRUE(f.collides(0, 15));
    EXPECT_TRUE(f.collides(7, 3));
}

TEST(QueryFilter, SetCollidesDisable) {
    QueryFilter f;
    f.setCollides(0, 1, false);
    EXPECT_FALSE(f.collides(0, 1));
    EXPECT_FALSE(f.collides(1, 0)); // symmetric
    EXPECT_TRUE(f.collides(0, 2));
}

TEST(QueryFilter, SetCollidesReenable) {
    QueryFilter f;
    f.setCollides(0, 1, false);
    f.setCollides(0, 1, true);
    EXPECT_TRUE(f.collides(0, 1));
}

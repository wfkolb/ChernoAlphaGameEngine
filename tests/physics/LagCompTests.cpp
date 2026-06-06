#include <gtest/gtest.h>
#include <physics/LagCompensator.h>
#include <physics/PhysicsWorld.h>
#include <physics/RigidBody.h>
#include <physics/ColliderShape.h>
#include <physics/QueryFilter.h>
#include <core/components/Transform.h>
#include <core/math/Vec.h>
#include <core/math/Quat.h>

using namespace engine::physics;
using namespace engine::core::math;
using engine::core::ecs::Entity;
using engine::core::ecs::kInvalidEntity;
using engine::core::Transform;

// ── Helpers ───────────────────────────────────────────────────────────────────

static Transform makeTransform(Vec3 pos) {
    Transform t;
    t.position = pos;
    t.rotation = Quat::identity();
    return t;
}

// Create a distinct entity handle from an integer index.
// generation is kept at zero so that entities are distinguishable.
static Entity makeEntity(uint32_t idx) {
    Entity e;
    e.index      = idx;
    e.generation = 0u;
    return e;
}

static BodyId addStaticSphere(PhysicsWorld& pw, Entity e, Vec3 pos, float r) {
    RigidBody rb;
    rb.type = RigidBodyType::Static;
    Collider c;
    c.shape = SphereShape{r};
    return pw.addRigidBody(e, makeTransform(pos), rb, c);
}

// ── Rewind to exact tick returns correct position ─────────────────────────────

// Entity lives at (100,0,0) now (off to the side of a +Z ray), but the
// historic snapshot says it was at (0,0,5) — directly in path.
// The rewound raycast should hit it.
TEST(LagCompensator, RewindToExactTickHitsEntity) {
    PhysicsWorld pw;
    const Entity e1 = makeEntity(1u);
    addStaticSphere(pw, e1, {100.0f, 0.0f, 0.0f}, 0.5f);

    LagCompensator lc(pw);

    EntityTransformSnapshot snap;
    snap.entity    = e1;
    snap.transform = makeTransform({0.0f, 0.0f, 5.0f});

    const RaycastHit hit = lc.rewindAndRaycast(
        std::span<const EntityTransformSnapshot>(&snap, 1),
        /*origin*/ Vec3{0.0f, 0.0f, 0.0f},
        /*dir*/    Vec3{0.0f, 0.0f, 1.0f},
        /*maxDist*/100.0f);

    EXPECT_TRUE(hit.hasHit);
    EXPECT_EQ(hit.entity, e1);
    // Sphere centre at z=5, radius 0.5 → surface hit at z=4.5
    EXPECT_NEAR(hit.distance, 4.5f, 0.05f);
}

// ── After rewind, live positions are restored ─────────────────────────────────

// Verify that after rewindAndRaycast the entity is back at its live position.
TEST(LagCompensator, LivePositionRestoredAfterRewind) {
    PhysicsWorld pw;
    const Entity e1 = makeEntity(2u);
    // Live: directly in the +Z ray path.
    addStaticSphere(pw, e1, {0.0f, 0.0f, 5.0f}, 0.5f);

    LagCompensator lc(pw);

    // Historic snapshot: entity was far off to the side — rewind ray should miss.
    EntityTransformSnapshot snap;
    snap.entity    = e1;
    snap.transform = makeTransform({100.0f, 0.0f, 0.0f});

    const RaycastHit rewoundHit = lc.rewindAndRaycast(
        std::span<const EntityTransformSnapshot>(&snap, 1),
        Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 0.0f, 1.0f}, 100.0f);
    EXPECT_FALSE(rewoundHit.hasHit);

    // Post-rewind live raycast: entity is back at (0,0,5) → should hit.
    const RaycastHit liveHit = pw.raycast(
        Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 0.0f, 1.0f}, 100.0f);
    EXPECT_TRUE(liveHit.hasHit);
    EXPECT_EQ(liveHit.entity, e1);
}

// ── Empty history degrades to live-position raycast ───────────────────────────

TEST(LagCompensator, EmptyHistoryUsesLivePositions) {
    PhysicsWorld pw;
    const Entity e1 = makeEntity(3u);
    addStaticSphere(pw, e1, {0.0f, 0.0f, 5.0f}, 0.5f);

    LagCompensator lc(pw);

    // No history entries: bodies untouched — live sphere should still be hit.
    const RaycastHit hit = lc.rewindAndRaycast(
        std::span<const EntityTransformSnapshot>{},
        Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 0.0f, 1.0f}, 100.0f);

    EXPECT_TRUE(hit.hasHit);
    EXPECT_EQ(hit.entity, e1);
}

// ── Raycast misses entity that moved out of the ray path in history ───────────

// Entity lives at (0,0,5) now (in path) but history says (100,0,0) (off path).
TEST(LagCompensator, RewindMissesEntityMovedOutOfPath) {
    PhysicsWorld pw;
    const Entity e1 = makeEntity(4u);
    addStaticSphere(pw, e1, {0.0f, 0.0f, 5.0f}, 0.5f);

    LagCompensator lc(pw);

    EntityTransformSnapshot snap;
    snap.entity    = e1;
    snap.transform = makeTransform({100.0f, 0.0f, 0.0f});

    const RaycastHit hit = lc.rewindAndRaycast(
        std::span<const EntityTransformSnapshot>(&snap, 1),
        Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 0.0f, 1.0f}, 100.0f);

    EXPECT_FALSE(hit.hasHit);
}

// ── Two entities: one rewound into path, one rewound out ──────────────────────

TEST(LagCompensator, RewindMultipleEntitiesSelectsCorrectOne) {
    PhysicsWorld pw;
    const Entity eIn  = makeEntity(5u);
    const Entity eOut = makeEntity(6u);

    // Live: both off to the side so a live +Z ray misses both.
    addStaticSphere(pw, eIn,  {50.0f, 0.0f, 0.0f}, 0.5f);
    addStaticSphere(pw, eOut, {60.0f, 0.0f, 0.0f}, 0.5f);

    LagCompensator lc(pw);

    EntityTransformSnapshot snaps[2];
    // eIn moved into the +Z ray path.
    snaps[0].entity    = eIn;
    snaps[0].transform = makeTransform({0.0f, 0.0f, 5.0f});
    // eOut stays off to the side.
    snaps[1].entity    = eOut;
    snaps[1].transform = makeTransform({60.0f, 0.0f, 0.0f});

    const RaycastHit hit = lc.rewindAndRaycast(
        std::span<const EntityTransformSnapshot>(snaps, 2),
        Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 0.0f, 1.0f}, 100.0f);

    EXPECT_TRUE(hit.hasHit);
    EXPECT_EQ(hit.entity, eIn);
}

// ── getTransformByEntity returns default for unknown entity ───────────────────

TEST(PhysicsWorldLagComp, GetTransformByEntityUnknown) {
    PhysicsWorld pw;
    const Transform t = pw.getTransformByEntity(makeEntity(99u));
    EXPECT_NEAR(t.position.x, 0.0f, 1e-5f);
    EXPECT_NEAR(t.position.y, 0.0f, 1e-5f);
    EXPECT_NEAR(t.position.z, 0.0f, 1e-5f);
}

// ── getTransformByEntity returns the live transform ───────────────────────────

TEST(PhysicsWorldLagComp, GetTransformByEntityKnown) {
    PhysicsWorld pw;
    const Entity e = makeEntity(10u);
    addStaticSphere(pw, e, {3.0f, 4.0f, 5.0f}, 0.5f);

    const Transform t = pw.getTransformByEntity(e);
    EXPECT_NEAR(t.position.x, 3.0f, 1e-5f);
    EXPECT_NEAR(t.position.y, 4.0f, 1e-5f);
    EXPECT_NEAR(t.position.z, 5.0f, 1e-5f);
}

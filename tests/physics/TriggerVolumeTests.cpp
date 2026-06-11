// Tests for Task #72 — Trigger Volume System (physics side)
#include <gtest/gtest.h>
#include <physics/PhysicsWorld.h>
#include <physics/TriggerEvents.h>
#include <physics/RigidBody.h>
#include <physics/ColliderShape.h>
#include <core/EventBus.h>
#include <core/components/Transform.h>
#include <core/math/Vec.h>

using engine::core::ecs::Entity;
using engine::core::math::Vec3;
using engine::physics::PhysicsWorld;
using engine::physics::RigidBody;
using engine::physics::RigidBodyType;
using engine::physics::Collider;
using engine::physics::SphereShape;
using engine::physics::BoxShape;
using engine::physics::TriggerEnterEvent;
using engine::physics::TriggerExitEvent;
using engine::core::EventBus;

static Entity makeEntity(uint32_t idx) { return Entity{ idx, 1 }; }

static engine::core::Transform makeTransform(Vec3 pos) {
    engine::core::Transform t;
    t.position = pos;
    return t;
}

static engine::physics::BodyId addDynamicSphere(PhysicsWorld& pw, Entity entity,
                                                  Vec3 pos, float radius = 0.3f) {
    RigidBody rb;
    rb.type = RigidBodyType::Dynamic;
    rb.mass = 1.0f;
    Collider col;
    col.shape = SphereShape{ radius };
    return pw.addRigidBody(entity, makeTransform(pos), rb, col);
}

// ── Basic add / remove ────────────────────────────────────────────────────────

TEST(TriggerVolume, AddAndRemoveTriggerNoCrash) {
    PhysicsWorld pw;
    PhysicsWorld::TriggerDesc desc;
    desc.entity = makeEntity(1);
    desc.center = {0.0f, 0.0f, 0.0f};
    desc.halfX = 1.0f; desc.halfY = 1.0f; desc.halfZ = 1.0f;
    pw.addTrigger(desc);
    pw.removeTrigger(makeEntity(1));
    // No crash = pass.
}

TEST(TriggerVolume, AddTriggerIsIdempotent) {
    PhysicsWorld pw;
    PhysicsWorld::TriggerDesc desc;
    desc.entity = makeEntity(5);
    desc.center = {0.0f, 0.0f, 0.0f};
    desc.halfX = desc.halfY = desc.halfZ = 1.0f;
    // Adding twice should not crash or create duplicate state.
    pw.addTrigger(desc);
    pw.addTrigger(desc);
    pw.removeTrigger(makeEntity(5));
}

TEST(TriggerVolume, UpdateTriggerCenter) {
    PhysicsWorld pw;
    PhysicsWorld::TriggerDesc desc;
    desc.entity = makeEntity(3);
    desc.center = {0.0f, 0.0f, 0.0f};
    desc.halfX = desc.halfY = desc.halfZ = 1.0f;
    pw.addTrigger(desc);
    pw.updateTrigger(makeEntity(3), {5.0f, 5.0f, 5.0f});
    // No crash = pass. Behavioral coverage is in overlap tests.
}

// ── Enter event ───────────────────────────────────────────────────────────────

TEST(TriggerVolume, EnterEventFiredWhenBodyOverlaps) {
    PhysicsWorld pw;
    EventBus bus;
    pw.setEventBus(&bus);

    const Entity trigEnt  = makeEntity(10);
    const Entity bodyEnt  = makeEntity(20);

    PhysicsWorld::TriggerDesc desc;
    desc.entity   = trigEnt;
    desc.center   = {0.0f, 0.0f, 0.0f};
    desc.halfX    = 2.0f; desc.halfY = 2.0f; desc.halfZ = 2.0f;
    desc.eventTag = 42u;
    pw.addTrigger(desc);

    // Dynamic body placed inside the trigger zone.
    addDynamicSphere(pw, bodyEnt, {0.0f, 0.0f, 0.0f});

    bool entered = false;
    bus.subscribe<TriggerEnterEvent>([&](const TriggerEnterEvent& e) {
        if (e.triggerEntity == trigEnt && e.enteringEntity == bodyEnt)
            entered = true;
    });

    pw.step(1.0f / 64.0f);
    EXPECT_TRUE(entered);
}

TEST(TriggerVolume, EnterEventCarriesEventTag) {
    PhysicsWorld pw;
    EventBus bus;
    pw.setEventBus(&bus);

    const Entity trigEnt = makeEntity(10);
    const Entity bodyEnt = makeEntity(20);

    PhysicsWorld::TriggerDesc desc;
    desc.entity   = trigEnt;
    desc.center   = {0.0f, 0.0f, 0.0f};
    desc.halfX    = 2.0f; desc.halfY = 2.0f; desc.halfZ = 2.0f;
    desc.eventTag = 99u;
    pw.addTrigger(desc);
    addDynamicSphere(pw, bodyEnt, {0.0f, 0.0f, 0.0f});

    uint32_t receivedTag = 0;
    bus.subscribe<TriggerEnterEvent>([&](const TriggerEnterEvent& e) {
        receivedTag = e.eventTag;
    });

    pw.step(1.0f / 64.0f);
    EXPECT_EQ(receivedTag, 99u);
}

TEST(TriggerVolume, NoEnterEventWhenBodyOutside) {
    PhysicsWorld pw;
    EventBus bus;
    pw.setEventBus(&bus);

    const Entity trigEnt = makeEntity(10);
    const Entity bodyEnt = makeEntity(20);

    PhysicsWorld::TriggerDesc desc;
    desc.entity = trigEnt;
    desc.center = {0.0f, 0.0f, 0.0f};
    desc.halfX  = 0.5f; desc.halfY = 0.5f; desc.halfZ = 0.5f;
    pw.addTrigger(desc);

    // Body placed far away from the trigger.
    addDynamicSphere(pw, bodyEnt, {100.0f, 100.0f, 100.0f});

    bool entered = false;
    bus.subscribe<TriggerEnterEvent>([&](const TriggerEnterEvent&) {
        entered = true;
    });

    pw.step(1.0f / 64.0f);
    EXPECT_FALSE(entered);
}

// ── Enter fires only once per overlap ────────────────────────────────────────

TEST(TriggerVolume, EnterEventFiredOnlyOnce) {
    PhysicsWorld pw;
    EventBus bus;
    pw.setEventBus(&bus);

    const Entity trigEnt = makeEntity(10);
    const Entity bodyEnt = makeEntity(20);

    PhysicsWorld::TriggerDesc desc;
    desc.entity = trigEnt;
    desc.center = {0.0f, 0.0f, 0.0f};
    desc.halfX  = 5.0f; desc.halfY = 5.0f; desc.halfZ = 5.0f;
    pw.addTrigger(desc);
    addDynamicSphere(pw, bodyEnt, {0.0f, 0.0f, 0.0f});

    int enterCount = 0;
    bus.subscribe<TriggerEnterEvent>([&](const TriggerEnterEvent& e) {
        if (e.triggerEntity == trigEnt && e.enteringEntity == bodyEnt) ++enterCount;
    });

    pw.step(1.0f / 64.0f);
    pw.step(1.0f / 64.0f);
    pw.step(1.0f / 64.0f);
    // Body stays inside; enter event should only fire on first step.
    EXPECT_EQ(enterCount, 1);
}

// ── Exit event ────────────────────────────────────────────────────────────────

TEST(TriggerVolume, ExitEventFiredWhenBodyLeaves) {
    PhysicsWorld pw;
    EventBus bus;
    pw.setEventBus(&bus);

    const Entity trigEnt = makeEntity(10);
    const Entity bodyEnt = makeEntity(20);

    PhysicsWorld::TriggerDesc desc;
    desc.entity = trigEnt;
    desc.center = {0.0f, 0.0f, 0.0f};
    desc.halfX  = 1.5f; desc.halfY = 1.5f; desc.halfZ = 1.5f;
    pw.addTrigger(desc);

    // Disable gravity so the body stays put between moves.
    pw.setGravity({0.0f, 0.0f, 0.0f});

    const engine::physics::BodyId bid =
        addDynamicSphere(pw, bodyEnt, {0.0f, 0.0f, 0.0f});

    // First step: body overlaps — enter event fires.
    pw.step(1.0f / 64.0f);

    // Teleport the body far outside the trigger.
    pw.setTransform(bid, makeTransform({100.0f, 100.0f, 100.0f}));

    bool exited = false;
    bus.subscribe<TriggerExitEvent>([&](const TriggerExitEvent& e) {
        if (e.triggerEntity == trigEnt && e.leavingEntity == bodyEnt)
            exited = true;
    });

    pw.step(1.0f / 64.0f);
    EXPECT_TRUE(exited);
}

// ── No crash without EventBus ─────────────────────────────────────────────────

TEST(TriggerVolume, NoEventIfNoEventBusSet) {
    PhysicsWorld pw;
    // Deliberately do NOT call setEventBus.

    PhysicsWorld::TriggerDesc desc;
    desc.entity = makeEntity(1);
    desc.center = {0.0f, 0.0f, 0.0f};
    desc.halfX  = 2.0f; desc.halfY = 2.0f; desc.halfZ = 2.0f;
    pw.addTrigger(desc);

    addDynamicSphere(pw, makeEntity(2), {0.0f, 0.0f, 0.0f});

    EXPECT_NO_THROW(pw.step(1.0f / 64.0f));
}

// ── Static bodies do not trigger ─────────────────────────────────────────────

TEST(TriggerVolume, StaticBodyDoesNotFireEnterEvent) {
    PhysicsWorld pw;
    EventBus bus;
    pw.setEventBus(&bus);

    const Entity trigEnt  = makeEntity(10);
    const Entity staticEnt = makeEntity(20);

    PhysicsWorld::TriggerDesc desc;
    desc.entity = trigEnt;
    desc.center = {0.0f, 0.0f, 0.0f};
    desc.halfX  = 5.0f; desc.halfY = 5.0f; desc.halfZ = 5.0f;
    pw.addTrigger(desc);

    // Add a static body inside the trigger zone.
    RigidBody rb;
    rb.type = RigidBodyType::Static;
    Collider col;
    col.shape = SphereShape{ 0.3f };
    pw.addRigidBody(staticEnt, makeTransform({0.0f, 0.0f, 0.0f}), rb, col);

    bool entered = false;
    bus.subscribe<TriggerEnterEvent>([&](const TriggerEnterEvent&) {
        entered = true;
    });

    pw.step(1.0f / 64.0f);
    EXPECT_FALSE(entered);
}

// ── Sphere trigger ────────────────────────────────────────────────────────────

TEST(TriggerVolume, SphereTriggerEnterEvent) {
    PhysicsWorld pw;
    EventBus bus;
    pw.setEventBus(&bus);

    const Entity trigEnt = makeEntity(10);
    const Entity bodyEnt = makeEntity(20);

    PhysicsWorld::TriggerDesc desc;
    desc.entity   = trigEnt;
    desc.center   = {0.0f, 0.0f, 0.0f};
    desc.halfX    = 3.0f; // radius for sphere trigger
    desc.halfY    = 3.0f;
    desc.halfZ    = 3.0f;
    desc.isSphere = true;
    pw.addTrigger(desc);

    // Body placed at origin, well within sphere radius.
    addDynamicSphere(pw, bodyEnt, {0.0f, 0.0f, 0.0f});

    bool entered = false;
    bus.subscribe<TriggerEnterEvent>([&](const TriggerEnterEvent& e) {
        if (e.triggerEntity == trigEnt && e.enteringEntity == bodyEnt)
            entered = true;
    });

    pw.step(1.0f / 64.0f);
    EXPECT_TRUE(entered);
}

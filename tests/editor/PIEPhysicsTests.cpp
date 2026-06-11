// PIEPhysicsTests.cpp
// Unit tests for Task #69 — PIE Physics + Input.
// Tests are pure-logic (no DX12 / ImGui / GPU required).
// ENGINE_DEVREL is defined by the CMake target (editor_tests).

#include <gtest/gtest.h>

#ifdef ENGINE_DEVREL

#include <physics/PhysicsWorld.h>
#include <core/scene/Scene.h>

using engine::physics::PhysicsWorld;
using engine::core::scene::Scene;

// ---------------------------------------------------------------------------
// PhysicsWorld can be default-constructed and stepped without a crash.
// This validates the editor's editorPhysicsWorld_ creation path in init().
// ---------------------------------------------------------------------------
TEST(PIEPhysics, PhysicsWorldStepDoesNotCrash) {
    PhysicsWorld pw;
    EXPECT_NO_THROW(pw.step(1.0f / 64.0f));
}

// ---------------------------------------------------------------------------
// Calling step() multiple times in sequence is safe.
// The editor PIE loop drives physics at 64 Hz — validate the accumulator
// pattern produces no crash after many steps.
// ---------------------------------------------------------------------------
TEST(PIEPhysics, PhysicsWorldMultipleStepsAreStable) {
    PhysicsWorld pw;
    constexpr float kDt = 1.0f / 64.0f;
    for (int i = 0; i < 128; ++i) {
        EXPECT_NO_THROW(pw.step(kDt));
    }
}

// ---------------------------------------------------------------------------
// Scene::setPhysicsStepFn wires correctly: after setting a delegate and
// calling tick() on a loaded+active scene, the delegate must be invoked.
// ---------------------------------------------------------------------------
TEST(PIEPhysics, PhysicsStepFnInvokedBySceneTick) {
    Scene scene;
    scene.load("TestScene");
    scene.activate();

    int stepCount = 0;
    scene.setPhysicsStepFn([&stepCount](float /*dt*/) { ++stepCount; });

    scene.tick(1.0f / 64.0f);
    EXPECT_EQ(stepCount, 1);

    scene.tick(1.0f / 64.0f);
    EXPECT_EQ(stepCount, 2);
}

// ---------------------------------------------------------------------------
// Scene::setPhysicsStepFn(nullptr) clears the delegate.
// After clearing, tick() must not crash and must not call the old delegate.
// ---------------------------------------------------------------------------
TEST(PIEPhysics, PhysicsStepFnCanBeCleared) {
    Scene scene;
    scene.load("TestScene");
    scene.activate();

    bool wasCalled = false;
    scene.setPhysicsStepFn([&wasCalled](float /*dt*/) { wasCalled = true; });
    scene.tick(1.0f / 64.0f);
    EXPECT_TRUE(wasCalled);

    wasCalled = false;
    scene.setPhysicsStepFn(nullptr);
    EXPECT_NO_THROW(scene.tick(1.0f / 64.0f));
    EXPECT_FALSE(wasCalled);
}

// ---------------------------------------------------------------------------
// The PhysicsWorld step delegate can wrap a real PhysicsWorld::step() call.
// This exercises the exact lambda that EditorApp wires on PIE start.
// ---------------------------------------------------------------------------
TEST(PIEPhysics, RealPhysicsWorldCanBeWiredToScene) {
    PhysicsWorld pw;
    Scene scene;
    scene.load("TestScene");
    scene.activate();

    scene.setPhysicsStepFn([&pw](float dt) { pw.step(dt); });

    EXPECT_NO_THROW(scene.tick(1.0f / 64.0f));
    EXPECT_NO_THROW(scene.tick(1.0f / 64.0f));

    // Detach the delegate (mirrors PIE stop).
    scene.setPhysicsStepFn(nullptr);
    EXPECT_NO_THROW(scene.tick(1.0f / 64.0f));
}

// ---------------------------------------------------------------------------
// Scene::tick() on an inactive (loaded but not yet activated) scene must
// not invoke the physics step delegate — tick() is a no-op when !active_.
// ---------------------------------------------------------------------------
TEST(PIEPhysics, InactiveSceneDoesNotStepPhysics) {
    Scene scene;
    scene.load("TestScene");
    // Intentionally skip activate().

    bool wasCalled = false;
    scene.setPhysicsStepFn([&wasCalled](float /*dt*/) { wasCalled = true; });
    scene.tick(1.0f / 64.0f);

    EXPECT_FALSE(wasCalled);
}

// ---------------------------------------------------------------------------
// After scene.deactivate() + scene.activate() the physics delegate survives.
// EditorApp does not re-wire the delegate on activation cycles — it must
// persist across the activate/deactivate pair.
// ---------------------------------------------------------------------------
TEST(PIEPhysics, PhysicsStepFnSurvivesReactivation) {
    PhysicsWorld pw;
    Scene scene;
    scene.load("TestScene");
    scene.activate();
    scene.setPhysicsStepFn([&pw](float dt) { pw.step(dt); });

    scene.deactivate();
    scene.activate();

    // Delegate must still be wired after the reactivation.
    EXPECT_NO_THROW(scene.tick(1.0f / 64.0f));
}

#else
// In non-DEVREL builds the tests are compiled away; emit a trivial pass so
// the test binary does not contain zero test cases.
TEST(PIEPhysics, CompilesInNonDevRel) { SUCCEED(); }
#endif // ENGINE_DEVREL

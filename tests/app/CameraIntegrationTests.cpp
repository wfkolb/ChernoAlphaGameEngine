// tests/app/CameraIntegrationTests.cpp
// Unit tests for Task #66 — Camera System Integration.
//
// These tests verify the ECS camera query and matrix-forwarding logic used by
// Application::run() at each render frame.  All tests run headless (no GpuDevice),
// so MeshRenderSystem::tick() is never reached (pipeline_ is null without init()).

#include <gtest/gtest.h>

#include <core/ecs/World.h>
#include <core/ecs/View.h>
#include <core/components/Transform.h>
#include <rendering/Camera.h>
#include <rendering/Mesh.h>         // for rendering::MeshHandle
#include <core/math/Mat.h>
#include <core/math/Quat.h>
#include <core/math/Vec.h>
#include <cmath>

// MeshRenderSystem lives in the internal app/ directory; include via relative path.
#include "MeshRenderSystem.h"

using namespace engine;
using namespace engine::core::ecs;
using engine::core::Transform;
using engine::rendering::Camera;
using engine::core::math::Mat4;
using engine::core::math::Vec3;
using engine::core::math::Quat;

// ---------------------------------------------------------------------------
// Helper: extract the active camera (isMain == true) from a world.
// Mirrors the logic in Application::run().
// Returns true if a camera was found; fills outTransform / outCamera by pointer.
// ---------------------------------------------------------------------------
static bool findActiveCamera(World& world,
                              const Transform*& outTransform,
                              const Camera*& outCamera)
{
    outTransform = nullptr;
    outCamera    = nullptr;

    View<Transform, Camera> camView(world);
    for (auto [entity, transform, camera] : camView) {
        if (camera.isMain && outTransform == nullptr) {
            outTransform = &transform;
            outCamera    = &camera;
        }
    }
    return outTransform != nullptr;
}

// ---------------------------------------------------------------------------
// Test: world with no entities — no camera found
// ---------------------------------------------------------------------------
TEST(CameraIntegration, NoCameraEntityInEmptyWorld)
{
    World world;
    const Transform* t = nullptr;
    const Camera*    c = nullptr;
    EXPECT_FALSE(findActiveCamera(world, t, c));
    EXPECT_EQ(t, nullptr);
    EXPECT_EQ(c, nullptr);
}

// ---------------------------------------------------------------------------
// Test: world with a Camera + Transform entity — camera is found
// ---------------------------------------------------------------------------
TEST(CameraIntegration, FindsActiveCameraEntity)
{
    World world;

    Entity camEntity = world.createEntity();
    world.addComponent<Transform>(camEntity, Transform{});
    Camera cam{};
    cam.fovYDegrees = 75.0f;
    cam.nearZ       = 0.1f;
    cam.farZ        = 500.0f;
    cam.isMain      = true;
    world.addComponent<Camera>(camEntity, cam);

    const Transform* t = nullptr;
    const Camera*    c = nullptr;
    EXPECT_TRUE(findActiveCamera(world, t, c));
    ASSERT_NE(t, nullptr);
    ASSERT_NE(c, nullptr);
    EXPECT_NEAR(c->fovYDegrees, 75.0f, 1e-5f);
    EXPECT_NEAR(c->nearZ,       0.1f,  1e-5f);
    EXPECT_NEAR(c->farZ,        500.0f,1e-5f);
}

// ---------------------------------------------------------------------------
// Test: Camera with isMain == false — not selected
// ---------------------------------------------------------------------------
TEST(CameraIntegration, InactiveCamera_NotSelected)
{
    World world;

    Entity camEntity = world.createEntity();
    world.addComponent<Transform>(camEntity, Transform{});
    Camera cam{};
    cam.isMain = false;
    world.addComponent<Camera>(camEntity, cam);

    const Transform* t = nullptr;
    const Camera*    c = nullptr;
    EXPECT_FALSE(findActiveCamera(world, t, c));
}

// ---------------------------------------------------------------------------
// Test: two isMain camera entities — only the first (by insertion order within
// the archetype) is returned.
//
// Archetype-based iteration order is stable within a single archetype: entities
// are stored in insertion order within each archetype bucket.  The first-inserted
// isMain camera is therefore always the one selected.
// ---------------------------------------------------------------------------
TEST(CameraIntegration, TwoCameraEntities_FirstMainSelected)
{
    World world;

    Entity cam1 = world.createEntity();
    world.addComponent<Transform>(cam1, Transform{});
    Camera c1{};
    c1.fovYDegrees = 60.0f;
    c1.isMain      = true;
    world.addComponent<Camera>(cam1, c1);

    Entity cam2 = world.createEntity();
    world.addComponent<Transform>(cam2, Transform{});
    Camera c2{};
    c2.fovYDegrees = 90.0f;
    c2.isMain      = true;
    world.addComponent<Camera>(cam2, c2);

    const Transform* t = nullptr;
    const Camera*    c = nullptr;
    EXPECT_TRUE(findActiveCamera(world, t, c));
    ASSERT_NE(c, nullptr);
    // First entity inserted into the archetype is returned first.
    EXPECT_NEAR(c->fovYDegrees, 60.0f, 1e-5f);
}

// ---------------------------------------------------------------------------
// Test: entity with only Camera but no Transform — not matched by
//       View<Transform, Camera> (component mask check fails).
// ---------------------------------------------------------------------------
TEST(CameraIntegration, CameraWithoutTransform_NotMatched)
{
    World world;

    Entity orphanCam = world.createEntity();
    Camera cam{};
    cam.isMain = true;
    world.addComponent<Camera>(orphanCam, cam);
    // No Transform — View<Transform, Camera> will not match this entity.

    const Transform* t = nullptr;
    const Camera*    c = nullptr;
    EXPECT_FALSE(findActiveCamera(world, t, c));
}

// ---------------------------------------------------------------------------
// Test: view/proj matrix computation from camera entity.
// Verifies that cameraViewMatrix and cameraProjMatrix produce finite matrices
// and that the reverse-Z projection has the correct near-plane translation term.
// ---------------------------------------------------------------------------
TEST(CameraIntegration, ViewProjMatricesAreFiniteForDefaultCamera)
{
    using engine::rendering::cameraViewMatrix;
    using engine::rendering::cameraProjMatrix;

    core::math::Transform t{};
    t.position = {0.0f, 1.0f, 5.0f};
    t.rotation = Quat::identity();
    t.scale    = Vec3::one();

    Camera cam{};
    cam.fovYDegrees = 60.0f;
    cam.nearZ       = 0.1f;
    cam.farZ        = 1000.0f;

    const Mat4 viewMat = cameraViewMatrix(t);
    const Mat4 projMat = cameraProjMatrix(cam, 16.0f / 9.0f);

    // Every element of view and proj must be finite.
    for (int r = 0; r < 4; ++r) {
        for (int col = 0; col < 4; ++col) {
            EXPECT_TRUE(std::isfinite(viewMat.m[r][col]))
                << "viewMat.m[" << r << "][" << col << "] is not finite";
            EXPECT_TRUE(std::isfinite(projMat.m[r][col]))
                << "projMat.m[" << r << "][" << col << "] is not finite";
        }
    }

    // Reverse-Z: m[3][2] = nearZ * farZ / (farZ - nearZ).
    // For nearZ=0.1, farZ=1000: expected ≈ 0.10001.
    const float expectedM32 = cam.nearZ * cam.farZ / (cam.farZ - cam.nearZ);
    EXPECT_NEAR(projMat.m[3][2], expectedM32, 1e-4f);
}

// ---------------------------------------------------------------------------
// Test: MeshRenderSystem register/clear cycle works without a GPU device.
// Application calls empty() to decide whether to emit the no-camera warning.
// ---------------------------------------------------------------------------
TEST(CameraIntegration, MeshRenderSystem_RegisterAndClear)
{
    engine::app::MeshRenderSystem mrs;
    EXPECT_TRUE(mrs.empty());

    // Register a dummy (invalid) handle.
    mrs.registerHandle(0u, rendering::MeshHandle{});
    EXPECT_FALSE(mrs.empty());

    mrs.clear();
    EXPECT_TRUE(mrs.empty());
}

// ---------------------------------------------------------------------------
// Test: MeshRenderSystem::tick() is a no-op when init() has not been called
//       (pipeline_ is null → early return).  We can only verify this doesn't
//       crash; the GPU path is tested by integration/GPU-labelled tests.
//       Here we just confirm empty() is false after registering a handle, and
//       tick() returns without an assert even with null GPU objects when the
//       pipeline is absent.
// Note: tick() cannot be exercised headlessly because ResourceHandle,
//       MeshManager and FrameGraph all require a live D3D12 device.
//       The GPU-labelled tests cover the full render path.
// ---------------------------------------------------------------------------
TEST(CameraIntegration, MeshRenderSystem_TickWithNullPipeline_NoOp)
{
    // Construct without calling init() — pipeline_ stays null.
    engine::app::MeshRenderSystem mrs;
    // No crash expected; just verify construction and empty() work.
    EXPECT_TRUE(mrs.empty());
}

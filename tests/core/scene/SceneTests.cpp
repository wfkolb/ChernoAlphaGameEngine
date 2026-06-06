#include <gtest/gtest.h>
#include <core/scene/Scene.h>
#include <core/scene/SceneManager.h>
#include <core/scene/BVH.h>
#include <core/scene/SceneGlobals.h>
#include <core/math/AABB.h>
#include <core/math/Mat.h>
#include <core/math/Frustum.h>
#include <core/components/Lifetime.h>
#include <core/components/Transform.h>
#include <core/ecs/HierarchyComponent.h>

using namespace engine::core::scene;
using namespace engine::core::math;
using engine::core::ecs::Entity;
using engine::core::ecs::kInvalidEntity;

// ── Scene lifecycle ───────────────────────────────────────────────────────────

TEST(Scene, InitialStateNotLoaded) {
    Scene s;
    EXPECT_FALSE(s.isLoaded());
    EXPECT_FALSE(s.isActive());
}

TEST(Scene, LoadSetsName) {
    Scene s;
    s.load("test_map");
    EXPECT_EQ(s.name(), "test_map");
    EXPECT_TRUE(s.isLoaded());
    EXPECT_FALSE(s.isActive());
}

TEST(Scene, LoadCreatesWorld) {
    Scene s;
    s.load("test_map");
    // World is accessible and functional — create an entity without crashing
    Entity e = s.world().createEntity();
    EXPECT_TRUE(s.world().isAlive(e));
}

TEST(Scene, ActivateRequiresLoad) {
    Scene s;
    s.activate(); // no-op without load
    EXPECT_FALSE(s.isActive());
}

TEST(Scene, ActivateSetsActive) {
    Scene s;
    s.load("test_map");
    s.activate();
    EXPECT_TRUE(s.isActive());
}

TEST(Scene, DeactivateSetsInactive) {
    Scene s;
    s.load("test_map");
    s.activate();
    s.deactivate();
    EXPECT_FALSE(s.isActive());
    EXPECT_TRUE(s.isLoaded()); // still loaded
}

TEST(Scene, UnloadClearsState) {
    Scene s;
    s.load("test_map");
    s.activate();
    s.unload();
    EXPECT_FALSE(s.isLoaded());
    EXPECT_FALSE(s.isActive());
}

TEST(Scene, UnloadResetsName) {
    Scene s;
    s.load("test_map");
    s.unload();
    EXPECT_TRUE(s.name().empty());
}

TEST(Scene, LoadIdIsSet) {
    Scene s;
    s.load("arena", 42u);
    EXPECT_EQ(s.id(), 42u);
    EXPECT_EQ(s.globals().sceneId, 42u);
}

// ── Globals ───────────────────────────────────────────────────────────────────

TEST(Scene, DefaultGravity) {
    Scene s;
    s.load("test");
    EXPECT_NEAR(s.globals().gravity.y, -9.81f, 1e-4f);
}

TEST(Scene, GlobalsCanBeModified) {
    Scene s;
    s.load("test");
    s.globals().matchTimeLimit = 120.0f;
    s.globals().maxPlayers     = 8;
    EXPECT_NEAR(s.globals().matchTimeLimit, 120.0f, 1e-5f);
    EXPECT_EQ(s.globals().maxPlayers, 8);
}

TEST(Scene, GlobalsSpawnPoints) {
    Scene s;
    s.load("test");
    s.globals().spawnPoints.push_back({1.0f, 0.0f, 2.0f});
    EXPECT_EQ(s.globals().spawnPoints.size(), 1u);
}

// ── Physics pointer ───────────────────────────────────────────────────────────

TEST(Scene, PhysicsWorldDefaultNull) {
    Scene s;
    s.load("test");
    EXPECT_EQ(s.physicsWorld(), nullptr);
}

TEST(Scene, PhysicsWorldSetAndGet) {
    Scene s;
    s.load("test");
    // Use a non-null sentinel (actual PhysicsWorld not constructed here).
    engine::physics::PhysicsWorld* sentinel = reinterpret_cast<engine::physics::PhysicsWorld*>(0x1);
    s.setPhysicsWorld(sentinel);
    EXPECT_EQ(s.physicsWorld(), sentinel);
}

TEST(Scene, PhysicsWorldClearedOnUnload) {
    Scene s;
    s.load("test");
    engine::physics::PhysicsWorld* sentinel = reinterpret_cast<engine::physics::PhysicsWorld*>(0x1);
    s.setPhysicsWorld(sentinel);
    s.unload();
    EXPECT_EQ(s.physicsWorld(), nullptr);
}

// ── Static BVH entries ────────────────────────────────────────────────────────

TEST(Scene, EmptyBVHAfterActivate) {
    Scene s;
    s.load("test");
    s.activate();
    EXPECT_TRUE(s.staticBVH().empty());
}

TEST(Scene, BVHBuiltFromStaticEntries) {
    Scene s;
    s.load("test");
    Entity e = s.world().createEntity();
    AABB aabb = AABB::fromCenterExtents({0,0,0}, {1,1,1});
    s.addStaticEntry(e, aabb);
    s.activate();
    EXPECT_FALSE(s.staticBVH().empty());
    EXPECT_EQ(s.staticBVH().entryCount(), 1);
}

// ── Dynamic spatial index ─────────────────────────────────────────────────────

TEST(Scene, DynamicQueryFindsEntry) {
    Scene s;
    s.load("test");
    s.activate();
    Entity e = s.world().createEntity();
    s.registerDynamic(e, AABB::fromCenterExtents({0,0,0}, {1,1,1}));
    std::vector<Entity> found;
    s.queryDynamic(AABB::fromCenterExtents({0,0,0}, {2,2,2}), found);
    ASSERT_EQ(found.size(), 1u);
    EXPECT_EQ(found[0], e);
}

TEST(Scene, DynamicGridClearedOnTick) {
    Scene s;
    s.load("test");
    s.activate();
    Entity e = s.world().createEntity();
    s.registerDynamic(e, AABB::fromCenterExtents({0,0,0}, {1,1,1}));
    s.tick(0.016f); // one tick clears old dynamic entries
    std::vector<Entity> found;
    s.queryDynamic(AABB::fromCenterExtents({0,0,0}, {2,2,2}), found);
    EXPECT_TRUE(found.empty()); // cleared by tick
}

// ── Lifetime tick ─────────────────────────────────────────────────────────────

TEST(Scene, TickDecrementsLifetime) {
    Scene s;
    s.load("test");
    s.activate();
    Entity e = s.world().createEntity();
    engine::core::Lifetime lt; lt.remaining = 1.0f;
    s.world().addComponent<engine::core::Lifetime>(e, lt);
    s.tick(0.5f);
    ASSERT_NE(s.world().tryGet<engine::core::Lifetime>(e), nullptr);
    EXPECT_NEAR(s.world().get<engine::core::Lifetime>(e).remaining, 0.5f, 1e-5f);
}

TEST(Scene, TickDestroysExpiredEntities) {
    Scene s;
    s.load("test");
    s.activate();
    Entity e = s.world().createEntity();
    engine::core::Lifetime lt; lt.remaining = 0.01f;
    s.world().addComponent<engine::core::Lifetime>(e, lt);
    s.tick(0.1f); // dt > remaining → entity destroyed
    EXPECT_FALSE(s.world().isAlive(e));
}

// ── SceneManager ─────────────────────────────────────────────────────────────

TEST(SceneManager, LoadCreatesScene) {
    SceneManager sm;
    Scene* s = sm.load("level_01");
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->name(), "level_01");
    EXPECT_TRUE(s->isLoaded());
}

TEST(SceneManager, LoadDuplicateReturnsNull) {
    SceneManager sm;
    sm.load("level_01");
    EXPECT_EQ(sm.load("level_01"), nullptr);
}

TEST(SceneManager, GetByName) {
    SceneManager sm;
    sm.load("level_01");
    EXPECT_NE(sm.get("level_01"), nullptr);
    EXPECT_EQ(sm.get("nonexistent"), nullptr);
}

TEST(SceneManager, GetByNameAlias) {
    SceneManager sm;
    Scene* s = sm.load("map_a");
    EXPECT_EQ(sm.getByName("map_a"), s);
}

TEST(SceneManager, ActivateScene) {
    SceneManager sm;
    sm.load("level_01");
    bool ok = sm.activate("level_01");
    EXPECT_TRUE(ok);
    EXPECT_TRUE(sm.get("level_01")->isActive());
}

TEST(SceneManager, ActivateNonexistentReturnsFalse) {
    SceneManager sm;
    EXPECT_FALSE(sm.activate("nonexistent"));
}

TEST(SceneManager, GetActiveReturnsActiveScene) {
    SceneManager sm;
    sm.load("level_01");
    sm.activate("level_01");
    EXPECT_EQ(sm.getActive(), sm.get("level_01"));
}

TEST(SceneManager, GetActiveNullWhenNoneActive) {
    SceneManager sm;
    sm.load("level_01"); // loaded but not activated
    EXPECT_EQ(sm.getActive(), nullptr);
}

TEST(SceneManager, DeactivateScene) {
    SceneManager sm;
    sm.load("level_01");
    sm.activate("level_01");
    sm.deactivate("level_01");
    EXPECT_FALSE(sm.get("level_01")->isActive());
    EXPECT_EQ(sm.getActive(), nullptr);
}

TEST(SceneManager, UnloadRemovesScene) {
    SceneManager sm;
    sm.load("level_01");
    sm.unload("level_01");
    EXPECT_EQ(sm.get("level_01"), nullptr);
    EXPECT_EQ(sm.sceneCount(), 0);
}

TEST(SceneManager, GetAllActive) {
    SceneManager sm;
    sm.load("a"); sm.activate("a");
    sm.load("b"); sm.activate("b");
    sm.load("c"); // not activated
    auto active = sm.getAllActive();
    EXPECT_EQ(active.size(), 2u);
}

TEST(SceneManager, TickActiveScenes) {
    SceneManager sm;
    sm.load("level_01");
    sm.activate("level_01");
    Scene* s = sm.get("level_01");

    // Add a short-lived entity
    Entity e = s->world().createEntity();
    engine::core::Lifetime lt; lt.remaining = 0.1f;
    s->world().addComponent<engine::core::Lifetime>(e, lt);

    sm.tickActive(0.2f); // should destroy the entity
    EXPECT_FALSE(s->world().isAlive(e));
}

// ── BVH ───────────────────────────────────────────────────────────────────────

// ── World transform propagation ───────────────────────────────────────────────

TEST(Scene, GetWorldTransformNullForNoTransform) {
    Scene s;
    s.load("test");
    s.activate();
    Entity e = s.world().createEntity(); // no Transform added
    s.tick(0.0f);
    EXPECT_EQ(s.getWorldTransform(e), nullptr);
}

TEST(Scene, GetWorldTransformRootMatchesLocalTransform) {
    Scene s;
    s.load("test");
    s.activate();
    Entity e = s.world().createEntity();
    engine::core::Transform t{};
    t.position = {3.f, 0.f, 0.f};
    s.world().addComponent<engine::core::Transform>(e, t);
    s.tick(0.0f);
    const auto* wt = s.getWorldTransform(e);
    ASSERT_NE(wt, nullptr);
    EXPECT_NEAR(wt->position.x, 3.f, 1e-5f);
    EXPECT_NEAR(wt->position.y, 0.f, 1e-5f);
    EXPECT_NEAR(wt->position.z, 0.f, 1e-5f);
}

TEST(Scene, GetWorldTransformChildComposesWithParent) {
    // Parent at (1,0,0), child at local (0,2,0) → world (1,2,0).
    Scene s;
    s.load("test");
    s.activate();

    Entity parent = s.world().createEntity();
    Entity child  = s.world().createEntity();

    engine::core::Transform parentT{};
    parentT.position = {1.f, 0.f, 0.f};
    s.world().addComponent<engine::core::Transform>(parent, parentT);

    engine::core::Transform childT{};
    childT.position = {0.f, 2.f, 0.f};
    s.world().addComponent<engine::core::Transform>(child, childT);

    s.world().addComponent<engine::core::ecs::HierarchyComponent>(
        child, engine::core::ecs::HierarchyComponent{});
    engine::core::ecs::linkChild(s.world(), parent, child);

    s.tick(0.0f);

    const auto* wt = s.getWorldTransform(child);
    ASSERT_NE(wt, nullptr);
    EXPECT_NEAR(wt->position.x, 1.f, 1e-4f);
    EXPECT_NEAR(wt->position.y, 2.f, 1e-4f);
    EXPECT_NEAR(wt->position.z, 0.f, 1e-4f);
}

TEST(Scene, GetWorldTransformClearedOnDeactivate) {
    Scene s;
    s.load("test");
    s.activate();
    Entity e = s.world().createEntity();
    engine::core::Transform t{};
    t.position = {1.f, 0.f, 0.f};
    s.world().addComponent<engine::core::Transform>(e, t);
    s.tick(0.0f);
    ASSERT_NE(s.getWorldTransform(e), nullptr);
    s.deactivate();
    EXPECT_EQ(s.getWorldTransform(e), nullptr);
}

// ── BVH ───────────────────────────────────────────────────────────────────────

TEST(BVH, EmptyQueryReturnsNoHit) {
    BVH bvh;
    auto hit = bvh.query({0,0,-10}, {0,0,1});
    EXPECT_FALSE(hit.hasHit);
}

TEST(BVH, SingleEntryRayHit) {
    BVH bvh;
    Entity e{1u, 1u};
    std::vector<BVHEntry> entries;
    entries.push_back({AABB::fromCenterExtents({0,0,5}, {1,1,1}), e});
    bvh.build(entries);

    auto hit = bvh.query({0,0,0}, {0,0,1}, 100.0f);
    EXPECT_TRUE(hit.hasHit);
    EXPECT_EQ(hit.entity, e);
    EXPECT_LT(hit.distance, 100.0f);
}

TEST(BVH, SingleEntryRayMiss) {
    BVH bvh;
    Entity e{1u, 1u};
    std::vector<BVHEntry> entries;
    entries.push_back({AABB::fromCenterExtents({0,0,5}, {1,1,1}), e});
    bvh.build(entries);

    auto hit = bvh.query({0,0,0}, {1,0,0}, 100.0f); // perpendicular
    EXPECT_FALSE(hit.hasHit);
}

TEST(BVH, MultipleEntriesNearestHit) {
    BVH bvh;
    Entity near{1u, 1u};
    Entity far_e{2u, 1u};
    std::vector<BVHEntry> entries;
    entries.push_back({AABB::fromCenterExtents({0,0,3}, {0.5f,0.5f,0.5f}), near});
    entries.push_back({AABB::fromCenterExtents({0,0,8}, {0.5f,0.5f,0.5f}), far_e});
    bvh.build(entries);

    auto hit = bvh.query({0,0,0}, {0,0,1}, 100.0f);
    EXPECT_TRUE(hit.hasHit);
    EXPECT_EQ(hit.entity, near);
}

TEST(BVH, FrustumQueryAllInside) {
    // Construct a frustum with all "accept" planes (d = +infinity).
    // frustumContainsAabb returns true when no plane rejects the AABB.
    // Use a simple orthographic-like view frustum via frustumFromViewProj.
    // For a simple test, just check that no results come from an empty BVH.
    BVH bvh;
    Frustum f{};
    // Set all planes to (0,0,0,1) — every point satisfies 0*x+0*y+0*z+1 >= 0.
    for (auto& p : f.planes) p = {0.0f, 0.0f, 0.0f, 1.0f};

    Entity e{1u, 1u};
    std::vector<BVHEntry> entries;
    entries.push_back({AABB::fromCenterExtents({0,0,5}, {1,1,1}), e});
    bvh.build(entries);

    std::vector<Entity> found;
    bvh.queryFrustum(f, found);
    EXPECT_FALSE(found.empty());
}

TEST(BVH, FrustumQueryNoneInside) {
    // Planes that reject everything: normal (0,1,0), d = -infinity equivalent.
    BVH bvh;
    Frustum f{};
    // First plane rejects all points: 0*x + 1*y + 0*z + (-1000) >= 0 fails for y < 1000.
    f.planes[0] = {0.0f, 1.0f, 0.0f, -1000.0f};
    for (int i = 1; i < 6; ++i) f.planes[i] = {0.0f, 0.0f, 0.0f, 1.0f};

    Entity e{1u, 1u};
    std::vector<BVHEntry> entries;
    entries.push_back({AABB::fromCenterExtents({0,0,5}, {1,1,1}), e});
    bvh.build(entries);

    std::vector<Entity> found;
    bvh.queryFrustum(f, found);
    EXPECT_TRUE(found.empty());
}

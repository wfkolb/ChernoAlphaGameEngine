#pragma once

#include <core/ecs/World.h>
#include <core/scene/SceneGlobals.h>
#include <core/scene/BVH.h>
#include <core/math/AABB.h>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>

// Forward declare to avoid core depending on physics module.
namespace engine::physics { class PhysicsWorld; }

namespace engine::core::scene {

class Scene {
public:
    Scene();
    ~Scene();

    Scene(const Scene&)            = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&&)                 = delete;
    Scene& operator=(Scene&&)      = delete;

    // ── Lifecycle ────────────────────────────────────────────────────────────

    // Initialise scene with a name and optional id. Clears existing state.
    void load(std::string_view name, uint32_t id = 0);

    // Build the static BVH from registered entries and mark as active.
    // setPhysicsWorld() should be called before activate() if integration is needed.
    void activate();

    // Per-frame tick: processes Lifetime components, clears dynamic grid.
    void tick(float dt);

    // Mark as inactive; leaves world and BVH intact.
    void deactivate();

    // Release all resources (world, BVH, globals, physics pointer).
    void unload();

    // ── ECS world ────────────────────────────────────────────────────────────

    ecs::World& world() noexcept;
    const ecs::World& world() const noexcept;

    // ── Globals ───────────────────────────────────────────────────────────────

    SceneGlobals& globals() noexcept;
    const SceneGlobals& globals() const noexcept;

    // ── Static BVH ────────────────────────────────────────────────────────────

    // Register an entry for inclusion in the static BVH.
    // Entries added before activate() are baked; activate() calls bvh.build().
    void addStaticEntry(ecs::Entity entity, const math::AABB& aabb);

    BVH& staticBVH() noexcept;
    const BVH& staticBVH() const noexcept;

    // ── Dynamic spatial index (rebuilt each tick) ─────────────────────────────

    void registerDynamic(ecs::Entity entity, const math::AABB& aabb);
    void queryDynamic(const math::AABB& queryAABB,
                      std::vector<ecs::Entity>& outEntities) const;

    // ── Physics integration (non-owning pointer, forward declared) ───────────

    void setPhysicsWorld(engine::physics::PhysicsWorld* pw) noexcept;
    engine::physics::PhysicsWorld* physicsWorld() noexcept;

    // ── State queries ─────────────────────────────────────────────────────────

    bool             isLoaded()  const noexcept;
    bool             isActive()  const noexcept;
    std::string_view name()      const noexcept;
    uint32_t         id()        const noexcept;

private:
    std::unique_ptr<ecs::World> world_;
    SceneGlobals                globals_;
    BVH                         staticBVH_;

    std::string name_;
    uint32_t    id_     = 0;
    bool        loaded_ = false;
    bool        active_ = false;

    engine::physics::PhysicsWorld* physicsWorld_ = nullptr;

    std::vector<BVHEntry> pendingStaticEntries_;

    struct DynamicEntry {
        math::AABB  aabb;
        ecs::Entity entity;
    };
    std::vector<DynamicEntry> dynamicEntries_;
};

} // namespace engine::core::scene

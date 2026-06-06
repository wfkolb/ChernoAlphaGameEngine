#pragma once

#include <core/ecs/World.h>
#include <core/scene/SceneGlobals.h>
#include <core/scene/BVH.h>
#include <core/math/AABB.h>
#include <core/components/Transform.h>
#include <core/components/MeshHandle.h>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
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

    // Store the physics world pointer and a step delegate.
    // The delegate is set automatically by setPhysicsWorld(); it is kept
    // separately so Scene.cpp never needs to include the physics header
    // (physics depends on core, not the other way around).
    void setPhysicsWorld(engine::physics::PhysicsWorld* pw) noexcept;
    void setPhysicsStepFn(std::function<void(float)> fn) noexcept;
    engine::physics::PhysicsWorld* physicsWorld() noexcept;

    // ── Mesh loading delegates ────────────────────────────────────────────────

    // Called by the app layer (which links engine::rendering) to upload geometry
    // for each entity that has a MeshHandle component when the scene activates.
    // Scene (engine::core) must not call loadEasset directly — that lives in tools/rendering.
    void setMeshLoadFn(std::function<void(ecs::Entity, const std::string&)> fn) noexcept;

    // Called by the app layer to release all uploaded GPU mesh handles on unload.
    void setMeshUnloadFn(std::function<void()> fn) noexcept;

    // ── World transforms ──────────────────────────────────────────────────────

    // Returns the world-space transform for entity e, or nullptr if e has no
    // Transform component. Populated each tick(); valid after the first tick.
    // Root entities return their own Transform; children return the composed
    // result of the full parent chain.
    const core::Transform* getWorldTransform(ecs::Entity e) const noexcept;

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
    // Step delegate — avoids including physics headers in this (core) module.
    std::function<void(float)>      physicsStepFn_;

    // Mesh load/unload delegates — avoids including rendering/tools headers in core.
    std::function<void(ecs::Entity, const std::string&)> meshLoadFn_;
    std::function<void()>                                meshUnloadFn_;

    // Keyed by Entity::index. Rebuilt from scratch every tick().
    std::unordered_map<uint32_t, core::Transform> worldTransforms_;

    std::vector<BVHEntry> pendingStaticEntries_;

    struct DynamicEntry {
        math::AABB  aabb;
        ecs::Entity entity;
    };
    std::vector<DynamicEntry> dynamicEntries_;
};

} // namespace engine::core::scene

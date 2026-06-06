#include "core/scene/Scene.h"
#include <core/components/Lifetime.h>
#include <core/components/MeshHandle.h>
#include <core/ecs/HierarchyComponent.h>
#include <core/diag/Assert.h>
#include <algorithm>
#include <vector>

namespace engine::core::scene {

Scene::Scene()  = default;
Scene::~Scene() = default;

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void Scene::load(std::string_view name, uint32_t id) {
    unload();
    world_   = std::make_unique<ecs::World>();
    name_    = std::string(name);
    id_      = id;
    globals_ = SceneGlobals{};
    globals_.sceneName = name_;
    globals_.sceneId   = id;
    loaded_ = true;
}

void Scene::activate() {
    if (!loaded_) return;
    staticBVH_.build(pendingStaticEntries_);

    if (meshLoadFn_) {
        world_->forEachEntity([&](ecs::Entity e) {
            const auto* mh = world_->tryGet<MeshHandle>(e);
            if (mh && mh->assetPath[0] != '\0') {
                meshLoadFn_(e, std::string(mh->assetPath));
            }
        });
    }

    active_ = true;
}

namespace {

// Recursive helper — asserts depth <= 8 and walks children.
void checkHierarchyDepth(ecs::World& world, ecs::Entity entity, int depth) {
    ENGINE_ASSERT(depth <= 8, "Scene: entity hierarchy depth exceeds 8 levels");
    const auto* hc = world.tryGet<ecs::HierarchyComponent>(entity);
    if (!hc) return;
    ecs::Entity child = hc->firstChild;
    while (child != ecs::kInvalidEntity) {
        checkHierarchyDepth(world, child, depth + 1);
        const auto* childHC = world.tryGet<ecs::HierarchyComponent>(child);
        child = childHC ? childHC->nextSibling : ecs::kInvalidEntity;
    }
}

} // anonymous namespace

void Scene::tick(float dt) {
    if (!active_) return;

    dynamicEntries_.clear();

    // Decrement Lifetime components; destroy expired entities.
    std::vector<ecs::Entity> toDestroy;
    world_->forEachEntity([&](ecs::Entity e) {
        auto* lt = world_->tryGet<core::Lifetime>(e);
        if (!lt) return;
        lt->remaining -= dt;
        if (lt->remaining <= 0.0f) toDestroy.push_back(e);
    });
    for (ecs::Entity e : toDestroy) {
        // Remove from static BVH before destroying (stale leaves cause ghost collisions).
        staticBVH_.remove(e);
        pendingStaticEntries_.erase(
            std::remove_if(pendingStaticEntries_.begin(), pendingStaticEntries_.end(),
                [&](const BVHEntry& entry) {
                    return entry.entity.index      == e.index
                        && entry.entity.generation == e.generation;
                }),
            pendingStaticEntries_.end());
        world_->destroyEntity(e);
    }

    // Advance the physics simulation. The step delegate wraps PhysicsWorld::step()
    // and is set by the app layer (which links engine::physics) via setPhysicsStepFn().
    // Scene (engine::core) must not include physics headers directly because the
    // dependency arrow runs physics → core, not the other way around.
    // CharacterController exposes no resultPosition field; the physics world
    // updates body positions internally via the BodyId registry.
    if (physicsStepFn_) {
        physicsStepFn_(dt);
    }

    // Validate hierarchy depths for all root entities (depth 0 = root).
    world_->forEachEntity([&](ecs::Entity e) {
        const auto* hc = world_->tryGet<ecs::HierarchyComponent>(e);
        if (hc && hc->parent != ecs::kInvalidEntity) return; // skip non-roots
        checkHierarchyDepth(*world_, e, 0);
    });

    // Propagate world transforms. computeWorldTransform() walks up the parent
    // chain composing local transforms, so calling it per-entity is correct for
    // any hierarchy depth. Entities without a Transform are skipped.
    worldTransforms_.clear();
    world_->forEachEntity([&](ecs::Entity e) {
        if (world_->tryGet<core::Transform>(e)) {
            worldTransforms_[e.index] = ecs::computeWorldTransform(*world_, e);
        }
    });
}

void Scene::deactivate() {
    if (!active_) return;
    active_ = false;
    dynamicEntries_.clear();
    worldTransforms_.clear();
}

void Scene::unload() {
    if (!loaded_) return;
    deactivate();

    if (meshUnloadFn_) meshUnloadFn_();
    meshLoadFn_   = nullptr;
    meshUnloadFn_ = nullptr;

    world_.reset();
    staticBVH_.clear();
    pendingStaticEntries_.clear();
    dynamicEntries_.clear();
    globals_       = SceneGlobals{};
    physicsWorld_  = nullptr;
    physicsStepFn_ = nullptr;
    name_.clear();
    id_     = 0;
    loaded_ = false;
}

// ── ECS world ─────────────────────────────────────────────────────────────────

ecs::World& Scene::world() noexcept {
    return *world_;
}
const ecs::World& Scene::world() const noexcept {
    return *world_;
}

// ── Globals ───────────────────────────────────────────────────────────────────

SceneGlobals& Scene::globals() noexcept { return globals_; }
const SceneGlobals& Scene::globals() const noexcept { return globals_; }

// ── Static BVH ────────────────────────────────────────────────────────────────

void Scene::addStaticEntry(ecs::Entity entity, const math::AABB& aabb) {
    pendingStaticEntries_.push_back({aabb, entity});
}

BVH& Scene::staticBVH() noexcept { return staticBVH_; }
const BVH& Scene::staticBVH() const noexcept { return staticBVH_; }

// ── Dynamic spatial index ─────────────────────────────────────────────────────

void Scene::registerDynamic(ecs::Entity entity, const math::AABB& aabb) {
    dynamicEntries_.push_back({aabb, entity});
}

void Scene::queryDynamic(const math::AABB& queryAABB,
                          std::vector<ecs::Entity>& outEntities) const {
    for (const auto& e : dynamicEntries_) {
        if (e.aabb.intersects(queryAABB)) outEntities.push_back(e.entity);
    }
}

// ── Physics integration ───────────────────────────────────────────────────────

void Scene::setPhysicsWorld(engine::physics::PhysicsWorld* pw) noexcept {
    physicsWorld_ = pw;
}

void Scene::setPhysicsStepFn(std::function<void(float)> fn) noexcept {
    physicsStepFn_ = std::move(fn);
}

engine::physics::PhysicsWorld* Scene::physicsWorld() noexcept {
    return physicsWorld_;
}

// ── Mesh loading delegates ─────────────────────────────────────────────────────

void Scene::setMeshLoadFn(std::function<void(ecs::Entity, const std::string&)> fn) noexcept {
    meshLoadFn_ = std::move(fn);
}

void Scene::setMeshUnloadFn(std::function<void()> fn) noexcept {
    meshUnloadFn_ = std::move(fn);
}

// ── World transforms ──────────────────────────────────────────────────────────

const core::Transform* Scene::getWorldTransform(ecs::Entity e) const noexcept {
    auto it = worldTransforms_.find(e.index);
    return it != worldTransforms_.end() ? &it->second : nullptr;
}

// ── State queries ─────────────────────────────────────────────────────────────

bool             Scene::isLoaded()  const noexcept { return loaded_; }
bool             Scene::isActive()  const noexcept { return active_; }
std::string_view Scene::name()      const noexcept { return name_;   }
uint32_t         Scene::id()        const noexcept { return id_;     }

} // namespace engine::core::scene

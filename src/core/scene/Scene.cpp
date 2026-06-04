#include "core/scene/Scene.h"
#include <core/components/Lifetime.h>
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
    active_ = true;
}

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
    for (ecs::Entity e : toDestroy) world_->destroyEntity(e);
}

void Scene::deactivate() {
    if (!active_) return;
    active_ = false;
    dynamicEntries_.clear();
}

void Scene::unload() {
    if (!loaded_) return;
    deactivate();
    world_.reset();
    staticBVH_.clear();
    pendingStaticEntries_.clear();
    dynamicEntries_.clear();
    globals_       = SceneGlobals{};
    physicsWorld_  = nullptr;
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
engine::physics::PhysicsWorld* Scene::physicsWorld() noexcept {
    return physicsWorld_;
}

// ── State queries ─────────────────────────────────────────────────────────────

bool             Scene::isLoaded()  const noexcept { return loaded_; }
bool             Scene::isActive()  const noexcept { return active_; }
std::string_view Scene::name()      const noexcept { return name_;   }
uint32_t         Scene::id()        const noexcept { return id_;     }

} // namespace engine::core::scene

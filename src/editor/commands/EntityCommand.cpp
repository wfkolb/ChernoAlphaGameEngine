#ifdef ENGINE_DEVREL

#include "editor/commands/EntityCommand.h"

#include <core/ecs/World.h>
#include <core/ecs/Name.h>

namespace engine::editor {

// ---- CreateEntityCommand --------------------------------------------------

CreateEntityCommand::CreateEntityCommand(core::ecs::World& world,
                                         std::string name,
                                         const core::Transform& transform)
    : world_(&world), displayName_(std::move(name)), transform_(transform) {}

void CreateEntityCommand::execute() {
    entity_ = world_->createEntity();
    if (!displayName_.empty()) {
        world_->addComponent<core::ecs::Name>(entity_, core::ecs::Name{displayName_.c_str()});
    }
    world_->addComponent<core::Transform>(entity_, transform_);
}

void CreateEntityCommand::undo() {
    if (world_->isAlive(entity_)) {
        world_->destroyEntity(entity_);
    }
    entity_ = core::ecs::kInvalidEntity;
}

// ---- DestroyEntityCommand -------------------------------------------------

DestroyEntityCommand::DestroyEntityCommand(core::ecs::World& world,
                                           core::ecs::Entity entity)
    : world_(&world), entity_(entity) {
    if (auto* nm = world_->tryGet<core::ecs::Name>(entity_)) {
        displayName_ = nm->c_str();
    }
    if (auto* tr = world_->tryGet<core::Transform>(entity_)) {
        transform_    = *tr;
        hadTransform_ = true;
    }
}

void DestroyEntityCommand::execute() {
    if (world_->isAlive(entity_)) {
        world_->destroyEntity(entity_);
    }
}

void DestroyEntityCommand::undo() {
    entity_ = world_->createEntity();
    if (!displayName_.empty()) {
        world_->addComponent<core::ecs::Name>(entity_, core::ecs::Name{displayName_.c_str()});
    }
    if (hadTransform_) {
        world_->addComponent<core::Transform>(entity_, transform_);
    }
}

// ---- DuplicateEntityCommand -----------------------------------------------

DuplicateEntityCommand::DuplicateEntityCommand(core::ecs::World& world,
                                               core::ecs::Entity source)
    : world_(&world), source_(source) {}

void DuplicateEntityCommand::execute() {
    if (!world_->isAlive(source_)) return;

    // On redo the previous duplicate was already removed by undo(); create fresh.
    if (world_->isAlive(duplicate_)) {
        world_->destroyEntity(duplicate_);
    }
    duplicate_ = world_->createEntity();

    // Snapshot all component bytes before touching duplicate_. Adding components
    // to duplicate_ may land it in the same archetype as source_, causing the
    // archetype's column vectors to reallocate — which would invalidate any
    // pointer obtained from forEachComponentOnEntity before the reallocation.
    struct Snapshot { core::ecs::ComponentTypeId id; std::vector<uint8_t> bytes; };
    std::vector<Snapshot> snapshots;
    world_->forEachComponentOnEntity(source_,
        [&](core::ecs::ComponentTypeId id, void* data) {
            const auto& meta = core::ecs::World::getComponentMeta(id);
            if (meta.size == 0) return;
            auto* p = static_cast<uint8_t*>(data);
            snapshots.push_back({id, std::vector<uint8_t>(p, p + meta.size)});
        });

    for (auto& snap : snapshots) {
        world_->addComponentRaw(duplicate_, snap.id, snap.bytes.data(), snap.bytes.size());
    }

    // Offset the duplicate's Transform by (0.5, 0, 0.5).
    if (auto* tr = world_->tryGet<core::Transform>(duplicate_)) {
        tr->position.x += 0.5f;
        tr->position.z += 0.5f;
    }
}

void DuplicateEntityCommand::undo() {
    if (world_->isAlive(duplicate_)) {
        world_->destroyEntity(duplicate_);
    }
    duplicate_ = core::ecs::kInvalidEntity;
}

} // namespace engine::editor

#endif // ENGINE_DEVREL

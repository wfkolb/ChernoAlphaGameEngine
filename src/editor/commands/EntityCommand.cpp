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

} // namespace engine::editor

#endif // ENGINE_DEVREL

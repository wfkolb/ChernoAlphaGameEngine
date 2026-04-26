#include "core/ecs/World.h"
#include "core/ecs/CommandBuffer.h"
#include "core/diag/Assert.h"

#include <algorithm>

namespace engine::core::ecs {

    World::World() {
        // Create the empty archetype (no components) that all new entities start in.
        auto arch      = std::make_unique<Archetype>();
        emptyArchetype_ = arch.get();
        archetypes_.push_back(std::move(arch));
    }

    const ComponentMeta& World::getComponentMeta(ComponentTypeId id) {
        return registry_[id];
    }

    Entity World::createEntity() {
        uint32_t index;
        if (!freeList_.empty()) {
            index = freeList_.back();
            freeList_.pop_back();
            // generation was already bumped in destroyEntity
        } else {
            index = static_cast<uint32_t>(entities_.size());
            entities_.push_back({});
        }

        EntityRecord& rec = entities_[index];
        rec.archetype = emptyArchetype_;
        rec.row       = emptyArchetype_->rowCount;

        Entity e{ index, rec.generation };
        archetypeAppendRow(*emptyArchetype_, e);
        return e;
    }

    void World::destroyEntity(Entity e) {
        ENGINE_ASSERT(isAlive(e), "destroyEntity: entity not alive");

        EntityRecord& rec = entities_[e.index];
        Archetype* arch   = rec.archetype;

        // Destruct components in the archetype columns.
        for (auto& [id, col] : arch->columns) {
            const ComponentMeta& meta = registry_[id];
            if (meta.destruct) {
                meta.destruct(col.data() + rec.row * meta.size);
            }
        }

        Entity moved = archetypeSwapRemoveRow(*arch, rec.row);
        if (moved != kInvalidEntity) {
            entities_[moved.index].row = rec.row;
        }

        // Invalidate the record and recycle the slot.
        ++rec.generation;
        rec.archetype = nullptr;
        rec.row       = 0;
        freeList_.push_back(e.index);
    }

    bool World::isAlive(Entity e) const {
        if (e.index >= static_cast<uint32_t>(entities_.size())) return false;
        const EntityRecord& rec = entities_[e.index];
        return rec.archetype != nullptr && rec.generation == e.generation;
    }

    bool World::hasComponent(Entity e, ComponentTypeId id) const {
        if (!isAlive(e)) return false;
        return entities_[e.index].archetype->mask.test(id);
    }

    void World::forEachEntity(std::function<void(Entity)> fn) const {
        for (const auto& archPtr : archetypes_) {
            for (const Entity& ent : archPtr->entities) {
                fn(ent);
            }
        }
    }

    void World::forEachComponentOnEntity(Entity e,
        std::function<void(ComponentTypeId, void*)> fn) {
        ENGINE_ASSERT(isAlive(e), "forEachComponentOnEntity: entity not alive");

        EntityRecord& rec = entities_[e.index];
        Archetype*  arch  = rec.archetype;

        for (auto& [id, col] : arch->columns) {
            const ComponentMeta& meta = registry_[id];
            fn(id, col.data() + rec.row * meta.size);
        }
    }

    Archetype* World::getOrCreateArchetype(const ComponentMask& mask) {
        for (const auto& archPtr : archetypes_) {
            if (archPtr->mask == mask) return archPtr.get();
        }

        auto arch  = std::make_unique<Archetype>();
        arch->mask = mask;

        // Create one empty column per component bit that is set.
        for (size_t i = 0; i < 256; ++i) {
            if (mask.test(i)) {
                arch->columns[static_cast<ComponentTypeId>(i)] = {};
            }
        }

        Archetype* ptr = arch.get();
        archetypes_.push_back(std::move(arch));
        return ptr;
    }

    void World::moveEntity(Entity e, Archetype* dst) {
        ENGINE_ASSERT(isAlive(e), "moveEntity: entity not alive");

        EntityRecord& rec = entities_[e.index];
        Archetype*    src = rec.archetype;
        const uint32_t srcRow = rec.row;

        // Append an uninitialised row in dst (construct zeros / call construct).
        const uint32_t dstRow = dst->rowCount;
        archetypeAppendRow(*dst, e);

        // Copy bytes for components present in both src and dst.
        archetypeCopySharedComponents(*src, srcRow, *dst, dstRow);

        // Swap-remove from src; if another entity was moved into srcRow, fix its record.
        Entity moved = archetypeSwapRemoveRow(*src, srcRow);
        if (moved != kInvalidEntity) {
            entities_[moved.index].row = srcRow;
        }

        rec.archetype = dst;
        rec.row       = dstRow;
    }

    void World::flushCommands(CommandBuffer& cb) {
        cb.flush(*this);
    }

} // namespace engine::core::ecs

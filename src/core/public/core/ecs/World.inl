// Included at the bottom of World.h — template / inline method bodies only.
#pragma once

#include "core/diag/Assert.h"

#include <cstring>
#include <type_traits>

namespace engine::core::ecs {

    template<typename T>
    ComponentTypeId World::registerComponent(ComponentMeta meta) {
        ENGINE_ASSERT(nextId_ < 255, "ComponentTypeId overflow");
        const ComponentTypeId id = nextId_++;
        registry_[id] = meta;
        return id;
    }

    template<typename T>
    void World::addComponent(Entity e, T value) {
        ENGINE_ASSERT(isAlive(e), "addComponent: entity not alive");

        const ComponentTypeId id = T::kComponentId;

        if (registry_[id].size == 0) {
            registry_[id].size  = sizeof(T);
            registry_[id].align = alignof(T);
        }

        EntityRecord& rec = entities_[e.index];
        ENGINE_ASSERT(rec.generation == e.generation, "addComponent: stale entity handle");

        Archetype* src = rec.archetype;
        ENGINE_ASSERT(!src->mask.test(id), "addComponent: component already present");

        ComponentMask newMask = src->mask;
        newMask.set(id);

        Archetype* dst = src->addEdge[id];
        if (!dst) {
            dst = getOrCreateArchetype(newMask);
            src->addEdge[id] = dst;
            dst->removeEdge[id] = src;
        }

        moveEntity(e, dst);

        // Overwrite the default-constructed slot with the caller's value.
        const uint32_t newRow = entities_[e.index].row;
        auto& col = dst->columns.at(id);
        std::memcpy(col.data() + newRow * sizeof(T), &value, sizeof(T));
    }

    template<typename T>
    void World::removeComponent(Entity e) {
        ENGINE_ASSERT(isAlive(e), "removeComponent: entity not alive");

        const ComponentTypeId id = T::kComponentId;

        EntityRecord& rec = entities_[e.index];
        ENGINE_ASSERT(rec.generation == e.generation, "removeComponent: stale entity handle");

        Archetype* src = rec.archetype;
        ENGINE_ASSERT(src->mask.test(id), "removeComponent: component not present");

        // Call the destructor on the component being dropped before the move.
        const ComponentMeta& meta = registry_[id];
        if (meta.destruct) {
            auto& col = src->columns.at(id);
            meta.destruct(col.data() + rec.row * meta.size);
        }

        ComponentMask newMask = src->mask;
        newMask.reset(id);

        Archetype* dst = src->removeEdge[id];
        if (!dst) {
            dst = getOrCreateArchetype(newMask);
            src->removeEdge[id] = dst;
            dst->addEdge[id] = src;
        }

        moveEntity(e, dst);
    }

    template<typename T>
    T* World::tryGet(Entity e) {
        if (!isAlive(e)) return nullptr;

        const ComponentTypeId id = T::kComponentId;
        const EntityRecord& rec  = entities_[e.index];
        Archetype* arch          = rec.archetype;

        auto it = arch->columns.find(id);
        if (it == arch->columns.end()) return nullptr;

        return reinterpret_cast<T*>(it->second.data() + rec.row * sizeof(T));
    }

    template<typename T>
    T& World::get(Entity e) {
        T* ptr = tryGet<T>(e);
        ENGINE_ASSERT(ptr != nullptr, "get: component not present on entity");
        return *ptr;
    }

} // namespace engine::core::ecs

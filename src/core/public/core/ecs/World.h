#pragma once

#include "core/ecs/Entity.h"
#include "core/ecs/Archetype.h"
#include "core/diag/Assert.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace engine::core::ecs {

    class CommandBuffer;

    class World {
    public:
        World();
        ~World() = default;

        ENGINE_NO_COPY(World);
        ENGINE_NO_MOVE(World);

        // ---- Component registration (call once per type at startup) ----

        template<typename T>
        static ComponentTypeId registerComponent(ComponentMeta meta);

        static const ComponentMeta& getComponentMeta(ComponentTypeId id);

        // ---- Entity lifecycle ----

        Entity createEntity();
        void   destroyEntity(Entity e);
        bool   isAlive(Entity e) const;

        // ---- Component access ----

        template<typename T>
        void addComponent(Entity e, T value = {});

        template<typename T>
        void removeComponent(Entity e);

        template<typename T>
        T* tryGet(Entity e);

        template<typename T>
        T& get(Entity e);

        bool hasComponent(Entity e, ComponentTypeId id) const;

        // ---- Iteration ----

        void forEachEntity(std::function<void(Entity)> fn) const;
        void forEachComponentOnEntity(Entity e,
            std::function<void(ComponentTypeId, void*)> fn);

        // ---- Command buffer integration ----

        void flushCommands(CommandBuffer& cb);

        // ---- Internal access used by View and CommandBuffer ----

        const std::vector<std::unique_ptr<Archetype>>& archetypes() const {
            return archetypes_;
        }

        friend class CommandBuffer;

    private:
        struct EntityRecord {
            Archetype* archetype = nullptr;
            uint32_t   row       = 0;
            uint32_t   generation = 0;
        };

        std::vector<EntityRecord>            entities_;
        std::vector<uint32_t>                freeList_;
        std::vector<std::unique_ptr<Archetype>> archetypes_;
        Archetype*                           emptyArchetype_ = nullptr;

        static inline ComponentTypeId                  nextId_   = 0;
        static inline std::array<ComponentMeta, 256>   registry_ = {};

        Archetype* getOrCreateArchetype(const ComponentMask& mask);

        // Moves entity e from its current archetype to dst.
        // Copies all shared component bytes, then updates the entity record.
        void moveEntity(Entity e, Archetype* dst);
    };

} // namespace engine::core::ecs

#include "core/ecs/World.inl"

#include "core/ecs/CommandBuffer.h"
#include "core/ecs/World.h"
#include "core/diag/Assert.h"

#include <cstring>
#include <unordered_map>

namespace engine::core::ecs {

    Entity CommandBuffer::createEntity() {
        const uint32_t pendingIndex = kPendingBase + pendingEntityCount_++;

        Cmd cmd;
        cmd.type   = CmdType::Create;
        cmd.entity = Entity{ pendingIndex, 0 };
        cmds_.push_back(cmd);

        return cmd.entity;
    }

    void CommandBuffer::destroyEntity(Entity e) {
        Cmd cmd;
        cmd.type   = CmdType::Destroy;
        cmd.entity = e;
        cmds_.push_back(std::move(cmd));
    }

    void CommandBuffer::flush(World& world) {
        // Maps placeholder indices (>= kPendingBase) to real entities created during flush.
        std::unordered_map<uint32_t, Entity> pendingMap;

        auto resolveEntity = [&](Entity e) -> Entity {
            if (e.index >= kPendingBase) {
                auto it = pendingMap.find(e.index);
                ENGINE_ASSERT(it != pendingMap.end(),
                    "CommandBuffer::flush: pending entity used before its Create command");
                return it->second;
            }
            return e;
        };

        for (Cmd& cmd : cmds_) {
            switch (cmd.type) {

            case CmdType::Create: {
                Entity real = world.createEntity();
                pendingMap[cmd.entity.index] = real;
                break;
            }

            case CmdType::Destroy: {
                Entity e = resolveEntity(cmd.entity);
                if (world.isAlive(e)) {
                    world.destroyEntity(e);
                }
                break;
            }

            case CmdType::Add: {
                Entity e = resolveEntity(cmd.entity);
                if (!world.isAlive(e)) break;
                if (world.hasComponent(e, cmd.componentId)) break;

                if (World::registry_[cmd.componentId].size == 0) {
                    World::registry_[cmd.componentId].size = cmd.data.size();
                }

                World::EntityRecord& rec = world.entities_[e.index];
                Archetype*           src = rec.archetype;

                ComponentMask newMask = src->mask;
                newMask.set(cmd.componentId);

                Archetype* dst = src->addEdge[cmd.componentId];
                if (!dst) {
                    dst = world.getOrCreateArchetype(newMask);
                    src->addEdge[cmd.componentId] = dst;
                    dst->removeEdge[cmd.componentId] = src;
                }

                world.moveEntity(e, dst);

                // Write the buffered bytes into the new row.
                const ComponentMeta& meta = World::getComponentMeta(cmd.componentId);
                auto& col = dst->columns.at(cmd.componentId);
                uint32_t newRow = world.entities_[e.index].row;
                ENGINE_ASSERT(cmd.data.size() == meta.size,
                    "CommandBuffer::flush: Add command data size mismatch");
                std::memcpy(col.data() + newRow * meta.size, cmd.data.data(), meta.size);
                break;
            }

            case CmdType::Remove: {
                Entity e = resolveEntity(cmd.entity);
                if (!world.isAlive(e)) break;
                if (!world.hasComponent(e, cmd.componentId)) break;

                World::EntityRecord& rec = world.entities_[e.index];
                Archetype*           src = rec.archetype;

                // Call the component destructor before the entity moves archetypes.
                const ComponentMeta& removeMeta = World::getComponentMeta(cmd.componentId);
                if (removeMeta.destruct) {
                    auto& removeCol = src->columns.at(cmd.componentId);
                    removeMeta.destruct(removeCol.data() + rec.row * removeMeta.size);
                }

                ComponentMask newMask = src->mask;
                newMask.reset(cmd.componentId);

                Archetype* dst = src->removeEdge[cmd.componentId];
                if (!dst) {
                    dst = world.getOrCreateArchetype(newMask);
                    src->removeEdge[cmd.componentId] = dst;
                    dst->addEdge[cmd.componentId] = src;
                }

                world.moveEntity(e, dst);
                break;
            }

            }
        }

        cmds_.clear();
        pendingEntityCount_ = 0;
    }

} // namespace engine::core::ecs

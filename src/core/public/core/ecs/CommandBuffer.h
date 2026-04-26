#pragma once

#include "core/ecs/Entity.h"
#include "core/diag/Assert.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace engine::core::ecs {

    class World;

    class CommandBuffer {
    public:
        CommandBuffer()  = default;
        ~CommandBuffer() = default;

        ENGINE_NO_COPY(CommandBuffer);
        ENGINE_NO_MOVE(CommandBuffer);

        // Returns a temporary "pending" entity.  Its real index is resolved on flush.
        Entity createEntity();

        void destroyEntity(Entity e);

        template<typename T>
        void addComponent(Entity e, T value);

        template<typename T>
        void removeComponent(Entity e);

        // Apply all buffered commands to world, then clear the buffer.
        void flush(World& world);

    private:
        enum class CmdType : uint8_t { Create, Destroy, Add, Remove };

        struct Cmd {
            CmdType         type;
            Entity          entity;
            ComponentTypeId componentId = 0;
            std::vector<uint8_t> data;
        };

        std::vector<Cmd> cmds_;
        uint32_t         pendingEntityCount_ = 0;

        // Pending entities use index values in the range [kPendingBase, kPendingBase + n).
        static constexpr uint32_t kPendingBase = 0x8000'0000u;
    };

    // ---- template bodies ----

    template<typename T>
    void CommandBuffer::addComponent(Entity e, T value) {
        Cmd cmd;
        cmd.type        = CmdType::Add;
        cmd.entity      = e;
        cmd.componentId = T::kComponentId;
        cmd.data.resize(sizeof(T));
        std::memcpy(cmd.data.data(), &value, sizeof(T));
        cmds_.push_back(std::move(cmd));
    }

    template<typename T>
    void CommandBuffer::removeComponent(Entity e) {
        Cmd cmd;
        cmd.type        = CmdType::Remove;
        cmd.entity      = e;
        cmd.componentId = T::kComponentId;
        cmds_.push_back(std::move(cmd));
    }

} // namespace engine::core::ecs

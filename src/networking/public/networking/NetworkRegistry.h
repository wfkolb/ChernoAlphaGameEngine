#pragma once
#include <core/ecs/Entity.h>
#include <cstdint>
#include <unordered_map>

namespace engine::networking {

// Maps network IDs to ECS entities.
class NetworkRegistry {
public:
    void                       registerEntity  (uint32_t netId, engine::core::ecs::Entity e);
    void                       unregisterEntity(uint32_t netId);
    engine::core::ecs::Entity  find            (uint32_t netId) const noexcept;
    bool                       contains        (uint32_t netId) const noexcept;
    void                       clear           () noexcept;
    size_t                     size            () const noexcept { return map_.size(); }

private:
    std::unordered_map<uint32_t, engine::core::ecs::Entity> map_;
};

} // namespace engine::networking

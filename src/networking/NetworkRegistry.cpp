#include <networking/NetworkRegistry.h>

namespace engine::networking {

void NetworkRegistry::registerEntity(uint32_t netId, engine::core::ecs::Entity e) {
    map_[netId] = e;
}

void NetworkRegistry::unregisterEntity(uint32_t netId) {
    map_.erase(netId);
}

engine::core::ecs::Entity NetworkRegistry::find(uint32_t netId) const noexcept {
    const auto it = map_.find(netId);
    return (it != map_.end()) ? it->second : engine::core::ecs::kInvalidEntity;
}

bool NetworkRegistry::contains(uint32_t netId) const noexcept {
    return map_.count(netId) > 0u;
}

void NetworkRegistry::clear() noexcept {
    map_.clear();
}

} // namespace engine::networking

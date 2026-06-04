#pragma once
#include <cstdint>
#include <string_view>

namespace engine::networking {

enum class RpcTarget : uint8_t {
    Server,
    AllClients,
    OwnerClient,
    NearbyClients,
};

enum class RpcReliability : uint8_t {
    Unreliable,
    Reliable,
};

struct RpcDescriptor {
    std::string_view name;
    RpcTarget        target;
    RpcReliability   reliability;
    uint16_t         id;  // stable 16-bit hash of name for wire encoding
};

namespace detail {
consteval uint16_t rpcHash(std::string_view s) noexcept {
    uint32_t h = 2166136261u;
    for (const char c : s)
        h = (h ^ static_cast<uint8_t>(c)) * 16777619u;
    return static_cast<uint16_t>(h);
}
} // namespace detail

// Declare a named RPC on a class.  Creates a static constexpr descriptor.
// Usage:   ENGINE_RPC(FireWeapon, Server, Reliable)
#define ENGINE_RPC(Name_, Target_, Reliability_) \
    static constexpr ::engine::networking::RpcDescriptor k##Name_##Rpc { \
        #Name_, \
        ::engine::networking::RpcTarget::Target_, \
        ::engine::networking::RpcReliability::Reliability_, \
        ::engine::networking::detail::rpcHash(#Name_) \
    }

} // namespace engine::networking

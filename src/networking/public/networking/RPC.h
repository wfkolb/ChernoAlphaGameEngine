#pragma once
#include <cstdint>
#include <functional>
#include <string_view>
#include <type_traits>

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

// The canonical RPC handler signature.
// Handlers receive a pointer to the raw serialized payload and its byte length.
// Using raw bytes keeps the handler independent of any particular serialization
// format and matches the networking layer's wire representation.
using RpcHandler = std::function<void(const uint8_t* data, size_t size)>;

namespace detail {

consteval uint16_t rpcHash(std::string_view s) noexcept {
    uint32_t h = 2166136261u;
    for (const char c : s)
        h = (h ^ static_cast<uint8_t>(c)) * 16777619u;
    return static_cast<uint16_t>(h);
}

// Helper: deduce the return type only when Fn is invocable; otherwise void.
// This avoids a hard error in invoke_result_t when Fn is not callable.
template<typename Fn, typename... Args>
struct InvokeResultOrVoid {
    using type = void;
};
template<typename Fn, typename... Args>
    requires std::is_invocable_v<Fn, Args...>
struct InvokeResultOrVoid<Fn, Args...> {
    using type = std::invoke_result_t<Fn, Args...>;
};

// Trait: is Fn callable with (const uint8_t*, size_t) and returns exactly void?
// std::is_invocable_r_v<void,...> accepts non-void returns because they are
// silently discarded, so we additionally require the actual return type is void.
template<typename Fn>
inline constexpr bool kIsValidRpcHandler =
    std::is_invocable_v<Fn, const uint8_t*, size_t> &&
    std::is_void_v<typename InvokeResultOrVoid<Fn, const uint8_t*, size_t>::type>;

} // namespace detail

// Register an RPC handler with compile-time signature checking.
//
// Usage:
//   engine::networking::registerRpc(
//       kFireWeaponRpc,
//       [](const uint8_t* data, size_t size) { /* handle payload */ });
//
// A static_assert fires at compile time if Fn is not callable as
// void(const uint8_t*, size_t).
template<typename Fn>
RpcHandler registerRpc(const RpcDescriptor& /*descriptor*/, Fn&& handler) {
    static_assert(
        detail::kIsValidRpcHandler<Fn>,
        "RPC handler must be callable as void(const uint8_t* data, size_t size). "
        "Check that your handler accepts exactly those two parameters."
    );
    return RpcHandler(std::forward<Fn>(handler));
}

// Declare a named RPC descriptor on a class.  Creates a static constexpr
// RpcDescriptor.  New code should use registerRpc() to attach a handler;
// this macro is kept for backward compatibility and for declaration-only usage.
//
// Usage:   ENGINE_RPC(FireWeapon, Server, Reliable)
#define ENGINE_RPC(Name_, Target_, Reliability_) \
    static constexpr ::engine::networking::RpcDescriptor k##Name_##Rpc { \
        #Name_, \
        ::engine::networking::RpcTarget::Target_, \
        ::engine::networking::RpcReliability::Reliability_, \
        ::engine::networking::detail::rpcHash(#Name_) \
    }

} // namespace engine::networking

#include <gtest/gtest.h>
#include <networking/RPC.h>

#include <cstddef>
#include <cstdint>

using namespace engine::networking;

// ── Compile-time trait checks ─────────────────────────────────────────────────
// These static_asserts run at compile time.  A static_assert failure here
// means the kIsValidRpcHandler trait is broken, not the user's handler.

// Correct signature: void(const uint8_t*, size_t)
static_assert(detail::kIsValidRpcHandler<void(*)(const uint8_t*, size_t)>,
    "plain function pointer with correct signature must pass");

static_assert(detail::kIsValidRpcHandler<decltype(
    [](const uint8_t*, size_t) {})>,
    "lambda with correct signature must pass");

// Wrong signatures must FAIL the trait.
static_assert(!detail::kIsValidRpcHandler<void(*)()>,
    "no-arg function must fail");

static_assert(!detail::kIsValidRpcHandler<void(*)(int)>,
    "single-int function must fail");

static_assert(!detail::kIsValidRpcHandler<void(*)(const uint8_t*)>,
    "missing size argument must fail");

static_assert(!detail::kIsValidRpcHandler<int(*)(const uint8_t*, size_t)>,
    "non-void return must fail");

static_assert(!detail::kIsValidRpcHandler<void(*)(uint8_t*, size_t)>,
    "non-const data pointer must fail");

// ── RpcDescriptor correctness ─────────────────────────────────────────────────

TEST(RpcDescriptor, HashIsStable) {
    constexpr RpcDescriptor d1{
        "FireWeapon",
        RpcTarget::Server,
        RpcReliability::Reliable,
        detail::rpcHash("FireWeapon")
    };
    constexpr RpcDescriptor d2{
        "FireWeapon",
        RpcTarget::Server,
        RpcReliability::Reliable,
        detail::rpcHash("FireWeapon")
    };
    // Same name → same hash.
    EXPECT_EQ(d1.id, d2.id);
}

TEST(RpcDescriptor, DifferentNamesHaveDifferentHashes) {
    constexpr auto h1 = detail::rpcHash("FireWeapon");
    constexpr auto h2 = detail::rpcHash("PlayerDied");
    EXPECT_NE(h1, h2);
}

// FakeSystem must be at namespace scope on MSVC: static constexpr members are
// not permitted in locally-defined classes inside function bodies (C++03 rule,
// retained in MSVC even under C++20).
namespace {
struct FakeSystem {
    ENGINE_RPC(DealDamage, Server, Reliable);
};
} // anonymous namespace

TEST(RpcDescriptor, EngineRpcMacroSetsCorrectFields) {
    EXPECT_EQ(FakeSystem::kDealDamageRpc.name,        "DealDamage");
    EXPECT_EQ(FakeSystem::kDealDamageRpc.target,      RpcTarget::Server);
    EXPECT_EQ(FakeSystem::kDealDamageRpc.reliability, RpcReliability::Reliable);
    EXPECT_EQ(FakeSystem::kDealDamageRpc.id,
              detail::rpcHash("DealDamage"));
}

// ── registerRpc() ─────────────────────────────────────────────────────────────

TEST(RegisterRpc, ValidHandlerReturnsCallableHandler) {
    constexpr RpcDescriptor desc{
        "TestRpc",
        RpcTarget::AllClients,
        RpcReliability::Unreliable,
        detail::rpcHash("TestRpc")
    };

    bool called = false;
    const uint8_t  payload[4] = { 0xDE, 0xAD, 0xBE, 0xEF };

    RpcHandler handler = registerRpc(desc,
        [&called](const uint8_t* /*data*/, size_t /*size*/) {
            called = true;
        });

    ASSERT_TRUE(static_cast<bool>(handler));
    handler(payload, sizeof(payload));
    EXPECT_TRUE(called);
}

TEST(RegisterRpc, HandlerReceivesPayloadBytesCorrectly) {
    constexpr RpcDescriptor desc{
        "DataRpc",
        RpcTarget::Server,
        RpcReliability::Reliable,
        detail::rpcHash("DataRpc")
    };

    const uint8_t sentPayload[] = { 0x01, 0x02, 0x03 };
    const uint8_t* receivedData = nullptr;
    size_t         receivedSize = 0;

    RpcHandler handler = registerRpc(desc,
        [&receivedData, &receivedSize](const uint8_t* data, size_t size) {
            receivedData = data;
            receivedSize = size;
        });

    handler(sentPayload, sizeof(sentPayload));
    EXPECT_EQ(receivedData, sentPayload);
    EXPECT_EQ(receivedSize, sizeof(sentPayload));
}

TEST(RegisterRpc, NullPayloadZeroSizeIsAccepted) {
    constexpr RpcDescriptor desc{
        "EmptyRpc",
        RpcTarget::OwnerClient,
        RpcReliability::Reliable,
        detail::rpcHash("EmptyRpc")
    };

    bool called = false;
    RpcHandler handler = registerRpc(desc,
        [&called](const uint8_t*, size_t size) {
            EXPECT_EQ(size, 0u);
            called = true;
        });

    handler(nullptr, 0u);
    EXPECT_TRUE(called);
}

TEST(RegisterRpc, AllTargetsAccepted) {
    // Confirm all four RpcTarget enum values can be used in a descriptor.
    constexpr RpcTarget targets[] = {
        RpcTarget::Server,
        RpcTarget::AllClients,
        RpcTarget::OwnerClient,
        RpcTarget::NearbyClients,
    };

    for (const RpcTarget t : targets) {
        const RpcDescriptor desc{ "T", t, RpcReliability::Reliable,
                                  detail::rpcHash("T") };
        int count = 0;
        RpcHandler h = registerRpc(desc,
            [&count](const uint8_t*, size_t) { ++count; });
        h(nullptr, 0u);
        EXPECT_EQ(count, 1);
    }
}

TEST(RegisterRpc, BothReliabilityModesAccepted) {
    constexpr RpcDescriptor relDesc{
        "Rel", RpcTarget::Server, RpcReliability::Reliable,
        detail::rpcHash("Rel")
    };
    constexpr RpcDescriptor unrelDesc{
        "Unrel", RpcTarget::Server, RpcReliability::Unreliable,
        detail::rpcHash("Unrel")
    };

    auto makeHandler = [](int& counter) {
        return [&counter](const uint8_t*, size_t) { ++counter; };
    };

    int r = 0, u = 0;
    RpcHandler rh = registerRpc(relDesc,   makeHandler(r));
    RpcHandler uh = registerRpc(unrelDesc, makeHandler(u));

    rh(nullptr, 0u);
    uh(nullptr, 0u);

    EXPECT_EQ(r, 1);
    EXPECT_EQ(u, 1);
}

# Task #79 — TD-14: ENGINE_RPC Compile-Time Validation

**Phase 9 — networking — Version 0.9.x**
**Audience:** Networking Lead
**Severity:** 🟡 Medium — signature mismatch is a silent runtime crash
**Depends on:** Nothing

---

## 1. Goal

The `ENGINE_RPC` macro registers an RPC handler but does not verify that the function signature matches the expected prototype. A mismatch compiles silently and crashes at runtime when the handler is called. Replace the macro with a template function that validates the signature at compile time.

---

## 2. Current State

**File:** `src/networking/public/networking/RPC.h`

```cpp
// Current (macro-based):
#define ENGINE_RPC(name, reliability, target, fn) \
    RpcRegistry::instance().registerHandler(#name, reliability, target, \
        [](const uint8_t* data, uint32_t size) { fn(data, size); })
```

The lambda casts `fn` to `void(*)(const uint8_t*, uint32_t)` silently — if `fn` has a different signature, the cast is undefined behaviour.

---

## 3. registerRpc<Fn>() Template

Replace the macro with:

```cpp
// Expected handler prototype:
using RpcHandler = std::function<void(const uint8_t* data, uint32_t size)>;

template<typename Fn>
void registerRpc(std::string_view name,
                 RpcReliability  reliability,
                 RpcTarget       target,
                 Fn&&            handler)
{
    static_assert(
        std::is_invocable_v<Fn, const uint8_t*, uint32_t>,
        "ENGINE_RPC handler must be callable as void(const uint8_t*, uint32_t). "
        "Check the function signature."
    );
    static_assert(
        std::is_same_v<std::invoke_result_t<Fn, const uint8_t*, uint32_t>, void>,
        "ENGINE_RPC handler must return void."
    );
    RpcRegistry::instance().registerHandler(
        name, reliability, target,
        RpcHandler(std::forward<Fn>(handler)));
}
```

Migration:
```cpp
// Before:
ENGINE_RPC("PlayerDied", Reliable, AllClients, &FpsGame::onPlayerDied);

// After:
registerRpc("PlayerDied", Reliable, AllClients,
    [this](const uint8_t* d, uint32_t s) { onPlayerDied(d, s); });
```

---

## 4. Macro Deprecation

Keep `ENGINE_RPC` as a deprecated wrapper for one phase:

```cpp
// Deprecated — use registerRpc() instead
#define ENGINE_RPC(name, rel, tgt, fn) \
    ::engine::networking::registerRpc(name, rel, tgt, fn)
```

This allows a gradual migration without breaking existing call sites. The macro is removed in Phase 10.

---

## 5. Files to Modify

| File | Change |
|------|--------|
| `src/networking/public/networking/RPC.h` | Add `registerRpc<Fn>()` template; deprecate macro |
| `src/networking/RPC.cpp` | Update `RpcRegistry` if needed |
| Call sites in `src/app/` | Migrate from `ENGINE_RPC` to `registerRpc()` |

---

## 6. Tests

**File:** Extend `tests/networking/ReplicationTests.cpp` (label: unit)

- Register an RPC with a valid handler signature: verify no compile error.
- Register an RPC with incorrect signature: verify compile-time error (`static_assert` fires). This is a negative compile test — use a `static_assert` in a `#if 0` block with a comment explaining what to uncomment to verify.
- Call registered RPC: verify handler is invoked with correct data pointer and size.

# Networking: Architecture and Session Lifecycle

Status: Approved (Phase 2)
Owner: Networking Lead
Task: #11
References: architecture.md §4, ecs-design.md §8, networking-transport.md, scope-networking.md

---

## 1. Server vs. Client Lifecycle

### 1.1 Roles

The engine supports two network roles, selected at startup via config:

| Role | Description |
|---|---|
| `Server` | Authoritative; runs simulation; sends snapshots; accepts up to 16 clients |
| `Client` | Receives snapshots; sends input; predicts owned entities |

Both roles are supported in the same executable. Server and client can co-exist in one process for local testing (see task #29).

### 1.2 Server lifecycle

```
1. WSA startup (WinsockGuard constructor)
2. Bind UDP socket to [network].port (default 7777)
3. Enter accept loop (non-blocking, polled in FixedUpdate)
4. For each connected client: create Session slot, exchange handshake
5. Run simulation tick at 64 Hz (FixedUpdate phase)
6. After simulation: capture ECS snapshot, delta-encode per client, send
7. Receive client input messages; apply to simulation
8. On timeout or disconnect: clean up Session slot
9. On shutdown: send FIN to all connected clients, close socket
```

### 1.3 Client lifecycle

```
1. WSA startup (WinsockGuard constructor)
2. Create UDP socket (ephemeral port)
3. Send connection request to server IP:port
4. Wait for handshake response (timeout: 5 s, retry: 3 attempts)
5. On connected: receive snapshots, apply to ECS
6. Send input messages at 60 Hz (Update phase)
7. Predict owned entity movement locally (see lag comp doc)
8. On server timeout: post ConnectionEvent::TimedOut to event bus; soft-reconnect window 30 s
9. On shutdown: send FIN to server, close socket
```

---

## 2. Handshake Protocol

All handshake packets carry flag `kFlagHandshake` and are sent via the reliable channel.

### 2.1 Sequence

```
Client                         Server
  |---  ConnectionRequest  --->|   (includes client protocol version)
  |<--  ConnectionAccepted ---| or ConnectionRejected
  |---  ConnectionAcknowledge ->|
  |         (game traffic)     |
  ...
  |---  Disconnect  ---------->|   (or timeout)
```

**ConnectionRequest** payload:
```cpp
struct ConnectRequest {
    uint32_t protocolVersion;   // kProtocolVersion (bumped on breaking change)
    uint32_t obfuscationKey;    // 0 in v1 (future: DH exchange)
};
```

**ConnectionAccepted** payload:
```cpp
struct ConnectAccepted {
    uint16_t clientSlot;        // assigned slot (0–15)
    uint32_t serverTickRate;    // ticks/s (should be 30)
    uint32_t serverTime;        // current server tick
};
```

**ConnectionRejected** payload:
```cpp
struct ConnectRejected {
    uint8_t reason;   // kRejectFull=0, kRejectVersionMismatch=1, kRejectBanned=2
};
```

After `ConnectionAcknowledge`, the connection is live. The XOR obfuscation key (if non-zero in future versions) is derived from `obfuscationKey` during the accept step; `xorObfuscate` is then applied to all subsequent packets.

---

## 3. Tick Model and ECS Phase Integration

### 3.1 Tick rates

| Entity | Rate | Phase |
|---|---|---|
| Server simulation tick | 64 Hz | `FixedUpdate` |
| Client input send | at render rate | `Update` (every frame) |
| Client snapshot apply | 64 Hz | `FixedUpdate` (when new snapshot arrives) |

The server's `FixedUpdate` runs at exactly 64 Hz, driven by `core::time::FixedTimestep` (not wall-clock drift). The fixed timestep accumulator handles frames that take longer than ~15.6 ms by running multiple ticks.

### 3.2 System registration order (networking systems)

Registered by `Session::bootstrap()`, called from `app::BootstrapOrder`:

**Server-side:**
```
FixedUpdate phase:
  1. NetworkReceive       — drain socket receive buffer, dispatch messages
  2. [gameplay systems]   — owned by app
  3. NetworkSnapshotCapture — read ECS, encode deltas, send to each client
```

**Client-side:**
```
Update phase:
  1. NetworkReceive       — drain socket receive buffer, apply snapshots
  2. NetworkInputSend     — sample input state, encode, send to server

FixedUpdate phase:
  1. ClientPredictionApply — apply input to owned entities for local simulation
```

The exact registration index within each phase is determined by `app::BootstrapOrder.cpp`, not by the networking module. The networking module exposes the system function pointers; `app` calls `world.addSystem(...)` with the order enforced by its single registration function.

### 3.3 Snapshot capture point

Snapshots are captured at the **end of `LateUpdate`**, before the `Render` phase. This means the snapshot reflects the final committed state of the simulation for that tick, including any `LateUpdate` transform finalization. The renderer reads transforms from this same state (via a read-only copy; see ecs-design.md §11).

---

## 4. Connection Manager

### 4.1 Connection slots

```cpp
constexpr uint16_t kMaxConnections = 16;

struct Connection {
    Endpoint       remoteEndpoint;
    uint16_t       slot;
    ConnectionState state;   // enum: Disconnected, Connecting, Connected, TimingOut
    AckState        ackState;
    uint32_t        lastRecvTimeMs;
    uint32_t        lastSendTimeMs;
    ReliableQueue   reliableQueue;     // from pool allocator
    ReassemblyTable fragmentTable;     // from pool allocator
};
```

### 4.2 Timeout and keep-alive

- If no packet is received from a remote for `kTimeoutMs = 5000` ms, the connection is marked timed-out and a `ConnectionEvent::TimedOut` is posted on the event bus.
- If no packet has been sent for `kKeepAliveMs = 1000` ms, send a minimal heartbeat packet (header only, no payload).
- Soft-reconnect: on receiving any packet from a timed-out connection within `kSoftReconnectWindowMs = 30000` ms, restore the connection in `Connected` state (slot is held open).

### 4.3 WinsockGuard

```cpp
class WinsockGuard {
public:
    WinsockGuard();   // WSAStartup; aborts on failure
    ~WinsockGuard();  // WSACleanup
    ENGINE_NO_COPY(WinsockGuard);
    ENGINE_NO_MOVE(WinsockGuard);
};
```

One `WinsockGuard` is owned by `networking::Session` and lives for the session's lifetime. `WSACleanup` is called exactly once on destruction.

---

## 5. Socket Wrapper

### 5.1 Endpoint

```cpp
struct Endpoint {
    uint8_t  addr[16];    // 4 bytes for IPv4 (in IPv4-mapped IPv6 form), 16 for IPv6
    uint16_t port;
    bool     isIPv6;

    static Endpoint fromString(std::string_view addrPort);   // "192.0.2.1:7777" or "[::1]:7777"
    std::string toString() const;
};
```

Parsing and formatting are symmetric (round-trip stable).

### 5.2 Socket

```cpp
class Socket {
public:
    static Socket createUdp(bool dualStack = true);
    void bind(uint16_t port);              // 0 = ephemeral

    // Non-blocking (O_NONBLOCK / FIONBIO set at creation)
    bool     send(const Endpoint& to, std::span<const uint8_t> data);
    bool     recv(Endpoint& from, std::span<uint8_t> buf, uint32_t& bytesOut);  // false if WOULD_BLOCK

    ENGINE_NO_COPY(Socket);
    Socket(Socket&&) noexcept;
    Socket& operator=(Socket&&) noexcept;
    ~Socket();   // closesocket()
private:
    SOCKET socket_ { INVALID_SOCKET };
};
```

No Winsock types appear in `Socket.h`'s public surface (`SOCKET` is `uintptr_t` on Win64 and is hidden behind `SOCKET socket_` in the private impl or in an opaque pimpl). If `SOCKET` must be stored as a member in the public header, use `#ifndef _WINSOCK2API_` guarding as a fallback; prefer pimpl.

All Winsock calls go through `ENGINE_HR`-equivalent Winsock error checking: a `checkWsaError(int result)` helper in the internal implementation that logs `WSAGetLastError()` and returns a `Result<>`.

---

## 6. Entity Snapshot Sync

### 6.1 Replicated components

A component is replicated if `world.hasComponent<NetReplicated>(entity)`. The `NetReplicated` flag component (zero-size marker) is registered by the networking module:

```cpp
struct NetReplicated {};   // marker; zero bytes on the wire
struct NetOwner { uint16_t clientSlot; };  // which client owns this entity
```

Only components with a registered `serialize` function pointer (from ECS reflection, ecs-design.md §10) are included in the snapshot. The networking module iterates `View<Transform, NetReplicated>()` plus any other component type that has a `serialize` function.

**Interest management in v1**: All replicated entities are sent to all connected clients. No visibility culling.

### 6.2 Snapshot structure

```
SnapshotHeader:
  serverTick   : uint32_t
  baselineTick : uint32_t   (0 = full snapshot; client's last acked tick otherwise)
  entityCount  : uint16_t

Per entity (delta):
  entityIndex  : varint
  componentMask: varint     (bitfield of which components changed)
  Per changed component:
    componentTypeId : varint
    componentBytes  : raw bytes (from serialize function pointer)
```

The snapshot is built into a `BitWriter` (see networking-transport.md §5). If the encoded snapshot exceeds `kMaxFragmentPayload`, it is fragmented.

### 6.3 Per-client baseline

Each connected client slot tracks `baselineTick` and a baseline copy of each replicated component's last-sent value. The snapshot system computes a delta against this baseline. Unchanged components are not included in the delta.

Baseline acknowledgment: the client sends a `SnapshotAck{tick}` reliable message when it applies a snapshot. The server advances the baseline for that client.

### 6.4 Client application of snapshots

On the client, when a snapshot arrives:
1. Apply non-owned entity updates directly to ECS.
2. For owned entities: compare server position with locally predicted position. If divergence > `kReconcileThreshold` (0.1 m), apply server correction and re-simulate from that point (see lag comp doc for rollback details).

---

## 7. Event Bus Integration

The networking module posts the following events to `core::events::EventBus`:

```cpp
namespace core::events {
    struct ConnectionEstablished { uint16_t clientSlot; Endpoint remote; };
    struct ConnectionLost        { uint16_t clientSlot; Endpoint remote; uint8_t reason; };
    struct ConnectionTimedOut    { uint16_t clientSlot; };
}
```

The session loop subscribes to none — it does not subscribe to its own events. App-layer systems subscribe to react to connection changes (e.g., spawning/despawning the player entity).

# Scope: Networking Lead

Status: Approved (Phase 1)
Owner: Team Leader (this doc); Networking Lead (the work it scopes).
References: `architecture.md`, `module-structure.md`, `ecs-design.md`, `coding-standards.md`.

This is the binding scope for the Networking Lead through Phases 2–4.

---

## What you own

The `engine_networking` static library and everything under `engine/src/networking/`.

In particular:

1. **Winsock2 socket wrapper.** WSA startup/teardown, blocking and non-blocking UDP sockets, dual-stack IPv4/IPv6 endpoints, optional TCP (handshake only).
2. **Transport layer over UDP.** Packet framing, sequence numbers, ack bitfield, optional reliable delivery for marked messages, MTU-aware fragmentation (≤ 1200 bytes per fragment, 1.4 KB MTU floor).
3. **Packet serializer / deserializer.** Bit-level writer/reader with varints, quaternion compression, range-quantized floats. Endianness: little-endian on the wire (we're Windows-only).
4. **Connection manager.** Handshake, keep-alive, timeout (5 s default), graceful disconnect, soft-reconnect within a 30 s window.
5. **Session loop.** Server-tick at 30 Hz, client send-rate at 30 Hz, with input messages allowed at 60 Hz.
6. **Entity snapshot sync.** Server-authoritative replication of ECS components flagged `NetReplicated`. Delta encoding against per-client baselines.
7. **Lag compensation** for hit registration. Server rewinds entity positions to the client's render time when validating a hitscan event.
8. **Phase 4 integration:** multiplayer stress test (16 clients, 30 Hz, 5 minutes, 0 unexpected disconnects) and reconnect handling (task #41).

## What you do NOT own

- **Game logic on top of replication.** What the components mean is gameplay; you only move the bytes.
- **Matchmaking, lobby, account services.** Out of v1 scope. Connections are made directly by IP:port for now.
- **Encryption / DTLS / authentication at scale.** v1 ships with optional pre-shared-key XOR-obfuscation hook but no real cryptography. Document the hook for future replacement.
- **NAT punching, STUN/TURN, relay servers.** Out of v1 scope.
- **Voice chat.** Out of v1 scope.
- **Web / browser clients.** Out of v1 scope.
- **The ECS, math, allocators, file I/O.** Use what `core` provides.
- **The logger, config, profiler.** Use `LOG_*`, `tools::Config`, `tools::Profiler`.
- **Compression beyond what serializer-level quantization gives you.** No LZ4/zstd in v1.

## Dependencies on other modules

| You depend on | For | Owner |
|---|---|---|
| `core::math` | Vec3, Quat for serialization helpers | Team Leader, task #17 |
| `core::memory` | Pool allocator for in-flight packets, ring buffer for ack history | Team Leader, task #18 |
| `core::ecs` | Iterating `NetReplicated` entities; reading and writing component bytes via reflection | Team Leader, task #19 |
| `core::events` | Posting `ConnectionEvent` (connected, disconnected, timed-out) | Team Leader, task #20 |
| `core::fs` | Loading server config and replay files (if any) | Team Leader, task #21 |
| `core::time::Clock` | Authoritative tick timing; do **not** call `QueryPerformanceCounter` directly | Team Leader |
| `tools::Logger` | All log output, including per-packet trace at `Trace` level | Tools Lead |
| `tools::Config` | Reading `[network]` section (port, tick rate, timeout overrides) | Tools Lead |
| Component reflection's `serialize` | Per-component on-the-wire representation | Each component's owning module |

## Phase 2 deliverables (scope/design docs)

You will produce three design documents under `engine/docs/`. These are written before any code beyond a skeleton `CMakeLists.txt` is committed to `engine_networking`.

| Task | Deliverable | Required content |
|---|---|---|
| #10 | `docs/networking-transport.md` | Packet header layout; sequence/ack/ack-bitfield; reliable channel design; fragmentation; MTU strategy; per-connection bandwidth budget; handling out-of-order and duplicates; serializer API including bit-level writer, varints, quat compression (smallest-three), float quantization helpers. |
| #11 | `docs/networking-architecture.md` | Server vs. client lifecycle; tick model and how it interacts with ECS phases (`FixedUpdate` is the simulation tick that the server replicates after); snapshot capture point (end of `LateUpdate`, before render); per-client baseline tracking; entity scope rules (interest management is "all entities" in v1, document so it's clear it's not "interest-managed"); component visibility rules. |
| #12 | `docs/networking-lag-compensation.md` | Client-side prediction policy (predict only entities owned by this client); server reconciliation message format; lag-compensation rewind window (200 ms default, configurable); how a hit-validation message is structured; cap on rewind to avoid abuse. |

Each design doc is reviewed and approved by the Team Leader before implementation tasks (#27–30) start.

## Phase 3 deliverables (code)

In ID order: tasks #27, #28, #29, #30.

Definitions of done:

- **#27 — Winsock2 socket wrapper.** Loopback test sends and receives a packet; WSA cleanup is correct (no leaked handles in PIX or app verifier). Endpoint formatting (`192.0.2.1:7777` and IPv6 bracket form) is symmetric with parsing.
- **#28 — Packet serializer/deserializer.** Round-trip tests pass for every supported primitive and compressed type. Sequence number wrap is handled. Fuzz harness (input random bytes, expect either a valid parse or a clean rejection — never UB) runs in CI for 30 s.
- **#29 — Connection manager and session loop.** A server accepts up to 16 connections; clients connect, exchange a heartbeat for 60 s, disconnect cleanly. Loopback integration test in `tests/networking/`.
- **#30 — Entity snapshot sync and lag comp.** A 2-client server replicates a `Transform`+`Velocity` entity; clients see the entity moving smoothly. A hitscan message validates against a 100 ms-rewound server state.

## Public API constraints

- No `<winsock2.h>`, no `SOCKET`, no `sockaddr_in` in public headers. Wrap them. `Endpoint`, `Socket`, `Address` are your public types.
- All public types are RAII; sockets close on destruction; `WSACleanup` is reference-counted via a `WinsockGuard` owned by the engine bootstrap.
- The `NetReplicated` component is registered by your module's bootstrap, but its definition lives in `networking/Replication.h` and is included by other modules that want to mark entities replicated.
- Constants use `kPascalCase`: `kDefaultTickRate = 30`, `kMaxFragmentSize = 1200`.

## Wire-format conventions

- **Endianness:** little-endian on the wire. Byte-level helpers in `Serializer.h` document this.
- **Quaternion compression:** smallest-three with 9-bit components and 2-bit "which is missing" header → 32 bits total per quat.
- **Position quantization:** configurable per-component fixed-point with default 1 mm precision over a ±2048 m world (24 bits per axis = 72 bits per Vec3).
- **Time:** server tick is a `uint32_t` tick counter, wrap-aware at the connection level. No wall-clock time on the wire.

## Determinism and ordering

- The simulation tick is deterministic by ECS-system-order rules (see `ecs-design.md` §8). Networking depends on this; do not introduce out-of-order writes to ECS components from networking systems.
- Networking systems are registered in the `FixedUpdate` phase (server-side simulate + collect snapshot) and `Update` phase (client-side apply + interpolate). Document the exact registration in your architecture design doc.

## Security posture

- Treat every received byte as hostile. Bounds-check before reading. The fuzz harness in #28 is required, not optional.
- No allocations driven by counts read from the wire without an explicit cap. Log and drop oversized packets.
- The XOR-obfuscation hook is not security; the design doc must say so explicitly so that future-you doesn't ship it as if it were.

## Performance targets (v1 baseline)

| Metric | Target |
|---|---|
| Snapshot encode for 1k replicated entities | ≤ 1.0 ms server-side |
| Snapshot decode | ≤ 0.5 ms client-side |
| Bandwidth per client at 30 Hz, 1k entities | ≤ 200 KB/s |
| Loopback round-trip | ≤ 1 ms |

These are aspirational baselines for the test lead's benchmarks (task #38).

## Communication

- Coordinate with the Team Leader on `NetReplicated` component registration and serialization-function plumbing. This is the highest-friction interface between your module and the ECS.
- Coordinate with the Tools Lead on the `[network]` config section schema.

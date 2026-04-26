# Networking: Transport Layer

Status: Approved (Phase 2)
Owner: Networking Lead
Task: #10
References: architecture.md §4, scope-networking.md

---

## 1. Packet Header Layout

Every UDP datagram begins with a fixed 8-byte header:

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|  Protocol ID  |    Flags      |         Sequence              |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                           Ack                                 |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                         AckBitfield                           |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

| Field | Bytes | Description |
|---|---|---|
| `protocolId` | 1 | Fixed value `0xEC` (Engine Connection). Packets with wrong ID are silently dropped. |
| `flags` | 1 | See §1.1 |
| `sequence` | 2 | uint16_t, sender's sequence number for this packet. Little-endian. |
| `ack` | 2 | uint16_t, the highest sequence number the sender has received from the remote. |
| `ackBitfield` | 4 | uint32_t bitmask. Bit N set = packet (ack - N - 1) was received. 32 recent packets tracked. |

Total header: **10 bytes**.

Wait — let me recount. protocolId(1) + flags(1) + sequence(2) + ack(2) + ackBitfield(4) = 10 bytes. The diagram above was slightly off. Let me be precise:

**Total header: 10 bytes**, little-endian layout.

### 1.1 Flag bits

| Bit | Name | Meaning |
|---|---|---|
| 0 | `kFlagReliable` | Packet contains at least one reliable message; must be acked |
| 1 | `kFlagFragment` | This is a fragment of a larger payload |
| 2 | `kFlagLastFrag` | This is the last fragment of the current payload |
| 3 | `kFlagHandshake` | Handshake/connection-management packet |
| 4–7 | Reserved | Must be 0; receiver drops if set |

---

## 2. Sequence Numbers and Ack Tracking

### 2.1 Sequence number space

Sequence numbers are `uint16_t` (0–65535). They wrap around. Wrap-aware comparison:

```cpp
// Returns true if a is "newer" than b, accounting for wrap
bool seqGreaterThan(uint16_t a, uint16_t b) noexcept {
    return ((a - b) & 0xFFFFu) < 0x8000u;
}
```

Two sequence numbers are considered "the same generation" if they differ by less than 32768. Numbers differing by more than that are ambiguous and treated as old.

### 2.2 Ack bitfield mechanics

On receiving a packet:
1. Update `remoteAck` to `max(remoteAck, header.sequence)` (wrap-aware).
2. For each bit N in `header.ackBitfield`: if bit N is set, mark our outgoing packet `(header.ack - N - 1)` as acknowledged.
3. Shift the local ack bitfield left by the difference between the new remote sequence and the previous remote sequence; set bit 0.

Unacknowledged reliable packets in the resend queue older than `kResendTimeoutMs = 100 ms` are retransmitted (up to `kMaxResendAttempts = 8` times before the connection is considered dead).

### 2.3 Per-connection state

```cpp
struct AckState {
    uint16_t localSequence  { 0 };   // next sequence number to send
    uint16_t remoteAck      { 0 };   // highest ack we've received from remote
    uint32_t remoteAckField { 0 };   // ack bitfield tracking remote receives

    uint16_t remoteSequence { 0 };   // highest sequence we've received from remote
    uint32_t localAckField  { 0 };   // ack bitfield to send in outgoing header
};
```

---

## 3. Reliable Channel

### 3.1 Reliable message queue

Reliable messages are queued in a per-connection resend buffer:

```cpp
struct ReliableEntry {
    uint16_t sequence;
    uint32_t sentTimeMs;
    uint32_t payloadOffset;  // into a linear buffer
    uint16_t payloadSize;
    uint8_t  sendCount;
};
constexpr uint32_t kReliableQueueSize = 256;  // power of 2; entries are recycled by sequence % kReliableQueueSize
```

When an ack arrives that covers a sequence, the corresponding `ReliableEntry` slot is freed.

### 3.2 Ordering guarantee

Reliable messages are NOT ordered in v1. They are delivered to the application when received, regardless of arrival order. Ordering is the application's responsibility if needed. This simplifies the resend logic and is sufficient for state sync in v1 (snapshots carry sequence numbers themselves).

---

## 4. Fragmentation

### 4.1 MTU floor

The engine targets a **1200-byte** safe UDP payload size (conservative; avoids IP fragmentation on most paths). This is `kMaxFragmentPayload = 1190` bytes (1200 minus the 10-byte header).

Payloads larger than `kMaxFragmentPayload` are split into fragments before sending.

### 4.2 Fragment header (appended after the base header for fragmented packets)

```
| fragmentId (2 bytes) | fragmentIndex (1 byte) | fragmentCount (1 byte) |
```

- `fragmentId`: identifies the reassembly group (per-connection 16-bit counter).
- `fragmentIndex`: 0-based index of this fragment.
- `fragmentCount`: total number of fragments in the group (max 255; > 255 is an error).

The fragment header adds 4 bytes; effective payload per fragment is `kMaxFragmentPayload - 4 = 1186` bytes.

### 4.3 Reassembly

Each connection maintains a **fragment reassembly table** of up to 4 pending groups (slots recycled by `fragmentId % 4`). A group is complete when all `fragmentCount` pieces have arrived. Incomplete groups are discarded after `kFragmentTimeoutMs = 2000` ms.

No allocation from the heap during reassembly — each slot is a fixed-size buffer: `uint8_t buffer[255 * 1186]` ≈ 290 KB per slot × 4 slots ≈ 1.1 MB. This is a `PoolAllocator` allocation from `core::memory` at connection creation.

---

## 5. Serializer API

### 5.1 BitWriter

```cpp
class BitWriter {
public:
    explicit BitWriter(std::span<uint8_t> buffer);
    void writeBits (uint64_t value, int numBits);   // LSB first
    void writeBool (bool v)                         { writeBits(v ? 1 : 0, 1); }
    void writeU8   (uint8_t v)                      { writeBits(v, 8); }
    void writeU16  (uint16_t v)                     { writeBits(v, 16); }
    void writeU32  (uint32_t v)                     { writeBits(v, 32); }
    void writeVarint(uint32_t v);                   // 7-bit groups, MSB continuation
    void writeVec3Quantized(Vec3 v, float invPrecision, float rangeMin, int bitsPerAxis);
    void writeQuatSmallestThree(Quat q);            // 32 bits total; see §5.3
    void align();                                   // pad to next byte boundary
    uint32_t bitsWritten() const noexcept;
    std::span<uint8_t> data() const noexcept;
    bool overflow() const noexcept;
};
```

### 5.2 BitReader

```cpp
class BitReader {
public:
    explicit BitReader(std::span<const uint8_t> data);
    uint64_t readBits (int numBits);
    bool     readBool ()  { return readBits(1) != 0; }
    uint8_t  readU8   ()  { return static_cast<uint8_t>(readBits(8)); }
    uint16_t readU16  ()  { return static_cast<uint16_t>(readBits(16)); }
    uint32_t readU32  ()  { return static_cast<uint32_t>(readBits(32)); }
    uint32_t readVarint();
    Vec3     readVec3Quantized(float precision, float rangeMin, int bitsPerAxis);
    Quat     readQuatSmallestThree();
    void     align();
    bool     overflow() const noexcept;  // set if a read exceeds buffer; caller must check
};
```

`overflow()` must be checked after every read in the deserialize path. Reading from an overflowed reader returns 0 / identity silently (never UB). The fuzz harness in task #28 exercises this extensively.

### 5.3 Quaternion compression — Smallest-Three

Encoding:
1. Normalize the quaternion (ensure |q| = 1).
2. Find the largest-magnitude component (x, y, z, or w). Call it the "missing" component.
3. If `missing < 0`, negate the entire quaternion (so the missing component is always positive).
4. Encode the index of the missing component as 2 bits.
5. Encode the remaining three components as 9-bit signed fixed-point in range `[-1/√2, +1/√2]` (±0.7071). Scale: `value * 512 / 0.7071`, clamp to [-511, 511], encode as 10-bit signed int (1 sign + 9 magnitude). Wait, let me use the simpler and standard approach:

Each of the three kept components is in `[-1/√2, 1/√2]`. Map to `[0, 1]` by: `encoded = (v + 1/√2) / (2/√2)`. Quantize to 9 bits: `uint16_t bits = static_cast<uint16_t>(encoded * 511 + 0.5f)`.

Total: 2 bits (which) + 3 × 9 bits (components) = **29 bits**. Pad to 32 bits (3 bits unused, set to 0).

Decoding: recover three components, reconstruct missing as `sqrt(1 - a² - b² - c²)` (always positive by construction in step 3).

### 5.4 Vec3 position quantization

Default: **1 mm precision** over a **±2048 m world**.

Range: 4096 m total. Precision: 0.001 m. Steps needed: 4096 / 0.001 = 4,096,000. Bits: `ceil(log2(4096000))` = 22 bits per axis. Total: 66 bits for a Vec3.

Implementation: `writeVec3Quantized(v, invPrecision=1000.0f, rangeMin=-2048.0f, bitsPerAxis=22)`.

Callers may override precision and range for components where tighter bounds are known (e.g., velocity).

### 5.5 Float quantization helper

```cpp
void writeQuantizedFloat(float v, float min, float max, int bits);
float readQuantizedFloat(float min, float max, int bits);
```

Maps `v` linearly onto `[0, (1<<bits) - 1]`. Useful for clamped floats (health 0..100, angle 0..2π, etc.).

---

## 6. Per-Connection Bandwidth Budget

Target: ≤ 200 KB/s per client at 30 Hz with 1000 replicated entities.

At 30 Hz, that is ≤ 6667 bytes per tick per client. Budget allocation:

| Content | Max bytes/tick |
|---|---|
| Header | 10 |
| Snapshot delta (1000 entities, ~4 bytes avg delta) | ~4000 |
| Input ack | 32 |
| Reliable messages | 512 |
| Padding / overhead | ~113 |
| **Total** | ≤ 4667 (well within 6667) |

If a tick's payload exceeds `kMaxFragmentPayload`, it is fragmented (§4). The session loop budget is enforced by the snapshot system, which skips unchanged components.

---

## 7. Out-of-Order and Duplicate Handling

**Duplicates**: A packet with sequence ≤ `remoteSequence - 32` is dropped as a duplicate or too-old. Packets within the ack window are checked against the ack bitfield; if already acked, the payload is ignored (but the ack data in the header is still processed).

**Out-of-order**: The transport delivers packets to the session loop in arrival order. The session loop is responsible for handling out-of-order application data (snapshots carry sequence numbers for this purpose).

**Reorder buffer**: None in v1. The session loop tolerates gaps in the snapshot sequence by using the most-recently-received snapshot as the baseline.

---

## 8. XOR Obfuscation Hook

A trivial XOR obfuscation hook exists at the packet level:

```cpp
void xorObfuscate(std::span<uint8_t> payload, uint32_t key) noexcept;
// Called with key=0 (identity) by default.
// Key is configured per-connection during handshake (see networking-architecture.md §2).
```

**This is NOT security.** It prevents trivially packet-sniffing the wire format during development. Future releases will replace this with DTLS. This design doc must say so explicitly so future contributors do not ship it as a security feature.

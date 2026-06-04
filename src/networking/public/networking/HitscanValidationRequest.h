#pragma once
#include <networking/BitWriter.h>
#include <networking/BitReader.h>
#include <core/math/Vec.h>
#include <cstdint>

namespace engine::networking {

// Client → server claim that a hitscan shot connected. Sent on the reliable
// channel so the server always processes the hit even under packet loss.
// `clientTick` is carried from the start so the wire format is stable once
// lag-compensation rewind is added; Phase 7 validates against current state.
struct HitscanValidationRequest {
    uint32_t                 fireSerial  = 0u;  // dedup key; matches InputFrame.fireSerial
    uint32_t                 clientTick  = 0u;  // tick at which the client fired
    engine::core::math::Vec3 origin      = engine::core::math::Vec3::zero();
    engine::core::math::Vec3 direction   = engine::core::math::Vec3::zero();  // normalized
    uint32_t                 targetNetId = 0u;  // client's claimed hit target

    // 36-byte wire layout: fireSerial(4) clientTick(4) origin(12) direction(12) targetNetId(4)
    static constexpr size_t kWireSize = 36u;

    void serialize(ByteWriter& bw) const {
        bw.writeU32(fireSerial);
        bw.writeU32(clientTick);
        bw.writeF32(origin.x);    bw.writeF32(origin.y);    bw.writeF32(origin.z);
        bw.writeF32(direction.x); bw.writeF32(direction.y); bw.writeF32(direction.z);
        bw.writeU32(targetNetId);
    }

    static HitscanValidationRequest deserialize(ByteReader& br) {
        HitscanValidationRequest r;
        r.fireSerial  = br.readU32();
        r.clientTick  = br.readU32();
        r.origin    = { br.readF32(), br.readF32(), br.readF32() };
        r.direction = { br.readF32(), br.readF32(), br.readF32() };
        r.targetNetId = br.readU32();
        return r;
    }
};

} // namespace engine::networking

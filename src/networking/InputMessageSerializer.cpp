#include <networking/InputMessageSerializer.h>

namespace engine::networking {

void writeInputMessage(BitWriter& writer, const InputMessage& msg) noexcept {
    writer.writeU32(msg.tick);
    writer.writeI8(msg.moveX);
    writer.writeI8(msg.moveZ);
    writer.writeI16(msg.yawDelta);
    writer.writeI16(msg.pitchDelta);
    writer.writeU8(msg.buttons);
    writer.writeU8(msg.fireSerial);
}

InputMessage readInputMessage(BitReader& reader) noexcept {
    InputMessage msg;
    msg.tick        = reader.readU32();
    msg.moveX       = reader.readI8();
    msg.moveZ       = reader.readI8();
    msg.yawDelta    = reader.readI16();
    msg.pitchDelta  = reader.readI16();
    msg.buttons     = reader.readU8();
    msg.fireSerial  = reader.readU8();
    return msg;
}

} // namespace engine::networking

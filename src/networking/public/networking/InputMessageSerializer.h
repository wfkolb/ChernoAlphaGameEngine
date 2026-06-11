#pragma once
#include <networking/InputMessage.h>
#include <networking/Serializer.h>

namespace engine::networking {

// Serialize an InputMessage into a BitWriter.
void writeInputMessage(BitWriter& writer, const InputMessage& msg) noexcept;

// Deserialize an InputMessage from a BitReader.
InputMessage readInputMessage(BitReader& reader) noexcept;

} // namespace engine::networking

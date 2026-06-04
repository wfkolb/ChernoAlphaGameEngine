#pragma once
#include <networking/BitWriter.h>
#include <networking/BitReader.h>

namespace engine::app {

// App-layer aliases for the networking bit-stream types.
// Used by IGameMode::serializeState / deserializeState.
using BitStreamWriter = networking::ByteWriter;
using BitStreamReader = networking::ByteReader;

} // namespace engine::app

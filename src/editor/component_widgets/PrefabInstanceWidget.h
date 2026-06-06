#pragma once
#ifdef ENGINE_DEVREL

#include <core/ecs/PrefabInstance.h>

namespace engine::editor {

// Returns true if any field was modified.
bool drawPrefabInstanceWidget(core::ecs::PrefabInstance& instance);

} // namespace engine::editor

#endif // ENGINE_DEVREL

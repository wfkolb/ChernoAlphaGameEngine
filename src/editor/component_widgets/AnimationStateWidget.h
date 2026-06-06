#pragma once
#ifdef ENGINE_DEVREL

#include <core/components/AnimationState.h>

namespace engine::editor {

// Returns true if any field was modified.
bool drawAnimationStateWidget(core::AnimationState& state);

} // namespace engine::editor

#endif // ENGINE_DEVREL

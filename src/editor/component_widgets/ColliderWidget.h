#pragma once
#ifdef ENGINE_DEVREL

namespace engine::core { struct ColliderComponent; }

namespace engine::editor {

// Returns true if the component was modified this frame.
bool drawColliderWidget(engine::core::ColliderComponent& collider);

} // namespace engine::editor

#endif // ENGINE_DEVREL

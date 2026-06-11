#pragma once
#ifdef ENGINE_DEVREL

#include <functional>

namespace engine::editor {

// Widget function: receives raw component pointer, returns true if value was mutated.
using ComponentWidget = std::function<bool(void*)>;

// Specialize this template for each component type to bundle a display name and
// a widget factory. Specializations live in EditorApp.cpp alongside the widget
// lambdas. The primary template signals "no widget" by returning an empty function;
// validateCoverage() will ENGINE_ASSERT for any ECS-registered component whose
// specialization still returns an empty function.
//
// Full compile-time type-list registration is deferred to Phase 10.
template <typename T>
struct ComponentTraits {
    // nullptr = fall back to the name from ComponentMeta (the default).
    static constexpr const char* displayName = nullptr;
    // Empty return = no widget registered for T.
    static ComponentWidget makeWidget() { return {}; }
};

} // namespace engine::editor

#endif // ENGINE_DEVREL

#pragma once
#include <core/components/MeshHandle.h>
namespace engine::rendering {
    // The ECS renderable component (id=12) is authored in core but owned
    // by the renderer. This alias gives callers a semantically correct name.
    using RenderableComponent = core::MeshHandle;
}

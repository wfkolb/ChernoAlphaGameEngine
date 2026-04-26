#pragma once
#include "rendering/FrameGraph.h"
#include "rendering/Mesh.h"
#include <cstdint>

namespace engine::rendering {

class  MeshManager;
struct FlatShadePipeline;

// Add a flat-shade draw pass to the frame graph for the given mesh.
// fg         : frame graph being recorded for this frame.
// mm         : mesh manager; provides vertexBufferView, indexBufferView, indexCount.
// mesh       : handle identifying the mesh within mm.
// pipeline   : compiled FlatShadePipeline (root sig + PSO).
// mvp        : 16 floats, row-major World*View*Proj, caller owns storage.
// backBuffer : resource handle returned by fg.importBackBuffer().
// depthBuffer: resource handle returned by fg.importDepthBuffer().
// width      : render target width in pixels (for viewport and scissor).
// height     : render target height in pixels (for viewport and scissor).
void addFlatShadePass(
    FrameGraph&              fg,
    const MeshManager&       mm,
    MeshHandle               mesh,
    const FlatShadePipeline& pipeline,
    const float              mvp[16],
    ResourceHandle           backBuffer,
    ResourceHandle           depthBuffer,
    uint32_t                 width,
    uint32_t                 height);

} // namespace engine::rendering

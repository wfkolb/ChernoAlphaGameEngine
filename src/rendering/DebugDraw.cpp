#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d12.h>
#include <wrl/client.h>

#include <rendering/DebugDraw.h>
#include <core/math/Vec.h>
#include <core/math/Quat.h>
#include <core/math/AABB.h>
#include <core/log.h>
#include <core/diag/Assert.h>

#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdint>

namespace engine::rendering::DebugDraw {

namespace {

    // ---------------------------------------------------------------------------
    // GPU-side flat vertex layout — must match DebugLineVS.hlsl VSIn.
    // position: float xyz[3], packedColor: R8G8B8A8_UNORM uint32.
    // ---------------------------------------------------------------------------
    struct DebugVertex {
        float    xyz[3];
        uint32_t packedColor; // R8G8B8A8_UNORM
    };

    struct LineEntry {
        core::math::Vec3 from;
        core::math::Vec3 to;
        core::math::Vec4 color;
    };

    struct TextEntry {
        core::math::Vec3 worldPos;
        core::math::Vec4 color;
        std::string      str;
    };

    constexpr uint32_t kMaxTextLabels = 256;

    std::vector<LineEntry>   gLines;
    std::vector<TextEntry>   gText;
    uint32_t                 gMaxPrimitives  = 65536;
    bool                     gOverflowWarned = false;

    // Per-frame flat GPU vertex buffer, populated inside flush().
    std::vector<DebugVertex> gGpuVerts;

    // ---------------------------------------------------------------------------
    inline uint32_t packColor(const core::math::Vec4& c) noexcept
    {
        auto clamp01 = [](float v) -> float {
            return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
        };
        const uint32_t r = static_cast<uint32_t>(clamp01(c.x) * 255.0f + 0.5f);
        const uint32_t g = static_cast<uint32_t>(clamp01(c.y) * 255.0f + 0.5f);
        const uint32_t b = static_cast<uint32_t>(clamp01(c.z) * 255.0f + 0.5f);
        const uint32_t a = static_cast<uint32_t>(clamp01(c.w) * 255.0f + 0.5f);
        return (r << 0) | (g << 8) | (b << 16) | (a << 24);
    }

    void pushLine(const core::math::Vec3& from,
                  const core::math::Vec3& to,
                  const core::math::Vec4& color)
    {
        if (gLines.size() >= gMaxPrimitives) {
            if (!gOverflowWarned) {
                LOG_WARN("DebugDraw: primitive limit ({}) exceeded; excess dropped", gMaxPrimitives);
                gOverflowWarned = true;
            }
            return;
        }
        gLines.push_back({from, to, color});
    }

    // ---------------------------------------------------------------------------
    // Persistent GPU state — null until initGpu() succeeds.
    // ---------------------------------------------------------------------------
    struct DebugGpuState {
        Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSig;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
        Microsoft::WRL::ComPtr<ID3D12Resource>      uploadBuf;
        DebugVertex*                                 mapped   = nullptr;
        uint32_t                                     capacity = 0; // vertex count
    };

    DebugGpuState g_gpu;

    // Load a compiled shader object (.cso) from a path relative to the exe.
    std::vector<uint8_t> loadCso(const wchar_t* relPath)
    {
        wchar_t exePath[MAX_PATH] = {};
        DWORD len = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        if (len == 0 || len >= MAX_PATH) return {};

        wchar_t* lastSep = nullptr;
        for (wchar_t* p = exePath; *p; ++p)
            if (*p == L'\\' || *p == L'/') lastSep = p;
        if (!lastSep) return {};
        *(lastSep + 1) = L'\0';

        wchar_t full[MAX_PATH] = {};
        if (wcscat_s(full, exePath) != 0) return {};
        if (wcscat_s(full, relPath) != 0) return {};

        HANDLE h = CreateFileW(full, GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return {};

        LARGE_INTEGER sz = {};
        if (!GetFileSizeEx(h, &sz) || sz.QuadPart <= 0) { CloseHandle(h); return {}; }

        std::vector<uint8_t> data(static_cast<size_t>(sz.QuadPart));
        DWORD bytesRead = 0;
        bool ok = ReadFile(h, data.data(), static_cast<DWORD>(data.size()),
                           &bytesRead, nullptr) != FALSE
               && bytesRead == static_cast<DWORD>(data.size());
        CloseHandle(h);
        return ok ? data : std::vector<uint8_t>{};
    }

} // anonymous namespace

// ---------------------------------------------------------------------------

void init(uint32_t maxPrimitives)
{
    gMaxPrimitives = (maxPrimitives == 0) ? 65536u : maxPrimitives;
    gLines.reserve(gMaxPrimitives);
    gText.reserve(kMaxTextLabels);
    gGpuVerts.reserve(gMaxPrimitives * 2);
}

void initGpu(void* d3d12Device)
{
    if (!d3d12Device) return;

    auto vsCode = loadCso(L"shaders\\DebugLineVS.cso");
    auto psCode = loadCso(L"shaders\\DebugLinePS.cso");
    if (vsCode.empty() || psCode.empty()) {
        LOG_WARN("DebugDraw::initGpu: shaders not found — GPU draw disabled");
        return;
    }

    auto* dev = static_cast<ID3D12Device*>(d3d12Device);

    // Root signature: 1 param — 16 root constants (float4x4 viewProj) at b0, vertex-visible.
    D3D12_ROOT_PARAMETER1 param = {};
    param.ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    param.Constants.ShaderRegister = 0;
    param.Constants.RegisterSpace  = 0;
    param.Constants.Num32BitValues = 16;
    param.ShaderVisibility         = D3D12_SHADER_VISIBILITY_VERTEX;

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.Version                = D3D_ROOT_SIGNATURE_VERSION_1_1;
    rsDesc.Desc_1_1.NumParameters = 1;
    rsDesc.Desc_1_1.pParameters  = &param;
    rsDesc.Desc_1_1.Flags        =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS        |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS      |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS    |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS;

    Microsoft::WRL::ComPtr<ID3DBlob> rsBlob, rsError;
    HRESULT hr = D3D12SerializeVersionedRootSignature(&rsDesc, &rsBlob, &rsError);
    ENGINE_ASSERT(SUCCEEDED(hr), "DebugDraw: root signature serialization failed");

    hr = dev->CreateRootSignature(0,
        rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(),
        IID_PPV_ARGS(&g_gpu.rootSig));
    ENGINE_ASSERT(SUCCEEDED(hr), "DebugDraw: CreateRootSignature failed");

    // Input layout matches DebugVertex: float xyz[3] + R8G8B8A8_UNORM color (16 bytes total).
    D3D12_INPUT_ELEMENT_DESC inputElems[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM,  0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = g_gpu.rootSig.Get();
    psoDesc.VS = { vsCode.data(), vsCode.size() };
    psoDesc.PS = { psCode.data(), psCode.size() };

    psoDesc.BlendState.AlphaToCoverageEnable  = FALSE;
    psoDesc.BlendState.IndependentBlendEnable = FALSE;
    for (auto& rt : psoDesc.BlendState.RenderTarget) {
        rt.BlendEnable           = FALSE;
        rt.LogicOpEnable         = FALSE;
        rt.SrcBlend              = D3D12_BLEND_ONE;
        rt.DestBlend             = D3D12_BLEND_ZERO;
        rt.BlendOp               = D3D12_BLEND_OP_ADD;
        rt.SrcBlendAlpha         = D3D12_BLEND_ONE;
        rt.DestBlendAlpha        = D3D12_BLEND_ZERO;
        rt.BlendOpAlpha          = D3D12_BLEND_OP_ADD;
        rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    }
    psoDesc.SampleMask = UINT_MAX;

    // No back-face culling for line primitives.
    psoDesc.RasterizerState.FillMode              = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode              = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
    psoDesc.RasterizerState.DepthBias             = D3D12_DEFAULT_DEPTH_BIAS;
    psoDesc.RasterizerState.DepthBiasClamp        = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    psoDesc.RasterizerState.SlopeScaledDepthBias  = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    psoDesc.RasterizerState.DepthClipEnable       = TRUE;
    psoDesc.RasterizerState.MultisampleEnable     = FALSE;
    psoDesc.RasterizerState.AntialiasedLineEnable = FALSE;
    psoDesc.RasterizerState.ForcedSampleCount     = 0;
    psoDesc.RasterizerState.ConservativeRaster    = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    // No depth test — debug lines always draw on top of scene geometry.
    psoDesc.DepthStencilState.DepthEnable   = FALSE;
    psoDesc.DepthStencilState.StencilEnable = FALSE;

    psoDesc.InputLayout           = { inputElems, 2 };
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    psoDesc.NumRenderTargets      = 1;
    psoDesc.RTVFormats[0]         = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.DSVFormat             = DXGI_FORMAT_UNKNOWN;
    psoDesc.SampleDesc.Count      = 1;

    hr = dev->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&g_gpu.pso));
    ENGINE_ASSERT(SUCCEEDED(hr), "DebugDraw: CreateGraphicsPipelineState failed");

    // Persistent CPU-visible upload buffer for vertex data.
    const UINT64 bufBytes = static_cast<UINT64>(gMaxPrimitives) * 2 * sizeof(DebugVertex);
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width            = bufBytes;
    rd.Height           = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels        = 1;
    rd.SampleDesc.Count = 1;
    rd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    hr = dev->CreateCommittedResource(
        &hp, D3D12_HEAP_FLAG_NONE, &rd,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&g_gpu.uploadBuf));
    ENGINE_ASSERT(SUCCEEDED(hr), "DebugDraw: upload buffer alloc failed");

    D3D12_RANGE readRange{0, 0};
    hr = g_gpu.uploadBuf->Map(0, &readRange,
                               reinterpret_cast<void**>(&g_gpu.mapped));
    ENGINE_ASSERT(SUCCEEDED(hr), "DebugDraw: upload buffer map failed");

    g_gpu.capacity = gMaxPrimitives * 2;
    LOG_INFO("DebugDraw::initGpu: ready ({} vertex capacity)", g_gpu.capacity);
}

void shutdown()
{
    if (g_gpu.uploadBuf && g_gpu.mapped) {
        g_gpu.uploadBuf->Unmap(0, nullptr);
        g_gpu.mapped = nullptr;
    }
    g_gpu.uploadBuf.Reset();
    g_gpu.pso.Reset();
    g_gpu.rootSig.Reset();
    g_gpu.capacity = 0;

    gLines.clear();
    gLines.shrink_to_fit();
    gText.clear();
    gText.shrink_to_fit();
    gGpuVerts.clear();
    gGpuVerts.shrink_to_fit();
    gOverflowWarned = false;
}

void flush(void* cmdList, const float viewProj[16],
           uint64_t rtvCpuHandle, uint32_t width, uint32_t height)
{
    if (!cmdList || gLines.empty() || !g_gpu.rootSig) {
        gLines.clear();
        gText.clear();
        gOverflowWarned = false;
        return;
    }

    // Pack all line segments into the flat vertex buffer.
    gGpuVerts.clear();
    gGpuVerts.reserve(gLines.size() * 2);
    for (const auto& ln : gLines) {
        const uint32_t pc = packColor(ln.color);
        gGpuVerts.push_back({{ln.from.x, ln.from.y, ln.from.z}, pc});
        gGpuVerts.push_back({{ln.to.x,   ln.to.y,   ln.to.z  }, pc});
    }

    const UINT drawCount = static_cast<UINT>(
        std::min<size_t>(gGpuVerts.size(), g_gpu.capacity));
    std::memcpy(g_gpu.mapped, gGpuVerts.data(), drawCount * sizeof(DebugVertex));

    auto* cmd = static_cast<ID3D12GraphicsCommandList*>(cmdList);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv{ rtvCpuHandle };
    cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

    D3D12_VIEWPORT vp{};
    vp.Width    = static_cast<float>(width);
    vp.Height   = static_cast<float>(height);
    vp.MaxDepth = 1.0f;
    cmd->RSSetViewports(1, &vp);
    D3D12_RECT sr{ 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
    cmd->RSSetScissorRects(1, &sr);

    cmd->SetPipelineState(g_gpu.pso.Get());
    cmd->SetGraphicsRootSignature(g_gpu.rootSig.Get());
    cmd->SetGraphicsRoot32BitConstants(0, 16, viewProj, 0);
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);

    D3D12_VERTEX_BUFFER_VIEW vbv{};
    vbv.BufferLocation = g_gpu.uploadBuf->GetGPUVirtualAddress();
    vbv.SizeInBytes    = drawCount * sizeof(DebugVertex);
    vbv.StrideInBytes  = sizeof(DebugVertex);
    cmd->IASetVertexBuffers(0, 1, &vbv);
    cmd->DrawInstanced(drawCount, 1, 0, 0);

    gLines.clear();
    gText.clear();
    gGpuVerts.clear();
    gOverflowWarned = false;
}

// ---------------------------------------------------------------------------

void line(core::math::Vec3 start, core::math::Vec3 end, core::math::Vec4 color)
{
    pushLine(start, end, color);
}

void aabb(const core::math::AABB& b, core::math::Vec4 color)
{
    using core::math::Vec3;

    const Vec3& lo = b.min;
    const Vec3& hi = b.max;

    const Vec3 c[8] = {
        {lo.x, lo.y, lo.z},
        {hi.x, lo.y, lo.z},
        {hi.x, hi.y, lo.z},
        {lo.x, hi.y, lo.z},
        {lo.x, lo.y, hi.z},
        {hi.x, lo.y, hi.z},
        {hi.x, hi.y, hi.z},
        {lo.x, hi.y, hi.z},
    };

    pushLine(c[0], c[1], color);
    pushLine(c[1], c[2], color);
    pushLine(c[2], c[3], color);
    pushLine(c[3], c[0], color);

    pushLine(c[4], c[5], color);
    pushLine(c[5], c[6], color);
    pushLine(c[6], c[7], color);
    pushLine(c[7], c[4], color);

    pushLine(c[0], c[4], color);
    pushLine(c[1], c[5], color);
    pushLine(c[2], c[6], color);
    pushLine(c[3], c[7], color);
}

void box(core::math::Vec3 center,
         core::math::Vec3 halfExtents,
         core::math::Quat rotation,
         core::math::Vec4 color)
{
    using core::math::Vec3;
    using core::math::rotate;

    const float hx = halfExtents.x;
    const float hy = halfExtents.y;
    const float hz = halfExtents.z;

    const Vec3 local[8] = {
        {-hx, -hy, -hz},
        { hx, -hy, -hz},
        { hx,  hy, -hz},
        {-hx,  hy, -hz},
        {-hx, -hy,  hz},
        { hx, -hy,  hz},
        { hx,  hy,  hz},
        {-hx,  hy,  hz},
    };

    Vec3 c[8];
    for (int i = 0; i < 8; ++i)
        c[i] = center + rotate(rotation, local[i]);

    pushLine(c[0], c[1], color);
    pushLine(c[1], c[2], color);
    pushLine(c[2], c[3], color);
    pushLine(c[3], c[0], color);

    pushLine(c[4], c[5], color);
    pushLine(c[5], c[6], color);
    pushLine(c[6], c[7], color);
    pushLine(c[7], c[4], color);

    pushLine(c[0], c[4], color);
    pushLine(c[1], c[5], color);
    pushLine(c[2], c[6], color);
    pushLine(c[3], c[7], color);
}

void sphere(core::math::Vec3 center,
            float            radius,
            core::math::Vec4 color)
{
    constexpr int segments = 16;
    constexpr float kTwoPi = 6.28318530717958647692f;
    const float step = kTwoPi / static_cast<float>(segments);

    for (int i = 0; i < segments; ++i) {
        const float a0 = step * static_cast<float>(i);
        const float a1 = step * static_cast<float>(i + 1);
        const float c0 = std::cos(a0) * radius;
        const float s0 = std::sin(a0) * radius;
        const float c1 = std::cos(a1) * radius;
        const float s1 = std::sin(a1) * radius;

        // XY plane
        pushLine({center.x + c0, center.y + s0, center.z},
                 {center.x + c1, center.y + s1, center.z},
                 color);
        // YZ plane
        pushLine({center.x, center.y + c0, center.z + s0},
                 {center.x, center.y + c1, center.z + s1},
                 color);
        // XZ plane
        pushLine({center.x + c0, center.y, center.z + s0},
                 {center.x + c1, center.y, center.z + s1},
                 color);
    }
}

void text(core::math::Vec3  worldPos,
          std::string_view  str,
          core::math::Vec4  color)
{
    LOG_TRACE("DebugDraw::text not yet wired");
    if (gText.size() >= kMaxTextLabels)
        return;
    gText.push_back({worldPos, color, std::string(str)});
}

} // namespace engine::rendering::DebugDraw

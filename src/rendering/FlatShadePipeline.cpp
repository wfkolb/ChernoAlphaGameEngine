#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "internal/FlatShadePipeline.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <core/diag/Assert.h>
#include <cstdint>
#include <cwchar>
#include <iterator>
#include <vector>

namespace engine::rendering {

// ---------------------------------------------------------------------------
// Internal helper: load a compiled shader object (.cso) from disk.
// path is relative to the directory containing the executable.
// Returns the raw bytecode in a vector; asserts on failure.
// ---------------------------------------------------------------------------
static std::vector<uint8_t> loadCso(const wchar_t* relPath)
{
    // Build absolute path: <exe dir>\relPath
    wchar_t exePath[MAX_PATH] = {};
    DWORD len = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    ENGINE_ASSERT(len > 0 && len < MAX_PATH, "GetModuleFileNameW failed or path too long");

    // Strip the executable filename to get the directory.
    wchar_t* lastSlash = nullptr;
    for (wchar_t* p = exePath; *p; ++p) {
        if (*p == L'\\' || *p == L'/') lastSlash = p;
    }
    ENGINE_ASSERT(lastSlash != nullptr, "Executable path has no directory separator");
    *(lastSlash + 1) = L'\0';   // keep trailing slash

    // Concatenate relative path.
    wchar_t fullPath[MAX_PATH] = {};
    errno_t e = wcscat_s(fullPath, MAX_PATH, exePath);
    ENGINE_VERIFY(e == 0, "wcscat_s failed building shader path (exe dir)");
    e = wcscat_s(fullPath, MAX_PATH, relPath);
    ENGINE_VERIFY(e == 0, "wcscat_s failed building shader path (rel path)");

    HANDLE hFile = CreateFileW(
        fullPath,
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    ENGINE_VERIFY(hFile != INVALID_HANDLE_VALUE, "Failed to open .cso file");

    LARGE_INTEGER fileSize = {};
    BOOL ok = GetFileSizeEx(hFile, &fileSize);
    ENGINE_VERIFY(ok && fileSize.QuadPart > 0, "Failed to get .cso file size");

    std::vector<uint8_t> bytecode(static_cast<size_t>(fileSize.QuadPart));

    DWORD bytesRead = 0;
    ok = ReadFile(hFile, bytecode.data(), static_cast<DWORD>(bytecode.size()), &bytesRead, nullptr);
    ENGINE_VERIFY(ok && bytesRead == static_cast<DWORD>(bytecode.size()), "Failed to read .cso file");

    CloseHandle(hFile);
    return bytecode;
}

// ---------------------------------------------------------------------------

FlatShadePipeline createFlatShadePipeline(
    ID3D12Device* device,
    uint32_t      backBufferFormat,
    uint32_t      depthFormat)
{
    ENGINE_ASSERT(device != nullptr, "device must not be null");

    FlatShadePipeline result;

    // -------------------------------------------------------------------------
    // Load compiled shader bytecode
    // -------------------------------------------------------------------------
    std::vector<uint8_t> vsCode = loadCso(L"shaders\\FlatShadeVS.cso");
    std::vector<uint8_t> psCode = loadCso(L"shaders\\FlatShadePS.cso");

    // -------------------------------------------------------------------------
    // Root signature
    // Single 32-bit-constants parameter: 16 floats (MVP matrix), vertex-visible.
    // -------------------------------------------------------------------------
    D3D12_ROOT_PARAMETER1 param = {};
    param.ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    param.Constants.ShaderRegister = 0;
    param.Constants.RegisterSpace  = 0;
    param.Constants.Num32BitValues = 16;
    param.ShaderVisibility         = D3D12_SHADER_VISIBILITY_VERTEX;

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.Version                = D3D_ROOT_SIGNATURE_VERSION_1_1;
    rsDesc.Desc_1_1.NumParameters = 1;
    rsDesc.Desc_1_1.pParameters   = &param;
    rsDesc.Desc_1_1.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    Microsoft::WRL::ComPtr<ID3DBlob> rsBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> rsError;
    HRESULT hr = D3D12SerializeVersionedRootSignature(&rsDesc, &rsBlob, &rsError);
    ENGINE_ASSERT(SUCCEEDED(hr), "FlatShade root signature serialization failed");

    hr = device->CreateRootSignature(
        0,
        rsBlob->GetBufferPointer(),
        rsBlob->GetBufferSize(),
        IID_PPV_ARGS(&result.rootSignature));
    ENGINE_ASSERT(SUCCEEDED(hr), "CreateRootSignature failed for FlatShade");

    // -------------------------------------------------------------------------
    // Input layout — matches VertexStatic (28-byte stride)
    // -------------------------------------------------------------------------
    D3D12_INPUT_ELEMENT_DESC inputElements[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,   0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R10G10B10A2_UNORM, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT",  0, DXGI_FORMAT_R10G10B10A2_UNORM, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,      0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    // -------------------------------------------------------------------------
    // PSO
    // -------------------------------------------------------------------------
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = result.rootSignature.Get();

    psoDesc.VS = { vsCode.data(), vsCode.size() };
    psoDesc.PS = { psCode.data(), psCode.size() };

    // Blend: opaque (no blending)
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

    // Rasterizer: solid fill, cull back, CW front faces
    psoDesc.RasterizerState.FillMode              = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode              = D3D12_CULL_MODE_BACK;
    psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
    psoDesc.RasterizerState.DepthBias             = D3D12_DEFAULT_DEPTH_BIAS;
    psoDesc.RasterizerState.DepthBiasClamp        = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    psoDesc.RasterizerState.SlopeScaledDepthBias  = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    psoDesc.RasterizerState.DepthClipEnable       = TRUE;
    psoDesc.RasterizerState.MultisampleEnable     = FALSE;
    psoDesc.RasterizerState.AntialiasedLineEnable = FALSE;
    psoDesc.RasterizerState.ForcedSampleCount     = 0;
    psoDesc.RasterizerState.ConservativeRaster    = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    // Depth: reverse-Z, keep fragments with depth >= stored value
    psoDesc.DepthStencilState.DepthEnable    = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    psoDesc.DepthStencilState.DepthFunc      = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
    psoDesc.DepthStencilState.StencilEnable  = FALSE;

    psoDesc.InputLayout           = { inputElements, static_cast<UINT>(std::size(inputElements)) };
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0]    = static_cast<DXGI_FORMAT>(backBufferFormat);
    psoDesc.DSVFormat        = static_cast<DXGI_FORMAT>(depthFormat);

    psoDesc.SampleDesc.Count   = 1;
    psoDesc.SampleDesc.Quality = 0;

    hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&result.pso));
    ENGINE_ASSERT(SUCCEEDED(hr), "CreateGraphicsPipelineState failed for FlatShade");

    return result;
}

// ---------------------------------------------------------------------------

void bindFlatShade(
    const FlatShadePipeline&   pipeline,
    ID3D12GraphicsCommandList* cmdList,
    const void*                vbView,
    const void*                ibView,
    const float                mvp[16])
{
    cmdList->SetGraphicsRootSignature(pipeline.rootSignature.Get());
    cmdList->SetPipelineState(pipeline.pso.Get());
    cmdList->IASetVertexBuffers(0, 1, static_cast<const D3D12_VERTEX_BUFFER_VIEW*>(vbView));
    cmdList->IASetIndexBuffer(static_cast<const D3D12_INDEX_BUFFER_VIEW*>(ibView));
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->SetGraphicsRoot32BitConstants(0, 16, mvp, 0);
}

} // namespace engine::rendering

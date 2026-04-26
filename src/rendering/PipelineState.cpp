#include "internal/PipelineState.h"
#include <d3d12.h>
#include <core/diag/Assert.h>
#include <cstdint>
#include <iterator>

namespace engine::rendering {

OpaquePassPipeline createOpaquePassPipeline(
    ID3D12Device* device,
    void*         vsBlob,
    void*         psBlob,
    uint32_t      backBufferFormat,
    uint32_t      depthFormat)
{
    ENGINE_ASSERT(device  != nullptr, "device must not be null");
    ENGINE_ASSERT(vsBlob  != nullptr, "vsBlob must not be null");
    ENGINE_ASSERT(psBlob  != nullptr, "psBlob must not be null");

    OpaquePassPipeline result;

    // -------------------------------------------------------------------------
    // Root signature
    // -------------------------------------------------------------------------
    // Param 0: CBV b0 (PerFrameConstants)     — ALL visibility
    // Param 1: CBV b1 (PerObjectConstants)    — VERTEX only
    // Param 2: SRV t0 (materials buffer)      — PIXEL only
    // Param 3: Descriptor table (t1..unbounded, space1, bindless textures) — PIXEL only
    // Static sampler: s0 LINEAR_WRAP          — PIXEL only
    // -------------------------------------------------------------------------

    D3D12_ROOT_PARAMETER1 params[4] = {};

    // Param 0: per-frame CBV b0, space0 — visible to all stages
    params[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;
    params[0].Descriptor.RegisterSpace  = 0;
    params[0].Descriptor.Flags          = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC;
    params[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;

    // Param 1: per-object CBV b1, space0 — vertex only
    params[1].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[1].Descriptor.ShaderRegister = 1;
    params[1].Descriptor.RegisterSpace  = 0;
    params[1].Descriptor.Flags          = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC;
    params[1].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;

    // Param 2: materials structured buffer SRV t0, space0 — pixel only
    params[2].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[2].Descriptor.ShaderRegister = 0;
    params[2].Descriptor.RegisterSpace  = 0;
    params[2].Descriptor.Flags          = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC;
    params[2].ShaderVisibility          = D3D12_SHADER_VISIBILITY_PIXEL;

    // Param 3: descriptor table — unbounded SRV range t1..*, space1 (bindless textures)
    D3D12_DESCRIPTOR_RANGE1 texRange = {};
    texRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    texRange.NumDescriptors                    = UINT_MAX;   // unbounded
    texRange.BaseShaderRegister                = 1;          // t1
    texRange.RegisterSpace                     = 1;          // space1
    texRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    texRange.Flags                             = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE
                                               | D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;

    params[3].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[3].DescriptorTable.NumDescriptorRanges = 1;
    params[3].DescriptorTable.pDescriptorRanges   = &texRange;
    params[3].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

    // Static sampler: s0 LinearWrap — pixel only
    D3D12_STATIC_SAMPLER_DESC staticSampler = {};
    staticSampler.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    staticSampler.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSampler.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSampler.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSampler.MipLODBias       = 0.0f;
    staticSampler.MaxAnisotropy    = 1;
    staticSampler.ComparisonFunc   = D3D12_COMPARISON_FUNC_NEVER;
    staticSampler.BorderColor      = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    staticSampler.MinLOD           = 0.0f;
    staticSampler.MaxLOD           = D3D12_FLOAT32_MAX;
    staticSampler.ShaderRegister   = 0;
    staticSampler.RegisterSpace    = 0;
    staticSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.Version                    = D3D_ROOT_SIGNATURE_VERSION_1_1;
    rsDesc.Desc_1_1.NumParameters     = 4;
    rsDesc.Desc_1_1.pParameters       = params;
    rsDesc.Desc_1_1.NumStaticSamplers = 1;
    rsDesc.Desc_1_1.pStaticSamplers   = &staticSampler;
    rsDesc.Desc_1_1.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS        |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS      |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

    Microsoft::WRL::ComPtr<ID3DBlob> rsBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> rsError;
    HRESULT hr = D3D12SerializeVersionedRootSignature(&rsDesc, &rsBlob, &rsError);
    ENGINE_ASSERT(SUCCEEDED(hr), "Root signature serialization failed");

    hr = device->CreateRootSignature(
        0,
        rsBlob->GetBufferPointer(),
        rsBlob->GetBufferSize(),
        IID_PPV_ARGS(&result.rootSignature));
    ENGINE_ASSERT(SUCCEEDED(hr), "CreateRootSignature failed");

    // -------------------------------------------------------------------------
    // Input layout — matches VertexStatic (28 bytes)
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
    auto* vs = static_cast<ID3DBlob*>(vsBlob);
    auto* ps = static_cast<ID3DBlob*>(psBlob);

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = result.rootSignature.Get();

    psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };

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

    // Rasterizer: cull back, solid fill
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
    psoDesc.DepthStencilState.DepthEnable      = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask   = D3D12_DEPTH_WRITE_MASK_ALL;
    psoDesc.DepthStencilState.DepthFunc        = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
    psoDesc.DepthStencilState.StencilEnable    = FALSE;

    psoDesc.InputLayout = { inputElements, static_cast<UINT>(std::size(inputElements)) };
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0]    = static_cast<DXGI_FORMAT>(backBufferFormat);
    psoDesc.DSVFormat        = static_cast<DXGI_FORMAT>(depthFormat);

    psoDesc.SampleDesc.Count   = 1;
    psoDesc.SampleDesc.Quality = 0;

    hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&result.pso));
    ENGINE_ASSERT(SUCCEEDED(hr), "CreateGraphicsPipelineState failed for opaque pass");

    return result;
}

} // namespace engine::rendering

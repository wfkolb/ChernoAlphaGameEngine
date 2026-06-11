#include "internal/PipelineState.h"
#include <d3d12.h>
#include <core/diag/Assert.h>
#include <cstdint>
#include <cstddef>
#include <iterator>

namespace engine::rendering {

OpaquePassPipeline createOpaquePassPipeline(
    ID3D12Device* device,
    const void*   vsCode,
    size_t        vsSize,
    const void*   psCode,
    size_t        psSize,
    uint32_t      backBufferFormat,
    uint32_t      depthFormat)
{
    ENGINE_ASSERT(device != nullptr, "device must not be null");
    ENGINE_ASSERT(vsCode != nullptr && vsSize > 0, "vsCode must not be null");
    ENGINE_ASSERT(psCode != nullptr && psSize > 0, "psCode must not be null");

    OpaquePassPipeline result;

    // -------------------------------------------------------------------------
    // Root signature
    // -------------------------------------------------------------------------
    // Param 0: CBV b0 (PerFrameConstants)                   — ALL visibility
    // Param 1: CBV b1 (PerObjectConstants)                   — VERTEX only
    // Param 2: SRV t0, space0 (materials buffer)             — PIXEL only
    // Param 3: Descriptor table (t1..unbounded, space1, bindless textures) — PIXEL only
    // Param 4: SRV t1, space0 (light buffer, <=64)           — PIXEL only
    // Param 5: Descriptor table — IBL + shadow SRVs t2..t6, space0 — PIXEL only
    //            [0] t2 = gIrradianceMap   (TextureCube)
    //            [1] t3 = gPrefilteredEnv  (TextureCube)
    //            [2] t4 = gBrdfLut         (Texture2D)
    //            [3] t5 = gShadowCascades  (Texture2DArray, 4 slices CSM)
    //            [4] t6 = gShadowSpots     (Texture2DArray, up to 8 slices)
    // Static sampler 0: s0 LINEAR_WRAP        — PIXEL only (material textures)
    // Static sampler 1: s1 LINEAR_CLAMP       — PIXEL only (IBL lookups)
    // Static sampler 2: s2 PCF LESS_EQUAL     — PIXEL only (shadow map comparison)
    //
    // TODO Phase 10 R3: bind IBL SRVs to root signature descriptor table.
    //   At draw time, when hasIbl != 0, set the descriptor table for Param 5
    //   to a heap range containing the five SRVs (IBL + shadow stubs).  When
    //   hasIbl == 0 the HLSL branch is not taken so any valid (null-compatible)
    //   descriptor is sufficient — set it to a dummy SRV in the same heap.
    // TODO Phase 10 R2: bind shadow SRV descriptors (t5, t6) to FrameGraph.
    //   Until wired, slots [3] and [4] must point at 1x1 opaque-white stub SRVs.
    // -------------------------------------------------------------------------

    D3D12_ROOT_PARAMETER1 params[6] = {};

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

    // Param 4: light structured buffer SRV t1, space0 — pixel only
    params[4].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[4].Descriptor.ShaderRegister = 1;          // t1
    params[4].Descriptor.RegisterSpace  = 0;          // space0
    params[4].Descriptor.Flags          = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE;
    params[4].ShaderVisibility          = D3D12_SHADER_VISIBILITY_PIXEL;

    // Param 5: combined descriptor table — IBL (t2..t4) + shadow (t5, t6) in space0 — pixel only
    // Descriptor layout:
    //   [0] t2 = gIrradianceMap   (TextureCube)
    //   [1] t3 = gPrefilteredEnv  (TextureCube)
    //   [2] t4 = gBrdfLut         (Texture2D)
    //   [3] t5 = gShadowCascades  (Texture2DArray, 4 slices CSM)
    //   [4] t6 = gShadowSpots     (Texture2DArray, up to 8 slices)
    // When shadows are disabled the slots are filled with 1x1 opaque-white stub SRVs
    // so the shader can reference them without crashing (guarded by hasShadows flag).
    D3D12_DESCRIPTOR_RANGE1 iblShadowRange = {};
    iblShadowRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    iblShadowRange.NumDescriptors                    = 5;          // t2..t6
    iblShadowRange.BaseShaderRegister                = 2;          // t2
    iblShadowRange.RegisterSpace                     = 0;          // space0
    iblShadowRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    iblShadowRange.Flags                             = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE
                                                     | D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;

    params[5].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[5].DescriptorTable.NumDescriptorRanges = 1;
    params[5].DescriptorTable.pDescriptorRanges   = &iblShadowRange;
    params[5].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

    // Static sampler 0: s0 LinearWrap — pixel only (material texture sampling)
    D3D12_STATIC_SAMPLER_DESC staticSamplers[3] = {};
    D3D12_STATIC_SAMPLER_DESC& samplerWrap  = staticSamplers[0];
    samplerWrap.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplerWrap.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplerWrap.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplerWrap.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplerWrap.MipLODBias       = 0.0f;
    samplerWrap.MaxAnisotropy    = 1;
    samplerWrap.ComparisonFunc   = D3D12_COMPARISON_FUNC_NEVER;
    samplerWrap.BorderColor      = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    samplerWrap.MinLOD           = 0.0f;
    samplerWrap.MaxLOD           = D3D12_FLOAT32_MAX;
    samplerWrap.ShaderRegister   = 0;
    samplerWrap.RegisterSpace    = 0;
    samplerWrap.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // Static sampler 1: s1 LinearClamp — pixel only (IBL texture lookups)
    D3D12_STATIC_SAMPLER_DESC& samplerClamp = staticSamplers[1];
    samplerClamp.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplerClamp.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplerClamp.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplerClamp.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplerClamp.MipLODBias       = 0.0f;
    samplerClamp.MaxAnisotropy    = 1;
    samplerClamp.ComparisonFunc   = D3D12_COMPARISON_FUNC_NEVER;
    samplerClamp.BorderColor      = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    samplerClamp.MinLOD           = 0.0f;
    samplerClamp.MaxLOD           = D3D12_FLOAT32_MAX;
    samplerClamp.ShaderRegister   = 1;    // s1
    samplerClamp.RegisterSpace    = 0;
    samplerClamp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // Static sampler 2: s2 PCF comparison sampler — pixel only (shadow map sampling).
    // Uses LESS_EQUAL comparison because shadow maps use standard [0,1] depth (not reverse-Z).
    // BORDER mode with opaque-white border means out-of-bounds samples return 1.0 (lit).
    D3D12_STATIC_SAMPLER_DESC& samplerShadow = staticSamplers[2];
    samplerShadow.Filter           = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    samplerShadow.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplerShadow.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplerShadow.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplerShadow.MipLODBias       = 0.0f;
    samplerShadow.MaxAnisotropy    = 1;
    samplerShadow.ComparisonFunc   = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    samplerShadow.BorderColor      = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    samplerShadow.MinLOD           = 0.0f;
    samplerShadow.MaxLOD           = D3D12_FLOAT32_MAX;
    samplerShadow.ShaderRegister   = 2;    // s2
    samplerShadow.RegisterSpace    = 0;
    samplerShadow.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.Version                    = D3D_ROOT_SIGNATURE_VERSION_1_1;
    rsDesc.Desc_1_1.NumParameters     = 6;
    rsDesc.Desc_1_1.pParameters       = params;
    rsDesc.Desc_1_1.NumStaticSamplers = 3;
    rsDesc.Desc_1_1.pStaticSamplers   = staticSamplers;
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
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = result.rootSignature.Get();

    psoDesc.VS = { vsCode, vsSize };
    psoDesc.PS = { psCode, psSize };

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

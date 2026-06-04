// InputDemo.cpp
// Shows the last key pressed: the clear color changes per key (in-window
// visual) and the window title shows the key name as text.  A white + crosshair
// follows the mouse cursor; the system cursor is hidden while the window runs.

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <rendering/Window.h>
#include <rendering/GpuDevice.h>

#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>

using Microsoft::WRL::ComPtr;

namespace {

// ── Key name table ─────────────────────────────────────────────────────────

static const char* vkName(int vk) {
    static const char* kLetters[26] = {
        "A","B","C","D","E","F","G","H","I","J","K","L","M",
        "N","O","P","Q","R","S","T","U","V","W","X","Y","Z"
    };
    static const char* kDigits[10] = {
        "0","1","2","3","4","5","6","7","8","9"
    };
    static const char* kFKeys[12] = {
        "F1","F2","F3","F4","F5","F6","F7","F8","F9","F10","F11","F12"
    };
    if (vk >= 'A' && vk <= 'Z')      return kLetters[vk - 'A'];
    if (vk >= '0' && vk <= '9')      return kDigits [vk - '0'];
    if (vk >= VK_F1 && vk <= VK_F12) return kFKeys  [vk - VK_F1];
    switch (vk) {
    case VK_SPACE:   return "Space";
    case VK_RETURN:  return "Enter";
    case VK_ESCAPE:  return "Escape";
    case VK_LEFT:    return "Left";
    case VK_RIGHT:   return "Right";
    case VK_UP:      return "Up";
    case VK_DOWN:    return "Down";
    case VK_SHIFT:   return "Shift";
    case VK_CONTROL: return "Ctrl";
    case VK_MENU:    return "Alt";
    case VK_TAB:     return "Tab";
    case VK_BACK:    return "Backspace";
    case VK_DELETE:  return "Delete";
    case VK_INSERT:  return "Insert";
    case VK_HOME:    return "Home";
    case VK_END:     return "End";
    case VK_PRIOR:   return "PageUp";
    case VK_NEXT:    return "PageDown";
    default:         return nullptr;
    }
}

// VK codes polled every frame.
static const int kPollKeys[] = {
    'A','B','C','D','E','F','G','H','I','J','K','L','M',
    'N','O','P','Q','R','S','T','U','V','W','X','Y','Z',
    '0','1','2','3','4','5','6','7','8','9',
    VK_SPACE, VK_RETURN, VK_ESCAPE,
    VK_LEFT,  VK_RIGHT,  VK_UP, VK_DOWN,
    VK_SHIFT, VK_CONTROL, VK_MENU, VK_TAB, VK_BACK, VK_DELETE,
    VK_INSERT, VK_HOME, VK_END, VK_PRIOR, VK_NEXT,
    VK_F1,  VK_F2,  VK_F3,  VK_F4,  VK_F5,  VK_F6,
    VK_F7,  VK_F8,  VK_F9,  VK_F10, VK_F11, VK_F12,
};

// ── Key → distinct background color (golden-angle HSV distribution) ─────────

static void keyToColor(int vk, float out[4]) {
    const float hue = std::fmodf(static_cast<float>(vk * 137) / 360.0f, 1.0f);
    constexpr float s = 0.75f;
    constexpr float v = 0.55f;

    const float h6 = hue * 6.0f;
    const int   hi = static_cast<int>(h6) % 6;
    const float f  = h6 - std::floorf(h6);
    const float p  = v * (1.0f - s);
    const float q  = v * (1.0f - f * s);
    const float t  = v * (1.0f - (1.0f - f) * s);

    switch (hi) {
    case 0: out[0]=v; out[1]=t; out[2]=p; break;
    case 1: out[0]=q; out[1]=v; out[2]=p; break;
    case 2: out[0]=p; out[1]=v; out[2]=t; break;
    case 3: out[0]=p; out[1]=q; out[2]=v; break;
    case 4: out[0]=t; out[1]=p; out[2]=v; break;
    case 5: out[0]=v; out[1]=p; out[2]=q; break;
    default: out[0]=out[1]=out[2]=0.3f;   break;
    }
    out[3] = 1.0f;
}

// ── Crosshair renderer ──────────────────────────────────────────────────────
// Two screen-space quads (horizontal + vertical bars) drawn as triangles.
// Upload-heap VB is written every frame with updated mouse NDC position.

static constexpr const char* kCrossVS = R"hlsl(
struct Out { float4 pos : SV_Position; };
Out VSMain(float2 p : POSITION) {
    Out o;
    o.pos = float4(p, 0.0f, 1.0f);
    return o;
}
)hlsl";

static constexpr const char* kCrossPS = R"hlsl(
float4 PSMain() : SV_Target { return float4(1.0f, 1.0f, 1.0f, 1.0f); }
)hlsl";

static constexpr UINT kCrossVerts = 12u;  // 6 per bar

struct Crosshair {
    ComPtr<ID3D12RootSignature> rootSig;
    ComPtr<ID3D12PipelineState> pso;
    ComPtr<ID3D12Resource>      vb;
    float*                      mapped = nullptr;
    D3D12_VERTEX_BUFFER_VIEW    vbView = {};

    bool init(ID3D12Device* dev) {
        // Empty root signature — shaders use no resources.
        D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
        rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        ComPtr<ID3DBlob> rsBlob;
        if (FAILED(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1,
                                               &rsBlob, nullptr)))
            return false;
        if (FAILED(dev->CreateRootSignature(0u,
                                            rsBlob->GetBufferPointer(),
                                            rsBlob->GetBufferSize(),
                                            IID_PPV_ARGS(&rootSig))))
            return false;

        // Compile inline shaders via D3DCompile (FXC path, fine for a demo).
        ComPtr<ID3DBlob> vsBlob, psBlob;
        if (FAILED(D3DCompile(kCrossVS, std::strlen(kCrossVS), nullptr,
                              nullptr, nullptr, "VSMain", "vs_5_1",
                              0u, 0u, &vsBlob, nullptr)))
            return false;
        if (FAILED(D3DCompile(kCrossPS, std::strlen(kCrossPS), nullptr,
                              nullptr, nullptr, "PSMain", "ps_5_1",
                              0u, 0u, &psBlob, nullptr)))
            return false;

        D3D12_INPUT_ELEMENT_DESC il = {
            "POSITION", 0u, DXGI_FORMAT_R32G32_FLOAT, 0u, 0u,
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0u
        };

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {};
        pd.pRootSignature        = rootSig.Get();
        pd.VS                    = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
        pd.PS                    = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
        pd.InputLayout           = { &il, 1u };
        pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pd.NumRenderTargets      = 1u;
        pd.RTVFormats[0]         = DXGI_FORMAT_B8G8R8A8_UNORM;
        pd.DSVFormat             = DXGI_FORMAT_UNKNOWN;  // no depth buffer for this pass
        pd.SampleDesc            = { 1u, 0u };
        pd.SampleMask            = UINT_MAX;
        pd.RasterizerState.FillMode       = D3D12_FILL_MODE_SOLID;
        pd.RasterizerState.CullMode       = D3D12_CULL_MODE_NONE;
        pd.RasterizerState.DepthClipEnable = TRUE;
        pd.BlendState.RenderTarget[0].BlendEnable           = FALSE;
        pd.BlendState.RenderTarget[0].SrcBlend              = D3D12_BLEND_ONE;
        pd.BlendState.RenderTarget[0].DestBlend             = D3D12_BLEND_ZERO;
        pd.BlendState.RenderTarget[0].BlendOp               = D3D12_BLEND_OP_ADD;
        pd.BlendState.RenderTarget[0].SrcBlendAlpha         = D3D12_BLEND_ONE;
        pd.BlendState.RenderTarget[0].DestBlendAlpha        = D3D12_BLEND_ZERO;
        pd.BlendState.RenderTarget[0].BlendOpAlpha          = D3D12_BLEND_OP_ADD;
        pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        pd.DepthStencilState.DepthEnable   = FALSE;
        pd.DepthStencilState.StencilEnable = FALSE;

        if (FAILED(dev->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&pso))))
            return false;

        // Persistently-mapped upload-heap vertex buffer.
        const UINT64 vbBytes = kCrossVerts * 2u * sizeof(float);

        D3D12_HEAP_PROPERTIES hp = {};
        hp.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC rd = {};
        rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width            = vbBytes;
        rd.Height           = 1u;
        rd.DepthOrArraySize = 1u;
        rd.MipLevels        = 1u;
        rd.Format           = DXGI_FORMAT_UNKNOWN;
        rd.SampleDesc       = { 1u, 0u };
        rd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        if (FAILED(dev->CreateCommittedResource(
                &hp, D3D12_HEAP_FLAG_NONE, &rd,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr, IID_PPV_ARGS(&vb))))
            return false;

        const D3D12_RANGE noRead = { 0u, 0u };
        if (FAILED(vb->Map(0u, &noRead, reinterpret_cast<void**>(&mapped))))
            return false;

        vbView.BufferLocation = vb->GetGPUVirtualAddress();
        vbView.SizeInBytes    = static_cast<UINT>(vbBytes);
        vbView.StrideInBytes  = 2u * sizeof(float);
        return true;
    }

    // Write 12 NDC vertices forming a + at (cx, cy).
    // lx/ly = arm half-length in NDC; tx/ty = half-thickness in NDC.
    void update(float cx, float cy, float W, float H) {
        const float lx = 32.0f / W;   // ±16 px arms
        const float ly = 32.0f / H;
        const float tx =  3.0f / W;   // ±1.5 px thickness
        const float ty =  3.0f / H;

        float* v = mapped;
        // Horizontal bar
        v[ 0]=cx-lx; v[ 1]=cy-ty;
        v[ 2]=cx+lx; v[ 3]=cy-ty;
        v[ 4]=cx+lx; v[ 5]=cy+ty;
        v[ 6]=cx-lx; v[ 7]=cy-ty;
        v[ 8]=cx+lx; v[ 9]=cy+ty;
        v[10]=cx-lx; v[11]=cy+ty;
        // Vertical bar
        v[12]=cx-tx; v[13]=cy-ly;
        v[14]=cx+tx; v[15]=cy-ly;
        v[16]=cx+tx; v[17]=cy+ly;
        v[18]=cx-tx; v[19]=cy-ly;
        v[20]=cx+tx; v[21]=cy+ly;
        v[22]=cx-tx; v[23]=cy+ly;
    }

    void draw(ID3D12GraphicsCommandList* cmd,
              D3D12_CPU_DESCRIPTOR_HANDLE rtv, UINT W, UINT H) {
        const D3D12_VIEWPORT vp{
            0.0f, 0.0f,
            static_cast<float>(W), static_cast<float>(H),
            0.0f, 1.0f
        };
        const D3D12_RECT sr{ 0, 0, static_cast<LONG>(W), static_cast<LONG>(H) };

        cmd->SetGraphicsRootSignature(rootSig.Get());
        cmd->SetPipelineState(pso.Get());
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmd->IASetVertexBuffers(0u, 1u, &vbView);
        cmd->OMSetRenderTargets(1u, &rtv, FALSE, nullptr);
        cmd->RSSetViewports(1u, &vp);
        cmd->RSSetScissorRects(1u, &sr);
        cmd->DrawInstanced(kCrossVerts, 1u, 0u, 0u);
    }
};

} // anonymous namespace

// ---------------------------------------------------------------------------

int main() {
    using namespace engine::rendering;

    Window window = Window::create({
        .width  = 1280,
        .height = 720,
        .title  = L"InputDemo — press any key",
    });

    GpuDevice device = GpuDevice::create({
        .window = &window,
        .vsync  = true,
    });
    if (!device.isValid()) return EXIT_FAILURE;

    HWND hwnd = static_cast<HWND>(window.nativeHandle());
    auto* dev  = static_cast<ID3D12Device*>(device.nativeDevice());

    Crosshair crosshair;
    if (!crosshair.init(dev)) return EXIT_FAILURE;

    ShowCursor(FALSE);

    std::array<bool, 256> prevState{};
    float clearColor[4] = { 0.05f, 0.05f, 0.10f, 1.0f };

    while (!window.wantsClose()) {
        // -- Detect newly pressed keys ----------------------------------------
        for (const int vk : kPollKeys) {
            const bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
            if (down && !prevState[static_cast<size_t>(vk)]) {
                const char* name = vkName(vk);
                if (name) {
                    keyToColor(vk, clearColor);
                    const std::string title =
                        std::string("InputDemo \xe2\x80\x94 Last key: ") + name;
                    SetWindowTextA(hwnd, title.c_str());
                }
            }
            prevState[static_cast<size_t>(vk)] = down;
        }

        // -- Update crosshair position ----------------------------------------
        RECT rc;
        GetClientRect(hwnd, &rc);
        const float W = static_cast<float>(rc.right  - rc.left);
        const float H = static_cast<float>(rc.bottom - rc.top);

        if (W > 0.0f && H > 0.0f) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);

            const float ndcX =  2.0f * static_cast<float>(pt.x) / W - 1.0f;
            const float ndcY = -(2.0f * static_cast<float>(pt.y) / H - 1.0f);
            crosshair.update(ndcX, ndcY, W, H);
        }

        // -- Render -----------------------------------------------------------
        device.beginFrame();

        auto* cmd = static_cast<ID3D12GraphicsCommandList*>(device.nativeCommandList());
        D3D12_CPU_DESCRIPTOR_HANDLE rtv{ device.currentBackBufferRtvHandle() };
        D3D12_CPU_DESCRIPTOR_HANDLE dsv{ device.depthBufferDsvHandle() };

        cmd->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
        cmd->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
        cmd->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0, 0, nullptr);

        if (W > 0.0f && H > 0.0f)
            crosshair.draw(cmd, rtv, static_cast<UINT>(W), static_cast<UINT>(H));

        device.endFrame();
    }

    ShowCursor(TRUE);
    device.flush();
    return EXIT_SUCCESS;
}

#ifdef ENGINE_DEVREL

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d12.h>

#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>

#include "editor/EditorApp.h"

#include <wrl/client.h>

#include <rendering/Window.h>
#include <rendering/GpuDevice.h>

#include <core/scene/Scene.h>
#include <core/ecs/World.h>
#include <core/ecs/Name.h>
#include <core/components/Transform.h>
#include <core/components/Health.h>

#include <tools/Config.h>

#include <cstdio>

// ImGui_ImplWin32_WndProcHandler is declared in imgui_impl_win32.h (included
// above) — no local re-declaration needed.

namespace engine::editor {

namespace {
constexpr uint32_t kWidth  = 1600;
constexpr uint32_t kHeight = 900;

// Original WndProc of the rendering window, chained after ImGui handling.
WNDPROC g_originalWndProc = nullptr;

LRESULT CALLBACK EditorWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui::GetCurrentContext() != nullptr &&
        ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) {
        return 1;
    }
    if (g_originalWndProc) {
        return CallWindowProcW(g_originalWndProc, hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
}

// ---------------------------------------------------------------------------
// Gpu: owns the window, device, framegraph, and ImGui's descriptor heap.
// ---------------------------------------------------------------------------
struct EditorApp::Gpu {
    std::unique_ptr<rendering::Window>    window;
    std::unique_ptr<rendering::GpuDevice> device;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> imguiHeap;
    bool imguiInitialized = false;

    ~Gpu() {
        if (imguiInitialized) {
            ImGui_ImplDX12_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
        }
    }
};

EditorApp::EditorApp() = default;
EditorApp::~EditorApp() { shutdown(); }

bool EditorApp::init() {
    gpu_ = std::make_unique<Gpu>();

    if (!rendering::GpuDevice::isAvailable()) {
        return false;   // headless / no DX12 — cannot run the editor.
    }

    // --- Window ---
    rendering::Window window = rendering::Window::create({
        .width  = kWidth,
        .height = kHeight,
        .title  = L"EngineEditor",
    });
    gpu_->window = std::make_unique<rendering::Window>(std::move(window));

    HWND hwnd = static_cast<HWND>(gpu_->window->nativeHandle());

    // --- Device ---
    rendering::GpuDevice device = rendering::GpuDevice::create({
        .window = gpu_->window.get(),
        .vsync  = true,
    });
    if (!device.isValid()) {
        return false;
    }
    gpu_->device = std::make_unique<rendering::GpuDevice>(std::move(device));

    auto* d3dDevice = static_cast<ID3D12Device*>(gpu_->device->nativeDevice());

    // --- Dedicated shader-visible heap for ImGui (font + viewport SRVs) ---
    {
        D3D12_DESCRIPTOR_HEAP_DESC hd = {};
        hd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        hd.NumDescriptors = 64;
        hd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(d3dDevice->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&gpu_->imguiHeap)))) {
            return false;
        }
    }

    // --- ImGui ---
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
#ifdef IMGUI_HAS_DOCK
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
#endif
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(hwnd);

    // Subclass the window's WndProc so ImGui receives input. The original proc
    // (which tracks resize/close) is chained after ImGui.
    g_originalWndProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&EditorWndProc)));

    const D3D12_CPU_DESCRIPTOR_HANDLE cpu = gpu_->imguiHeap->GetCPUDescriptorHandleForHeapStart();
    const D3D12_GPU_DESCRIPTOR_HANDLE gpu = gpu_->imguiHeap->GetGPUDescriptorHandleForHeapStart();
    ImGui_ImplDX12_Init(
        d3dDevice,
        static_cast<int>(rendering::GpuDevice::kMaxFramesInFlight),
        DXGI_FORMAT_R8G8B8A8_UNORM,
        gpu_->imguiHeap.Get(),
        cpu, gpu);

    gpu_->imguiInitialized = true;

    // --- Editor state ---
    inspectorPanel_ = std::make_unique<InspectorPanel>(componentRegistry_);
    loadPreferences();
    newScene();

    running_ = true;
    return true;
}

void EditorApp::loadPreferences() {
    // project.toml / editor_prefs.toml are loaded best-effort via the engine
    // Config system; missing files fall back to defaults.
    tools::Config::init();
    projectName_ = tools::Config::getString("project", "name", "Untitled Project");
    contentRoot_ = tools::Config::getString("project", "contentRoot", "");

    if (!contentRoot_.empty()) {
        assetPanel_.setRoot(contentRoot_);
    }
    assetPanel_.setOpenSceneCallback([this](const std::filesystem::path& p) {
        openScene(p.wstring());
    });

    consolePanel_.log(ConsolePanel::Level::Info, "Editor initialized");
}

void EditorApp::newScene() {
    sceneManager_.unload("EditorScene");
    activeScene_ = sceneManager_.load("EditorScene");   // load() also calls Scene::load()
    if (activeScene_) {
        sceneManager_.activate("EditorScene");          // activate() builds BVH + marks active
    }
    selected_ = core::ecs::kInvalidEntity;
    undo_.clear();
    currentScenePath_.clear();
    consolePanel_.log(ConsolePanel::Level::Info, "New scene created");
}

void EditorApp::openScene(const std::wstring& path) {
    // Scene file loading is delegated to tools::SceneSerializer by the host;
    // here we just record the path and log. Full deserialize wiring lives in the
    // serializer task and is invoked through the asset browser callback.
    currentScenePath_ = path;
    char buf[512];
    std::snprintf(buf, sizeof(buf), "Open scene requested");
    consolePanel_.log(ConsolePanel::Level::Info, buf);
}

void EditorApp::saveScene(const std::wstring& path) {
    currentScenePath_ = path;
    consolePanel_.log(ConsolePanel::Level::Info, "Save scene requested");
}

void EditorApp::buildDockLayout() {
#ifdef IMGUI_HAS_DOCK
    // Dockspace over the main viewport; panels are dockable into it.
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::SetNextWindowViewport(vp->ID);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                             ImGuiWindowFlags_MenuBar;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##EditorDockHost", nullptr, flags);
    ImGui::PopStyleVar(3);

    const ImGuiID dockId = ImGui::GetID("EditorDockSpace");
    ImGui::DockSpace(dockId, ImVec2(0, 0), ImGuiDockNodeFlags_None);

    drawMenuBar();

    ImGui::End();
#else
    // Non-docking ImGui build: panels are independent floating windows and the
    // menu lives on the main menu bar.
    if (ImGui::BeginMainMenuBar()) {
        drawMenuBarItems();
        ImGui::EndMainMenuBar();
    }
#endif
}

void EditorApp::drawMenuBar() {
    if (!ImGui::BeginMenuBar()) return;
    drawMenuBarItems();
    ImGui::EndMenuBar();
}

void EditorApp::drawMenuBarItems() {
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New Scene", "Ctrl+N")) newScene();
        if (ImGui::MenuItem("Save Scene", "Ctrl+S")) saveScene(currentScenePath_);
        ImGui::Separator();
        if (ImGui::MenuItem("Exit")) running_ = false;
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit")) {
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, undo_.canUndo())) undo_.undo();
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, undo_.canRedo())) undo_.redo();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Window")) {
        ImGui::MenuItem("Hierarchy", nullptr, &showHierarchy_);
        ImGui::MenuItem("Inspector", nullptr, &showInspector_);
        ImGui::MenuItem("Viewport",  nullptr, &showViewport_);
        ImGui::MenuItem("Assets",    nullptr, &showAssets_);
        ImGui::MenuItem("Console",   nullptr, &showConsole_);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Play")) {
        const bool playing = pie_.isPlaying();
        if (ImGui::MenuItem("Play", nullptr, false, !playing && activeScene_)) {
            if (activeScene_) pie_.start(*activeScene_);
        }
        if (ImGui::MenuItem("Stop", nullptr, false, playing)) {
            pie_.stop();
        }
        ImGui::EndMenu();
    }

    // Status on the right.
    char status[128];
    std::snprintf(status, sizeof(status), "%s%s",
                  projectName_.c_str(),
                  pie_.isPlaying() ? "  [PLAYING]" : "");
    const float tw = ImGui::CalcTextSize(status).x;
    ImGui::SameLine(ImGui::GetWindowWidth() - tw - 20.0f);
    ImGui::TextUnformatted(status);
}

void EditorApp::drawPanels() {
    if (!activeScene_) return;
    core::ecs::World& world = activeScene_->world();

    if (showHierarchy_) {
        selected_ = hierarchyPanel_.draw(world, selected_, undo_, &showHierarchy_);
    }
    if (showInspector_) {
        inspectorPanel_->draw(world, selected_, &showInspector_);
    }
    if (showViewport_) {
        // No offscreen scene RT is wired yet; pass 0 to show the placeholder.
        viewportPanel_.draw(world, selected_, camera_, picking_, undo_, 0, &showViewport_);
    }
    if (showAssets_) {
        assetPanel_.draw(&showAssets_);
    }
    if (showConsole_) {
        consolePanel_.draw(&showConsole_);
    }
}

void EditorApp::frame() {
    // PIE advances the simulation while playing.
    if (pie_.isPlaying()) {
        ImGuiIO& io = ImGui::GetIO();
        pie_.tick(io.DeltaTime > 0.0f ? io.DeltaTime : 1.0f / 64.0f);
    }

    gpu_->device->beginFrame();

    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    buildDockLayout();
    drawPanels();

    ImGui::Render();

    // Record ImGui into the back buffer via the open command list.
    auto* cmdList = static_cast<ID3D12GraphicsCommandList*>(gpu_->device->nativeCommandList());
    ID3D12DescriptorHeap* heaps[] = { gpu_->imguiHeap.Get() };
    cmdList->SetDescriptorHeaps(1, heaps);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv{ gpu_->device->currentBackBufferRtvHandle() };
    const float clearColor[4] = { 0.10f, 0.10f, 0.12f, 1.0f };
    cmdList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
    cmdList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmdList);

    gpu_->device->endFrame();
}

void EditorApp::run() {
    if (!running_) return;

    while (running_ && !gpu_->window->wantsClose()) {
        frame();
    }

    gpu_->device->flush();
}

void EditorApp::shutdown() {
    if (!gpu_) return;
    if (gpu_->device) gpu_->device->flush();
    pie_.stop();
    tools::Config::shutdown();
    gpu_.reset();   // tears down ImGui + device + window in Gpu::~Gpu
}

} // namespace engine::editor

#endif // ENGINE_DEVREL

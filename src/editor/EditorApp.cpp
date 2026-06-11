#ifdef ENGINE_DEVREL

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>

#include "editor/EditorApp.h"
#include "editor/FileDialog.h"
#include "editor/ComponentTraits.h"
#include "editor/commands/SaveAsPrefabCommand.h"
#include "editor/commands/MaterialChangeCommand.h"
#include "editor/component_widgets/ColliderWidget.h"
#include "editor/component_widgets/AnimationStateWidget.h"
#include "editor/component_widgets/PrefabInstanceWidget.h"

#include <core/Input.h>
#include <core/components/ColliderComponent.h>
#include <core/components/AnimationState.h>
#include <core/components/MeshHandle.h>
#include <core/components/SpawnPointComponent.h>
#include <core/components/TriggerComponent.h>
#include <core/ecs/HierarchyComponent.h>
#include <core/ecs/PrefabInstance.h>
#include <core/ecs/View.h>
#include <core/input/InputReceiverComponent.h>
#include <core/math/Quat.h>
#include <physics/RigidBody.h>
#include <physics/CharacterController.h>
#include <tools/SceneSerializer.h>
#include <tools/PrefabSerializer.h>

#include <wrl/client.h>

#include <rendering/Window.h>
#include <rendering/GpuDevice.h>

#include <core/scene/Scene.h>
#include <core/ecs/World.h>
#include <core/ecs/Name.h>
#include <core/components/Transform.h>
#include <core/components/Health.h>
#include <core/components/Lifetime.h>
#include <core/components/TeamTag.h>

#include <tools/Config.h>
#include <tools/Logger.h>
#include <tools/EassetLoader.h>

#include <core/diag/Assert.h>

#include <rendering/MeshManager.h>
#include <rendering/Camera.h>
#include <rendering/Light.h>

#include <core/math/Mat.h>
#include <core/math/Vec.h>

#include <FrameGraphImpl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <span>

// imgui_impl_win32.h guards this declaration in #if 0 to avoid pulling in
// <windows.h>. Forward-declare it here per imgui's documented usage pattern.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace engine::editor {

// ---- PieWindow -----------------------------------------------------------
// Owns the separate game window opened when PIE starts. Defined in the
// engine::editor namespace (not nested) so PieWndProc can access it.
struct PieWindow {
    HWND                                          hwnd = nullptr;
    Microsoft::WRL::ComPtr<IDXGISwapChain3>       swapChain;
    Microsoft::WRL::ComPtr<ID3D12Resource>        backBuffers[rendering::GpuDevice::kBackBufferCount];
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>  rtvHeap;
    D3D12_CPU_DESCRIPTOR_HANDLE                   rtvHandles[rendering::GpuDevice::kBackBufferCount]{};
    Microsoft::WRL::ComPtr<ID3D12Resource>        depthRt;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>  dsvHeap;
    D3D12_CPU_DESCRIPTOR_HANDLE                   dsvHandle{};
    uint32_t                                      width     = 1280;
    uint32_t                                      height    = 720;
    bool                                          wantsClose = false;
};

namespace {
constexpr uint32_t kWidth  = 1600;
constexpr uint32_t kHeight = 900;

// Original WndProc of the rendering window, chained after ImGui handling.
WNDPROC g_originalWndProc = nullptr;

// Non-owning pointer to the active PIEController so EditorWndProc can route
// raw input during PIE without coupling the static callback to EditorApp.
PIEController* g_pieController = nullptr;

// Non-owning pointer to the active PIE game window so PieWndProc can flag
// WM_CLOSE and route raw input without coupling to EditorApp state.
PieWindow* g_pieWindow = nullptr;

LRESULT CALLBACK EditorWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // Route WM_INPUT to the low-level InputSystem when PIE is playing so the
    // player entity receives keyboard and mouse input.
    if (msg == WM_INPUT && g_pieController && g_pieController->isPlaying()) {
        engine::core::InputSystem::processRawInput(reinterpret_cast<void*>(lParam));
    }

    if (ImGui::GetCurrentContext() != nullptr &&
        ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) {
        return 1;
    }
    if (g_originalWndProc) {
        return CallWindowProcW(g_originalWndProc, hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK PieWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_CLOSE ||
        (msg == WM_KEYDOWN && wParam == VK_ESCAPE)) {
        if (g_pieWindow) g_pieWindow->wantsClose = true;
        return 0;
    }
    if (msg == WM_INPUT && g_pieController && g_pieController->isPlaying()) {
        engine::core::InputSystem::processRawInput(reinterpret_cast<void*>(lParam));
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
    uint32_t imguiDescSize      = 0;
    uint32_t imguiNextDescSlot  = 0;
    bool imguiInitialized = false;

    // Offscreen viewport RT.
    Microsoft::WRL::ComPtr<ID3D12Resource>       viewportColorRt;
    Microsoft::WRL::ComPtr<ID3D12Resource>       viewportDepthRt;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> viewportRtvHeap; // non-shader-visible, 1 slot
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> viewportDsvHeap; // non-shader-visible, 1 slot
    D3D12_CPU_DESCRIPTOR_HANDLE                  viewportRtvCpu{};
    D3D12_CPU_DESCRIPTOR_HANDLE                  viewportDsvCpu{};
    uint64_t                                     viewportSrvGpu          = 0;
    uint64_t                                     viewportSrvCpu          = 0;
    bool                                         viewportSrvAllocated    = false;
    uint32_t                                     viewportRtWidth         = 0;
    uint32_t                                     viewportRtHeight        = 0;

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

void EditorApp::setProjectRoot(std::filesystem::path root) {
    projectRoot_ = std::filesystem::absolute(root);
}

core::ecs::EntityFactory& EditorApp::entityFactory() {
    return entityFactory_;
}

bool EditorApp::init() {
    tools::Logger::init(::engine::core::log::LogLevel::Trace);
    LOG_INFO("EngineEditor init started");

    gpu_ = std::make_unique<Gpu>();

    if (!rendering::GpuDevice::isAvailable()) {
        LOG_ERROR("No DX12-capable device found (headless?). Editor cannot start.");
        return false;
    }
    LOG_INFO("DX12 device available");

    // --- Window ---
    rendering::Window window = rendering::Window::create({
        .width  = kWidth,
        .height = kHeight,
        .title  = L"EngineEditor",
    });
    gpu_->window = std::make_unique<rendering::Window>(std::move(window));
    LOG_INFO("Window created ({}x{})", kWidth, kHeight);

    HWND hwnd = static_cast<HWND>(gpu_->window->nativeHandle());

    // --- Device ---
    rendering::GpuDevice device = rendering::GpuDevice::create({
        .window = gpu_->window.get(),
        .vsync  = true,
    });
    if (!device.isValid()) {
        LOG_ERROR("GpuDevice creation failed (swapchain error?)");
        return false;
    }
    gpu_->device = std::make_unique<rendering::GpuDevice>(std::move(device));
    LOG_INFO("GpuDevice created");

    auto* d3dDevice = static_cast<ID3D12Device*>(gpu_->device->nativeDevice());

    // --- Dedicated shader-visible heap for ImGui (font + viewport SRVs) ---
    {
        D3D12_DESCRIPTOR_HEAP_DESC hd = {};
        hd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        hd.NumDescriptors = 64;
        hd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(d3dDevice->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&gpu_->imguiHeap)))) {
            LOG_ERROR("Failed to create ImGui descriptor heap");
            return false;
        }
        gpu_->imguiDescSize = d3dDevice->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
    LOG_INFO("ImGui descriptor heap created");

    // --- ImGui ---
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
#ifdef IMGUI_HAS_DOCK
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    LOG_TRACE("ImGui docking enabled");
#else
    LOG_WARN("ImGui built without docking support (IMGUI_HAS_DOCK not defined)");
#endif
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(hwnd);
    LOG_INFO("ImGui Win32 backend initialized");

    // Subclass the window's WndProc so ImGui receives input. The original proc
    // (which tracks resize/close) is chained after ImGui.
    g_originalWndProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&EditorWndProc)));

    // ImGui 1.91.5+ uses ImGui_ImplDX12_InitInfo. The backend sets
    // RendererHasTextures (requires CommandQueue) so font atlas uploads
    // happen automatically inside NewFrame — no manual CreateFontsTexture needed.
    {
        Gpu* gpuPtr = gpu_.get();
        ImGui_ImplDX12_InitInfo dx12Info = {};
        dx12Info.Device           = d3dDevice;
        dx12Info.CommandQueue     = static_cast<ID3D12CommandQueue*>(gpu_->device->nativeCommandQueue());
        dx12Info.NumFramesInFlight = static_cast<int>(rendering::GpuDevice::kMaxFramesInFlight);
        dx12Info.RTVFormat        = DXGI_FORMAT_R8G8B8A8_UNORM;
        dx12Info.DSVFormat        = DXGI_FORMAT_UNKNOWN;
        dx12Info.SrvDescriptorHeap = gpu_->imguiHeap.Get();
        dx12Info.UserData         = gpuPtr;
        dx12Info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* info,
                                           D3D12_CPU_DESCRIPTOR_HANDLE* outCpu,
                                           D3D12_GPU_DESCRIPTOR_HANDLE* outGpu) {
            auto* g   = static_cast<Gpu*>(info->UserData);
            uint32_t slot = g->imguiNextDescSlot++;
            outCpu->ptr = g->imguiHeap->GetCPUDescriptorHandleForHeapStart().ptr
                          + static_cast<SIZE_T>(slot) * g->imguiDescSize;
            outGpu->ptr = g->imguiHeap->GetGPUDescriptorHandleForHeapStart().ptr
                          + static_cast<UINT64>(slot) * g->imguiDescSize;
        };
        dx12Info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE,
                                          D3D12_GPU_DESCRIPTOR_HANDLE) {
            // Bump allocator — slots are never individually freed in the editor.
        };
        ImGui_ImplDX12_Init(&dx12Info);
    }
    LOG_INFO("ImGui DX12 backend initialized");

    gpu_->imguiInitialized = true;

    // --- Viewport render target + mesh render system ---
    materialManager_ = std::make_unique<rendering::MaterialManager>(*gpu_->device);
    // Default material at index 0 — must exist before any mesh is rendered.
    {
        rendering::GpuMaterial mat = {};
        mat.albedoTextureIndex     = 0xFFFFFFFFu;
        mat.normalTextureIndex     = 0xFFFFFFFFu;
        mat.metallicRoughnessIndex = 0xFFFFFFFFu;
        mat.emissiveTextureIndex   = 0xFFFFFFFFu;
        mat.albedoFactor[0] = 0.8f; mat.albedoFactor[1] = 0.8f;
        mat.albedoFactor[2] = 0.8f; mat.albedoFactor[3] = 1.0f;
        mat.metallicFactor  = 0.0f;
        mat.roughnessFactor = 0.5f;
        materialManager_->add(mat, "default");
    }
    // Init MeshRenderSystem pipeline (format must match RT formats used in createViewportRt).
    meshRenderSystem_.init(*gpu_->device, *materialManager_,
                           static_cast<uint32_t>(DXGI_FORMAT_R8G8B8A8_UNORM),
                           static_cast<uint32_t>(DXGI_FORMAT_D32_FLOAT));

    // Wire FrameGraph device before any compile() calls.
    rendering::frameGraphSetDevice(sceneFrameGraph_, d3dDevice);

    // Create initial offscreen RT at window size.
    createViewportRt(kWidth, kHeight);
    LOG_INFO("Viewport offscreen RT created ({}x{})", kWidth, kHeight);

    // --- Editor state ---
    inspectorPanel_ = std::make_unique<InspectorPanel>(componentRegistry_);
    registerComponentWidgets();

    undo_.setOnModified([this]() { sceneDirty_ = true; });

    registerComponents();
    loadPreferences();

    // Create the editor-owned PhysicsWorld. It is wired to the active scene
    // before PIE starts and detached when PIE stops.
    editorPhysicsWorld_ = std::make_unique<physics::PhysicsWorld>();

    // Publish the PIEController pointer so the static WndProc can route raw
    // input to the InputSystem during PIE.
    g_pieController = &pie_;

    newScene();

    // ThumbnailRenderer init — pass SRV alloc callback so it can place SRVs in shared heap.
    thumbnailRenderer_.init(*gpu_->device,
        [this](uint64_t& outCpuPtr, uint64_t& outGpuPtr) {
            uint32_t slot = gpu_->imguiNextDescSlot++;
            outCpuPtr = gpu_->imguiHeap->GetCPUDescriptorHandleForHeapStart().ptr
                        + static_cast<SIZE_T>(slot) * gpu_->imguiDescSize;
            outGpuPtr = gpu_->imguiHeap->GetGPUDescriptorHandleForHeapStart().ptr
                        + static_cast<UINT64>(slot) * gpu_->imguiDescSize;
        });

    // MeshPreviewPanel — allocate one SRV slot in the shared ImGui heap.
    {
        uint32_t slot      = gpu_->imguiNextDescSlot++;
        uint64_t cpuHandle = gpu_->imguiHeap->GetCPUDescriptorHandleForHeapStart().ptr
                             + static_cast<SIZE_T>(slot) * gpu_->imguiDescSize;
        uint64_t gpuHandle = gpu_->imguiHeap->GetGPUDescriptorHandleForHeapStart().ptr
                             + static_cast<UINT64>(slot) * gpu_->imguiDescSize;
        previewPanel_.init(*gpu_->device, cpuHandle, gpuHandle);
    }

    // Wire inline preview into the asset browser right column.
    assetPanel_.setPreviewDrawFn([this](ImVec2 sz) { previewPanel_.drawInline(sz); });

    LOG_INFO("EngineEditor init complete");
    running_ = true;
    return true;
}

void EditorApp::loadPreferences() {
    tools::Config::init();
    projectName_ = tools::Config::getString("project", "name", "Untitled Project");
    contentRoot_ = tools::Config::getString("project", "contentRoot", "");

    // --project CLI arg takes precedence over the engine config value.
    if (!projectRoot_.empty()) {
        contentRoot_  = projectRoot_.string();
        projectName_  = projectRoot_.filename().string();
    }

    if (contentRoot_.empty()) {
        consolePanel_.log(ConsolePanel::Level::Warning,
            "No project found. Asset browser will be empty. "
            "Run FPSGameEditor.exe from the FPSGame directory, or pass --project <path>.");
    }

    if (!contentRoot_.empty()) {
        // Root the asset browser at the assets/ subdirectory, not the whole project root.
        const std::filesystem::path assetsDir =
            std::filesystem::path(contentRoot_) / "assets";
        assetPanel_.setRoot(assetsDir);

        // Load physics material and collision layer configs from the content root.
        const std::string physMatPath = contentRoot_ + "/config/physics_materials.toml";
        physMatTable_.load(physMatPath);

        const std::string layersPath = contentRoot_ + "/config/collision_layers.toml";
        loadQueryFilterFromToml(globalQueryFilter_, layersPath);

        prefsPath_ = std::filesystem::path(contentRoot_) / "editor_prefs.toml";
        prefs_.loadFromDisk(prefsPath_);
        pie_.setPiePort(prefs_.piePort());

        // PS5 — Scan assets/ for .prefab files and register each one with the
        // entity factory so they can be spawned by name at runtime.
        try {
            for (const auto& entry :
                 std::filesystem::recursive_directory_iterator(assetsDir)) {
                if (!entry.is_regular_file()) continue;
                if (entry.path().extension() != ".prefab") continue;

                const std::string stemName  = entry.path().stem().string();
                const std::filesystem::path prefabPath = entry.path();

                entityFactory_.registerArchetype(
                    stemName,
                    [prefabPath](core::ecs::Entity /*e*/,
                                 const core::ecs::SpawnParams& params,
                                 core::ecs::World& world) {
                        auto data = tools::PrefabSerializer::load(prefabPath);
                        if (data) {
                            tools::PrefabSerializer::instantiate(*data, params, world);
                        }
                    });
            }
        } catch (const std::filesystem::filesystem_error& /*e*/) {
            // assets/ directory may not exist yet — silently skip.
        }
    }

    assetPanel_.setOpenSceneCallback([this](const std::filesystem::path& p) {
        openScene(p.wstring());
    });
    assetPanel_.setImportCallback([this](const std::filesystem::path& sourcePath) {
        if (contentRoot_.empty()) {
            consolePanel_.log(ConsolePanel::Level::Warning,
                "No project loaded — launch with --project <path> before importing");
            return;
        }
        AssetImportSettings settings{};
        const std::filesystem::path outputDir =
            std::filesystem::path(contentRoot_) / "assets" / "meshes";
        if (!importer_.beginImport(sourcePath, outputDir, settings)) {
            consolePanel_.log(ConsolePanel::Level::Warning, "Import already in progress");
        }
    });
    assetPanel_.setImportWithSettingsCallback(
        [this](const std::filesystem::path& sourcePath, const AssetImportSettings& settings) {
            if (contentRoot_.empty()) {
                consolePanel_.log(ConsolePanel::Level::Warning,
                    "No project loaded — launch with --project <path> before importing");
                return;
            }
            const std::filesystem::path outputDir =
                std::filesystem::path(contentRoot_) / "assets" / "meshes";
            if (!importer_.beginImport(sourcePath, outputDir, settings)) {
                consolePanel_.log(ConsolePanel::Level::Warning, "Import already in progress");
            }
        });
    assetPanel_.setPreviewCallback([this](const std::filesystem::path& path) {
        previewPanel_.loadAsset(path);
    });
    assetPanel_.setImportTextureCallback([this](const std::filesystem::path& sourcePath) {
        if (contentRoot_.empty()) {
            consolePanel_.log(ConsolePanel::Level::Warning,
                "No project loaded — launch with --project <path> before importing");
            return;
        }
        const std::filesystem::path outputPath =
            sourcePath.parent_path() / (sourcePath.stem().string() + ".easset");
        const tools::ImportResult r = tools::importPng(sourcePath, outputPath);
        if (r.ok) {
            char buf[512];
            std::snprintf(buf, sizeof(buf), "Texture imported: %s",
                          outputPath.filename().string().c_str());
            consolePanel_.log(ConsolePanel::Level::Info, buf);
            assetPanel_.refresh();
        } else {
            char buf[512];
            std::snprintf(buf, sizeof(buf), "PNG import failed: %s", r.errorMessage.c_str());
            consolePanel_.log(ConsolePanel::Level::Error, buf);
        }
    });
    assetPanel_.setInstantiatePrefabCallback([this](const std::filesystem::path& prefabPath) {
        if (!activeScene_) return;
        auto data = tools::PrefabSerializer::load(prefabPath);
        if (!data) {
            consolePanel_.log(ConsolePanel::Level::Error, "Failed to load prefab");
            return;
        }
        core::ecs::SpawnParams params{};
        core::ecs::Entity root = tools::PrefabSerializer::instantiate(
            *data, params, activeScene_->world());
        if (root != core::ecs::kInvalidEntity) {
            core::ecs::PrefabInstance pi{};
            const std::string relPath = prefabPath.filename().string();
            strncpy_s(pi.sourcePrefabPath, sizeof(pi.sourcePrefabPath),
                      relPath.c_str(), _TRUNCATE);
            activeScene_->world().addComponent<core::ecs::PrefabInstance>(root, pi);
            selected_ = root;
            sceneDirty_ = true;
            consolePanel_.log(ConsolePanel::Level::Info, "Prefab instantiated");
        }
    });

    hierarchyPanel_.setEntityFactory(&entityFactory_, &sceneDirty_);
    hierarchyPanel_.setSaveAsPrefabCallback(
        [this](core::ecs::Entity entity, core::ecs::World& world) {
            const auto path = FileDialog::saveFile(
                L"Prefab Files\0*.prefab\0All Files\0*.*\0",
                L"prefab",
                L"Save as Prefab");
            if (path.empty()) return;
            const std::filesystem::path fsPath(path);
            tools::PrefabSerializer::PrefabData data =
                tools::PrefabSerializer::capture(entity, world);
            data.name = fsPath.stem().string();
            if (tools::PrefabSerializer::save(data, fsPath)) {
                undo_.push(std::make_unique<SaveAsPrefabCommand>(fsPath));
                assetPanel_.refresh();
                sceneDirty_ = true;
                char buf[512];
                std::snprintf(buf, sizeof(buf), "Saved prefab: %s",
                              fsPath.filename().string().c_str());
                consolePanel_.log(ConsolePanel::Level::Info, buf);
            } else {
                consolePanel_.log(ConsolePanel::Level::Error, "Failed to save prefab");
            }
        });

    consolePanel_.log(ConsolePanel::Level::Info, "Editor initialized");
}

void EditorApp::newScene() {
    // E1 — Guard against discarding unsaved changes.
    if (sceneDirty_) {
        pendingAction_ = PendingAction::NewScene;
        confirmDiscardChanges();
        return;
    }

    sceneManager_.unload("EditorScene");
    activeScene_ = sceneManager_.load("EditorScene");   // load() also calls Scene::load()
    if (activeScene_) {
        wireScene(*activeScene_);
        sceneManager_.activate("EditorScene");          // activate() builds BVH + marks active
    }
    selected_ = core::ecs::kInvalidEntity;
    undo_.clear();
    sceneDirty_ = false;
    currentScenePath_.clear();
    consolePanel_.log(ConsolePanel::Level::Info, "New scene created");
}

void EditorApp::openScene(const std::wstring& path) {
    if (path.empty()) return;

    // E1 — Guard against discarding unsaved changes.
    if (sceneDirty_) {
        pendingOpenPath_ = path;
        pendingAction_   = PendingAction::OpenScene;
        confirmDiscardChanges();
        return;
    }

    const std::filesystem::path fsPath(path);

    // E3 / E8 — Validate before destroying the current scene.
    if (!tools::SceneSerializer::validate(fsPath)) {
        char buf[512];
        std::snprintf(buf, sizeof(buf), "Cannot open scene: file is corrupt or has an invalid format.\n%ls", path.c_str());
        consolePanel_.log(ConsolePanel::Level::Error, buf);
        errorMsg_ = buf;
        ImGui::OpenPopup("##ErrorDlg");
        return;
    }

    sceneManager_.unload("EditorScene");
    activeScene_ = sceneManager_.load("EditorScene");
    if (!activeScene_) {
        consolePanel_.log(ConsolePanel::Level::Error, "Failed to create scene slot");
        return;
    }

    if (!tools::SceneSerializer::load(*activeScene_, fsPath)) {
        consolePanel_.log(ConsolePanel::Level::Error, "Failed to load scene file");
        errorMsg_ = "Failed to load scene file. The file may be corrupt or partially written.";
        ImGui::OpenPopup("##ErrorDlg");
        sceneManager_.unload("EditorScene");
        activeScene_ = nullptr;
        newScene();
        return;
    }

    wireScene(*activeScene_);
    sceneManager_.activate("EditorScene");
    selected_ = core::ecs::kInvalidEntity;
    undo_.clear();
    sceneDirty_ = false;
    currentScenePath_ = path;

    // E8 — Warn on partial load: file is non-trivial in size but the world
    // ended up with zero entities, which likely means a section was truncated.
    {
        std::error_code ec;
        const auto fileSize = std::filesystem::file_size(fsPath, ec);
        if (!ec && fileSize > 512) {
            uint32_t count = 0;
            activeScene_->world().forEachEntity([&count](core::ecs::Entity) { ++count; });
            if (count == 0) {
                errorMsg_ = "Scene loaded but contains no entities. The file may have been partially written.";
                ImGui::OpenPopup("##ErrorDlg");
            }
        }
    }

    if (!prefsPath_.empty()) {
        prefs_.addRecentScene(fsPath);
        prefs_.saveToDisk(prefsPath_);
    }

    char buf[512];
    std::snprintf(buf, sizeof(buf), "Opened: %ls", path.c_str());
    consolePanel_.log(ConsolePanel::Level::Info, buf);
}

void EditorApp::saveScene(const std::wstring& path) {
    if (path.empty()) {
        saveSceneDialog();
        return;
    }

    if (!activeScene_) {
        consolePanel_.log(ConsolePanel::Level::Error, "No active scene to save");
        return;
    }

    const std::filesystem::path fsPath(path);
    if (!tools::SceneSerializer::save(*activeScene_, fsPath)) {
        consolePanel_.log(ConsolePanel::Level::Error, "Failed to save scene file");
        errorMsg_ = "Failed to save scene. Check that the disk is not full and the path is writable.";
        ImGui::OpenPopup("##ErrorDlg");
        return;
    }

    currentScenePath_ = path;
    sceneDirty_ = false;

    if (!prefsPath_.empty()) {
        prefs_.addRecentScene(fsPath);
        prefs_.saveToDisk(prefsPath_);
    }

    char buf[512];
    std::snprintf(buf, sizeof(buf), "Saved: %ls", path.c_str());
    consolePanel_.log(ConsolePanel::Level::Info, buf);
}

void EditorApp::openSceneDialog() {
    std::wstring scenesDir;
    if (!contentRoot_.empty())
        scenesDir = (std::filesystem::path(contentRoot_) / "scenes").wstring();

    const auto p = FileDialog::openFile(
        L"Scene Files\0*.scene\0All Files\0*.*\0",
        L"Open Scene",
        scenesDir.empty() ? nullptr : scenesDir.c_str());
    if (!p.empty()) openScene(p.wstring());
}

void EditorApp::saveSceneDialog() {
    std::wstring scenesDir;
    if (!contentRoot_.empty())
        scenesDir = (std::filesystem::path(contentRoot_) / "scenes").wstring();

    const auto p = FileDialog::saveFile(
        L"Scene Files\0*.scene\0All Files\0*.*\0",
        L"scene",
        L"Save Scene",
        scenesDir.empty() ? nullptr : scenesDir.c_str());
    if (!p.empty()) saveScene(p.wstring());
}

// ---------------------------------------------------------------------------
// E1 — Unsaved-changes modal
// ---------------------------------------------------------------------------
bool EditorApp::confirmDiscardChanges() {
    // If there is nothing to guard, the action can proceed immediately.
    if (!sceneDirty_) return true;

    // Open the modal (idempotent if already open).
    if (!unsavedModalOpen_) {
        ImGui::OpenPopup("##UnsavedChanges");
        unsavedModalOpen_ = true;
    }
    return false; // resolved asynchronously via the modal drawn in frame()
}

void EditorApp::executePendingAction() {
    const PendingAction action = pendingAction_;
    const std::wstring  path   = pendingOpenPath_;
    pendingAction_    = PendingAction::None;
    pendingOpenPath_.clear();
    unsavedModalOpen_ = false;

    switch (action) {
    case PendingAction::NewScene:
        // sceneDirty_ is already false at this point; recurse safely.
        newScene();
        break;
    case PendingAction::OpenScene:
        openScene(path);
        break;
    case PendingAction::CloseWindow:
        running_ = false;
        break;
    default:
        break;
    }
}

void EditorApp::drawErrorDialog() {
    if (ImGui::BeginPopupModal("##ErrorDlg", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(errorMsg_.c_str());
        ImGui::Spacing();
        if (ImGui::Button("OK")) {
            errorMsg_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// ---------------------------------------------------------------------------
// createViewportRt — creates (or re-creates on resize) the offscreen RT.
// ---------------------------------------------------------------------------
void EditorApp::createViewportRt(uint32_t w, uint32_t h) {
    auto* d3dDevice = static_cast<ID3D12Device*>(gpu_->device->nativeDevice());

    // Release old resources (GPU must be idle before this is called).
    gpu_->viewportColorRt.Reset();
    gpu_->viewportDepthRt.Reset();

    // Create color RT: R8G8B8A8_UNORM, initial state = RENDER_TARGET.
    {
        D3D12_HEAP_PROPERTIES hp = {};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC rd = {};
        rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width            = w;
        rd.Height           = h;
        rd.DepthOrArraySize = 1;
        rd.MipLevels        = 1;
        rd.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
        rd.SampleDesc       = { 1, 0 };
        rd.Flags            = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        D3D12_CLEAR_VALUE cv = {};
        cv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        cv.Color[0] = 0.1f; cv.Color[1] = 0.1f; cv.Color[2] = 0.12f; cv.Color[3] = 1.0f;

        HRESULT hr = d3dDevice->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_RENDER_TARGET, &cv,
            IID_PPV_ARGS(&gpu_->viewportColorRt));
        ENGINE_ASSERT(SUCCEEDED(hr), "createViewportRt: failed to create color RT");
    }

    // Create depth RT: D32_FLOAT, initial state = DEPTH_WRITE, clear depth = 0.0 (reverse-Z).
    {
        D3D12_HEAP_PROPERTIES hp = {};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC rd = {};
        rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width            = w;
        rd.Height           = h;
        rd.DepthOrArraySize = 1;
        rd.MipLevels        = 1;
        rd.Format           = DXGI_FORMAT_D32_FLOAT;
        rd.SampleDesc       = { 1, 0 };
        rd.Flags            = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        D3D12_CLEAR_VALUE cv = {};
        cv.Format             = DXGI_FORMAT_D32_FLOAT;
        cv.DepthStencil.Depth = 0.0f; // Reverse-Z: far = 0

        HRESULT hr = d3dDevice->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_DEPTH_WRITE, &cv,
            IID_PPV_ARGS(&gpu_->viewportDepthRt));
        ENGINE_ASSERT(SUCCEEDED(hr), "createViewportRt: failed to create depth RT");
    }

    // Create RTV heap (1 slot) if not yet created.
    if (!gpu_->viewportRtvHeap) {
        D3D12_DESCRIPTOR_HEAP_DESC hd = {};
        hd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        hd.NumDescriptors = 1;
        hd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        HRESULT hr = d3dDevice->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&gpu_->viewportRtvHeap));
        ENGINE_ASSERT(SUCCEEDED(hr), "createViewportRt: failed to create RTV heap");
        gpu_->viewportRtvCpu = gpu_->viewportRtvHeap->GetCPUDescriptorHandleForHeapStart();
    }
    d3dDevice->CreateRenderTargetView(gpu_->viewportColorRt.Get(), nullptr, gpu_->viewportRtvCpu);

    // Create DSV heap (1 slot) if not yet created.
    if (!gpu_->viewportDsvHeap) {
        D3D12_DESCRIPTOR_HEAP_DESC hd = {};
        hd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        hd.NumDescriptors = 1;
        hd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        HRESULT hr = d3dDevice->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&gpu_->viewportDsvHeap));
        ENGINE_ASSERT(SUCCEEDED(hr), "createViewportRt: failed to create DSV heap");
        gpu_->viewportDsvCpu = gpu_->viewportDsvHeap->GetCPUDescriptorHandleForHeapStart();
    }
    {
        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format        = DXGI_FORMAT_D32_FLOAT;
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        d3dDevice->CreateDepthStencilView(gpu_->viewportDepthRt.Get(), &dsvDesc, gpu_->viewportDsvCpu);
    }

    // Allocate SRV slot in the shared ImGui heap — only once; on resize reuse the same slot.
    if (!gpu_->viewportSrvAllocated) {
        uint32_t slot           = gpu_->imguiNextDescSlot++;
        gpu_->viewportSrvCpu    = gpu_->imguiHeap->GetCPUDescriptorHandleForHeapStart().ptr
                                  + static_cast<SIZE_T>(slot) * gpu_->imguiDescSize;
        gpu_->viewportSrvGpu    = gpu_->imguiHeap->GetGPUDescriptorHandleForHeapStart().ptr
                                  + static_cast<UINT64>(slot) * gpu_->imguiDescSize;
        gpu_->viewportSrvAllocated = true;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels     = 1;
    D3D12_CPU_DESCRIPTOR_HANDLE srvCpuHandle{ gpu_->viewportSrvCpu };
    d3dDevice->CreateShaderResourceView(gpu_->viewportColorRt.Get(), &srvDesc, srvCpuHandle);

    gpu_->viewportRtWidth  = w;
    gpu_->viewportRtHeight = h;
}

// ---------------------------------------------------------------------------
// wireScene — set up mesh load/unload delegates for a scene.
// ---------------------------------------------------------------------------
void EditorApp::wireScene(core::scene::Scene& scene) {
    scene.setMeshLoadFn([this](core::ecs::Entity e, const std::string& assetPath) {
        pendingMeshLoads_.push_back({ assetPath, e.index, e.generation });
    });
    scene.setMeshUnloadFn([this]() {
        meshRenderSystem_.clear();
        pendingMeshLoads_.clear();
        registeredMeshPaths_.clear();
    });
}

// ---------------------------------------------------------------------------
// createPieWindow / destroyPieWindow
// ---------------------------------------------------------------------------
void EditorApp::createPieWindow() {
    auto* d3dDevice = static_cast<ID3D12Device*>(gpu_->device->nativeDevice());
    auto* cmdQueue  = static_cast<ID3D12CommandQueue*>(gpu_->device->nativeCommandQueue());

    pieWindow_ = std::make_unique<PieWindow>();
    g_pieWindow = pieWindow_.get();

    // Register the PIE window class once per process.
    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSEXW wc   = {};
        wc.cbSize        = sizeof(wc);
        wc.style         = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc   = PieWndProc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.hCursor       = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
        wc.lpszClassName = L"PieGameWindow";
        RegisterClassExW(&wc);
        classRegistered = true;
    }

    // Calculate window size that yields the requested 1280×720 client area.
    RECT wr = { 0, 0, 1280, 720 };
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);

    pieWindow_->hwnd = CreateWindowExW(
        0, L"PieGameWindow", L"Game (Play In Editor)",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        wr.right - wr.left, wr.bottom - wr.top,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    ENGINE_ASSERT(pieWindow_->hwnd, "createPieWindow: CreateWindowEx failed");

    ShowWindow(pieWindow_->hwnd, SW_SHOW);
    UpdateWindow(pieWindow_->hwnd);

    // Read actual client dimensions (AdjustWindowRect is approximate on HiDPI).
    RECT cr;
    GetClientRect(pieWindow_->hwnd, &cr);
    pieWindow_->width  = std::max(1u, static_cast<uint32_t>(cr.right  - cr.left));
    pieWindow_->height = std::max(1u, static_cast<uint32_t>(cr.bottom - cr.top));

    // Create DXGI swapchain sharing the existing device's command queue.
    Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    ENGINE_ASSERT(SUCCEEDED(hr), "createPieWindow: CreateDXGIFactory1 failed");

    DXGI_SWAP_CHAIN_DESC1 scd = {};
    scd.BufferCount      = rendering::GpuDevice::kBackBufferCount;
    scd.Width            = pieWindow_->width;
    scd.Height           = pieWindow_->height;
    scd.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.SwapEffect       = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scd.SampleDesc.Count = 1;

    Microsoft::WRL::ComPtr<IDXGISwapChain1> sc1;
    hr = factory->CreateSwapChainForHwnd(cmdQueue, pieWindow_->hwnd, &scd, nullptr, nullptr, &sc1);
    ENGINE_ASSERT(SUCCEEDED(hr), "createPieWindow: CreateSwapChainForHwnd failed");
    sc1.As(&pieWindow_->swapChain);

    // RTV heap — one slot per back buffer.
    {
        D3D12_DESCRIPTOR_HEAP_DESC hd = {};
        hd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        hd.NumDescriptors = rendering::GpuDevice::kBackBufferCount;
        hd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        hr = d3dDevice->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&pieWindow_->rtvHeap));
        ENGINE_ASSERT(SUCCEEDED(hr), "createPieWindow: RTV heap creation failed");
    }

    const uint32_t rtvStride =
        d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    const D3D12_CPU_DESCRIPTOR_HANDLE rtvBase =
        pieWindow_->rtvHeap->GetCPUDescriptorHandleForHeapStart();

    for (uint32_t i = 0; i < rendering::GpuDevice::kBackBufferCount; ++i) {
        hr = pieWindow_->swapChain->GetBuffer(i, IID_PPV_ARGS(&pieWindow_->backBuffers[i]));
        ENGINE_ASSERT(SUCCEEDED(hr), "createPieWindow: GetBuffer failed");
        pieWindow_->rtvHandles[i].ptr = rtvBase.ptr + static_cast<SIZE_T>(i) * rtvStride;
        d3dDevice->CreateRenderTargetView(
            pieWindow_->backBuffers[i].Get(), nullptr, pieWindow_->rtvHandles[i]);
    }

    // Depth buffer for PIE rendering (reverse-Z, cleared to 0.0 = far).
    {
        D3D12_HEAP_PROPERTIES hp = {};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC rd = {};
        rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width            = pieWindow_->width;
        rd.Height           = pieWindow_->height;
        rd.DepthOrArraySize = 1;
        rd.MipLevels        = 1;
        rd.Format           = DXGI_FORMAT_D32_FLOAT;
        rd.SampleDesc       = { 1, 0 };
        rd.Flags            = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        D3D12_CLEAR_VALUE cv = {};
        cv.Format             = DXGI_FORMAT_D32_FLOAT;
        cv.DepthStencil.Depth = 0.0f; // Reverse-Z convention

        hr = d3dDevice->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_DEPTH_WRITE, &cv,
            IID_PPV_ARGS(&pieWindow_->depthRt));
        ENGINE_ASSERT(SUCCEEDED(hr), "createPieWindow: depth RT creation failed");

        D3D12_DESCRIPTOR_HEAP_DESC dsvHd = {};
        dsvHd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvHd.NumDescriptors = 1;
        dsvHd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        hr = d3dDevice->CreateDescriptorHeap(&dsvHd, IID_PPV_ARGS(&pieWindow_->dsvHeap));
        ENGINE_ASSERT(SUCCEEDED(hr), "createPieWindow: DSV heap creation failed");

        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format        = DXGI_FORMAT_D32_FLOAT;
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        pieWindow_->dsvHandle = pieWindow_->dsvHeap->GetCPUDescriptorHandleForHeapStart();
        d3dDevice->CreateDepthStencilView(
            pieWindow_->depthRt.Get(), &dsvDesc, pieWindow_->dsvHandle);
    }

    // Wire FrameGraph device so pieFrameGraph_ can compile/execute.
    rendering::frameGraphSetDevice(pieFrameGraph_, d3dDevice);

    // Register raw input on the PIE window so WM_INPUT delivers keyboard and
    // mouse events to PieWndProc while PIE is running.
    core::InputSystem::registerRawInput(pieWindow_->hwnd);
}

void EditorApp::destroyPieWindow() {
    if (!pieWindow_) return;
    gpu_->device->flush(); // drain all in-flight GPU work that touches PIE backbuffers
    for (auto& bb : pieWindow_->backBuffers) bb.Reset();
    pieWindow_->depthRt.Reset();
    pieWindow_->rtvHeap.Reset();
    pieWindow_->dsvHeap.Reset();
    pieWindow_->swapChain.Reset();
    if (pieWindow_->hwnd) {
        DestroyWindow(pieWindow_->hwnd);
        pieWindow_->hwnd = nullptr;
    }
    g_pieWindow = nullptr;
    pieWindow_.reset();
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

    if (!dockBuilt_) {
        dockBuilt_ = true;

        ImGui::DockBuilderRemoveNode(dockId);
        ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockId, ImGui::GetMainViewport()->WorkSize);

        // Split bottom strip (25%) for Assets + Console.
        ImGuiID topId, bottomId;
        ImGui::DockBuilderSplitNode(dockId,  ImGuiDir_Down, 0.25f, &bottomId, &topId);

        // Split top row: left sidebar (20%), then right sidebar (25% of remainder).
        ImGuiID leftId, midRightId;
        ImGui::DockBuilderSplitNode(topId,   ImGuiDir_Left, 0.20f, &leftId,   &midRightId);

        ImGuiID centerId, rightId;
        ImGui::DockBuilderSplitNode(midRightId, ImGuiDir_Right, 0.25f, &rightId, &centerId);

        // Split bottom row: Assets (left 50%) + Console (right 50%).
        ImGuiID assetsId, consoleId;
        ImGui::DockBuilderSplitNode(bottomId, ImGuiDir_Left, 0.5f, &assetsId, &consoleId);

        ImGui::DockBuilderDockWindow("Hierarchy", leftId);
        ImGui::DockBuilderDockWindow("Viewport",  centerId);
        ImGui::DockBuilderDockWindow("Inspector", rightId);
        ImGui::DockBuilderDockWindow("Assets",    assetsId);
        ImGui::DockBuilderDockWindow("Console",   consoleId);

        ImGui::DockBuilderFinish(dockId);
    }

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
        if (ImGui::MenuItem("New Scene",     "Ctrl+N"))       newScene();
        if (ImGui::MenuItem("Open Scene...", "Ctrl+O"))       openSceneDialog();
        ImGui::Separator();
        if (ImGui::MenuItem("Save Scene",    "Ctrl+S"))       saveScene(currentScenePath_);
        if (ImGui::MenuItem("Save Scene As...","Ctrl+Shift+S")) saveSceneDialog();
        ImGui::Separator();
        if (ImGui::MenuItem("Exit")) {
            if (!sceneDirty_) {
                running_ = false;
            } else {
                pendingAction_    = PendingAction::CloseWindow;
                closePromptShown_ = true;
                confirmDiscardChanges();
            }
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit")) {
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, undo_.canUndo())) undo_.undo();
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, undo_.canRedo())) undo_.redo();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Window")) {
        ImGui::MenuItem("Hierarchy",          nullptr, &showHierarchy_);
        ImGui::MenuItem("Inspector",          nullptr, &showInspector_);
        ImGui::MenuItem("Viewport",           nullptr, &showViewport_);
        ImGui::MenuItem("Assets",             nullptr, &showAssets_);
        ImGui::MenuItem("Console",            nullptr, &showConsole_);
        ImGui::Separator();
        ImGui::MenuItem("Scene Properties",   nullptr, &showSceneProps_);
        ImGui::MenuItem("Physics Materials",  nullptr, &showPhysMats_);
        ImGui::MenuItem("Collision Layers",   nullptr, &showCollisionLayers_);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Play")) {
        const bool playing = pie_.isPlaying();
        if (ImGui::MenuItem("Play", nullptr, false, !playing && activeScene_)) {
            if (activeScene_ && editorPhysicsWorld_) {
                // Wire the physics step delegate on the scene before starting PIE.
                // The delegate is detached when PIE stops so editor-mode scene ticks
                // do not drive physics.
                activeScene_->setPhysicsStepFn(
                    [this](float dt) { editorPhysicsWorld_->step(dt); });
                prePieCameraState_ = camera_.save();
                // Open the game window before pie_.start() so mouse capture
                // can be directed to the PIE window rather than the editor.
                createPieWindow();
                pie_.start(*activeScene_, camera_.position());
                if (pie_.isCapturingMouse() && prefs_.pieMouseCapture()) {
                    HWND pieHwnd = pieWindow_ ? pieWindow_->hwnd : nullptr;
                    if (pieHwnd) {
                        ShowCursor(FALSE);
                        RECT r;
                        GetClientRect(pieHwnd, &r);
                        MapWindowPoints(pieHwnd, nullptr, reinterpret_cast<POINT*>(&r), 2);
                        ClipCursor(&r);
                        SetCapture(pieHwnd);
                    }
                    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouse;
                }
            }
        }
        if (ImGui::MenuItem("Stop", nullptr, false, playing)) {
            if (activeScene_) {
                activeScene_->setPhysicsStepFn(nullptr);
            }
            pie_.stop();
            camera_.restore(prePieCameraState_);
            if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NoMouse) {
                ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
                ClipCursor(nullptr);
                ReleaseCapture();
                ShowCursor(TRUE);
            }
            destroyPieWindow();
        }
        ImGui::EndMenu();
    }

    // Status on the right.
    char status[128];
    std::snprintf(status, sizeof(status), "%s%s%s",
                  projectName_.c_str(),
                  sceneDirty_      ? "  [Modified]" : "",
                  pie_.isPlaying() ? "  [PLAYING]"  : "");
    const float tw = ImGui::CalcTextSize(status).x;
    ImGui::SameLine(ImGui::GetWindowWidth() - tw - 20.0f);
    ImGui::TextUnformatted(status);
}

void EditorApp::drawPanels() {
    if (!activeScene_) return;
    core::ecs::World& world = activeScene_->world();

    // When PIE is active and there is NO separate game window, mirror the first
    // player entity's Transform into the editor camera so the viewport shows
    // the player's point of view. When a PIE window is open the game view lives
    // there, so the editor viewport stays at the author's camera position.
    const bool pieActive = pie_.isUsingPlayerCamera();
    if (pieActive && !pieWindow_) {
        core::ecs::View<core::Transform, core::input::InputReceiverComponent> view(world);
        auto it = view.begin();
        if (it != view.end()) {
            auto [e, tr, recv] = *it;
            static_cast<void>(e);
            static_cast<void>(recv);

            // Decompose quaternion into yaw/pitch for EditorCamera.
            // Uses the same YXZ Euler decomposition as the Transform widget.
            const core::math::Mat4 rm    = core::math::toMat4(tr.rotation);
            const float            sinP  = -rm.m[2][1];
            const float            pitch = std::asin(std::clamp(sinP, -1.0f, 1.0f));
            const float            cp    = std::cos(pitch);
            const float            yaw   = cp > 0.0001f
                                               ? std::atan2(rm.m[2][0], rm.m[2][2])
                                               : std::atan2(-rm.m[0][2], rm.m[0][0]);
            camera_.setFirstPersonView(tr.position, yaw, pitch);
        }
    }

    if (showHierarchy_) {
        selected_ = hierarchyPanel_.draw(world, selected_, undo_, &showHierarchy_);
    }
    if (showInspector_) {
        inspectorPanel_->draw(world, selected_, &showInspector_);
    }
    if (showViewport_) {
        viewportPanel_.setPieActive(pieActive);
        viewportPanel_.draw(world, selected_, camera_, picking_, undo_,
                            gpu_->viewportSrvGpu, &showViewport_, viewMode_);
    }
    if (showAssets_) {
        assetPanel_.draw(&showAssets_);
    }
    if (showConsole_) {
        consolePanel_.draw(&showConsole_);
    }
    if (showSceneProps_ && activeScene_) {
        if (scenePropsPanel_.draw(activeScene_->globals(), &showSceneProps_)) {
            sceneDirty_ = true;
        }
    }
    if (showPhysMats_) {
        const std::string physMatPath = contentRoot_.empty()
            ? "config/physics_materials.toml"
            : contentRoot_ + "/config/physics_materials.toml";
        physMatPanel_.draw(physMatTable_, physMatPath, &showPhysMats_);
    }
    if (showCollisionLayers_) {
        const std::string layersPath = contentRoot_.empty()
            ? "config/collision_layers.toml"
            : contentRoot_ + "/config/collision_layers.toml";
        collisionLayerPanel_.draw(globalQueryFilter_, layersPath, &showCollisionLayers_);
    }
}


void EditorApp::frame() {
    // E2 — Update window title whenever the desired title changes.
    {
        std::wstring title;
        if (currentScenePath_.empty()) {
            title = L"EngineEditor — Untitled";
        } else {
            title = L"EngineEditor — " +
                    std::filesystem::path(currentScenePath_).filename().wstring();
        }
        if (sceneDirty_) title += L'*';
        if (title != lastWindowTitle_) {
            lastWindowTitle_ = title;
            HWND hwnd = static_cast<HWND>(gpu_->window->nativeHandle());
            SetWindowTextW(hwnd, title.c_str());
        }
    }

    // Poll background import job.
    importer_.tick([this](const EditorImporter::Result& r) {
        if (r.succeeded) {
            char buf[512];
            std::snprintf(buf, sizeof(buf), "Import complete: %s",
                          r.eassetPath.filename().string().c_str());
            consolePanel_.log(ConsolePanel::Level::Info, buf);
            assetPanel_.refresh();
        } else {
            consolePanel_.log(ConsolePanel::Level::Error, r.errorMessage.c_str());
        }
    });

    // If the user closed the PIE game window, treat it as pressing Stop.
    if (pieWindow_ && pieWindow_->wantsClose) {
        if (activeScene_) activeScene_->setPhysicsStepFn(nullptr);
        pie_.stop();
        camera_.restore(prePieCameraState_);
        if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NoMouse) {
            ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
            ClipCursor(nullptr);
            ReleaseCapture();
            ShowCursor(TRUE);
        }
        destroyPieWindow();
    }

    // PIE advances the simulation while playing.
    if (pie_.isPlaying()) {
        ImGuiIO& io = ImGui::GetIO();
        pie_.tick(io.DeltaTime > 0.0f ? io.DeltaTime : 1.0f / 64.0f);
    }

    // Viewport resize check — must happen before beginFrame.
    {
        const uint32_t newW = std::max(1u, static_cast<uint32_t>(viewportPanel_.contentWidth()));
        const uint32_t newH = std::max(1u, static_cast<uint32_t>(viewportPanel_.contentHeight()));
        if (newW != gpu_->viewportRtWidth || newH != gpu_->viewportRtHeight) {
            gpu_->device->flush();
            createViewportRt(newW, newH);
        }
    }

    gpu_->device->beginFrame();

    // Get command list (must happen after beginFrame).
    auto* cmdList = static_cast<ID3D12GraphicsCommandList*>(gpu_->device->nativeCommandList());

    // Lazy-init MeshManager (CLAUDE.md: must be constructed after beginFrame).
    if (!meshManager_) {
        meshManager_ = std::make_unique<rendering::MeshManager>(*gpu_->device);
        thumbnailRenderer_.setMeshManager(meshManager_.get());
        previewPanel_.setMeshManager(meshManager_.get());
    }
    // Sync cmdList every frame: GpuDevice rotates per-frame command list objects,
    // so the pointer captured at MeshManager construction goes stale after frame 0.
    meshManager_->syncCommandList(cmdList);

    // Scan for MeshHandle entities that were added or modified after scene activation.
    // Also detects asset path changes (e.g. user picks a different mesh in the inspector)
    // and component removal/re-addition (old handle is evicted so the new path uploads).
    if (activeScene_ && meshManager_) {
        activeScene_->world().forEachEntity([&](core::ecs::Entity e) {
            const auto* mh = activeScene_->world().tryGet<core::MeshHandle>(e);

            // No component or cleared to (none) — drop any stale GPU handle.
            if (!mh || mh->assetPath[0] == '\0') {
                if (meshRenderSystem_.hasHandle(e.index)) {
                    meshRenderSystem_.unregisterHandle(e.index);
                    registeredMeshPaths_.erase(e.index);
                }
                return;
            }

            // Already loaded with the same path — nothing to do.
            auto it = registeredMeshPaths_.find(e.index);
            if (it != registeredMeshPaths_.end() && it->second == mh->assetPath) return;

            // Path changed or first load — evict the old handle so the new one takes over.
            if (meshRenderSystem_.hasHandle(e.index)) {
                meshRenderSystem_.unregisterHandle(e.index);
                registeredMeshPaths_.erase(e.index);
            }

            // Don't queue the same entity twice in one frame.
            for (const auto& p : pendingMeshLoads_) {
                if (p.entityIndex == e.index) return;
            }

            char info[512];
            std::snprintf(info, sizeof(info), "Loading mesh: %s", mh->assetPath);
            consolePanel_.log(ConsolePanel::Level::Info, info);
            pendingMeshLoads_.push_back({ std::string(mh->assetPath), e.index, e.generation });
        });
    }

    // Drain pending mesh uploads (entities loaded by scene activation or scan above).
    if (!pendingMeshLoads_.empty() && meshManager_) {
        for (const auto& load : pendingMeshLoads_) {
            auto cpuMesh = tools::loadEasset(std::filesystem::path(load.assetPath));
            if (cpuMesh) {
                auto gpuHandle = meshManager_->uploadStatic(
                    std::span<const rendering::VertexStatic>(cpuMesh->vertices),
                    std::span<const uint32_t>(cpuMesh->indices));
                meshRenderSystem_.registerHandle(load.entityIndex, gpuHandle);
                registeredMeshPaths_[load.entityIndex] = load.assetPath;

                // TX-4: Upload embedded textures and create a material entry.
                if (!cpuMesh->textures.empty() && activeScene_) {
                    rendering::GpuMaterial mat = {};
                    mat.albedoTextureIndex     = 0xFFFFFFFFu;
                    mat.normalTextureIndex     = 0xFFFFFFFFu;
                    mat.metallicRoughnessIndex = 0xFFFFFFFFu;
                    mat.emissiveTextureIndex   = 0xFFFFFFFFu;
                    mat.albedoFactor[0] = mat.albedoFactor[1] = mat.albedoFactor[2] = 1.0f;
                    mat.albedoFactor[3] = 1.0f;
                    mat.roughnessFactor = 0.5f;

                    const auto& texs = cpuMesh->textures;
                    if (texs.size() > 0)
                        mat.albedoTextureIndex     = meshRenderSystem_.uploadTexture(*gpu_->device, texs[0]);
                    if (texs.size() > 1)
                        mat.normalTextureIndex     = meshRenderSystem_.uploadTexture(*gpu_->device, texs[1]);
                    if (texs.size() > 2)
                        mat.metallicRoughnessIndex = meshRenderSystem_.uploadTexture(*gpu_->device, texs[2]);
                    if (texs.size() > 3)
                        mat.emissiveTextureIndex   = meshRenderSystem_.uploadTexture(*gpu_->device, texs[3]);

                    const rendering::MaterialHandle mh = materialManager_->add(mat);
                    core::ecs::Entity e{ load.entityIndex, load.entityGeneration };
                    if (auto* mhComp = activeScene_->world().tryGet<core::MeshHandle>(e))
                        mhComp->materialIndex = mh.index;
                }

                char ok[256];
                std::snprintf(ok, sizeof(ok), "Mesh uploaded (entity %u, gpu id %u)",
                              load.entityIndex, gpuHandle.id);
                consolePanel_.log(ConsolePanel::Level::Info, ok);
            } else {
                char buf[512];
                std::snprintf(buf, sizeof(buf), "Failed to load mesh: %s", load.assetPath.c_str());
                consolePanel_.log(ConsolePanel::Level::Error, buf);
            }
        }
        pendingMeshLoads_.clear();
    }

    // Process thumbnail render requests.
    if (meshManager_) {
        thumbnailRenderer_.flushPending(cmdList);
    }

    // Render scene to offscreen RT.
    bool viewportRtInSrvState = false;
    if (activeScene_ && meshManager_ && gpu_->viewportColorRt) {
        sceneFrameGraph_.reset();

        // Import color RT — it starts in RENDER_TARGET state.
        const rendering::ResourceHandle colorHandle = sceneFrameGraph_.importBackBuffer(
            gpu_->viewportColorRt.Get(),
            gpu_->viewportRtvCpu.ptr,
            static_cast<uint32_t>(D3D12_RESOURCE_STATE_RENDER_TARGET));
        const rendering::ResourceHandle depthHandle = sceneFrameGraph_.importDepthBuffer(
            gpu_->viewportDepthRt.Get(),
            gpu_->viewportDsvCpu.ptr);

        // Unconditional clear every frame so the RT doesn't retain stale geometry
        // when mesh components are removed mid-session (no draw passes = no clear otherwise).
        sceneFrameGraph_.addPass(
            "ViewportClear",
            [colorHandle, depthHandle](rendering::FrameGraph::PassBuilder& b) {
                b.write(colorHandle, static_cast<uint32_t>(D3D12_RESOURCE_STATE_RENDER_TARGET));
                b.read (depthHandle, static_cast<uint32_t>(D3D12_RESOURCE_STATE_DEPTH_WRITE));
            },
            [colorHandle, depthHandle](void* cl, const rendering::PassResources& res) {
                auto* cmd = static_cast<ID3D12GraphicsCommandList*>(cl);
                D3D12_CPU_DESCRIPTOR_HANDLE rtv; rtv.ptr = res.getRtvHandle(colorHandle);
                D3D12_CPU_DESCRIPTOR_HANDLE dsv; dsv.ptr = res.getDsvHandle(depthHandle);
                static constexpr float kClear[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
                cmd->ClearRenderTargetView(rtv, kClear, 0, nullptr);
                cmd->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0, 0, nullptr);
            });

        const float aspect = (gpu_->viewportRtHeight > 0)
            ? static_cast<float>(gpu_->viewportRtWidth) / static_cast<float>(gpu_->viewportRtHeight)
            : 1.0f;
        constexpr float kPi = 3.14159265358979f;
        const core::math::Mat4 view     = camera_.viewMatrix();
        const core::math::Mat4 proj     = core::math::perspectiveRhYupReverseZ(
            camera_.fovYDegrees * (kPi / 180.0f), aspect, camera_.nearZ, camera_.farZ);
        const core::math::Mat4 viewProj = view * proj;

        const core::math::Vec3 camPos3 = camera_.position();
        const float camPos[3] = { camPos3.x, camPos3.y, camPos3.z };
        meshRenderSystem_.tick(activeScene_->world(), *meshManager_, sceneFrameGraph_,
                               &view.m[0][0], &proj.m[0][0], &viewProj.m[0][0],
                               colorHandle, depthHandle,
                               gpu_->viewportRtWidth, gpu_->viewportRtHeight, 0, camPos,
                               viewMode_);

        sceneFrameGraph_.compile();
        sceneFrameGraph_.execute(cmdList);

        // RENDER_TARGET → PIXEL_SHADER_RESOURCE so ImGui::Image can sample it.
        D3D12_RESOURCE_BARRIER toSrv = {};
        toSrv.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toSrv.Transition.pResource   = gpu_->viewportColorRt.Get();
        toSrv.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        toSrv.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        toSrv.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &toSrv);
        viewportRtInSrvState = true;
    }

    // Render the game view into the PIE window's current back buffer.
    if (pie_.isPlaying() && pieWindow_ && !pieWindow_->wantsClose &&
            activeScene_ && meshManager_) {
        // Build view matrix from the first entity with Camera (isMain==true).
        core::math::Mat4 pieView{};
        core::math::Mat4 pieProj{};
        core::math::Mat4 pieViewProj{};
        float pieCamPos[3] = { 0.0f, 0.0f, 0.0f };
        bool hasPieView = false;
        {
            core::ecs::View<core::Transform, rendering::Camera>
                pv(activeScene_->world());
            for (auto [e, tr, cam] : pv) {
                if (!cam.isMain) continue;
                core::math::Transform mt{};
                mt.position = tr.position;
                mt.rotation = tr.rotation;
                mt.scale    = tr.scale;
                pieView = rendering::cameraViewMatrix(mt);
                constexpr float kPi = 3.14159265358979f;
                const float aspect  = (pieWindow_->height > 0)
                    ? static_cast<float>(pieWindow_->width) /
                      static_cast<float>(pieWindow_->height)
                    : 1.0f;
                const float fovRad = cam.fovYDegrees * (kPi / 180.0f);
                pieProj     = core::math::perspectiveRhYupReverseZ(fovRad, aspect, cam.nearZ, cam.farZ);
                pieViewProj = pieView * pieProj;
                pieCamPos[0] = tr.position.x;
                pieCamPos[1] = tr.position.y;
                pieCamPos[2] = tr.position.z;
                hasPieView  = true;
                break;
            }
        }

        if (hasPieView) {
            const uint32_t bbIdx = pieWindow_->swapChain->GetCurrentBackBufferIndex();

            pieFrameGraph_.reset();

            // Import back buffer (starts in PRESENT state; FG transitions to RT).
            const rendering::ResourceHandle pieColor = pieFrameGraph_.importBackBuffer(
                pieWindow_->backBuffers[bbIdx].Get(),
                pieWindow_->rtvHandles[bbIdx].ptr,
                static_cast<uint32_t>(D3D12_RESOURCE_STATE_PRESENT));
            const rendering::ResourceHandle pieDepth = pieFrameGraph_.importDepthBuffer(
                pieWindow_->depthRt.Get(),
                pieWindow_->dsvHandle.ptr);

            pieFrameGraph_.addPass("PieClear",
                [pieColor, pieDepth](rendering::FrameGraph::PassBuilder& b) {
                    b.write(pieColor,
                            static_cast<uint32_t>(D3D12_RESOURCE_STATE_RENDER_TARGET));
                    b.read (pieDepth,
                            static_cast<uint32_t>(D3D12_RESOURCE_STATE_DEPTH_WRITE));
                },
                [pieColor, pieDepth](void* cl, const rendering::PassResources& res) {
                    auto* cmd = static_cast<ID3D12GraphicsCommandList*>(cl);
                    D3D12_CPU_DESCRIPTOR_HANDLE rtv{ res.getRtvHandle(pieColor) };
                    D3D12_CPU_DESCRIPTOR_HANDLE dsv{ res.getDsvHandle(pieDepth) };
                    static constexpr float kClear[4] = { 0.05f, 0.05f, 0.08f, 1.0f };
                    cmd->ClearRenderTargetView(rtv, kClear, 0, nullptr);
                    cmd->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0, 0, nullptr);
                });

            meshRenderSystem_.tick(activeScene_->world(), *meshManager_, pieFrameGraph_,
                                   &pieView.m[0][0], &pieProj.m[0][0], &pieViewProj.m[0][0],
                                   pieColor, pieDepth,
                                   pieWindow_->width, pieWindow_->height,
                                   /*frameSlot=*/1, pieCamPos,
                                   app::ViewMode::Lit);

            pieFrameGraph_.compile();
            pieFrameGraph_.execute(cmdList);

            // Back buffer is now in RENDER_TARGET — transition to PRESENT for flip.
            D3D12_RESOURCE_BARRIER toPresent = {};
            toPresent.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            toPresent.Transition.pResource   = pieWindow_->backBuffers[bbIdx].Get();
            toPresent.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
            toPresent.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
            toPresent.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            cmdList->ResourceBarrier(1, &toPresent);
        }
    }

    // Render mesh preview panel offscreen RT before ImGui consumes the command list.
    previewPanel_.render(cmdList);

    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    buildDockLayout();

    // Keyboard shortcuts (checked once per frame, not per-panel).
    if (!ImGui::GetIO().WantTextInput) {
        const bool ctrl  = ImGui::GetIO().KeyCtrl;
        const bool shift = ImGui::GetIO().KeyShift;
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_N, false)) newScene();
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_O, false)) openSceneDialog();
        if (ctrl && !shift && ImGui::IsKeyPressed(ImGuiKey_S, false))
            saveScene(currentScenePath_);
        if (ctrl && shift  && ImGui::IsKeyPressed(ImGuiKey_S, false))
            saveSceneDialog();
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) undo_.undo();
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) undo_.redo();
        if (pie_.isPlaying() && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            if (activeScene_) activeScene_->setPhysicsStepFn(nullptr);
            pie_.stop();
            camera_.restore(prePieCameraState_);
            if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NoMouse) {
                ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
                ClipCursor(nullptr);
                ReleaseCapture();
                ShowCursor(TRUE);
            }
            destroyPieWindow();
        }
    }

    // E1 — Unsaved-changes modal.  Must be rendered every frame while open.
    if (unsavedModalOpen_) {
        // Center on the main viewport.
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("##UnsavedChanges", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoTitleBar)) {
            ImGui::Text("You have unsaved changes.");
            ImGui::Spacing();
            if (ImGui::Button("Save")) {
                ImGui::CloseCurrentPopup();
                saveScene(currentScenePath_);
                executePendingAction();
            }
            ImGui::SameLine();
            if (ImGui::Button("Discard")) {
                ImGui::CloseCurrentPopup();
                sceneDirty_ = false;
                executePendingAction();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                ImGui::CloseCurrentPopup();
                pendingAction_    = PendingAction::None;
                pendingOpenPath_.clear();
                unsavedModalOpen_ = false;
            }
            ImGui::EndPopup();
        }
    }

    drawErrorDialog();

    drawPanels();

    ImGui::Render();

    // Record ImGui into the back buffer via the open command list.
    // cmdList was already obtained above (after beginFrame).
    ID3D12DescriptorHeap* heaps[] = { gpu_->imguiHeap.Get() };
    cmdList->SetDescriptorHeaps(1, heaps);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv{ gpu_->device->currentBackBufferRtvHandle() };
    const float clearColor[4] = { 0.10f, 0.10f, 0.12f, 1.0f };
    cmdList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
    cmdList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmdList);

    // Transition viewport RT back to RENDER_TARGET for the next frame.
    // Only needed when we transitioned it to PIXEL_SHADER_RESOURCE above.
    if (viewportRtInSrvState && gpu_->viewportColorRt) {
        D3D12_RESOURCE_BARRIER toRt = {};
        toRt.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toRt.Transition.pResource   = gpu_->viewportColorRt.Get();
        toRt.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        toRt.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
        toRt.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &toRt);
    }

    // Transition preview panel RT back to RENDER_TARGET for the next frame.
    previewPanel_.postFrameBarrier(cmdList);

    gpu_->device->endFrame();

    // Present the PIE window after the GPU work is submitted by endFrame().
    if (pieWindow_ && pieWindow_->swapChain && pie_.isPlaying()) {
        pieWindow_->swapChain->Present(1, 0);
    }
}

void EditorApp::run() {
    if (!running_) return;

    while (running_) {
        // E1 — When the OS close button is clicked (wantsClose() latches to true
        // on WM_DESTROY), guard against discarding unsaved work.
        // closePromptShown_ prevents the modal from re-opening every frame once
        // the user has dismissed it via Cancel (wantsClose remains true permanently
        // after the X is clicked on Win32).
        if (gpu_->window->wantsClose() && !unsavedModalOpen_) {
            if (!sceneDirty_) {
                // Nothing to save — exit the loop immediately.
                break;
            }
            if (!closePromptShown_) {
                // First time seeing the close request while dirty: prompt.
                closePromptShown_ = true;
                pendingAction_    = PendingAction::CloseWindow;
                confirmDiscardChanges();
                // Fall through to frame() to render the modal this frame.
            }
            // If closePromptShown_ is already true the user cancelled the
            // close; we ignore wantsClose() and let the editor keep running.
            // The user can save and then close normally via File > Exit.
        }
        frame();
    }

    gpu_->device->flush();
}

void EditorApp::shutdown() {
    if (!gpu_) return;
    if (gpu_->device) gpu_->device->flush();
    destroyPieWindow(); // release PIE window GPU resources before device teardown
    // Detach physics step delegate before stopping PIE so the scene is clean.
    if (activeScene_) {
        activeScene_->setPhysicsStepFn(nullptr);
    }
    g_pieController = nullptr;
    pie_.stop();
    if (!prefsPath_.empty()) {
        prefs_.saveToDisk(prefsPath_);
    }
    tools::Config::shutdown();
    gpu_.reset();   // tears down ImGui + device + window in Gpu::~Gpu
    LOG_INFO("EngineEditor shutdown complete");
    tools::Logger::shutdown();
}

void EditorApp::registerComponentWidgets() {
    using namespace core::math;
    constexpr float kRad2Deg = 180.0f / 3.14159265358979f;
    constexpr float kDeg2Rad = 3.14159265358979f / 180.0f;

    // Transform: position (drag), rotation (Euler degrees via YXZ decomposition), scale (drag).
    componentRegistry_.registerWidget(core::Transform::kComponentId, [=](void* data) -> bool {
        auto* tr = static_cast<core::Transform*>(data);
        bool changed = false;

        float pos[3] = { tr->position.x, tr->position.y, tr->position.z };
        if (ImGui::DragFloat3("Position", pos, 0.05f)) {
            tr->position = Vec3{ pos[0], pos[1], pos[2] };
            changed = true;
        }

        // Decompose quaternion → YXZ Euler angles (yaw/pitch/roll) for editing.
        // Derivation: toMat4 gives M[2][1]=-sin(p), M[2][0]=sin(y)*cos(p), M[2][2]=cos(y)*cos(p).
        const Mat4 rm    = toMat4(tr->rotation);
        const float sinP = -rm.m[2][1];
        const float pitch = std::asin(std::clamp(sinP, -1.0f, 1.0f));
        const float cp    = std::cos(pitch);
        const float yaw  = cp > 0.0001f ? std::atan2(rm.m[2][0], rm.m[2][2]) : std::atan2(-rm.m[0][2], rm.m[0][0]);
        const float roll = cp > 0.0001f ? std::atan2(rm.m[0][1], rm.m[1][1]) : 0.0f;
        float euler[3] = { yaw * kRad2Deg, pitch * kRad2Deg, roll * kRad2Deg };
        if (ImGui::DragFloat3("Rotation", euler, 0.5f)) {
            tr->rotation = fromEulerYxz(euler[0] * kDeg2Rad, euler[1] * kDeg2Rad, euler[2] * kDeg2Rad);
            changed = true;
        }

        float scl[3] = { tr->scale.x, tr->scale.y, tr->scale.z };
        if (ImGui::DragFloat3("Scale", scl, 0.01f, 0.0001f, 10000.0f)) {
            tr->scale = Vec3{ scl[0], scl[1], scl[2] };
            changed = true;
        }
        return changed;
    });

    // Health: current/max HP (clamped to [0, max]) and shield fraction shown as %.
    componentRegistry_.registerWidget(core::Health::kComponentId, [](void* data) -> bool {
        auto* h = static_cast<core::Health*>(data);
        bool changed = false;
        changed |= ImGui::DragFloat("Max HP",     &h->maxHp,     1.0f, 0.0f, 100000.0f);
        h->currentHp = std::clamp(h->currentHp, 0.0f, h->maxHp);
        changed |= ImGui::DragFloat("Current HP", &h->currentHp, 1.0f, 0.0f, h->maxHp);
        float shieldPct = h->shieldPercent * 100.0f;
        if (ImGui::SliderFloat("Shield %", &shieldPct, 0.0f, 100.0f, "%.1f%%")) {
            h->shieldPercent = shieldPct / 100.0f;
            changed = true;
        }
        return changed;
    });

    // Lifetime: seconds remaining before the entity is destroyed.
    componentRegistry_.registerWidget(core::Lifetime::kComponentId, [](void* data) -> bool {
        auto* l = static_cast<core::Lifetime*>(data);
        return ImGui::DragFloat("Remaining (s)", &l->remaining, 0.1f, 0.0f, 86400.0f, "%.2f s");
    });

    // TeamTag: uint8 team ID (clamped 0–255).
    componentRegistry_.registerWidget(core::TeamTag::kComponentId, [](void* data) -> bool {
        auto* t = static_cast<core::TeamTag*>(data);
        int teamId = t->teamId;
        if (ImGui::InputInt("Team ID", &teamId)) {
            t->teamId = static_cast<uint8_t>(std::clamp(teamId, 0, 255));
            return true;
        }
        return false;
    });

    // ColliderComponent: shape, dimensions, layer, material, trigger flag.
    componentRegistry_.registerWidget(core::ColliderComponent::kComponentId, [](void* data) -> bool {
        auto* c = static_cast<core::ColliderComponent*>(data);
        return drawColliderWidget(*c);
    });

    // AnimationState: clip/time display (read-only) + blend weight + force-clip PIE combo.
    componentRegistry_.registerWidget(core::AnimationState::kComponentId, [](void* data) -> bool {
        auto* s = static_cast<core::AnimationState*>(data);
        return drawAnimationStateWidget(*s);
    });

    // MeshHandle: asset path + material index + shadow flags + PBR factor overrides.
    componentRegistry_.registerWidget(core::MeshHandle::kComponentId, [this](void* data) -> bool {
        auto* m = static_cast<core::MeshHandle*>(data);
        bool changed = false;

        // Snapshot material fields for undo — captured before any mutation.
        const uint32_t oldMatIdx        = m->materialIndex;
        const bool     oldCastShadow    = m->castShadow;
        const bool     oldReceiveShadow = m->receiveShadow;

        // Build a filtered list of .easset entries from the asset browser.
        const auto& allEntries = assetPanel_.entries();
        std::vector<const AssetBrowserPanel::Entry*> meshEntries;
        int currentIdx = -1;
        for (const auto& e : allEntries) {
            if (e.type != AssetBrowserPanel::AssetType::Easset) continue;
            if (e.path.string() == m->assetPath)
                currentIdx = static_cast<int>(meshEntries.size());
            meshEntries.push_back(&e);
        }

        const char* preview = currentIdx >= 0
            ? meshEntries[currentIdx]->displayName.c_str()
            : (m->assetPath[0] != '\0' ? m->assetPath : "(none)");

        if (ImGui::BeginCombo("Mesh Asset", preview)) {
            if (ImGui::Selectable("(none)", currentIdx < 0)) {
                m->assetPath[0] = '\0';
                changed = true;
            }
            for (int i = 0; i < static_cast<int>(meshEntries.size()); ++i) {
                const bool selected = (i == currentIdx);
                if (ImGui::Selectable(meshEntries[i]->displayName.c_str(), selected)) {
                    const std::string pathStr = meshEntries[i]->path.string();
                    strncpy_s(m->assetPath, sizeof(m->assetPath), pathStr.c_str(), _TRUNCATE);
                    changed = true;
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        // Material — live dropdown from MaterialManager + raw index fallback.
        ImGui::Separator();
        int matIdx = static_cast<int>(m->materialIndex);
        {
            const uint32_t matCount = materialManager_ ? materialManager_->count() : 0u;
            char matPreview[64];
            if (matCount > 0 && static_cast<uint32_t>(matIdx) < matCount) {
                const char* nm = materialManager_->getName(rendering::MaterialHandle{static_cast<uint16_t>(matIdx)});
                std::snprintf(matPreview, sizeof(matPreview), "%s", (nm && nm[0]) ? nm : "#");
                if (!nm || !nm[0])
                    std::snprintf(matPreview, sizeof(matPreview), "#%d", matIdx);
            } else {
                std::snprintf(matPreview, sizeof(matPreview), "#%d", matIdx);
            }

            if (ImGui::BeginCombo("Material", matPreview)) {
                for (uint32_t i = 0; i < matCount; ++i) {
                    const rendering::MaterialHandle h{static_cast<uint16_t>(i)};
                    const char* nm = materialManager_->getName(h);
                    char label[64];
                    if (nm && nm[0])
                        std::snprintf(label, sizeof(label), "%s", nm);
                    else
                        std::snprintf(label, sizeof(label), "#%u", i);
                    const bool sel = (static_cast<uint32_t>(matIdx) == i);
                    if (ImGui::Selectable(label, sel)) {
                        m->materialIndex = i;
                        changed = true;
                    }
                    if (sel) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            if (ImGui::DragInt("Material Index", &matIdx, 1, 0, 4095)) {
                m->materialIndex = static_cast<uint32_t>(matIdx);
                changed = true;
            }
        }

        // Shadow flags.
        if (ImGui::Checkbox("Cast Shadow",    &m->castShadow))    changed = true;
        if (ImGui::Checkbox("Receive Shadow", &m->receiveShadow)) changed = true;

        // Push undo command if any material/shadow field changed this frame.
        if (activeScene_ && (m->materialIndex != oldMatIdx ||
                             m->castShadow    != oldCastShadow ||
                             m->receiveShadow != oldReceiveShadow)) {
            undo_.push(std::make_unique<MaterialChangeCommand>(
                activeScene_->world(), selected_,
                oldMatIdx,       m->materialIndex,
                oldCastShadow,   m->castShadow,
                oldReceiveShadow, m->receiveShadow));
        }

        // PBR factor overrides + texture slots — shown when a GpuMaterial is assigned.
        ImGui::Separator();
        const rendering::MaterialHandle matH{static_cast<uint16_t>(m->materialIndex)};
        rendering::GpuMaterial* gpuMat = materialManager_ ? materialManager_->getMutable(matH) : nullptr;
        if (gpuMat) {
            ImGui::TextDisabled("Material Factors");
            if (ImGui::ColorEdit4("Albedo Factor", gpuMat->albedoFactor,
                    ImGuiColorEditFlags_Float))
                changed = true;
            if (ImGui::SliderFloat("Metallic",  &gpuMat->metallicFactor,  0.0f, 1.0f)) changed = true;
            if (ImGui::SliderFloat("Roughness", &gpuMat->roughnessFactor, 0.0f, 1.0f)) changed = true;

            ImGui::Separator();
            ImGui::TextDisabled("Textures");
            struct SlotDef { const char* label; uint32_t* texIdx; };
            SlotDef slots[] = {
                { "Albedo",     &gpuMat->albedoTextureIndex     },
                { "Normal",     &gpuMat->normalTextureIndex     },
                { "MetalRough", &gpuMat->metallicRoughnessIndex },
                { "Emissive",   &gpuMat->emissiveTextureIndex   },
            };
            for (auto& s : slots) {
                ImGui::PushID(s.label);
                const bool hasTexture = (*s.texIdx != 0xFFFFFFFFu);
                const char* displayName = "None";
                std::string nameStr;
                if (hasTexture) {
                    auto it = textureSrvNames_.find(*s.texIdx);
                    if (it != textureSrvNames_.end()) {
                        nameStr = it->second;
                    } else {
                        nameStr = "#" + std::to_string(*s.texIdx);
                    }
                    displayName = nameStr.c_str();
                }
                ImGui::Text("%-10s", s.label);
                ImGui::SameLine();
                ImGui::Button(displayName, ImVec2(140.0f, 0.0f));
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload =
                            ImGui::AcceptDragDropPayload("ASSET_PATH")) {
                        const char* dropped = static_cast<const char*>(payload->Data);
                        const std::string droppedPath(dropped);
                        auto it = uploadedTexturePaths_.find(droppedPath);
                        if (it != uploadedTexturePaths_.end()) {
                            *s.texIdx = it->second;
                            changed   = true;
                        } else {
                            auto cpuMesh = tools::loadEasset(
                                std::filesystem::path(droppedPath));
                            if (cpuMesh && !cpuMesh->textures.empty()) {
                                const uint32_t slot = meshRenderSystem_.uploadTexture(
                                    *gpu_->device, cpuMesh->textures[0]);
                                if (slot != UINT32_MAX) {
                                    uploadedTexturePaths_[droppedPath] = slot;
                                    std::filesystem::path p(droppedPath);
                                    textureSrvNames_[slot] = p.stem().string();
                                    *s.texIdx = slot;
                                    changed   = true;
                                }
                            }
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                if (hasTexture) {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("X")) {
                        *s.texIdx = 0xFFFFFFFFu;
                        changed   = true;
                    }
                }
                ImGui::PopID();
            }
        } else {
            ImGui::TextDisabled("(no material assigned)");
        }

        return changed;
    });

    // HierarchyComponent: read-only info showing parent entity.
    componentRegistry_.registerWidget(core::ecs::HierarchyComponent::kComponentId,
        [](void* data) -> bool {
            const auto* hc = static_cast<core::ecs::HierarchyComponent*>(data);
            if (hc->parent == core::ecs::kInvalidEntity) {
                ImGui::TextDisabled("(root entity)");
            } else {
                ImGui::Text("Parent: %u:%u", hc->parent.index, hc->parent.generation);
            }
            return false;
        });

    // PrefabInstance: source path + override bitmask.
    componentRegistry_.registerWidget(core::ecs::PrefabInstance::kComponentId,
        [](void* data) -> bool {
            auto* pi = static_cast<core::ecs::PrefabInstance*>(data);
            return drawPrefabInstanceWidget(*pi);
        });

    // Name: editable entity name (also shown as inspector header, but editable here).
    componentRegistry_.registerWidget(core::ecs::Name::kComponentId, [](void* data) -> bool {
        auto* nm = static_cast<core::ecs::Name*>(data);
        ImGui::SetNextItemWidth(-1.0f);
        return ImGui::InputText("##entityname", nm->buf, sizeof(nm->buf));
    });

    // InputReceiverComponent: player ID, priority, input-consume flag, focus group.
    componentRegistry_.registerWidget(core::input::InputReceiverComponent::kComponentId,
        [](void* data) -> bool {
            auto* rc = static_cast<core::input::InputReceiverComponent*>(data);
            bool changed = false;
            int pid = rc->playerId;
            if (ImGui::InputInt("Player ID", &pid)) {
                rc->playerId = static_cast<uint8_t>(std::clamp(pid, 0, 255));
                changed = true;
            }
            int pri = rc->priority;
            if (ImGui::InputInt("Priority", &pri)) {
                rc->priority = static_cast<uint8_t>(std::clamp(pri, 0, 255));
                changed = true;
            }
            changed |= ImGui::Checkbox("Consumes Input", &rc->consumesInput);
            static const char* kFocusGroups[] = { "Gameplay", "UI", "Console", "Cutscene" };
            int fg = static_cast<int>(rc->focusGroup);
            if (ImGui::Combo("Focus Group", &fg, kFocusGroups, 4)) {
                rc->focusGroup = static_cast<core::input::FocusGroup>(fg);
                changed = true;
            }
            return changed;
        });

    // RigidBody: body type, mass, damping, friction, restitution. Velocity shown read-only.
    componentRegistry_.registerWidget(physics::RigidBody::kComponentId, [](void* data) -> bool {
        auto* rb = static_cast<physics::RigidBody*>(data);
        bool changed = false;
        static const char* kBodyTypes[] = { "Static", "Kinematic", "Dynamic" };
        int bt = static_cast<int>(rb->type);
        if (ImGui::Combo("Body Type", &bt, kBodyTypes, 3)) {
            rb->type = static_cast<physics::RigidBodyType>(bt);
            changed = true;
        }
        const bool isDynamic = rb->type == physics::RigidBodyType::Dynamic;
        if (!isDynamic) ImGui::BeginDisabled();
        changed |= ImGui::DragFloat("Mass",            &rb->mass,           0.01f, 0.001f, 1e6f);
        if (!isDynamic) ImGui::EndDisabled();
        changed |= ImGui::DragFloat("Linear Damping",  &rb->linearDamping,  0.001f, 0.0f, 1.0f);
        changed |= ImGui::DragFloat("Angular Damping", &rb->angularDamping, 0.001f, 0.0f, 1.0f);
        changed |= ImGui::DragFloat("Friction",        &rb->friction,       0.01f,  0.0f, 1.0f);
        changed |= ImGui::DragFloat("Restitution",     &rb->restitution,    0.01f,  0.0f, 1.0f);
        ImGui::TextDisabled("Velocity: (%.2f, %.2f, %.2f)",
            rb->velocity.x, rb->velocity.y, rb->velocity.z);
        return changed;
    });

    // CharacterController: capsule geometry, step + slope limits. isGrounded is read-only.
    componentRegistry_.registerWidget(physics::CharacterController::kComponentId,
        [](void* data) -> bool {
            auto* cc = static_cast<physics::CharacterController*>(data);
            bool changed = false;
            changed |= ImGui::DragFloat("Capsule Radius", &cc->capsuleRadius, 0.01f, 0.01f, 10.0f);
            changed |= ImGui::DragFloat("Capsule Height", &cc->capsuleHeight, 0.01f, 0.1f,  10.0f);
            changed |= ImGui::DragFloat("Step Up Height", &cc->stepUpHeight,  0.01f, 0.0f,  1.0f);
            changed |= ImGui::DragFloat("Max Slope (deg)",&cc->maxSlopeAngle, 0.5f,  0.0f,  90.0f);
            ImGui::TextDisabled("Grounded: %s", cc->isGrounded ? "yes" : "no");
            return changed;
        });

    // SpawnPointComponent: team, priority, exclusion radius.
    componentRegistry_.registerWidget(core::SpawnPointComponent::kComponentId,
        [](void* data) -> bool {
            auto* sp = static_cast<core::SpawnPointComponent*>(data);
            bool changed = false;
            int teamId = sp->teamId;
            if (ImGui::InputInt("Team ID (0=any)", &teamId)) {
                sp->teamId = static_cast<uint8_t>(std::clamp(teamId, 0, 255));
                changed = true;
            }
            int pri = sp->priority;
            if (ImGui::InputInt("Priority", &pri)) {
                sp->priority = static_cast<uint8_t>(std::clamp(pri, 0, 255));
                changed = true;
            }
            changed |= ImGui::DragFloat("Exclusion Radius (m)", &sp->radius, 0.1f, 0.0f, 100.0f);
            return changed;
        });

    // TriggerComponent: shape selector, team filter, event tag, shape dimensions.
    componentRegistry_.registerWidget(core::TriggerComponent::kComponentId,
        [](void* data) -> bool {
            auto* tc = static_cast<core::TriggerComponent*>(data);
            bool changed = false;
            static const char* kShapes[] = { "Box", "Sphere", "Capsule", "ConvexHull", "TriangleMesh" };
            int shape = static_cast<int>(tc->shape);
            if (ImGui::Combo("Shape", &shape, kShapes, 5)) {
                tc->shape = static_cast<core::ColliderComponent::Shape>(shape);
                changed = true;
            }
            int tf = tc->teamFilter;
            if (ImGui::InputInt("Team Filter (0=any)", &tf)) {
                tc->teamFilter = static_cast<uint8_t>(std::clamp(tf, 0, 255));
                changed = true;
            }
            int et = static_cast<int>(tc->eventTag);
            if (ImGui::InputInt("Event Tag", &et)) {
                tc->eventTag = static_cast<uint32_t>(et);
                changed = true;
            }
            switch (tc->shape) {
                case core::ColliderComponent::Shape::Box:
                    changed |= ImGui::DragFloat("Half X", &tc->params.box.halfX, 0.01f, 0.001f, 1000.f);
                    changed |= ImGui::DragFloat("Half Y", &tc->params.box.halfY, 0.01f, 0.001f, 1000.f);
                    changed |= ImGui::DragFloat("Half Z", &tc->params.box.halfZ, 0.01f, 0.001f, 1000.f);
                    break;
                case core::ColliderComponent::Shape::Sphere:
                    changed |= ImGui::DragFloat("Radius", &tc->params.sphere.radius, 0.01f, 0.001f, 1000.f);
                    break;
                case core::ColliderComponent::Shape::Capsule:
                    changed |= ImGui::DragFloat("Radius",      &tc->params.capsule.radius,     0.01f, 0.001f, 100.f);
                    changed |= ImGui::DragFloat("Half Height", &tc->params.capsule.halfHeight,  0.01f, 0.001f, 100.f);
                    break;
                default:
                    ImGui::TextDisabled("(mesh-derived shape — edit via MeshHandle)");
                    break;
            }
            return changed;
        });

    // Camera: FOV, near/far planes, isMain flag.
    componentRegistry_.registerWidget(rendering::Camera::kComponentId,
        [](void* data) -> bool {
            auto* cam = static_cast<rendering::Camera*>(data);
            bool changed = false;
            changed |= ImGui::DragFloat("FOV Y (deg)",  &cam->fovYDegrees, 0.5f, 1.0f,  179.0f);
            changed |= ImGui::DragFloat("Near Z",       &cam->nearZ,       0.001f, 0.001f, 1.0f);
            changed |= ImGui::DragFloat("Far Z",        &cam->farZ,        1.0f,  1.0f, 100000.0f);
            changed |= ImGui::Checkbox("Is Main",       &cam->isMain);
            return changed;
        });

    // FpsCameraController: movement speed, look sensitivity, yaw/pitch, active toggle.
    componentRegistry_.registerWidget(rendering::FpsCameraController::kComponentId,
        [](void* data) -> bool {
            auto* c = static_cast<rendering::FpsCameraController*>(data);
            bool changed = false;
            changed |= ImGui::DragFloat("Move Speed",       &c->moveSpeed,       0.1f, 0.01f, 100.0f);
            changed |= ImGui::DragFloat("Look Sensitivity", &c->lookSensitivity, 0.001f, 0.0001f, 1.0f);
            changed |= ImGui::DragFloat("Yaw (deg)",        &c->yaw,             0.5f);
            changed |= ImGui::DragFloat("Pitch (deg, ±89)", &c->pitch,           0.5f, -89.0f, 89.0f);
            changed |= ImGui::Checkbox("Active",            &c->active);
            return changed;
        });

    // Light: type, color, intensity, range, cone angles, shadow toggle.
    componentRegistry_.registerWidget(rendering::Light::kComponentId,
        [](void* data) -> bool {
            auto* light = static_cast<rendering::Light*>(data);
            bool changed = false;

            static const char* kTypes[] = { "Directional", "Point", "Spot" };
            int typeIdx = static_cast<int>(light->type);
            if (ImGui::Combo("Type", &typeIdx, kTypes, 3)) {
                light->type = static_cast<rendering::Light::Type>(typeIdx);
                changed = true;
            }

            changed |= ImGui::ColorEdit3("Color", light->color);
            changed |= ImGui::DragFloat("Intensity", &light->intensity, 1.0f, 0.0f, 100000.0f);

            const bool isDirectional = (light->type == rendering::Light::Type::Directional);
            if (isDirectional) ImGui::BeginDisabled();
            changed |= ImGui::DragFloat("Range (m)", &light->range, 0.1f, 0.01f, 10000.0f);
            if (isDirectional) ImGui::EndDisabled();

            const bool isSpot = (light->type == rendering::Light::Type::Spot);
            if (!isSpot) ImGui::BeginDisabled();
            // Display cone angles in degrees for usability; store in radians.
            float innerDeg = light->innerConeAngle * (180.0f / 3.14159265f);
            float outerDeg = light->outerConeAngle * (180.0f / 3.14159265f);
            if (ImGui::DragFloat("Inner Cone (deg)", &innerDeg, 0.5f, 0.0f, 89.0f)) {
                light->innerConeAngle = innerDeg * (3.14159265f / 180.0f);
                changed = true;
            }
            if (ImGui::DragFloat("Outer Cone (deg)", &outerDeg, 0.5f, 0.0f, 89.0f)) {
                light->outerConeAngle = outerDeg * (3.14159265f / 180.0f);
                changed = true;
            }
            if (!isSpot) ImGui::EndDisabled();

            changed |= ImGui::Checkbox("Cast Shadow", &light->castShadow);

            return changed;
        });

    // All ECS-registered component types must have a widget. Assert at startup.
    componentRegistry_.validateCoverage();
}

namespace {
template<typename T>
void registerOne(const char* name) {
    core::ecs::World::registerComponent<T>({
        name, sizeof(T), alignof(T),
        [](void* p) { new(p) T{}; }, nullptr, nullptr
    });
    tools::SceneSerializer::registerComponentLoader(T::kComponentId,
        [](core::ecs::World& w, core::ecs::Entity e, const uint8_t* data, size_t sz) {
            T comp{};
            if (sz >= sizeof(T)) std::memcpy(&comp, data, sizeof(T));
            w.addComponent<T>(e, comp);
        });
}
} // anonymous namespace

void EditorApp::registerComponents() {
    registerOne<core::ecs::Name>                       ("Name");
    registerOne<core::Transform>                       ("Transform");
    registerOne<core::input::InputReceiverComponent>   ("InputReceiverComponent");
    registerOne<core::Health>                          ("Health");
    registerOne<core::Lifetime>                        ("Lifetime");
    registerOne<core::TeamTag>                         ("TeamTag");
    registerOne<physics::RigidBody>                    ("RigidBody");
    registerOne<physics::CharacterController>          ("CharacterController");
    // ID 8 = NetworkIdentity — not linked in editor; files with it skip gracefully.
    registerOne<core::ColliderComponent>               ("ColliderComponent");
    registerOne<core::AnimationState>                  ("AnimationState");
    registerOne<core::ecs::HierarchyComponent>         ("HierarchyComponent");
    registerOne<core::MeshHandle>                      ("MeshHandle");
    registerOne<core::ecs::PrefabInstance>             ("PrefabInstance");
    registerOne<core::SpawnPointComponent>             ("SpawnPointComponent");
    registerOne<core::TriggerComponent>                ("TriggerComponent");
    registerOne<rendering::Camera>                     ("Camera");             // id 16
    registerOne<rendering::FpsCameraController>        ("FpsCameraController"); // id 17
    registerOne<rendering::Light>                      ("Light");              // id 18
}

} // namespace engine::editor

#endif // ENGINE_DEVREL

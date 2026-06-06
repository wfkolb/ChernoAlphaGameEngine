#ifdef ENGINE_DEVREL

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d12.h>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>

#include "editor/EditorApp.h"
#include "editor/FileDialog.h"
#include "editor/commands/SaveAsPrefabCommand.h"
#include "editor/component_widgets/ColliderWidget.h"
#include "editor/component_widgets/AnimationStateWidget.h"
#include "editor/component_widgets/PrefabInstanceWidget.h"

#include <core/components/ColliderComponent.h>
#include <core/components/AnimationState.h>
#include <core/components/MeshHandle.h>
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

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

// imgui_impl_win32.h guards this declaration in #if 0 to avoid pulling in
// <windows.h>. Forward-declare it here per imgui's documented usage pattern.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

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
    uint32_t imguiDescSize      = 0;
    uint32_t imguiNextDescSlot  = 0;
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

    // --- Editor state ---
    inspectorPanel_ = std::make_unique<InspectorPanel>(componentRegistry_);
    registerComponentWidgets();

    undo_.setOnModified([this]() { sceneDirty_ = true; });

    registerComponents();
    loadPreferences();
    newScene();

    LOG_INFO("EngineEditor init complete");
    running_ = true;
    return true;
}

void EditorApp::loadPreferences() {
    tools::Config::init();
    projectName_ = tools::Config::getString("project", "name", "Untitled Project");
    contentRoot_ = tools::Config::getString("project", "contentRoot", "");

    if (!contentRoot_.empty()) {
        assetPanel_.setRoot(contentRoot_);

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
            const std::filesystem::path assetsDir =
                std::filesystem::path(contentRoot_) / "assets";
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
        if (contentRoot_.empty()) return;
        AssetImportSettings settings{};
        const std::filesystem::path outputDir = std::filesystem::path(contentRoot_) / "meshes";
        if (!importer_.beginImport(sourcePath, outputDir, settings)) {
            consolePanel_.log(ConsolePanel::Level::Warning, "Import already in progress");
        }
    });
    assetPanel_.setImportWithSettingsCallback(
        [this](const std::filesystem::path& sourcePath, const AssetImportSettings& settings) {
            if (contentRoot_.empty()) return;
            const std::filesystem::path outputDir =
                std::filesystem::path(contentRoot_) / "meshes";
            if (!importer_.beginImport(sourcePath, outputDir, settings)) {
                consolePanel_.log(ConsolePanel::Level::Warning, "Import already in progress");
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
    const auto p = FileDialog::openFile(
        L"Scene Files\0*.scene\0All Files\0*.*\0",
        L"Open Scene");
    if (!p.empty()) openScene(p.wstring());
}

void EditorApp::saveSceneDialog() {
    const auto p = FileDialog::saveFile(
        L"Scene Files\0*.scene\0All Files\0*.*\0",
        L"scene",
        L"Save Scene");
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
            if (activeScene_) pie_.start(*activeScene_);
        }
        if (ImGui::MenuItem("Stop", nullptr, false, playing)) {
            pie_.stop();
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

    // P2 — When PIE is active, mirror the first player entity's Transform into
    // the editor camera so the viewport shows the player's point of view.
    // The editor camera WASD input is suppressed via setPieActive() below.
    const bool pieActive = pie_.isUsingPlayerCamera();
    if (pieActive) {
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
        // No offscreen scene RT is wired yet; pass 0 to show the placeholder.
        viewportPanel_.draw(world, selected_, camera_, picking_, undo_, 0, &showViewport_);
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

    // MeshHandle: asset path (editable text field).
    componentRegistry_.registerWidget(core::MeshHandle::kComponentId, [](void* data) -> bool {
        auto* m = static_cast<core::MeshHandle*>(data);
        return ImGui::InputText("Asset Path", m->assetPath, sizeof(m->assetPath));
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
}

} // namespace engine::editor

#endif // ENGINE_DEVREL

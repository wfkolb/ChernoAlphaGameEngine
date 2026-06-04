#pragma once
#ifdef ENGINE_DEVREL

#include "editor/EditorCamera.h"
#include "editor/SelectionSystem.h"
#include "editor/UndoStack.h"
#include "editor/PIEController.h"
#include "editor/panels/SceneHierarchyPanel.h"
#include "editor/panels/InspectorPanel.h"
#include "editor/panels/ViewportPanel.h"
#include "editor/panels/AssetBrowserPanel.h"
#include "editor/panels/ConsolePanel.h"

#include <core/ecs/Entity.h>
#include <core/scene/SceneManager.h>

#include <cstdint>
#include <memory>
#include <string>

namespace engine::rendering { class Window; class GpuDevice; }

namespace engine::editor {

// Top-level editor application. Owns the OS window, DX12 device, ImGui context,
// the panel set, and the scene being authored. Created by main.cpp.
//
// The editor creates its own HWND + WndProc (forwarding to ImGui_ImplWin32) and
// its own shader-visible CBV/SRV/UAV descriptor heap from the device, so it does
// not depend on changes to rendering::Window or GpuDevice.
class EditorApp {
public:
    EditorApp();
    ~EditorApp();

    EditorApp(const EditorApp&)            = delete;
    EditorApp& operator=(const EditorApp&) = delete;

    // Create window, device, ImGui. Returns false on fatal init failure (e.g.
    // no DX12 / headless), in which case run() must not be called.
    bool init();

    // Blocking main loop until the window closes.
    void run();

    void shutdown();

private:
    void registerComponentWidgets();  // ImGui editors for built-in component types
    void loadPreferences();           // project.toml + editor_prefs.toml
    void buildDockLayout();
    void drawMenuBar();        // wraps items in Begin/EndMenuBar (dock host)
    void drawMenuBarItems();   // the actual File/Edit/Window/Play menus
    void drawPanels();
    void frame();                     // one editor frame (UI + present)

    void newScene();
    void openScene(const std::wstring& path);
    void saveScene(const std::wstring& path);

    // ---- Owned subsystems ----
    struct Gpu;                       // hides DX12 + ImGui backend details
    std::unique_ptr<Gpu>              gpu_;

    core::scene::SceneManager         sceneManager_;
    core::scene::Scene*               activeScene_ = nullptr;

    EditorCamera                      camera_;
    SelectionSystem                   picking_;
    UndoStack                         undo_;
    PIEController                     pie_;
    ComponentEditorRegistry           componentRegistry_;

    SceneHierarchyPanel               hierarchyPanel_;
    std::unique_ptr<InspectorPanel>   inspectorPanel_;
    ViewportPanel                     viewportPanel_;
    AssetBrowserPanel                 assetPanel_;
    ConsolePanel                      consolePanel_;

    core::ecs::Entity                 selected_ = core::ecs::kInvalidEntity;

    // Panel visibility.
    bool showHierarchy_ = true;
    bool showInspector_ = true;
    bool showViewport_  = true;
    bool showAssets_    = true;
    bool showConsole_   = true;

    bool        dockBuilt_      = false;
    bool        running_        = false;
    std::string projectName_    = "Untitled Project";
    std::string contentRoot_;
    std::wstring currentScenePath_;
};

} // namespace engine::editor

#endif // ENGINE_DEVREL

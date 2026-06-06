#pragma once
#ifdef ENGINE_DEVREL

#include "editor/EditorCamera.h"
#include "editor/EditorImporter.h"
#include "editor/EditorPrefs.h"
#include "editor/SelectionSystem.h"
#include "editor/UndoStack.h"
#include "editor/PIEController.h"
#include "editor/panels/SceneHierarchyPanel.h"
#include "editor/panels/InspectorPanel.h"
#include "editor/panels/ViewportPanel.h"
#include "editor/panels/AssetBrowserPanel.h"
#include "editor/panels/ConsolePanel.h"
#include "editor/panels/ScenePropertiesPanel.h"
#include "editor/panels/PhysicsMaterialsPanel.h"
#include "editor/panels/CollisionLayerPanel.h"

#include <core/ecs/Entity.h>
#include <core/scene/SceneManager.h>
#include <physics/PhysicsMaterialTable.h>
#include <physics/QueryFilter.h>

#include <core/ecs/EntityFactory.h>

#include <cstdint>
#include <filesystem>
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
    void registerComponents();        // World + SceneSerializer component registration
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
    void openSceneDialog();
    void saveSceneDialog();

    // Returns true if it is safe to discard the current scene (either it is
    // not dirty, or the user chose Save or Discard in the modal).  When the
    // scene is dirty this opens an ImGui modal and returns false immediately;
    // the resolved action is stored in pendingAction_ and executed the next
    // frame via frame().
    bool confirmDiscardChanges();

    // Actions that may be deferred until the unsaved-changes modal resolves.
    enum class PendingAction { None, NewScene, OpenScene, CloseWindow };
    void executePendingAction();

    // ---- Owned subsystems ----
    struct Gpu;                       // hides DX12 + ImGui backend details
    std::unique_ptr<Gpu>              gpu_;

    core::ecs::EntityFactory          entityFactory_;

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
    ScenePropertiesPanel              scenePropsPanel_;
    PhysicsMaterialsPanel             physMatPanel_;
    CollisionLayerPanel               collisionLayerPanel_;

    physics::PhysicsMaterialTable     physMatTable_;
    physics::QueryFilter              globalQueryFilter_;
    EditorImporter                    importer_;
    EditorPrefs                       prefs_;
    std::filesystem::path             prefsPath_;

    core::ecs::Entity                 selected_ = core::ecs::kInvalidEntity;

    // Panel visibility.
    bool showHierarchy_        = true;
    bool showInspector_        = true;
    bool showViewport_         = true;
    bool showAssets_           = true;
    bool showConsole_          = true;
    bool showSceneProps_       = false;
    bool showPhysMats_         = false;
    bool showCollisionLayers_  = false;

    bool        dockBuilt_      = false;
    bool        running_        = false;
    bool        sceneDirty_     = false;
    std::string projectName_    = "Untitled Project";
    std::string contentRoot_;
    std::wstring currentScenePath_;

    // E1 — Unsaved-changes modal state.
    bool          unsavedModalOpen_  = false;
    PendingAction pendingAction_     = PendingAction::None;
    std::wstring  pendingOpenPath_;  // path stored while waiting for modal
    bool          closePromptShown_  = false; // prevents re-opening after Cancel on close

    // E2 — Window title dirty indicator.
    std::wstring lastWindowTitle_;

    // E8 — Error dialog. Non-empty triggers the modal on the next frame draw.
    std::string errorMsg_;
    void drawErrorDialog();
};

} // namespace engine::editor

#endif // ENGINE_DEVREL

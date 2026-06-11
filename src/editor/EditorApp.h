#pragma once
#ifdef ENGINE_DEVREL

#include "editor/EditorCamera.h"
#include "editor/EditorImporter.h"
#include "editor/EditorPrefs.h"
#include "editor/SelectionSystem.h"
#include "editor/UndoStack.h"
#include "editor/PIEController.h"
#include "editor/ThumbnailRenderer.h"
#include "editor/panels/SceneHierarchyPanel.h"
#include "editor/panels/InspectorPanel.h"
#include "editor/panels/ViewportPanel.h"
#include "editor/panels/AssetBrowserPanel.h"
#include "editor/panels/ConsolePanel.h"
#include "editor/panels/ScenePropertiesPanel.h"
#include "editor/panels/PhysicsMaterialsPanel.h"
#include "editor/panels/CollisionLayerPanel.h"
#include "editor/MeshPreviewPanel.h"

#include <app/MeshRenderSystem.h>
#include <rendering/FrameGraph.h>
#include <rendering/MaterialManager.h>
#include <rendering/MeshManager.h>
#include <rendering/GpuDevice.h>

#include <core/ecs/Entity.h>
#include <core/scene/SceneManager.h>
#include <physics/PhysicsMaterialTable.h>
#include <physics/PhysicsWorld.h>
#include <physics/QueryFilter.h>

#include <core/ecs/EntityFactory.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

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

    // Optional: call before init() to override the project root used for the
    // asset browser, scene dialogs, and import output paths.
    void setProjectRoot(std::filesystem::path root);

    // Create window, device, ImGui. Returns false on fatal init failure (e.g.
    // no DX12 / headless), in which case run() must not be called.
    bool init();

    // Blocking main loop until the window closes.
    void run();

    void shutdown();

    // Access the entity factory after init() to register game-specific archetypes.
    // Game-side editor mains call registerFpsArchetypes(app.entityFactory()) before run().
    core::ecs::EntityFactory& entityFactory();

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

    // Create / destroy the separate game window used during PIE.
    void createPieWindow();
    void destroyPieWindow();

    // Create (or resize) the offscreen viewport render target.
    void createViewportRt(uint32_t w, uint32_t h);

    // Wire mesh-load/unload delegates on a newly activated scene.
    void wireScene(core::scene::Scene& scene);

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

    // Mesh rendering for the viewport.
    std::unique_ptr<rendering::MaterialManager> materialManager_;
    app::MeshRenderSystem                       meshRenderSystem_;
    rendering::FrameGraph                   sceneFrameGraph_;
    rendering::FrameGraph                   pieFrameGraph_;  // used during PIE to render to game window
    std::unique_ptr<rendering::MeshManager> meshManager_;   // lazy: after first beginFrame
    ThumbnailRenderer                       thumbnailRenderer_;

    struct PendingMeshLoad {
        std::string assetPath;
        uint32_t    entityIndex;
        uint32_t    entityGeneration;
    };
    std::vector<PendingMeshLoad>             pendingMeshLoads_;
    std::unordered_map<uint32_t, std::string> registeredMeshPaths_; // entityIndex → last uploaded path

    core::ecs::EntityFactory          entityFactory_;

    core::scene::SceneManager         sceneManager_;
    core::scene::Scene*               activeScene_ = nullptr;

    EditorCamera                      camera_;
    EditorCamera::State               prePieCameraState_{};
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
    MeshPreviewPanel                  previewPanel_;

    // Physics world used during PIE — created in init(), wired to the active
    // scene before PIE starts and detached on PIE stop.
    std::unique_ptr<physics::PhysicsWorld> editorPhysicsWorld_;

    // Separate game window opened when PIE starts; destroyed when PIE stops.
    // Defined in EditorApp.cpp (engine::editor namespace, not nested).
    std::unique_ptr<struct PieWindow> pieWindow_;

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

    // Set via setProjectRoot() (from --project CLI arg). Overrides contentRoot_
    // from Config when non-empty. Stored as absolute path.
    std::filesystem::path projectRoot_;

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

    // VM-4: current viewmode for the editor viewport (PIE always uses Lit).
    app::ViewMode viewMode_ = app::ViewMode::Lit;

    // TX-6: tracks uploaded texture assets to avoid re-uploading the same .easset.
    std::unordered_map<std::string, uint32_t> uploadedTexturePaths_; // absPath → srvSlot
    std::unordered_map<uint32_t, std::string> textureSrvNames_;      // srvSlot → display name
};

} // namespace engine::editor

#endif // ENGINE_DEVREL

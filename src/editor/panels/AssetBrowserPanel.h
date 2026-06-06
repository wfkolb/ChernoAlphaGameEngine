#pragma once
#ifdef ENGINE_DEVREL

#include "editor/MetaFileWriter.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace engine::editor {

// Filesystem-backed asset browser. Lists recognised asset files under a root
// directory and exposes hooks for opening scenes and importing source meshes.
class AssetBrowserPanel {
public:
    enum class AssetType { Unknown, Easset, Scene, Gltf, Prefab };

    struct Entry {
        std::filesystem::path path;
        std::string           displayName;
        AssetType             type = AssetType::Unknown;
    };

    // Callback invoked when the user double-clicks a .scene asset.
    using OpenSceneFn    = std::function<void(const std::filesystem::path&)>;
    // Callback invoked when the user double-clicks a .glb/.gltf to import with default settings.
    using ImportFn       = std::function<void(const std::filesystem::path&)>;
    // Callback invoked after the user confirms the import-settings modal (drag-drop path).
    using ImportWithSettingsFn = std::function<void(const std::filesystem::path&,
                                                     const AssetImportSettings&)>;
    // Callback invoked when the user double-clicks or drag-drops a .prefab asset.
    using InstPrefabFn   = std::function<void(const std::filesystem::path&)>;

    void setRoot(std::filesystem::path root);
    void setOpenSceneCallback(OpenSceneFn fn)                    { onOpenScene_          = std::move(fn); }
    void setImportCallback(ImportFn fn)                          { onImport_             = std::move(fn); }
    void setImportWithSettingsCallback(ImportWithSettingsFn fn)  { onImportWithSettings_ = std::move(fn); }
    void setInstantiatePrefabCallback(InstPrefabFn fn)           { onInstPrefab_         = std::move(fn); }

    // Re-scan the root directory. Safe to call when the root does not exist.
    void refresh();

    void draw(bool* open);

    static AssetType classify(const std::filesystem::path& p);

    const std::vector<Entry>& entries() const noexcept { return entries_; }

private:
    void drawImportModal();

    std::filesystem::path  root_;
    std::vector<Entry>     entries_;
    OpenSceneFn            onOpenScene_;
    ImportFn               onImport_;
    ImportWithSettingsFn   onImportWithSettings_;
    InstPrefabFn           onInstPrefab_;
    char                   filter_[128] = {};
    int                    selectedIndex_ = -1;

    // E4 — import-settings modal state.
    std::filesystem::path  pendingImportPath_;
    AssetImportSettings    importSettings_;
    bool                   importModalOpen_ = false;
};

} // namespace engine::editor

#endif // ENGINE_DEVREL

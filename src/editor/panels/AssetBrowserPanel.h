#pragma once
#ifdef ENGINE_DEVREL

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace engine::editor {

// Filesystem-backed asset browser. Lists recognised asset files under a root
// directory and exposes hooks for opening scenes and importing source meshes.
class AssetBrowserPanel {
public:
    enum class AssetType { Unknown, Easset, Scene, Gltf };

    struct Entry {
        std::filesystem::path path;
        std::string           displayName;
        AssetType             type = AssetType::Unknown;
    };

    // Callback invoked when the user double-clicks a .scene asset.
    using OpenSceneFn = std::function<void(const std::filesystem::path&)>;
    // Callback invoked when the user imports a .glb/.gltf (e.g. via drag-drop).
    using ImportFn    = std::function<void(const std::filesystem::path&)>;

    void setRoot(std::filesystem::path root);
    void setOpenSceneCallback(OpenSceneFn fn) { onOpenScene_ = std::move(fn); }
    void setImportCallback(ImportFn fn)       { onImport_ = std::move(fn); }

    // Re-scan the root directory. Safe to call when the root does not exist.
    void refresh();

    void draw(bool* open);

    static AssetType classify(const std::filesystem::path& p);

    const std::vector<Entry>& entries() const noexcept { return entries_; }

private:
    std::filesystem::path  root_;
    std::vector<Entry>     entries_;
    OpenSceneFn            onOpenScene_;
    ImportFn               onImport_;
    char                  filter_[128] = {};
    int                   selectedIndex_ = -1;
};

} // namespace engine::editor

#endif // ENGINE_DEVREL

#pragma once
#ifdef ENGINE_DEVREL

#include <filesystem>
#include <string>
#include <vector>
#include <cstdint>

namespace engine::editor {

// Persists editor preferences across sessions.
// Reads/writes <contentRoot>/editor_prefs.toml.
// All methods are safe to call even when no prefs file exists yet.
class EditorPrefs {
public:
    static constexpr size_t   kMaxRecentScenes = 10;
    static constexpr uint16_t kDefaultPIEPort  = 57300;

    // Load from disk. Missing file produces default values.
    void loadFromDisk(const std::filesystem::path& prefsPath);

    // Write current state to disk. Creates parent dirs if needed.
    void saveToDisk(const std::filesystem::path& prefsPath) const;

    // Prepend path to list. Deduplicates and caps at kMaxRecentScenes.
    // Entries that no longer exist on disk are filtered on get.
    void addRecentScene(const std::filesystem::path& path);

    // Returns existing paths only (stale entries silently removed).
    std::vector<std::filesystem::path> getRecentScenes() const;

    uint16_t piePort() const noexcept { return piePort_; }
    void     setPiePort(uint16_t p) noexcept { piePort_ = p; }

private:
    std::vector<std::filesystem::path> recentScenes_;
    uint16_t                           piePort_ = kDefaultPIEPort;
};

} // namespace engine::editor

#endif // ENGINE_DEVREL

#ifdef ENGINE_DEVREL

#include "editor/EditorPrefs.h"

#include <toml++/toml.hpp>
#include <core/log.h>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <system_error>

namespace engine::editor {

void EditorPrefs::loadFromDisk(const std::filesystem::path& prefsPath) {
    std::error_code ec;
    if (!std::filesystem::exists(prefsPath, ec)) return;

    try {
        const auto tbl = toml::parse_file(prefsPath.string());

        if (const auto* arr = tbl["recent_scenes"].as_array()) {
            recentScenes_.clear();
            for (const auto& elem : *arr) {
                if (auto s = elem.value<std::string>())
                    recentScenes_.emplace_back(*s);
            }
        }

        if (auto p = tbl["pie_port"].value<int64_t>())
            piePort_ = static_cast<uint16_t>(*p);

        pieMouseCapture_ = tbl["editor"]["pieMouseCapture"].value_or(true);

    } catch (const toml::parse_error& e) {
        LOG_WARN("EditorPrefs: failed to parse '{}': {}", prefsPath.string(), e.description());
    }
}

void EditorPrefs::saveToDisk(const std::filesystem::path& prefsPath) const {
    std::error_code ec;
    std::filesystem::create_directories(prefsPath.parent_path(), ec);

    toml::array arr;
    for (const auto& p : recentScenes_)
        arr.push_back(p.string());

    toml::table editorTbl;
    editorTbl.insert_or_assign("pieMouseCapture", pieMouseCapture_);

    toml::table tbl;
    tbl.insert_or_assign("recent_scenes", arr);
    tbl.insert_or_assign("pie_port", static_cast<int64_t>(piePort_));
    tbl.insert_or_assign("editor", editorTbl);

    std::ofstream out(prefsPath);
    if (!out) {
        LOG_WARN("EditorPrefs: could not write to '{}'", prefsPath.string());
        return;
    }
    out << tbl;
}

void EditorPrefs::addRecentScene(const std::filesystem::path& path) {
    // Remove any existing entry for the same path.
    recentScenes_.erase(
        std::remove(recentScenes_.begin(), recentScenes_.end(), path),
        recentScenes_.end());

    // Prepend.
    recentScenes_.insert(recentScenes_.begin(), path);

    // Cap.
    if (recentScenes_.size() > kMaxRecentScenes)
        recentScenes_.resize(kMaxRecentScenes);
}

std::vector<std::filesystem::path> EditorPrefs::getRecentScenes() const {
    std::vector<std::filesystem::path> result;
    result.reserve(recentScenes_.size());
    std::error_code ec;
    for (const auto& p : recentScenes_) {
        if (std::filesystem::exists(p, ec))
            result.push_back(p);
    }
    return result;
}

} // namespace engine::editor

#endif // ENGINE_DEVREL

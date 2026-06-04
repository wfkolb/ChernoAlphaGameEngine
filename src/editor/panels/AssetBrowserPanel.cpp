#ifdef ENGINE_DEVREL

#include "editor/panels/AssetBrowserPanel.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <system_error>

namespace engine::editor {

namespace fs = std::filesystem;

AssetBrowserPanel::AssetType AssetBrowserPanel::classify(const fs::path& p) {
    std::string ext = p.extension().string();
    for (char& c : ext) c = static_cast<char>(std::tolower((unsigned char)c));
    if (ext == ".easset") return AssetType::Easset;
    if (ext == ".scene")  return AssetType::Scene;
    if (ext == ".glb" || ext == ".gltf") return AssetType::Gltf;
    return AssetType::Unknown;
}

void AssetBrowserPanel::setRoot(fs::path root) {
    root_ = std::move(root);
    refresh();
}

void AssetBrowserPanel::refresh() {
    entries_.clear();
    selectedIndex_ = -1;

    std::error_code ec;
    if (root_.empty() || !fs::exists(root_, ec) || !fs::is_directory(root_, ec)) {
        return;
    }

    for (auto it = fs::recursive_directory_iterator(root_, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const AssetType type = classify(it->path());
        if (type == AssetType::Unknown) continue;

        Entry e;
        e.path        = it->path();
        e.displayName = it->path().filename().string();
        e.type        = type;
        entries_.push_back(std::move(e));
    }

    std::sort(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) {
        if (a.type != b.type) return a.type < b.type;
        return a.displayName < b.displayName;
    });
}

namespace {
const char* typeLabel(AssetBrowserPanel::AssetType t) {
    switch (t) {
        case AssetBrowserPanel::AssetType::Easset: return "[mesh] ";
        case AssetBrowserPanel::AssetType::Scene:  return "[scene]";
        case AssetBrowserPanel::AssetType::Gltf:   return "[gltf] ";
        default:                                   return "[?]    ";
    }
}
}

void AssetBrowserPanel::draw(bool* open) {
    if (open && !*open) return;
    if (!ImGui::Begin("Assets", open)) {
        ImGui::End();
        return;
    }

    if (ImGui::Button("Refresh")) refresh();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", root_.empty() ? "(no content root)" : root_.string().c_str());

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##assetfilter", "Filter", filter_, sizeof(filter_));
    ImGui::Separator();

    if (ImGui::BeginChild("##assetlist")) {
        for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
            const Entry& e = entries_[i];
            if (filter_[0] != '\0' && e.displayName.find(filter_) == std::string::npos) {
                continue;
            }

            ImGui::PushID(i);
            std::string label = std::string(typeLabel(e.type)) + " " + e.displayName;
            if (ImGui::Selectable(label.c_str(), selectedIndex_ == i,
                                  ImGuiSelectableFlags_AllowDoubleClick)) {
                selectedIndex_ = i;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    if (e.type == AssetType::Scene && onOpenScene_) onOpenScene_(e.path);
                    if (e.type == AssetType::Gltf  && onImport_)    onImport_(e.path);
                }
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    // Accept a path dropped from outside (e.g. an OS drag-drop bridged into the
    // ImGui payload "ASSET_PATH"). The viewport/host fills the payload.
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
            const char* dropped = static_cast<const char*>(payload->Data);
            fs::path p(dropped);
            if (classify(p) == AssetType::Gltf && onImport_) onImport_(p);
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::End();
}

} // namespace engine::editor

#endif // ENGINE_DEVREL

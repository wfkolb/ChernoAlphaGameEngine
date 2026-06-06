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
    if (ext == ".easset")               return AssetType::Easset;
    if (ext == ".scene")                return AssetType::Scene;
    if (ext == ".glb" || ext == ".gltf") return AssetType::Gltf;
    if (ext == ".prefab")               return AssetType::Prefab;
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
// E7 — short type tag shown before each file name.
const char* typeLabel(AssetBrowserPanel::AssetType t) {
    switch (t) {
        case AssetBrowserPanel::AssetType::Easset:  return "[MESH] ";
        case AssetBrowserPanel::AssetType::Scene:   return "[SCENE] ";
        case AssetBrowserPanel::AssetType::Gltf:    return "[GLB] ";
        case AssetBrowserPanel::AssetType::Prefab:  return "[PREFAB] ";
        default:                                    return "";
    }
}
} // anonymous namespace

// E4 — import-settings modal.
void AssetBrowserPanel::drawImportModal() {
    if (importModalOpen_) {
        ImGui::OpenPopup("##ImportSettings");
        importModalOpen_ = false;
    }

    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("##ImportSettings", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Import: %s", pendingImportPath_.filename().string().c_str());
        ImGui::Separator();

        ImGui::DragFloat("Uniform Scale", &importSettings_.uniformScale, 0.01f, 0.001f, 1000.0f, "%.3f");

        const char* upAxisItems[] = { "Y-Up", "Z-Up" };
        int upAxisIdx = importSettings_.upAxisZ ? 1 : 0;
        if (ImGui::Combo("Up Axis", &upAxisIdx, upAxisItems, 2)) {
            importSettings_.upAxisZ = (upAxisIdx == 1);
        }

        ImGui::Checkbox("Generate Collision", &importSettings_.generateCollision);
        ImGui::Checkbox("Merge Meshes",       &importSettings_.mergeMeshes);

        // LOD count is fixed at 1 for Phase 8 — show as read-only.
        ImGui::BeginDisabled(true);
        int lodDisplay = 1;
        ImGui::SliderInt("LOD Count", &lodDisplay, 1, 1);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("(Phase 8 limit)");

        ImGui::Separator();

        if (ImGui::Button("Import", ImVec2(120.0f, 0.0f))) {
            importSettings_.lodCount = 1;
            if (onImportWithSettings_) onImportWithSettings_(pendingImportPath_, importSettings_);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
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
            // E7 — prefix the display name with a type tag.
            std::string label = std::string(typeLabel(e.type)) + e.displayName;
            if (ImGui::Selectable(label.c_str(), selectedIndex_ == i,
                                  ImGuiSelectableFlags_AllowDoubleClick)) {
                selectedIndex_ = i;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    if (e.type == AssetType::Scene  && onOpenScene_)  onOpenScene_(e.path);
                    if (e.type == AssetType::Gltf   && onImport_)     onImport_(e.path);
                    if (e.type == AssetType::Prefab && onInstPrefab_) onInstPrefab_(e.path);
                }
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    // E4 — accept drag-drop of a source mesh and open the import-settings modal.
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
            const char* dropped = static_cast<const char*>(payload->Data);
            fs::path p(dropped);
            if (classify(p) == AssetType::Gltf) {
                pendingImportPath_ = std::move(p);
                importSettings_    = AssetImportSettings{};
                importModalOpen_   = true;
            }
        }
        ImGui::EndDragDropTarget();
    }

    // Draw the modal after the child window so it can be centred over the full viewport.
    drawImportModal();

    ImGui::End();
}

} // namespace engine::editor

#endif // ENGINE_DEVREL

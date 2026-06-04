#ifdef ENGINE_DEVREL

#include "editor/panels/ConsolePanel.h"

#include <imgui.h>

#include <cstring>

namespace engine::editor {

namespace {
ImVec4 colorFor(ConsolePanel::Level level) {
    switch (level) {
        case ConsolePanel::Level::Trace:   return ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
        case ConsolePanel::Level::Info:    return ImVec4(0.85f, 0.85f, 0.85f, 1.0f);
        case ConsolePanel::Level::Warning: return ImVec4(0.95f, 0.80f, 0.30f, 1.0f);
        case ConsolePanel::Level::Error:   return ImVec4(0.95f, 0.35f, 0.35f, 1.0f);
    }
    return ImVec4(1, 1, 1, 1);
}
}

void ConsolePanel::log(Level level, std::string message) {
    entries_.push_back({level, std::move(message)});
    // Cap history so a long session does not grow unbounded.
    constexpr size_t kMax = 4096;
    if (entries_.size() > kMax) {
        entries_.erase(entries_.begin(), entries_.begin() + (entries_.size() - kMax));
    }
}

void ConsolePanel::clear() { entries_.clear(); }

bool ConsolePanel::passesFilter(const Entry& e) const {
    switch (e.level) {
        case Level::Trace:   if (!showTrace_)   return false; break;
        case Level::Info:    if (!showInfo_)    return false; break;
        case Level::Warning: if (!showWarning_) return false; break;
        case Level::Error:   if (!showError_)   return false; break;
    }
    if (filter_[0] != '\0' && e.text.find(filter_) == std::string::npos) {
        return false;
    }
    return true;
}

void ConsolePanel::draw(bool* open) {
    if (open && !*open) return;
    if (!ImGui::Begin("Console", open)) {
        ImGui::End();
        return;
    }

    if (ImGui::Button("Clear")) clear();
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &autoScroll_);
    ImGui::SameLine();
    ImGui::Checkbox("Trace", &showTrace_);
    ImGui::SameLine();
    ImGui::Checkbox("Info", &showInfo_);
    ImGui::SameLine();
    ImGui::Checkbox("Warn", &showWarning_);
    ImGui::SameLine();
    ImGui::Checkbox("Error", &showError_);

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##consolefilter", "Filter", filter_, sizeof(filter_));

    ImGui::Separator();

    if (ImGui::BeginChild("##consolescroll", ImVec2(0, 0), false,
                          ImGuiWindowFlags_HorizontalScrollbar)) {
        for (const Entry& e : entries_) {
            if (!passesFilter(e)) continue;
            ImGui::PushStyleColor(ImGuiCol_Text, colorFor(e.level));
            ImGui::TextUnformatted(e.text.c_str());
            ImGui::PopStyleColor();
        }
        if (autoScroll_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
            ImGui::SetScrollHereY(1.0f);
        }
    }
    ImGui::EndChild();

    ImGui::End();
}

} // namespace engine::editor

#endif // ENGINE_DEVREL

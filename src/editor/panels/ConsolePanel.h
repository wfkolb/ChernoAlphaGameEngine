#pragma once
#ifdef ENGINE_DEVREL

#include <string>
#include <vector>

namespace engine::editor {

// Scrolling log console with severity filtering. The editor pushes lines into
// it; draw() renders the ImGui window.
class ConsolePanel {
public:
    enum class Level { Trace, Info, Warning, Error };

    void log(Level level, std::string message);
    void clear();

    void draw(bool* open);

private:
    struct Entry {
        Level       level;
        std::string text;
    };

    std::vector<Entry> entries_;
    bool  autoScroll_   = true;
    bool  showTrace_    = true;
    bool  showInfo_     = true;
    bool  showWarning_  = true;
    bool  showError_    = true;
    char  filter_[128]  = {};

    bool passesFilter(const Entry& e) const;
};

} // namespace engine::editor

#endif // ENGINE_DEVREL

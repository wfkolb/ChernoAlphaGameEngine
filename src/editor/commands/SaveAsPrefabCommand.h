#pragma once
#ifdef ENGINE_DEVREL

#include "editor/UndoStack.h"

#include <filesystem>

namespace engine::editor {

// Represents a "Save as Prefab" operation. execute() is a no-op because the
// file is written before this command is pushed. undo() removes the file so
// the prefab doesn't outlive the editor action that created it.
class SaveAsPrefabCommand : public ICommand {
public:
    explicit SaveAsPrefabCommand(std::filesystem::path path)
        : path_(std::move(path)) {}

    void execute() override {}

    void undo() override {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }

    const char* name() const override { return "Save as Prefab"; }

private:
    std::filesystem::path path_;
};

} // namespace engine::editor

#endif // ENGINE_DEVREL

#ifdef ENGINE_DEVREL

#include "UndoStack.h"

namespace engine::editor {

void UndoStack::push(std::unique_ptr<ICommand> command) {
    if (!command) return;

    command->execute();
    redoStack_.clear();
    undoStack_.push_back(std::move(command));

    // Drop the oldest command when over the cap. erase(begin) is O(n) but the
    // cap is small and pushes are user-driven, so this is not a hot path.
    if (undoStack_.size() > kMaxCommands) {
        undoStack_.erase(undoStack_.begin());
    }

    if (onModified_) onModified_();
}

bool UndoStack::undo() {
    if (undoStack_.empty()) return false;

    std::unique_ptr<ICommand> cmd = std::move(undoStack_.back());
    undoStack_.pop_back();
    cmd->undo();
    redoStack_.push_back(std::move(cmd));
    if (onModified_) onModified_();
    return true;
}

bool UndoStack::redo() {
    if (redoStack_.empty()) return false;

    std::unique_ptr<ICommand> cmd = std::move(redoStack_.back());
    redoStack_.pop_back();
    cmd->execute();
    undoStack_.push_back(std::move(cmd));
    if (onModified_) onModified_();
    return true;
}

void UndoStack::clear() {
    undoStack_.clear();
    redoStack_.clear();
}

const char* UndoStack::peekUndoName() const {
    return undoStack_.empty() ? "" : undoStack_.back()->name();
}

const char* UndoStack::peekRedoName() const {
    return redoStack_.empty() ? "" : redoStack_.back()->name();
}

} // namespace engine::editor

#endif // ENGINE_DEVREL

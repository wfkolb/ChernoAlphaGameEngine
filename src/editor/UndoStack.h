#pragma once
#ifdef ENGINE_DEVREL

#include <functional>
#include <memory>
#include <vector>

namespace engine::editor {

// A single undoable editor operation.
class ICommand {
public:
    virtual ~ICommand() = default;

    // Apply the change. Called once when the command is pushed and again on redo.
    virtual void execute() = 0;

    // Revert the change applied by execute().
    virtual void undo() = 0;

    // Short human-readable label shown in the editor UI.
    virtual const char* name() const = 0;
};

// Bounded undo/redo history. Pushing a command executes it immediately and
// clears the redo stack. The history is capped at kMaxCommands; the oldest
// entry is dropped when the cap is exceeded.
class UndoStack {
public:
    static constexpr size_t kMaxCommands = 100;

    // Executes the command, then stores it for later undo. Clears redo history.
    void push(std::unique_ptr<ICommand> command);

    // Reverts the most recent command, moving it onto the redo stack.
    bool undo();

    // Re-applies the most recently undone command.
    bool redo();

    void clear();

    bool   canUndo() const noexcept { return !undoStack_.empty(); }
    bool   canRedo() const noexcept { return !redoStack_.empty(); }
    size_t undoCount() const noexcept { return undoStack_.size(); }
    size_t redoCount() const noexcept { return redoStack_.size(); }

    // Label of the command undo()/redo() would act on, or "" if none.
    const char* peekUndoName() const;
    const char* peekRedoName() const;

    // Called after every push, undo, or redo so the editor can track dirty state.
    using DirtyFn = std::function<void()>;
    void setOnModified(DirtyFn fn) { onModified_ = std::move(fn); }

private:
    std::vector<std::unique_ptr<ICommand>> undoStack_;
    std::vector<std::unique_ptr<ICommand>> redoStack_;
    DirtyFn                                onModified_;
};

} // namespace engine::editor

#endif // ENGINE_DEVREL

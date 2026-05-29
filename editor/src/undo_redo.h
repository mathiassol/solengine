#pragma once
#include <functional>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Lightweight undo/redo command stack for the SolEngine editor.
// Each command stores an execute function (do / redo) and an undo function.
// push() calls execute_fn immediately and registers the command for undo.
// ---------------------------------------------------------------------------

struct IEditorCommand {
    std::string           description;
    std::function<void()> execute_fn;
    std::function<void()> undo_fn;
};

class UndoRedoStack {
public:
    // Execute cmd.execute_fn immediately, trim any forward redo history,
    // append to stack, and cap history at kMaxHistory entries.
    void push(IEditorCommand cmd);

    // Undo the most recent command (call undo_fn, decrement cursor).
    void undo();

    // Redo the next command (call execute_fn, increment cursor).
    void redo();

    bool can_undo() const { return m_cursor > 0; }
    bool can_redo() const { return m_cursor < static_cast<int>(m_stack.size()); }

    // Returns the description of the most recently executed command, or empty string.
    const std::string& last_action() const;

    static constexpr int kMaxHistory = 64;

private:
    std::vector<IEditorCommand> m_stack;
    int                         m_cursor = 0;
};

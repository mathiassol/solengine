#include "undo_redo.h"

void UndoRedoStack::push(IEditorCommand cmd) {
    // Trim any redo history forward of the current cursor.
    if (m_cursor < static_cast<int>(m_stack.size()))
        m_stack.erase(m_stack.begin() + m_cursor, m_stack.end());

    // Execute the command immediately.
    if (cmd.execute_fn) cmd.execute_fn();

    m_stack.push_back(std::move(cmd));
    ++m_cursor;

    // Cap history length.
    if (static_cast<int>(m_stack.size()) > kMaxHistory) {
        m_stack.erase(m_stack.begin());
        --m_cursor;
    }
}

void UndoRedoStack::undo() {
    if (m_cursor <= 0) return;
    --m_cursor;
    if (m_stack[m_cursor].undo_fn) m_stack[m_cursor].undo_fn();
}

void UndoRedoStack::redo() {
    if (m_cursor >= static_cast<int>(m_stack.size())) return;
    if (m_stack[m_cursor].execute_fn) m_stack[m_cursor].execute_fn();
    ++m_cursor;
}

const std::string& UndoRedoStack::last_action() const {
    static const std::string empty;
    if (m_cursor <= 0 || m_stack.empty()) return empty;
    return m_stack[static_cast<size_t>(m_cursor - 1)].description;
}

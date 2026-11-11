#pragma once

#include "tactus/model/Project.h"

#include <string>
#include <vector>

namespace tactus
{

/** Undo / redo by whole-document snapshots.

    The Project is plain data, so "undo" is simply "put the previous Project back".
    That makes every edit undoable by construction -- there is no per-operation
    undo code to forget or get wrong -- at the cost of one Project copy per step.
    (When projects grow large, the copies can share unchanged patterns; the
    interface stays the same.)
*/
class UndoHistory
{
public:
    explicit UndoHistory (std::size_t maxSteps = 200) : limit (maxSteps) {}

    /** Records the state the project had *before* an edit. Clears the redo stack. */
    void push (Project before, std::string action);

    bool canUndo() const noexcept { return ! undoStack.empty(); }
    bool canRedo() const noexcept { return ! redoStack.empty(); }
    /** Name of the action undo() / redo() would act on, or empty. */
    std::string undoName() const { return undoStack.empty() ? std::string() : undoStack.back().action; }
    std::string redoName() const { return redoStack.empty() ? std::string() : redoStack.back().action; }

    /** Swaps `current` with the previous / next state. Returns false if there is none. */
    bool undo (Project& current);
    bool redo (Project& current);

    void clear();
    std::size_t numUndoSteps() const noexcept { return undoStack.size(); }

private:
    struct Entry
    {
        Project state;
        std::string action;
    };

    std::vector<Entry> undoStack, redoStack;
    std::size_t limit;
};

} // namespace tactus

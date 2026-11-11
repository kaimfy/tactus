#include "tactus/model/UndoHistory.h"

namespace tactus
{

void UndoHistory::push (Project before, std::string action)
{
    undoStack.push_back ({ std::move (before), std::move (action) });
    if (undoStack.size() > limit)
        undoStack.erase (undoStack.begin()); // forget the oldest step
    redoStack.clear();
}

bool UndoHistory::undo (Project& current)
{
    if (undoStack.empty())
        return false;
    auto entry = std::move (undoStack.back());
    undoStack.pop_back();
    redoStack.push_back ({ std::move (current), entry.action });
    current = std::move (entry.state);
    return true;
}

bool UndoHistory::redo (Project& current)
{
    if (redoStack.empty())
        return false;
    auto entry = std::move (redoStack.back());
    redoStack.pop_back();
    undoStack.push_back ({ std::move (current), entry.action });
    current = std::move (entry.state);
    return true;
}

void UndoHistory::clear()
{
    undoStack.clear();
    redoStack.clear();
}

} // namespace tactus

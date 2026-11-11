#include "PianoRoll.h"

namespace tactus::ui
{

namespace
{
    constexpr int kHeaderHeight = 26;
    constexpr int kRulerHeight = 20;
    constexpr int kKeyboardWidth = 68;
    constexpr int kScrollBarSize = 12;
    constexpr int kResizeHandle = 7;
    constexpr int kNumKeys = 128;

    bool isBlackKey (int key)
    {
        switch (key % 12)
        {
            case 1: case 3: case 6: case 8: case 10: return true;
            default: return false;
        }
    }

    struct SnapOption { const char* name; std::int64_t ticks; };
    const SnapOption kSnapOptions[] = {
        { "Snap: 1/4", kPPQ }, { "Snap: 1/8", kPPQ / 2 }, { "Snap: 1/16", kPPQ / 4 },
        { "Snap: 1/32", kPPQ / 8 }, { "Snap: 1/64", kPPQ / 16 }, { "Snap: off", 1 }
    };
}

PianoRoll::PianoRoll (Session& s) : session (s)
{
    for (int i = 0; i < (int) std::size (kSnapOptions); ++i)
        snapBox.addItem (kSnapOptions[i].name, i + 1);
    snapBox.setSelectedId (3, juce::dontSendNotification);
    snapBox.setTooltip ("Grid that notes snap to when added, moved or resized");
    snapBox.setWantsKeyboardFocus (false);
    snapBox.onChange = [this] { repaint(); };
    addAndMakeVisible (snapBox);

    titleLabel.setFont (juce::Font (juce::FontOptions (11.5f, juce::Font::bold)));
    titleLabel.setColour (juce::Label::textColourId, palette().textDim);
    titleLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (titleLabel);

    for (auto* bar : { &horizontalBar, &verticalBar })
    {
        bar->setAutoHide (false);
        bar->addListener (this);
        addAndMakeVisible (bar);
    }

    setWantsKeyboardFocus (true);
    session.addChangeListener (this);
    changeListenerCallback (nullptr);
}

PianoRoll::~PianoRoll()
{
    stopPreview();
    session.removeChangeListener (this);
}

void PianoRoll::changeListenerCallback (juce::ChangeBroadcaster*)
{
    const auto* channel = session.selectedChannelOrNull();
    titleLabel.setText (channel != nullptr ? "PIANO ROLL  /  " + juce::String::fromUTF8 (channel->name.c_str()).toUpperCase()
                                           : juce::String ("PIANO ROLL"),
                        juce::dontSendNotification);

    // Undo, redo or a new project replaced the notes: indices no longer mean the same thing.
    if (session.historyGeneration() != shownGeneration)
    {
        shownGeneration = session.historyGeneration();
        selection.clear();
    }

    // When a different channel or pattern comes into view, bring its notes on screen.
    const Id channelId = channel != nullptr ? channel->id : 0;
    const Id patternId = session.pattern().id;
    if (channelId != shownChannel || patternId != shownPattern)
    {
        selection.clear();
        shownChannel = channelId;
        shownPattern = patternId;
        centreOnNotes();
    }

    updateScrollBars();
    repaint();
}

void PianoRoll::centreOnNotes()
{
    const auto grid = gridArea();
    const auto* channel = session.selectedChannelOrNull();
    if (channel == nullptr || grid.isEmpty())
        return;

    const auto& patternNotes = session.pattern().notes;
    const auto it = patternNotes.find (channel->id);
    if (it == patternNotes.end() || it->second.empty())
        return; // nothing to show: stay where the user was

    int lowest = kNumKeys, highest = 0;
    for (const auto& n : it->second)
    {
        lowest = juce::jmin (lowest, n.key);
        highest = juce::jmax (highest, n.key);
    }
    const double middleKey = (lowest + highest) * 0.5;
    scrollY = ((double) (kNumKeys - 1) - middleKey + 0.5) * rowHeight - grid.getHeight() * 0.5;
    initialScrollDone = true;
}

//==============================================================================
// Geometry

juce::Rectangle<int> PianoRoll::gridArea() const
{
    return getLocalBounds().withTrimmedTop (kHeaderHeight + kRulerHeight).withTrimmedLeft (kKeyboardWidth)
                           .withTrimmedRight (kScrollBarSize).withTrimmedBottom (kScrollBarSize);
}

juce::Rectangle<int> PianoRoll::keyboardArea() const
{
    return gridArea().withX (0).withWidth (kKeyboardWidth);
}

juce::Rectangle<int> PianoRoll::rulerArea() const
{
    const auto grid = gridArea();
    return { grid.getX(), kHeaderHeight, grid.getWidth(), kRulerHeight };
}

int PianoRoll::tickToX (double tick) const
{
    return gridArea().getX() + juce::roundToInt (tick * pixelsPerTick() - scrollX);
}

double PianoRoll::xToTick (int x) const
{
    return ((double) (x - gridArea().getX()) + scrollX) / pixelsPerTick();
}

int PianoRoll::keyToY (int key) const
{
    return gridArea().getY() + (kNumKeys - 1 - key) * rowHeight - juce::roundToInt (scrollY);
}

int PianoRoll::keyAtY (int y) const
{
    const int row = (int) std::floor (((double) (y - gridArea().getY()) + scrollY) / rowHeight);
    return juce::jlimit (0, kNumKeys - 1, kNumKeys - 1 - row);
}

juce::Rectangle<int> PianoRoll::noteBounds (const Note& n) const
{
    const int x = tickToX ((double) n.start);
    const int right = tickToX ((double) (n.start + n.length));
    return { x, keyToY (n.key), juce::jmax (4, right - x), rowHeight };
}

std::int64_t PianoRoll::snapTicks() const
{
    const int i = snapBox.getSelectedId() - 1;
    return i >= 0 && i < (int) std::size (kSnapOptions) ? kSnapOptions[i].ticks : kTicksPerStep;
}

std::int64_t PianoRoll::snap (double tick) const
{
    const auto unit = snapTicks();
    return (std::int64_t) std::llround (tick / (double) unit) * unit;
}

void PianoRoll::clampScroll()
{
    const auto grid = gridArea();
    // Leave a bar of empty space after the pattern end so the last notes are easy to reach.
    const double contentWidth = (double) (session.pattern().lengthSteps + 16) * pixelsPerStep;
    scrollX = juce::jlimit (0.0, juce::jmax (0.0, contentWidth - grid.getWidth()), scrollX);
    scrollY = juce::jlimit (0.0, juce::jmax (0.0, (double) (kNumKeys * rowHeight - grid.getHeight())), scrollY);
}

void PianoRoll::updateScrollBars()
{
    const auto grid = gridArea();
    if (grid.isEmpty())
        return;

    if (! initialScrollDone)
    {
        // Open centred around the octaves most music lives in (C3..C5).
        scrollY = (double) ((kNumKeys - 1 - 60) * rowHeight) - grid.getHeight() * 0.5;
        initialScrollDone = true;
    }
    clampScroll();

    const double contentWidth = (double) (session.pattern().lengthSteps + 16) * pixelsPerStep;
    horizontalBar.setRangeLimits (0.0, contentWidth, juce::dontSendNotification);
    horizontalBar.setCurrentRange (scrollX, grid.getWidth(), juce::dontSendNotification);
    verticalBar.setRangeLimits (0.0, (double) (kNumKeys * rowHeight), juce::dontSendNotification);
    verticalBar.setCurrentRange (scrollY, grid.getHeight(), juce::dontSendNotification);
}

void PianoRoll::scrollBarMoved (juce::ScrollBar* bar, double newRangeStart)
{
    if (bar == &horizontalBar)
        scrollX = newRangeStart;
    else
        scrollY = newRangeStart;
    clampScroll();
    repaint();
}

void PianoRoll::resized()
{
    auto header = getLocalBounds().removeFromTop (kHeaderHeight);
    snapBox.setBounds (header.removeFromRight (124).reduced (4, 3));
    titleLabel.setBounds (header.reduced (6, 0));

    const auto grid = gridArea();
    horizontalBar.setBounds (grid.getX(), grid.getBottom(), grid.getWidth(), kScrollBarSize);
    verticalBar.setBounds (grid.getRight(), grid.getY(), kScrollBarSize, grid.getHeight());
    if (! initialScrollDone)
        centreOnNotes(); // first layout: the size was unknown when the project loaded
    updateScrollBars();
}

//==============================================================================
// Painting

void PianoRoll::paint (juce::Graphics& g)
{
    const auto& p = palette();
    g.fillAll (p.panel);
    drawPanelHeader (g, getLocalBounds().removeFromTop (kHeaderHeight), {});

    if (session.selectedChannelOrNull() == nullptr)
    {
        g.setColour (p.textDim);
        g.setFont (14.0f);
        g.drawText ("Add a channel to start writing notes.", getLocalBounds().withTrimmedTop (kHeaderHeight),
                    juce::Justification::centred);
        return;
    }

    paintGrid (g);
    paintNotes (g);
    paintRuler (g);
    paintKeyboard (g);

    // Corner between ruler and keyboard.
    g.setColour (p.panelRaised);
    g.fillRect (0, kHeaderHeight, kKeyboardWidth, kRulerHeight);
}

void PianoRoll::paintGrid (juce::Graphics& g)
{
    const auto& p = palette();
    const auto grid = gridArea();
    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (grid);

    g.setColour (p.background);
    g.fillRect (grid);

    // Rows: black-key rows slightly darker, a stronger line under each C.
    const int topKey = keyAtY (grid.getY());
    const int bottomKey = keyAtY (grid.getBottom());
    for (int key = topKey; key >= bottomKey; --key)
    {
        const int y = keyToY (key);
        if (isBlackKey (key))
        {
            g.setColour (p.panel.darker (0.15f));
            g.fillRect (grid.getX(), y, grid.getWidth(), rowHeight);
        }
        g.setColour (key % 12 == 0 ? p.outline : p.outline.withAlpha (0.35f));
        g.fillRect (grid.getX(), y + rowHeight - 1, grid.getWidth(), 1);
    }

    // Columns: snap lines, with steps, beats and bars progressively stronger.
    const auto& project = session.project();
    const int beatTicks = juce::jmax (1, kPPQ * 4 / juce::jmax (1, project.timeSigDenominator));
    const int barTicks = beatTicks * juce::jmax (1, project.timeSigNumerator);

    std::int64_t lineTicks = juce::jmax<std::int64_t> (snapTicks(), kPPQ / 16);
    while ((double) lineTicks * pixelsPerTick() < 9.0) // never draw lines closer than 9 px
        lineTicks *= 2;

    const auto firstTick = (std::int64_t) (juce::jmax (0.0, xToTick (grid.getX())) / (double) lineTicks) * lineTicks;
    const double lastTick = xToTick (grid.getRight());
    for (auto tick = firstTick; (double) tick <= lastTick; tick += lineTicks)
    {
        if (tick % barTicks == 0)
            g.setColour (p.textDim.withAlpha (0.55f));
        else if (tick % beatTicks == 0)
            g.setColour (p.outline.brighter (0.15f));
        else
            g.setColour (p.outline.withAlpha (0.45f));
        g.fillRect (tickToX ((double) tick), grid.getY(), 1, grid.getHeight());
    }

    // Everything after the pattern end is dimmed: notes there are kept but not played.
    const int endX = tickToX ((double) session.pattern().lengthTicks());
    if (endX < grid.getRight())
    {
        g.setColour (juce::Colours::black.withAlpha (0.38f));
        g.fillRect (juce::jmax (grid.getX(), endX), grid.getY(), grid.getRight() - endX, grid.getHeight());
        g.setColour (p.accent.withAlpha (0.7f));
        g.fillRect (endX, grid.getY(), 2, grid.getHeight());
    }

    if (playheadX >= grid.getX())
    {
        g.setColour (p.playhead);
        g.fillRect (playheadX, grid.getY(), 2, grid.getHeight());
    }
}

void PianoRoll::paintNotes (juce::Graphics& g)
{
    const auto& p = palette();
    const auto* channel = session.selectedChannelOrNull();
    if (channel == nullptr)
        return;

    const auto grid = gridArea();
    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (grid);

    const auto& patternNotes = session.pattern().notes;
    const auto it = patternNotes.find (channel->id);
    if (it == patternNotes.end())
        return;

    const auto patternEnd = session.pattern().lengthTicks();
    for (std::size_t noteIndex = 0; noteIndex < it->second.size(); ++noteIndex)
    {
        const auto& n = it->second[noteIndex];
        const bool selected = isSelected (noteIndex);
        const auto bounds = noteBounds (n);
        if (! bounds.intersects (grid))
            continue;

        // Louder notes are drawn more saturated.
        auto colour = p.accentAlt.withMultipliedSaturation (0.55f + 0.45f * n.velocity)
                                 .withMultipliedBrightness (0.7f + 0.3f * n.velocity);
        if (n.start >= patternEnd)
            colour = colour.withAlpha (0.35f);
        if (selected)
            colour = colour.brighter (0.45f);

        const auto r = bounds.toFloat().reduced (0.5f, 1.0f);
        g.setColour (colour);
        g.fillRoundedRectangle (r, 2.5f);
        g.setColour (colour.brighter (0.5f));
        g.fillRect (r.withWidth (2.0f));
        g.setColour (selected ? juce::Colours::white : juce::Colours::black.withAlpha (0.35f));
        g.drawRoundedRectangle (r, 2.5f, selected ? 1.6f : 1.0f);
    }

    if (! marquee.isEmpty())
    {
        g.setColour (p.accent.withAlpha (0.15f));
        g.fillRect (marquee);
        g.setColour (p.accent);
        g.drawRect (marquee, 1);
    }
}

void PianoRoll::paintRuler (juce::Graphics& g)
{
    const auto& p = palette();
    const auto ruler = rulerArea();
    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (ruler);

    g.setColour (p.panelRaised);
    g.fillRect (ruler);

    const auto& project = session.project();
    const int beatTicks = juce::jmax (1, kPPQ * 4 / juce::jmax (1, project.timeSigDenominator));
    const int barTicks = beatTicks * juce::jmax (1, project.timeSigNumerator);

    g.setFont (11.0f);
    const auto firstBeat = (std::int64_t) (juce::jmax (0.0, xToTick (ruler.getX())) / beatTicks);
    for (auto beat = firstBeat; (double) (beat * beatTicks) <= xToTick (ruler.getRight()); ++beat)
    {
        const auto tick = beat * beatTicks;
        const int x = tickToX ((double) tick);
        const bool isBar = tick % barTicks == 0;
        g.setColour (isBar ? p.text : p.textDim.withAlpha (0.6f));
        g.fillRect (x, ruler.getBottom() - (isBar ? 9 : 4), 1, isBar ? 9 : 4);
        if (isBar)
            g.drawText (juce::String (tick / barTicks + 1), x + 4, ruler.getY(), 40, ruler.getHeight() - 2,
                        juce::Justification::centredLeft);
    }

    g.setColour (p.outline);
    g.fillRect (ruler.getX(), ruler.getBottom() - 1, ruler.getWidth(), 1);
}

void PianoRoll::paintKeyboard (juce::Graphics& g)
{
    const auto& p = palette();
    const auto keys = keyboardArea();
    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (keys);

    g.setColour (p.panel);
    g.fillRect (keys);
    g.setFont (10.5f);

    const int topKey = keyAtY (keys.getY());
    const int bottomKey = keyAtY (keys.getBottom());
    for (int key = topKey; key >= bottomKey; --key)
    {
        const int y = keyToY (key);
        const bool black = isBlackKey (key);
        auto colour = black ? juce::Colour (0xff23262d) : juce::Colour (0xffd9dce2);
        if (key == previewKey)
            colour = p.accent;

        g.setColour (colour);
        g.fillRect (keys.getX(), y, black ? keys.getWidth() * 2 / 3 : keys.getWidth() - 1, rowHeight - 1);

        if (key % 12 == 0)
        {
            // Octave label: MIDI note 60 is C4.
            g.setColour (key == previewKey ? juce::Colours::black : juce::Colour (0xff50555f));
            g.drawText ("C" + juce::String (key / 12 - 1), keys.getX(), y, keys.getWidth() - 6, rowHeight - 1,
                        juce::Justification::centredRight);
        }
    }

    g.setColour (p.outline);
    g.fillRect (keys.getRight() - 1, keys.getY(), 1, keys.getHeight());
}

//==============================================================================
// Editing

std::vector<Note>* PianoRoll::notesOrNull()
{
    auto* channel = session.selectedChannelOrNull();
    return channel != nullptr ? &session.pattern().notes[channel->id] : nullptr;
}

int PianoRoll::noteIndexAt (juce::Point<int> position) const
{
    auto& self = const_cast<PianoRoll&> (*this);
    if (auto* notes = self.notesOrNull())
        for (int i = (int) notes->size(); --i >= 0;) // topmost (most recently added) first
            if (noteBounds ((*notes)[(std::size_t) i]).contains (position))
                return i;
    return -1;
}

void PianoRoll::eraseAt (juce::Point<int> position)
{
    const int index = noteIndexAt (position);
    if (index < 0)
        return;
    auto* notes = notesOrNull();
    notes->erase (notes->begin() + index);
    selection.clear(); // indices after the erased note have shifted
    session.editedContinuous ("Delete notes");
}

void PianoRoll::startPreview (int key, float velocity)
{
    const auto* channel = session.selectedChannelOrNull();
    if (channel == nullptr || (key == previewKey && channel->id == previewChannel))
        return;
    stopPreview();
    if (session.engine().noteOn (channel->id, key, velocity))
    {
        previewKey = key;
        previewChannel = channel->id;
        repaint (keyboardArea());
    }
}

void PianoRoll::stopPreview()
{
    if (previewKey < 0)
        return;
    if (! session.engine().noteOff (previewChannel, previewKey))
        juce::Logger::writeToLog ("PianoRoll: could not queue note-off for the preview note");
    previewKey = -1;
    repaint (keyboardArea());
}

bool PianoRoll::isSelected (std::size_t index) const
{
    return std::find (selection.begin(), selection.end(), index) != selection.end();
}

void PianoRoll::setSelection (editing::Indices newSelection)
{
    selection = std::move (newSelection);
    repaint();
}

void PianoRoll::mouseDown (const juce::MouseEvent& e)
{
    dragMode = DragMode::none;
    grabKeyboardFocus(); // so Delete, Ctrl+C and friends act on this editor
    auto* notes = notesOrNull();
    if (notes == nullptr)
        return;

    if (keyboardArea().contains (e.getPosition()))
    {
        dragMode = DragMode::keyboard;
        startPreview (keyAtY (e.y), 0.8f);
        return;
    }

    if (! gridArea().contains (e.getPosition()))
        return;

    if (e.mods.isPopupMenu())
    {
        dragMode = DragMode::erase;
        eraseAt (e.getPosition());
        return;
    }

    int index = noteIndexAt (e.getPosition());
    if (index < 0)
    {
        if (e.mods.isCommandDown())
        {
            // Ctrl+drag on empty space: rubber-band selection (Shift keeps what is selected).
            dragMode = DragMode::marquee;
            marqueeStart = e.getPosition();
            marquee = {};
            if (! e.mods.isShiftDown())
                selection.clear();
            marqueeBase = selection;
            repaint();
            return;
        }

        // Add a note where the user clicked, using the length of the last note they touched.
        const auto unit = snapTicks();
        const auto start = (std::int64_t) std::floor (xToTick (e.x) / (double) unit) * unit;
        if (start < 0 || start >= session.pattern().lengthTicks())
        {
            setSelection ({}); // a click outside the pattern just deselects
            return;
        }
        notes->push_back (Note { start, lastNoteLength, keyAtY (e.y), 0.8f });
        index = (int) notes->size() - 1;
        selection = { (std::size_t) index };
        dragMode = DragMode::move;
        dragAction = "Add note"; // placing and positioning a new note is one undo step
        session.editedContinuous (dragAction);
    }
    else
    {
        const auto clicked = (std::size_t) index;
        if (e.mods.isShiftDown())
        {
            // Shift+click adds to / removes from the selection.
            if (isSelected (clicked))
            {
                std::erase (selection, clicked);
                repaint();
                return;
            }
            selection.push_back (clicked);
        }
        else if (! isSelected (clicked))
        {
            selection = { clicked };
        }

        const auto bounds = noteBounds ((*notes)[clicked]);
        dragMode = e.x >= bounds.getRight() - kResizeHandle ? DragMode::resize : DragMode::move;
        const bool several = selection.size() > 1;
        dragAction = dragMode == DragMode::resize ? (several ? "Resize notes" : "Resize note")
                                                  : (several ? "Move notes" : "Move note");
        repaint();
    }

    dragIndex = index;
    dragOriginal = (*notes)[(std::size_t) index];
    dragOriginals = editing::copyNotes (*notes, selection);
    std::sort (selection.begin(), selection.end()); // same order as dragOriginals
    dragStartTick = xToTick (e.x);
    dragStartKey = keyAtY (e.y);
    lastNoteLength = dragOriginal.length;
    startPreview (dragOriginal.key, dragOriginal.velocity);
}

void PianoRoll::mouseDrag (const juce::MouseEvent& e)
{
    auto* notes = notesOrNull();
    if (notes == nullptr)
        return;

    switch (dragMode)
    {
        case DragMode::keyboard:
            startPreview (keyAtY (e.y), 0.8f); // glide across the keys
            return;
        case DragMode::erase:
            eraseAt (e.getPosition());
            return;
        case DragMode::marquee:
        {
            marquee = juce::Rectangle<int> (marqueeStart, e.getPosition()).getIntersection (gridArea());
            selection = marqueeBase;
            for (std::size_t i = 0; i < notes->size(); ++i)
                if (noteBounds ((*notes)[i]).intersects (marquee) && ! isSelected (i))
                    selection.push_back (i);
            repaint();
            return;
        }
        case DragMode::none:
            return;
        case DragMode::move:
        case DragMode::resize:
            break;
    }

    // The selection may have been invalidated under the drag (e.g. undo pressed mid-drag).
    if (dragIndex < 0 || dragIndex >= (int) notes->size() || dragOriginals.size() != selection.size())
        return;
    for (auto i : selection)
        if (i >= notes->size())
            return;

    const double deltaTicks = xToTick (e.x) - dragStartTick;
    bool changed = false;

    if (dragMode == DragMode::move)
    {
        // The clicked note snaps to the grid; the others keep their distance from it.
        auto dTick = snap ((double) dragOriginal.start + deltaTicks) - dragOriginal.start;
        int dKey = keyAtY (e.y) - dragStartKey;

        // Stop the whole group at the edges, so it never changes shape.
        const auto lastStart = session.pattern().lengthTicks() - 1;
        for (const auto& original : dragOriginals)
        {
            dTick = juce::jlimit<std::int64_t> (-original.start, juce::jmax<std::int64_t> (-original.start, lastStart - original.start), dTick);
            dKey = juce::jlimit (-original.key, kNumKeys - 1 - original.key, dKey);
        }

        for (std::size_t k = 0; k < selection.size(); ++k)
        {
            auto& note = (*notes)[selection[k]];
            const Note before = note;
            note.start = dragOriginals[k].start + dTick;
            note.key = dragOriginals[k].key + dKey;
            changed = changed || ! (note == before);
        }

        const int anchorKey = dragOriginal.key + dKey;
        if (anchorKey != previewKey)
            startPreview (anchorKey, dragOriginal.velocity);
    }
    else
    {
        const auto minimum = juce::jmax<std::int64_t> (snapTicks(), kPPQ / 32);
        const auto end = snap ((double) (dragOriginal.start + dragOriginal.length) + deltaTicks);
        const auto dLength = juce::jmax (minimum, end - dragOriginal.start) - dragOriginal.length;

        for (std::size_t k = 0; k < selection.size(); ++k)
        {
            auto& note = (*notes)[selection[k]];
            const Note before = note;
            note.length = juce::jmax (minimum, dragOriginals[k].length + dLength);
            changed = changed || ! (note == before);
        }
        lastNoteLength = (*notes)[(std::size_t) dragIndex].length;
    }

    if (changed)
        session.editedContinuous (dragAction);
}

void PianoRoll::mouseUp (const juce::MouseEvent&)
{
    stopPreview();
    session.endGesture();
    if (dragMode == DragMode::marquee)
    {
        marquee = {};
        repaint();
    }
    dragMode = DragMode::none;
    dragIndex = -1;
}

bool PianoRoll::keyPressed (const juce::KeyPress& key)
{
    auto* notes = notesOrNull();
    if (notes == nullptr)
        return false;

    const auto command = juce::ModifierKeys::commandModifier;
    const auto limit = session.pattern().lengthTicks();
    const bool hasSelection = ! selection.empty();

    auto copySelection = [&] { session.noteClipboard = editing::copyNotes (*notes, selection); };
    auto deleteSelection = [&] (const char* action)
    {
        editing::deleteNotes (*notes, selection);
        selection.clear();
        session.edited (action);
    };

    if (key == juce::KeyPress (juce::KeyPress::deleteKey) || key == juce::KeyPress (juce::KeyPress::backspaceKey))
    {
        if (hasSelection)
            deleteSelection ("Delete notes");
        return true;
    }
    if (key == juce::KeyPress ('a', command, 0))
    {
        editing::Indices all (notes->size());
        for (std::size_t i = 0; i < all.size(); ++i)
            all[i] = i;
        setSelection (std::move (all));
        return true;
    }
    if (key == juce::KeyPress (juce::KeyPress::escapeKey))
    {
        setSelection ({});
        return true;
    }
    if (key == juce::KeyPress ('c', command, 0))
    {
        if (hasSelection)
            copySelection();
        return true;
    }
    if (key == juce::KeyPress ('x', command, 0))
    {
        if (hasSelection)
        {
            copySelection();
            deleteSelection ("Cut notes");
        }
        return true;
    }
    if (key == juce::KeyPress ('v', command, 0))
    {
        if (session.noteClipboard.empty())
            return true;
        // Paste under the mouse if it is over the grid, otherwise right after the selection.
        const auto mouse = getMouseXYRelative();
        const auto unit = snapTicks();
        std::int64_t at = 0;
        if (gridArea().contains (mouse))
            at = (std::int64_t) std::floor (xToTick (mouse.x) / (double) unit) * unit;
        else if (hasSelection)
            at = (editing::noteSpan (*notes, selection).end + unit - 1) / unit * unit;

        auto pasted = editing::pasteNotes (*notes, session.noteClipboard, at, limit);
        if (! pasted.empty())
        {
            selection = std::move (pasted);
            session.edited ("Paste notes");
        }
        return true;
    }
    if (key == juce::KeyPress ('d', command, 0))
    {
        auto copies = editing::duplicateNotes (*notes, selection, snapTicks(), limit);
        if (! copies.empty())
        {
            selection = std::move (copies);
            session.edited ("Duplicate notes");
        }
        return true;
    }
    if (key.isKeyCode (juce::KeyPress::upKey) || key.isKeyCode (juce::KeyPress::downKey))
    {
        if (key.getModifiers().isCommandDown() || key.getModifiers().isAltDown())
            return false;
        const int direction = key.isKeyCode (juce::KeyPress::upKey) ? 1 : -1;
        if (editing::transposeNotes (*notes, selection, direction * (key.getModifiers().isShiftDown() ? 12 : 1)))
            session.editedContinuous ("Transpose notes");
        return true;
    }
    if (key.isKeyCode (juce::KeyPress::leftKey) || key.isKeyCode (juce::KeyPress::rightKey))
    {
        if (key.getModifiers().isCommandDown() || key.getModifiers().isAltDown())
            return false;
        const auto direction = key.isKeyCode (juce::KeyPress::rightKey) ? 1 : -1;
        if (editing::nudgeNotes (*notes, selection, direction * snapTicks(), limit))
            session.editedContinuous ("Nudge notes");
        return true;
    }
    if (key == juce::KeyPress ('q', 0, 0))
    {
        // Quantise the selection, or every note if nothing is selected.
        auto targets = selection;
        if (targets.empty())
            for (std::size_t i = 0; i < notes->size(); ++i)
                targets.push_back (i);
        if (editing::quantiseNotes (*notes, targets, snapTicks(), limit))
            session.edited ("Quantise notes");
        return true;
    }

    return false; // not ours: Space, Ctrl+Z and the rest go on to the main window
}

void PianoRoll::mouseMove (const juce::MouseEvent& e)
{
    auto shape = juce::MouseCursor::NormalCursor;
    if (gridArea().contains (e.getPosition()))
    {
        const int index = noteIndexAt (e.getPosition());
        if (index >= 0)
        {
            const auto bounds = noteBounds ((*notesOrNull())[(std::size_t) index]);
            shape = e.x >= bounds.getRight() - kResizeHandle ? juce::MouseCursor::LeftRightResizeCursor
                                                             : juce::MouseCursor::DraggingHandCursor;
        }
    }
    setMouseCursor (shape);
}

void PianoRoll::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCtrlDown() || e.mods.isCommandDown())
    {
        // Zoom time around the mouse position.
        const double tickUnderMouse = xToTick (e.x);
        pixelsPerStep = juce::jlimit (6.0, 160.0, pixelsPerStep * std::pow (1.25, (double) wheel.deltaY * 4.0));
        scrollX = tickUnderMouse * pixelsPerTick() - (double) (e.x - gridArea().getX());
    }
    else if (e.mods.isShiftDown())
    {
        scrollX -= (double) (wheel.deltaY + wheel.deltaX) * 240.0;
    }
    else
    {
        scrollY -= (double) wheel.deltaY * 180.0;
        scrollX -= (double) wheel.deltaX * 240.0;
    }
    updateScrollBars();
    repaint();
}

void PianoRoll::tick()
{
    const auto& engine = session.engine();
    const bool show = engine.isPlaying() && session.playMode() == PlayMode::pattern; // song time is not pattern time
    const int x = show ? tickToX (engine.getPositionTicks()) : -1;
    if (x == playheadX)
        return;

    const auto grid = gridArea();
    if (playheadX >= 0)
        repaint (playheadX - 1, grid.getY(), 4, grid.getHeight());
    playheadX = x;
    if (playheadX >= 0)
        repaint (playheadX - 1, grid.getY(), 4, grid.getHeight());
}

} // namespace tactus::ui

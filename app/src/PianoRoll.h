#pragma once

#include "Session.h"
#include "Theme.h"

#include "tactus/model/Editing.h"

namespace tactus::ui
{

/** Note editor for the selected channel in the current pattern.

    Mouse:  click empty grid = add note (and drag to place it)
            click a note = select it; Shift+click = add to / remove from the selection
            Ctrl+drag on empty grid = rubber-band select
            drag a note = move the selection, drag its right edge = resize it
            right-click / right-drag = delete
            click the keyboard = audition a key
    Keys:   Delete, Ctrl+A, Ctrl+C / X / V (paste lands under the mouse), Ctrl+D = duplicate,
            arrows = transpose / nudge (Shift+Up/Down = octave), Q = quantise, Esc = deselect
    Wheel:  scroll vertically; Shift = horizontally; Ctrl = zoom time
*/
class PianoRoll final : public juce::Component,
                        private juce::ChangeListener,
                        private juce::ScrollBar::Listener
{
public:
    explicit PianoRoll (Session&);
    ~PianoRoll() override;

    /** Called ~30 times a second: moves the playhead. */
    void tick();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    enum class DragMode { none, move, resize, erase, keyboard, marquee };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;

    // Geometry
    juce::Rectangle<int> gridArea() const;
    juce::Rectangle<int> keyboardArea() const;
    juce::Rectangle<int> rulerArea() const;
    double pixelsPerTick() const { return pixelsPerStep / kTicksPerStep; }
    int tickToX (double tick) const;
    double xToTick (int x) const;
    int keyToY (int key) const;
    int keyAtY (int y) const;
    juce::Rectangle<int> noteBounds (const Note&) const;
    std::int64_t snapTicks() const;
    std::int64_t snap (double tick) const;
    void updateScrollBars();
    void clampScroll();
    /** Scrolls vertically so the selected channel's notes are in view. */
    void centreOnNotes();

    // Editing
    std::vector<Note>* notesOrNull();
    int noteIndexAt (juce::Point<int>) const;
    void eraseAt (juce::Point<int>);
    void startPreview (int key, float velocity);
    void stopPreview();
    bool isSelected (std::size_t noteIndex) const;
    void setSelection (editing::Indices);

    void paintKeyboard (juce::Graphics&);
    void paintGrid (juce::Graphics&);
    void paintNotes (juce::Graphics&);
    void paintRuler (juce::Graphics&);

    Session& session;
    juce::ComboBox snapBox;
    juce::Label titleLabel;
    juce::ScrollBar horizontalBar { false };
    juce::ScrollBar verticalBar { true };

    double pixelsPerStep = 30.0;
    int rowHeight = 15;
    double scrollX = 0.0; // pixels
    double scrollY = 0.0;
    bool initialScrollDone = false;
    Id shownChannel = 0;
    Id shownPattern = 0;

    DragMode dragMode = DragMode::none;
    int dragIndex = -1;
    Note dragOriginal;
    juce::String dragAction;

    // Selection: indices into the selected channel's notes in the current pattern.
    editing::Indices selection;
    std::vector<Note> dragOriginals;  // the selected notes as they were when the drag began
    editing::Indices marqueeBase;     // selection before the rubber band started
    juce::Point<int> marqueeStart;
    juce::Rectangle<int> marquee;
    int shownGeneration = -1;
    double dragStartTick = 0.0;
    int dragStartKey = 0;
    std::int64_t lastNoteLength = kTicksPerStep;
    int previewKey = -1;
    Id previewChannel = 0;
    int playheadX = -1;
};

} // namespace tactus::ui

#pragma once

#include "Session.h"
#include "Theme.h"

#include "tactus/model/Editing.h"

namespace tactus::ui
{

/** The arrangement (playlist): tracks running left to right, with pattern clips on them.

    Mouse:  click empty lane = place the current pattern (and drag to position it)
            click a clip = select it; Shift+click = add to / remove from the selection
            Ctrl+drag on an empty lane = rubber-band select
            drag a clip = move the selection (between tracks too); right edge = resize / loop
            Ctrl+drag a clip = duplicate; Alt+click = split; right-click / right-drag = delete
            double-click a pattern clip = open its pattern in the piano roll
            drop audio files from the file manager onto a track to import them
    Keys:   Delete, Ctrl+A, Ctrl+C / X / V (paste lands under the mouse), Ctrl+D = duplicate,
            Esc = deselect
    Ruler:  click = move the playhead; drag = set the loop region; right-click = clear it
    Tracks: M = mute, S = solo; right-click the name for rename / colour / delete
    Wheel:  scroll vertically; Shift = horizontally; Ctrl = zoom time
*/
class ArrangementView final : public juce::Component,
                              public juce::FileDragAndDropTarget,
                              private juce::ChangeListener,
                              private juce::ScrollBar::Listener
{
public:
    explicit ArrangementView (Session&);
    ~ArrangementView() override;

    /** Called ~30 times a second: moves the playhead. */
    void tick();

    /** Called when the user double-clicks a clip to edit its pattern. */
    std::function<void()> onOpenPattern;
    /** Reports problems to the status bar. */
    std::function<void (const juce::String&)> onMessage;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    /** Puts an audio file on the timeline as a clip. Returns false (after reporting why
        through onMessage) if the file cannot be read. */
    bool importAudioFile (const juce::File&, std::int64_t atTick, int track);
    /** Puts a finished recording on the first empty track at the position it was recorded at.
        offsetSeconds skips the start of the file (latency compensation). */
    bool addRecordedTake (const juce::File&, std::int64_t startTick, double offsetSeconds);
    /** Imports files one under another, starting on the first track that has no clips. */
    void importAudioFiles (const juce::Array<juce::File>&);

    static juce::Colour trackColour (int index);

private:
    enum class DragMode { none, move, resize, erase, loopRegion, marquee };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;

    // Geometry
    juce::Rectangle<int> gridArea() const;
    juce::Rectangle<int> rulerArea() const;
    juce::Rectangle<int> headerArea() const; // the column of track headers
    double pixelsPerTick() const { return pixelsPerBeat / (double) kPPQ; }
    int tickToX (double tick) const;
    double xToTick (int x) const;
    int trackToY (int track) const;
    int trackAtY (int y) const; // may be out of range
    juce::Rectangle<int> clipBounds (const Clip&) const;
    juce::Rectangle<int> muteButtonBounds (int track) const;
    juce::Rectangle<int> soloButtonBounds (int track) const;
    std::int64_t snapTicks() const;
    std::int64_t snap (double tick) const;
    std::int64_t snapDown (double tick) const;
    double contentWidth() const;
    void updateScrollBars();

    // Editing
    Clip* clipAt (juce::Point<int>);
    void eraseAt (juce::Point<int>);
    void headerClicked (const juce::MouseEvent&);
    void showTrackMenu (int track);
    void renameTrack (int track);
    int firstEmptyTrack() const;

    void paintTracks (juce::Graphics&);
    void paintClip (juce::Graphics&, const Clip&);
    void paintPatternClipBody (juce::Graphics&, const Clip&, const Pattern&, juce::Rectangle<float> bounds, juce::Colour);
    void paintAudioClipBody (juce::Graphics&, const Clip&, const AudioData&, juce::Rectangle<float> bounds, juce::Colour);
    bool isSelected (Id clip) const;
    /** Longest an audio clip can be before it runs past the end of its file, in ticks. */
    std::int64_t maxAudioClipLength (const Clip&);
    void paintRuler (juce::Graphics&);
    void paintHeaders (juce::Graphics&);

    Session& session;
    juce::ComboBox snapBox;
    juce::TextButton addTrackButton { "+ Track" };
    juce::ScrollBar horizontalBar { false };
    juce::ScrollBar verticalBar { true };

    double pixelsPerBeat = 24.0;
    double scrollX = 0.0, scrollY = 0.0;

    DragMode dragMode = DragMode::none;
    Id dragClip = 0;
    Clip dragOriginal;                // the clip under the mouse when the drag began
    std::vector<Id> selection;
    std::vector<Clip> dragOriginals;  // the selected clips as they were when the drag began
    std::vector<Id> marqueeBase;
    juce::Point<int> marqueeStart;
    juce::Rectangle<int> marquee;
    int shownGeneration = -1;
    juce::String dragAction;
    double dragStartTick = 0.0;
    int dragStartTrack = 0;
    std::int64_t loopAnchor = 0;
    bool loopDragMoved = false;
    int playheadX = -1;
};

} // namespace tactus::ui

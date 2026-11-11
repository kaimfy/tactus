#include "ArrangementView.h"

namespace tactus::ui
{

namespace
{
    constexpr int kHeaderHeight = 26;   // panel title strip
    constexpr int kRulerHeight = 22;
    constexpr int kTrackHeaderWidth = 150;
    constexpr int kTrackHeight = 46;
    constexpr int kScrollBarSize = 12;
    constexpr int kResizeHandle = 8;
    constexpr int kExtraBars = 8;       // empty room after the end of the song

    const juce::uint32 kTrackColours[] = { 0xff45c4b0, 0xfff0a23c, 0xff6aa6f8, 0xffe9739a,
                                           0xff9bd65c, 0xffb48cf0, 0xfff07d4f, 0xff5cc8e8 };

    enum SnapChoice { snapBar = 1, snapBeat, snapStep };
}

juce::Colour ArrangementView::trackColour (int index)
{
    const int count = (int) std::size (kTrackColours);
    return juce::Colour (kTrackColours[((index % count) + count) % count]);
}

ArrangementView::ArrangementView (Session& s) : session (s)
{
    snapBox.addItem ("Snap: bar", snapBar);
    snapBox.addItem ("Snap: beat", snapBeat);
    snapBox.addItem ("Snap: 1/16", snapStep);
    snapBox.setSelectedId (snapBar, juce::dontSendNotification);
    snapBox.setTooltip ("Grid that clips snap to");
    snapBox.setWantsKeyboardFocus (false);
    addAndMakeVisible (snapBox);

    addTrackButton.setWantsKeyboardFocus (false);
    addTrackButton.onClick = [this]
    {
        session.project().addTrack();
        session.edited ("Add track");
    };
    addAndMakeVisible (addTrackButton);

    for (auto* bar : { &horizontalBar, &verticalBar })
    {
        bar->setAutoHide (false);
        bar->addListener (this);
        addAndMakeVisible (bar);
    }

    setWantsKeyboardFocus (true);
    session.addChangeListener (this);
}

ArrangementView::~ArrangementView()
{
    session.removeChangeListener (this);
}

bool ArrangementView::isSelected (Id clip) const
{
    return std::find (selection.begin(), selection.end(), clip) != selection.end();
}

void ArrangementView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // Keep only selected clips that still exist (undo, delete track, ...).
    std::erase_if (selection, [this] (Id id) { return session.project().findClip (id) == nullptr; });
    updateScrollBars();
    repaint();
}

//==============================================================================
// Geometry

juce::Rectangle<int> ArrangementView::gridArea() const
{
    return getLocalBounds().withTrimmedTop (kHeaderHeight + kRulerHeight).withTrimmedLeft (kTrackHeaderWidth)
                           .withTrimmedRight (kScrollBarSize).withTrimmedBottom (kScrollBarSize);
}

juce::Rectangle<int> ArrangementView::rulerArea() const
{
    const auto grid = gridArea();
    return { grid.getX(), kHeaderHeight, grid.getWidth(), kRulerHeight };
}

juce::Rectangle<int> ArrangementView::headerArea() const
{
    return gridArea().withX (0).withWidth (kTrackHeaderWidth);
}

int ArrangementView::tickToX (double tick) const
{
    return gridArea().getX() + juce::roundToInt (tick * pixelsPerTick() - scrollX);
}

double ArrangementView::xToTick (int x) const
{
    return ((double) (x - gridArea().getX()) + scrollX) / pixelsPerTick();
}

int ArrangementView::trackToY (int track) const
{
    return gridArea().getY() + track * kTrackHeight - juce::roundToInt (scrollY);
}

int ArrangementView::trackAtY (int y) const
{
    return (int) std::floor (((double) (y - gridArea().getY()) + scrollY) / kTrackHeight);
}

juce::Rectangle<int> ArrangementView::clipBounds (const Clip& c) const
{
    const int x = tickToX ((double) c.start);
    return { x, trackToY (c.track) + 2, juce::jmax (6, tickToX ((double) c.end()) - x), kTrackHeight - 4 };
}

juce::Rectangle<int> ArrangementView::muteButtonBounds (int track) const
{
    return { kTrackHeaderWidth - 50, trackToY (track) + (kTrackHeight - 18) / 2, 20, 18 };
}

juce::Rectangle<int> ArrangementView::soloButtonBounds (int track) const
{
    return muteButtonBounds (track).translated (23, 0);
}

std::int64_t ArrangementView::snapTicks() const
{
    switch (snapBox.getSelectedId())
    {
        case snapBeat: return session.project().beatTicks();
        case snapStep: return kTicksPerStep;
        default:       return session.project().barTicks();
    }
}

std::int64_t ArrangementView::snap (double tick) const
{
    const auto unit = snapTicks();
    return (std::int64_t) std::llround (tick / (double) unit) * unit;
}

std::int64_t ArrangementView::snapDown (double tick) const
{
    const auto unit = snapTicks();
    return (std::int64_t) std::floor (tick / (double) unit) * unit;
}

double ArrangementView::contentWidth() const
{
    const auto& project = session.project();
    return (double) (project.songLengthTicks() + kExtraBars * project.barTicks()) * pixelsPerTick();
}

void ArrangementView::updateScrollBars()
{
    const auto grid = gridArea();
    if (grid.isEmpty())
        return;

    const double width = juce::jmax (contentWidth(), (double) grid.getWidth());
    const double height = juce::jmax ((double) session.project().arrangement.tracks.size() * kTrackHeight,
                                      (double) grid.getHeight());
    scrollX = juce::jlimit (0.0, width - grid.getWidth(), scrollX);
    scrollY = juce::jlimit (0.0, height - grid.getHeight(), scrollY);

    horizontalBar.setRangeLimits (0.0, width, juce::dontSendNotification);
    horizontalBar.setCurrentRange (scrollX, grid.getWidth(), juce::dontSendNotification);
    verticalBar.setRangeLimits (0.0, height, juce::dontSendNotification);
    verticalBar.setCurrentRange (scrollY, grid.getHeight(), juce::dontSendNotification);
}

void ArrangementView::scrollBarMoved (juce::ScrollBar* bar, double newRangeStart)
{
    (bar == &horizontalBar ? scrollX : scrollY) = newRangeStart;
    repaint();
}

void ArrangementView::resized()
{
    auto header = getLocalBounds().removeFromTop (kHeaderHeight);
    snapBox.setBounds (header.removeFromRight (124).reduced (4, 3));
    addTrackButton.setBounds (header.removeFromRight (84).reduced (4, 3));

    const auto grid = gridArea();
    horizontalBar.setBounds (grid.getX(), grid.getBottom(), grid.getWidth(), kScrollBarSize);
    verticalBar.setBounds (grid.getRight(), grid.getY(), kScrollBarSize, grid.getHeight());
    updateScrollBars();
}

//==============================================================================
// Painting

void ArrangementView::paint (juce::Graphics& g)
{
    const auto& p = palette();
    g.fillAll (p.panel);
    drawPanelHeader (g, getLocalBounds().removeFromTop (kHeaderHeight),
                     session.playMode() == PlayMode::song ? "Arrangement  /  song mode"
                                                          : "Arrangement  /  switch to SONG to play it");
    paintTracks (g);
    paintRuler (g);
    paintHeaders (g);

    g.setColour (p.panelRaised); // corner between ruler and track headers
    g.fillRect (0, kHeaderHeight, kTrackHeaderWidth, kRulerHeight);
    g.setColour (p.outline);
    g.fillRect (kTrackHeaderWidth - 1, kHeaderHeight, 1, getHeight() - kHeaderHeight);
}

void ArrangementView::paintTracks (juce::Graphics& g)
{
    const auto& p = palette();
    const auto& project = session.project();
    const auto& arrangement = project.arrangement;
    const auto grid = gridArea();
    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (grid);

    g.setColour (p.background);
    g.fillRect (grid);

    // Lanes
    for (int t = 0; t < (int) arrangement.tracks.size(); ++t)
    {
        const int y = trackToY (t);
        if (y > grid.getBottom() || y + kTrackHeight < grid.getY())
            continue;
        if (t % 2 == 1)
        {
            g.setColour (p.panel.withAlpha (0.55f));
            g.fillRect (grid.getX(), y, grid.getWidth(), kTrackHeight);
        }
        g.setColour (p.outline.withAlpha (0.6f));
        g.fillRect (grid.getX(), y + kTrackHeight - 1, grid.getWidth(), 1);
    }

    // Bar and beat lines (beats only when there is room for them).
    const auto beat = project.beatTicks();
    const auto bar = project.barTicks();
    const bool showBeats = (double) beat * pixelsPerTick() >= 10.0;
    const auto unit = showBeats ? beat : bar;
    const auto first = (std::int64_t) (juce::jmax (0.0, xToTick (grid.getX())) / (double) unit) * unit;
    for (auto tick = first; (double) tick <= xToTick (grid.getRight()); tick += unit)
    {
        g.setColour (tick % bar == 0 ? p.outline.brighter (0.25f) : p.outline.withAlpha (0.4f));
        g.fillRect (tickToX ((double) tick), grid.getY(), 1, grid.getHeight());
    }

    // Loop region tint
    if (arrangement.hasLoopRegion())
    {
        const int x1 = tickToX ((double) arrangement.loopStart), x2 = tickToX ((double) arrangement.loopEnd);
        g.setColour (p.accent.withAlpha (0.07f));
        g.fillRect (x1, grid.getY(), x2 - x1, grid.getHeight());
        g.setColour (p.accent.withAlpha (0.6f));
        g.fillRect (x1, grid.getY(), 1, grid.getHeight());
        g.fillRect (x2, grid.getY(), 1, grid.getHeight());
    }

    for (const auto& clip : arrangement.clips)
        if (clipBounds (clip).intersects (grid))
            paintClip (g, clip);

    if (arrangement.clips.empty())
    {
        g.setColour (p.textDim);
        g.setFont (14.0f);
        g.drawText ("Click a track to place the current pattern, or drop audio files here.", grid, juce::Justification::centred);
    }
    if (! marquee.isEmpty())
    {
        g.setColour (p.accent.withAlpha (0.15f));
        g.fillRect (marquee);
        g.setColour (p.accent);
        g.drawRect (marquee, 1);
    }

    if (playheadX >= grid.getX())
    {
        g.setColour (p.playhead);
        g.fillRect (playheadX, grid.getY(), 2, grid.getHeight());
    }
}

void ArrangementView::paintClip (juce::Graphics& g, const Clip& clip)
{
    const auto& project = session.project();
    const auto* pattern = project.findPattern (clip.pattern);
    const auto* audioFile = project.findAudioFile (clip.audio);
    if (pattern == nullptr && audioFile == nullptr)
        return;

    std::shared_ptr<const AudioData> audio;
    juce::String title;
    if (clip.isAudio())
    {
        audio = session.controller().audioPool().get (audioFile->path); // cached; nullptr if missing
        title = juce::File (juce::String::fromUTF8 (audioFile->path.c_str())).getFileNameWithoutExtension();
        if (audio == nullptr)
            title = "MISSING: " + title;
    }
    else
    {
        title = juce::String::fromUTF8 (pattern->name.c_str());
    }

    const bool audible = isTrackAudible (project.arrangement, clip.track);
    const int colourIndex = project.arrangement.tracks[(std::size_t) clip.track].colour;
    auto colour = trackColour (colourIndex);
    if (! audible)
        colour = colour.withSaturation (0.1f).withMultipliedBrightness (0.6f);
    if (clip.isAudio() && audio == nullptr)
        colour = palette().playhead; // a missing file must be impossible to overlook

    const auto bounds = clipBounds (clip).toFloat();
    g.setColour (colour.withAlpha (0.28f));
    g.fillRoundedRectangle (bounds, 4.0f);

    // Title strip with the pattern name.
    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (bounds.toNearestInt());
    auto titleStrip = bounds.withHeight (14.0f);
    g.setColour (colour.withAlpha (0.85f));
    g.fillRect (titleStrip);
    g.setColour (juce::Colours::black.withAlpha (0.85f));
    g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
    g.drawText (title, titleStrip.reduced (5.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft, true);

    if (pattern != nullptr)
        paintPatternClipBody (g, clip, *pattern, bounds, colour);
    else if (audio != nullptr)
        paintAudioClipBody (g, clip, *audio, bounds, colour);

    const bool selected = isSelected (clip.id);
    g.setColour (selected ? juce::Colours::white : colour.withAlpha (0.9f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, selected ? 1.6f : 1.0f);
}

void ArrangementView::paintAudioClipBody (juce::Graphics& g, const Clip& clip, const AudioData& audio,
                                          juce::Rectangle<float> bounds, juce::Colour colour)
{
    // One vertical line per pixel column, from the lowest to the highest sample in that column.
    const auto body = bounds.withTrimmedTop (15.0f).reduced (0.0f, 1.0f);
    const float mid = body.getCentreY();
    const float half = body.getHeight() * 0.5f;
    const double secondsPerPixel = session.project().secondsPerTick() / pixelsPerTick();
    const auto numBlocks = (std::int64_t) (audio.peaks.size() / 2);
    const auto visible = g.getClipBounds();

    g.setColour (colour.brighter (0.45f));
    const int firstColumn = juce::jmax (0, visible.getX() - (int) bounds.getX());
    const int lastColumn = juce::jmin ((int) bounds.getWidth(), visible.getRight() - (int) bounds.getX());
    for (int column = firstColumn; column < lastColumn; ++column)
    {
        const double t0 = clip.audioOffset + (double) column * secondsPerPixel;
        auto b0 = (std::int64_t) (t0 * audio.sampleRate) / AudioData::kPeakBlock;
        auto b1 = (std::int64_t) ((t0 + secondsPerPixel) * audio.sampleRate) / AudioData::kPeakBlock;
        if (b0 >= numBlocks)
            break; // past the end of the file
        b1 = juce::jlimit (b0, numBlocks - 1, b1);

        float lo = 0.0f, hi = 0.0f;
        const auto stride = juce::jmax<std::int64_t> (1, (b1 - b0) / 64); // bounded work when zoomed far out
        for (auto b = b0; b <= b1; b += stride)
        {
            lo = juce::jmin (lo, audio.peaks[(std::size_t) b * 2]);
            hi = juce::jmax (hi, audio.peaks[(std::size_t) b * 2 + 1]);
        }
        lo = juce::jmax (-1.0f, lo * clip.gain);
        hi = juce::jmin (1.0f, hi * clip.gain);
        g.fillRect (bounds.getX() + (float) column, mid - hi * half, 1.0f, juce::jmax (1.0f, (hi - lo) * half));
    }
}

void ArrangementView::paintPatternClipBody (juce::Graphics& g, const Clip& clip, const Pattern& patternRef,
                                            juce::Rectangle<float> bounds, juce::Colour colour)
{
    const auto& project = session.project();
    const auto* pattern = &patternRef;

    // Miniature of the notes: one row per channel, repeated like the pattern is.
    const auto patternLength = juce::jmax<std::int64_t> (1, pattern->lengthTicks());
    const auto offset = clip.offset % patternLength;
    const auto body = bounds.withTrimmedTop (16.0f).reduced (0.0f, 2.0f);
    const int numChannels = juce::jmax (1, (int) project.channels.size());
    const float rowHeight = body.getHeight() / (float) numChannels;
    const double ppt = pixelsPerTick();

    g.setColour (colour.brighter (0.35f));
    for (int ch = 0; ch < (int) project.channels.size(); ++ch)
    {
        const auto it = pattern->notes.find (project.channels[(std::size_t) ch].id);
        if (it == pattern->notes.end())
            continue;
        const float y = body.getY() + rowHeight * (float) ch;
        for (const auto& n : it->second)
        {
            if (n.start >= patternLength)
                continue;
            auto position = n.start - offset;
            if (position < 0)
                position += patternLength;
            for (; position < clip.length; position += patternLength)
            {
                const float x = bounds.getX() + (float) ((double) position * ppt);
                const float w = juce::jmax (1.5f, (float) ((double) juce::jmin (n.length, clip.length - position) * ppt) - 1.0f);
                g.fillRect (x, y + 0.5f, w, juce::jmax (1.0f, rowHeight - 1.0f));
            }
        }
    }

    // A notch wherever the pattern starts again inside the clip.
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    for (auto position = patternLength - offset; position < clip.length; position += patternLength)
        g.fillRect (bounds.getX() + (float) ((double) position * ppt), bounds.getY() + 14.0f, 1.0f, bounds.getHeight() - 14.0f);
}

void ArrangementView::paintRuler (juce::Graphics& g)
{
    const auto& p = palette();
    const auto& project = session.project();
    const auto ruler = rulerArea();
    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (ruler);

    g.setColour (p.panelRaised);
    g.fillRect (ruler);

    if (project.arrangement.hasLoopRegion())
    {
        const int x1 = tickToX ((double) project.arrangement.loopStart), x2 = tickToX ((double) project.arrangement.loopEnd);
        g.setColour (p.accent.withAlpha (0.55f));
        g.fillRect (x1, ruler.getY() + 2, x2 - x1, ruler.getHeight() - 5);
    }

    // Label every bar, or every 2nd / 4th / ... when zoomed out.
    const auto bar = project.barTicks();
    int labelEvery = 1;
    while ((double) (bar * labelEvery) * pixelsPerTick() < 34.0)
        labelEvery *= 2;

    g.setFont (11.0f);
    const auto firstBar = (std::int64_t) (juce::jmax (0.0, xToTick (ruler.getX())) / (double) bar);
    for (auto b = firstBar; (double) (b * bar) <= xToTick (ruler.getRight()); ++b)
    {
        const int x = tickToX ((double) (b * bar));
        const bool labelled = b % labelEvery == 0;
        g.setColour (labelled ? p.text : p.textDim.withAlpha (0.6f));
        g.fillRect (x, ruler.getBottom() - (labelled ? 9 : 4), 1, labelled ? 9 : 4);
        if (labelled)
            g.drawText (juce::String (b + 1), x + 4, ruler.getY(), 40, ruler.getHeight() - 2, juce::Justification::centredLeft);
    }

    g.setColour (p.outline);
    g.fillRect (ruler.getX(), ruler.getBottom() - 1, ruler.getWidth(), 1);
}

void ArrangementView::paintHeaders (juce::Graphics& g)
{
    const auto& p = palette();
    const auto& tracks = session.project().arrangement.tracks;
    const auto area = headerArea();
    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (area);

    g.setColour (p.panel);
    g.fillRect (area);

    for (int t = 0; t < (int) tracks.size(); ++t)
    {
        const auto& track = tracks[(std::size_t) t];
        const int y = trackToY (t);
        if (y > area.getBottom() || y + kTrackHeight < area.getY())
            continue;

        g.setColour (trackColour (track.colour));
        g.fillRect (0, y + 1, 4, kTrackHeight - 2);

        g.setColour (isTrackAudible (session.project().arrangement, t) ? p.text : p.textDim);
        g.setFont (13.0f);
        g.drawText (juce::String::fromUTF8 (track.name.c_str()), 12, y, kTrackHeaderWidth - 68, kTrackHeight,
                    juce::Justification::centredLeft, true);

        auto drawToggle = [&] (juce::Rectangle<int> bounds, const char* letter, bool on, juce::Colour onColour)
        {
            g.setColour (on ? onColour : p.control);
            g.fillRoundedRectangle (bounds.toFloat(), 3.0f);
            g.setColour (on ? juce::Colours::black : p.textDim);
            g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
            g.drawText (letter, bounds, juce::Justification::centred);
        };
        drawToggle (muteButtonBounds (t), "M", track.muted, p.playhead);
        drawToggle (soloButtonBounds (t), "S", track.solo, p.accent);

        g.setColour (p.outline.withAlpha (0.6f));
        g.fillRect (0, y + kTrackHeight - 1, kTrackHeaderWidth, 1);
    }
}

//==============================================================================
// Editing

Clip* ArrangementView::clipAt (juce::Point<int> position)
{
    auto& clips = session.project().arrangement.clips;
    for (int i = (int) clips.size(); --i >= 0;) // topmost (most recently added) first
        if (clipBounds (clips[(std::size_t) i]).contains (position))
            return &clips[(std::size_t) i];
    return nullptr;
}

void ArrangementView::eraseAt (juce::Point<int> position)
{
    if (const auto* clip = clipAt (position))
    {
        session.project().removeClip (clip->id);
        session.editedContinuous ("Delete clips");
    }
}

void ArrangementView::headerClicked (const juce::MouseEvent& e)
{
    const int track = trackAtY (e.y);
    auto& tracks = session.project().arrangement.tracks;
    if (track < 0 || track >= (int) tracks.size())
        return;

    auto& t = tracks[(std::size_t) track];
    if (e.mods.isPopupMenu())
    {
        showTrackMenu (track);
    }
    else if (muteButtonBounds (track).contains (e.getPosition()))
    {
        t.muted = ! t.muted;
        session.edited (t.muted ? "Mute track" : "Unmute track");
    }
    else if (soloButtonBounds (track).contains (e.getPosition()))
    {
        t.solo = ! t.solo;
        session.edited (t.solo ? "Solo track" : "Unsolo track");
    }
}

void ArrangementView::showTrackMenu (int track)
{
    juce::PopupMenu colours;
    for (int i = 0; i < (int) std::size (kTrackColours); ++i)
    {
        juce::PopupMenu::Item item ("Colour " + juce::String (i + 1));
        item.itemID = 100 + i;
        item.colour = trackColour (i);
        item.isTicked = session.project().arrangement.tracks[(std::size_t) track].colour % (int) std::size (kTrackColours) == i;
        colours.addItem (item);
    }

    juce::PopupMenu menu;
    menu.addItem (1, "Rename...");
    menu.addSubMenu ("Colour", colours);
    menu.addSubMenu ("Audio to mixer insert", session.mixerInsertMenu (session.project().arrangement.tracks[(std::size_t) track].mixerInsert, 1000));
    menu.addSeparator();
    menu.addItem (2, "Delete track and its clips");

    const auto trackId = session.project().arrangement.tracks[(std::size_t) track].id;
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [this, trackId] (int choice)
    {
        // Look the track up again by id: the project may have changed while the menu was open.
        auto& tracks = session.project().arrangement.tracks;
        int index = -1;
        for (int i = 0; i < (int) tracks.size(); ++i)
            if (tracks[(std::size_t) i].id == trackId)
                index = i;
        if (index < 0)
            return;

        if (choice >= 1000)
        {
            const auto insert = session.insertFromMenuItem (choice, 1000);
            if (insert >= 0)
            {
                tracks[(std::size_t) index].mixerInsert = (Id) insert;
                session.edited ("Route track audio");
            }
        }
        else if (choice == 1)
        {
            renameTrack (index);
        }
        else if (choice == 2)
        {
            session.project().removeTrack (index);
            session.edited ("Delete track");
        }
        else if (choice >= 100)
        {
            tracks[(std::size_t) index].colour = choice - 100;
            session.edited ("Change track colour");
        }
    });
}

void ArrangementView::renameTrack (int track)
{
    const auto& t = session.project().arrangement.tracks[(std::size_t) track];
    const auto trackId = t.id;

    auto* window = new juce::AlertWindow ("Rename track", {}, juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor ("name", juce::String::fromUTF8 (t.name.c_str()));
    window->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    window->enterModalState (true, juce::ModalCallbackFunction::create ([this, window, trackId] (int result)
    {
        const auto name = window->getTextEditorContents ("name").trim();
        if (result != 1 || name.isEmpty())
            return;
        for (auto& candidate : session.project().arrangement.tracks)
        {
            if (candidate.id == trackId)
            {
                candidate.name = name.toStdString();
                session.edited ("Rename track");
            }
        }
    }), true);
}

void ArrangementView::mouseDown (const juce::MouseEvent& e)
{
    dragMode = DragMode::none;
    auto& project = session.project();

    if (headerArea().contains (e.getPosition()))
    {
        headerClicked (e);
        return;
    }

    if (rulerArea().contains (e.getPosition()))
    {
        if (e.mods.isPopupMenu())
        {
            if (project.arrangement.hasLoopRegion())
            {
                project.arrangement.loopStart = project.arrangement.loopEnd = 0;
                session.edited ("Clear loop region");
            }
            return;
        }
        dragMode = DragMode::loopRegion;
        loopAnchor = juce::jmax<std::int64_t> (0, snap (xToTick (e.x)));
        loopDragMoved = false;
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

    grabKeyboardFocus(); // so Delete, Ctrl+C and friends act on the arrangement
    auto* clip = clipAt (e.getPosition());

    if (clip != nullptr && e.mods.isAltDown())
    {
        // Split at the grid line nearest the mouse.
        if (project.splitClip (clip->id, snap (xToTick (e.x))) != 0)
            session.edited ("Split clip");
        else if (onMessage)
            onMessage ("Nothing to split there: the nearest grid line is not inside the clip. Try a finer snap.");
        return;
    }

    if (clip == nullptr)
    {
        if (e.mods.isCommandDown())
        {
            // Ctrl+drag on an empty lane: rubber-band selection (Shift keeps what is selected).
            dragMode = DragMode::marquee;
            marqueeStart = e.getPosition();
            marquee = {};
            if (! e.mods.isShiftDown())
                selection.clear();
            marqueeBase = selection;
            repaint();
            return;
        }

        const int track = trackAtY (e.y);
        if (track < 0 || track >= (int) project.arrangement.tracks.size())
        {
            selection.clear();
            repaint();
            return;
        }
        clip = project.addClip (session.pattern().id, track, snapDown (xToTick (e.x)));
        if (clip == nullptr)
            return;
        dragMode = DragMode::move;
        dragAction = "Add clip"; // placing and positioning a new clip is one undo step
        dragClip = clip->id;
        dragOriginal = *clip;
        selection = { clip->id };
        session.editedContinuous (dragAction);
    }
    else if (e.mods.isCommandDown())
    {
        // Ctrl+drag a clip: leave the selection where it is and drag copies of it.
        if (! isSelected (clip->id))
            selection = { clip->id };
        const Id clickedId = clip->id;
        std::vector<Id> copies;
        for (const auto& original : editing::copyClips (project, selection))
        {
            Clip copy = original;
            copy.id = project.allocateId();
            project.arrangement.clips.push_back (copy); // invalidates `clip`
            copies.push_back (copy.id);
            if (original.id == clickedId)
                dragClip = copy.id;
        }
        selection = copies;
        dragOriginal = *project.findClip (dragClip);
        dragMode = DragMode::move;
        dragAction = copies.size() > 1 ? "Duplicate clips" : "Duplicate clip";
        session.editedContinuous (dragAction);
    }
    else
    {
        const Id clickedId = clip->id;
        if (e.mods.isShiftDown())
        {
            // Shift+click adds to / removes from the selection.
            if (isSelected (clickedId))
            {
                std::erase (selection, clickedId);
                repaint();
                return;
            }
            selection.push_back (clickedId);
        }
        else if (! isSelected (clickedId))
        {
            selection = { clickedId };
        }

        dragMode = e.x >= clipBounds (*clip).getRight() - kResizeHandle ? DragMode::resize : DragMode::move;
        const bool several = selection.size() > 1;
        dragAction = dragMode == DragMode::resize ? (several ? "Resize clips" : "Resize clip")
                                                  : (several ? "Move clips" : "Move clip");
        dragClip = clickedId;
        dragOriginal = *clip;
        repaint();
    }

    dragOriginals = editing::copyClips (project, selection);
    dragStartTick = xToTick (e.x);
    dragStartTrack = trackAtY (e.y);
}

void ArrangementView::mouseDrag (const juce::MouseEvent& e)
{
    auto& project = session.project();

    switch (dragMode)
    {
        case DragMode::none:
            return;

        case DragMode::erase:
            eraseAt (e.getPosition());
            return;

        case DragMode::loopRegion:
        {
            const auto here = juce::jmax<std::int64_t> (0, snap (xToTick (e.x)));
            if (here == loopAnchor && ! loopDragMoved)
                return;
            loopDragMoved = true;
            auto& a = project.arrangement;
            const auto start = juce::jmin (loopAnchor, here), end = juce::jmax (loopAnchor, here);
            if (a.loopStart != start || a.loopEnd != end)
            {
                a.loopStart = start;
                a.loopEnd = end; // start == end means "no region", which is what a zero-width drag should give
                session.editedContinuous ("Set loop region");
            }
            return;
        }

        case DragMode::marquee:
        {
            marquee = juce::Rectangle<int> (marqueeStart, e.getPosition()).getIntersection (gridArea());
            selection = marqueeBase;
            for (const auto& c : project.arrangement.clips)
                if (clipBounds (c).intersects (marquee) && ! isSelected (c.id))
                    selection.push_back (c.id);
            repaint();
            return;
        }

        case DragMode::move:
        case DragMode::resize:
            break;
    }

    if (project.findClip (dragClip) == nullptr)
        return;

    const double deltaTicks = xToTick (e.x) - dragStartTick;
    const int lastTrack = (int) project.arrangement.tracks.size() - 1;
    bool changed = false;

    if (dragMode == DragMode::move)
    {
        // The clip under the mouse snaps to the grid; the others keep their distance from it.
        auto dTick = snap ((double) dragOriginal.start + deltaTicks) - dragOriginal.start;
        int dTrack = trackAtY (e.y) - dragStartTrack;

        // Stop the whole group at the edges, so it never changes shape.
        for (const auto& original : dragOriginals)
        {
            dTick = juce::jmax (dTick, -original.start);
            dTrack = juce::jlimit (-original.track, juce::jmax (-original.track, lastTrack - original.track), dTrack);
        }

        for (const auto& original : dragOriginals)
        {
            if (auto* clip = project.findClip (original.id))
            {
                const Clip before = *clip;
                clip->start = original.start + dTick;
                clip->track = original.track + dTrack;
                changed = changed || ! (*clip == before);
            }
        }
    }
    else
    {
        const auto end = snap ((double) dragOriginal.end() + deltaTicks);
        const auto dLength = juce::jmax (snapTicks(), end - dragOriginal.start) - dragOriginal.length;

        for (const auto& original : dragOriginals)
        {
            if (auto* clip = project.findClip (original.id))
            {
                const Clip before = *clip;
                clip->length = juce::jmax (snapTicks(), original.length + dLength);
                if (clip->isAudio()) // an audio clip cannot be longer than what is left of its file
                    clip->length = juce::jmax<std::int64_t> (1, juce::jmin (clip->length, maxAudioClipLength (*clip)));
                changed = changed || ! (*clip == before);
            }
        }
    }

    if (changed)
        session.editedContinuous (dragAction);
}

std::int64_t ArrangementView::maxAudioClipLength (const Clip& clip)
{
    const auto* file = session.project().findAudioFile (clip.audio);
    const auto data = file != nullptr ? session.controller().audioPool().get (file->path) : nullptr;
    if (data == nullptr)
        return clip.length; // unknown: leave it as it is
    const double remaining = juce::jmax (0.0, data->durationSeconds() - clip.audioOffset);
    return (std::int64_t) std::ceil (remaining / session.project().secondsPerTick());
}

//==============================================================================
// Keyboard

bool ArrangementView::keyPressed (const juce::KeyPress& key)
{
    auto& project = session.project();
    const auto command = juce::ModifierKeys::commandModifier;
    const bool hasSelection = ! selection.empty();

    if (key == juce::KeyPress (juce::KeyPress::deleteKey) || key == juce::KeyPress (juce::KeyPress::backspaceKey))
    {
        if (hasSelection)
        {
            editing::deleteClips (project, selection);
            selection.clear();
            session.edited ("Delete clips");
        }
        return true;
    }
    if (key == juce::KeyPress ('a', command, 0))
    {
        selection.clear();
        for (const auto& c : project.arrangement.clips)
            selection.push_back (c.id);
        repaint();
        return true;
    }
    if (key == juce::KeyPress (juce::KeyPress::escapeKey))
    {
        selection.clear();
        repaint();
        return true;
    }
    if (key == juce::KeyPress ('c', command, 0))
    {
        if (hasSelection)
            session.clipClipboard = editing::copyClips (project, selection);
        return true;
    }
    if (key == juce::KeyPress ('x', command, 0))
    {
        if (hasSelection)
        {
            session.clipClipboard = editing::copyClips (project, selection);
            editing::deleteClips (project, selection);
            selection.clear();
            session.edited ("Cut clips");
        }
        return true;
    }
    if (key == juce::KeyPress ('v', command, 0))
    {
        if (session.clipClipboard.empty())
            return true;
        // Paste under the mouse if it is over the lanes, otherwise at the end of the song on the first track.
        const auto mouse = getMouseXYRelative();
        std::int64_t at = project.arrangement.clips.empty() ? 0 : project.songLengthTicks();
        int track = 0;
        if (gridArea().contains (mouse))
        {
            at = snapDown (xToTick (mouse.x));
            track = juce::jlimit (0, juce::jmax (0, (int) project.arrangement.tracks.size() - 1), trackAtY (mouse.y));
        }
        auto pasted = editing::pasteClips (project, session.clipClipboard, at, track);
        if (! pasted.empty())
        {
            selection = std::move (pasted);
            session.edited ("Paste clips");
        }
        else if (onMessage)
        {
            onMessage ("Nothing pasted: the copied clips' patterns or audio files no longer exist.");
        }
        return true;
    }
    if (key == juce::KeyPress ('d', command, 0))
    {
        auto copies = editing::duplicateClips (project, selection, snapTicks());
        if (! copies.empty())
        {
            selection = std::move (copies);
            session.edited ("Duplicate clips");
        }
        return true;
    }

    return false; // not ours: Space, Ctrl+Z and the rest go on to the main window
}

//==============================================================================
// Audio import

bool ArrangementView::importAudioFile (const juce::File& file, std::int64_t atTick, int track)
{
    auto& project = session.project();
    const auto path = file.getFullPathName().toStdString();
    auto& pool = session.controller().audioPool();
    pool.forgetFailures(); // the user may have fixed or replaced the file since the last attempt

    const auto data = pool.get (path);
    if (data == nullptr)
    {
        const auto reason = juce::String::fromUTF8 (pool.errorFor (path).c_str());
        juce::Logger::writeToLog ("Audio import failed: " + reason);
        if (onMessage)
            onMessage (reason);
        return false;
    }

    while ((int) project.arrangement.tracks.size() <= track)
        project.addTrack();

    const auto length = (std::int64_t) std::ceil (data->durationSeconds() / project.secondsPerTick());
    const auto* clip = project.addAudioClip (project.addAudioFile (path), track, atTick, length);
    if (clip == nullptr)
        return false;
    selection = { clip->id };
    return true;
}

int ArrangementView::firstEmptyTrack() const
{
    const auto& arrangement = session.project().arrangement;
    int track = 0;
    for (; track < (int) arrangement.tracks.size(); ++track)
        if (std::none_of (arrangement.clips.begin(), arrangement.clips.end(),
                          [track] (const Clip& c) { return c.track == track; }))
            break;
    return track; // == number of tracks when all are in use: the importer then adds one
}

bool ArrangementView::addRecordedTake (const juce::File& file, std::int64_t startTick, double offsetSeconds)
{
    if (! importAudioFile (file, startTick, firstEmptyTrack()))
        return false;

    // importAudioFile selected the new clip; trim the latency off its start.
    if (auto* clip = session.project().findClip (selection.front()))
    {
        const auto skipped = (std::int64_t) std::floor (offsetSeconds / session.project().secondsPerTick());
        clip->audioOffset = offsetSeconds;
        clip->length = juce::jmax<std::int64_t> (1, clip->length - skipped);
    }
    session.edited ("Record audio");
    return true;
}

void ArrangementView::importAudioFiles (const juce::Array<juce::File>& files)
{
    // Start on the first track with nothing on it, so imports never land on top of existing clips.
    const int track = firstEmptyTrack();
    int imported = 0;
    for (const auto& file : files)
        if (importAudioFile (file, 0, track + imported))
            ++imported;
    if (imported > 0)
        session.edited (imported > 1 ? "Import audio files" : "Import audio");
}

bool ArrangementView::isInterestedInFileDrag (const juce::StringArray& files)
{
    const auto wildcard = juce::String (AudioPool::supportedWildcard());
    for (const auto& path : files)
        if (juce::File (path).hasFileExtension (wildcard.removeCharacters ("*")))
            return true;
    return false;
}

void ArrangementView::filesDropped (const juce::StringArray& files, int x, int y)
{
    const auto at = juce::jmax<std::int64_t> (0, snapDown (xToTick (x)));
    const int firstTrack = juce::jmax (0, trackAtY (y));
    int imported = 0;
    for (const auto& path : files)
        if (importAudioFile (juce::File (path), at, firstTrack + imported))
            ++imported;
    if (imported > 0)
        session.edited (imported > 1 ? "Import audio files" : "Import audio");
}

void ArrangementView::mouseUp (const juce::MouseEvent& e)
{
    if (dragMode == DragMode::loopRegion && ! loopDragMoved)
    {
        // A plain click on the ruler moves the playhead.
        if (session.playMode() != PlayMode::song)
        {
            if (onMessage)
                onMessage ("Switch to SONG mode to move the playhead along the arrangement.");
        }
        else if (! session.engine().locate ((double) juce::jmax<std::int64_t> (0, snap (xToTick (e.x)))) && onMessage)
        {
            onMessage ("Could not move the playhead: the audio engine is not responding.");
        }
    }

    session.endGesture();
    if (dragMode == DragMode::marquee)
    {
        marquee = {};
        repaint();
    }
    dragMode = DragMode::none;
    dragClip = 0;
}

void ArrangementView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! gridArea().contains (e.getPosition()))
        return;
    const auto* clip = clipAt (e.getPosition());
    if (clip == nullptr || clip->isAudio())
        return;

    const auto& patterns = session.project().patterns;
    for (int i = 0; i < (int) patterns.size(); ++i)
    {
        if (patterns[(std::size_t) i].id == clip->pattern)
        {
            session.setPatternIndex (i);
            if (onOpenPattern)
                onOpenPattern();
            return;
        }
    }
}

void ArrangementView::mouseMove (const juce::MouseEvent& e)
{
    auto shape = juce::MouseCursor::NormalCursor;
    if (gridArea().contains (e.getPosition()))
    {
        if (const auto* clip = clipAt (e.getPosition()))
            shape = e.x >= clipBounds (*clip).getRight() - kResizeHandle ? juce::MouseCursor::LeftRightResizeCursor
                                                                         : juce::MouseCursor::DraggingHandCursor;
    }
    else if (rulerArea().contains (e.getPosition()))
    {
        shape = juce::MouseCursor::PointingHandCursor;
    }
    setMouseCursor (shape);
}

void ArrangementView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCtrlDown() || e.mods.isCommandDown())
    {
        const double tickUnderMouse = xToTick (e.x);
        pixelsPerBeat = juce::jlimit (3.0, 200.0, pixelsPerBeat * std::pow (1.25, (double) wheel.deltaY * 4.0));
        scrollX = tickUnderMouse * pixelsPerTick() - (double) (e.x - gridArea().getX());
    }
    else if (e.mods.isShiftDown())
    {
        scrollX -= (double) (wheel.deltaY + wheel.deltaX) * 240.0;
    }
    else
    {
        scrollY -= (double) wheel.deltaY * 140.0;
        scrollX -= (double) wheel.deltaX * 240.0;
    }
    updateScrollBars();
    repaint();
}

void ArrangementView::tick()
{
    const auto& engine = session.engine();
    const bool show = session.playMode() == PlayMode::song; // in song mode the playhead is shown even when stopped
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

#pragma once

#include "Session.h"
#include "Theme.h"

#include <functional>

namespace tactus::ui
{

/** Editor for a sampler or drum-kit channel: loading samples, the zones they are played
    from, slicing, and the envelope. Lives inside the InstrumentPanel, which scrolls it and
    calls refresh() whenever the session changes.

    A drum kit shows sixteen pads (keys from C4 up, so the step sequencer plays pad 1);
    a sampler shows the key range of each zone instead. Otherwise the two are the same. */
class SamplerPanel final : public juce::Component,
                           public juce::FileDragAndDropTarget
{
public:
    static constexpr int kNumPads = 16;

    explicit SamplerPanel (Session&);
    ~SamplerPanel() override;

    /** Re-reads the selected channel and resizes itself to fit its controls. */
    void refresh();

    void resized() override;
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    class WaveDisplay;
    class Pad;
    friend class WaveDisplay;
    friend class Pad;

    using Setter = std::function<void (Channel&, SampleZone*, double)>;

    struct Control
    {
        juce::Label label;
        juce::Slider slider;
        bool perZone = false; // needs a selected zone
    };

    Channel* channel();
    SampleZone* zone();
    /** The one-key zone on a pad's key, if any. */
    SampleZone* zoneOnKey (int key);
    std::shared_ptr<const AudioData> dataFor (const SampleZone&);
    juce::String fileNameFor (const SampleZone&);
    double effectiveEnd (const SampleZone&);

    void addControl (Control&, const juce::String& name, double low, double high, double step, double midPoint,
                     const juce::String& suffix, int decimals, bool perZone, Setter);
    void addKeyControl (Control&, const juce::String& name, Setter);
    void setUpButton (juce::TextButton&, const juce::String& text, bool toggles);

    int layoutControls(); // places everything; returns the height needed
    void setStatus (const juce::String&, bool isError = false);

    void chooseFiles (const juce::String& title, bool multiple, std::function<void (const juce::Array<juce::File>&)>);
    /** padKey >= 0: the first file goes onto that pad, replacing what is there. */
    void addFiles (const juce::Array<juce::File>&, bool replaceAll, int padKey = -1);
    void removeSelectedZone();
    void showToolsMenu();
    void slice (int equalParts); // 0 = at transients
    void showPadMenu (int key, juce::Component& target);
    void selectZone (Id);
    void audition (int key, bool on);

    Session& session;
    Id selectedZone = 0;
    bool kit = false;
    bool updating = false;
    int auditionKey = -1;
    Id auditionChannel = 0;

    juce::TextButton loadButton, addButton, toolsButton, removeButton;
    juce::TextButton loopButton, reverseButton, oneShotButton;
    juce::ComboBox zoneBox;
    std::unique_ptr<WaveDisplay> wave;
    juce::OwnedArray<Pad> pads;
    Control start, end, loopStart, tune, gain, pan, rootKey, lowKey, highKey;
    Control attack, decay, sustain, release;
    std::vector<Control*> controls;
    juce::Label status;
    std::unique_ptr<juce::FileChooser> chooser;
};

} // namespace tactus::ui

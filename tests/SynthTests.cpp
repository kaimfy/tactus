#include "TestFramework.h"
#include "TestHelpers.h"

#include "tactus/dsp/PolySynth.h"
#include "tactus/model/ProjectSerializer.h"
#include "tactus/model/SynthPresets.h"

#include <set>

using namespace tactus;

namespace
{
    constexpr double kRate = 48000.0;
    constexpr double kPi = 3.141592653589793;

    /** A synth on its own, with the audio it has produced so far. */
    struct Bench
    {
        PolySynth synth;
        SynthParams params;
        std::vector<float> left, right;

        Bench()
        {
            // A plain, bright, steady tone unless a test says otherwise.
            params.waveform = Waveform::sine;
            params.cutoffHz = 18000.0f;
            params.resonance = 0.0f;
            params.attack = 0.002f;
            params.sustain = 1.0f;
            params.release = 0.01f;
            synth.prepare (kRate);
        }

        /** Renders `seconds` more; returns the index of the first new sample. */
        std::size_t run (double seconds)
        {
            const auto at = left.size();
            const auto n = (std::size_t) (seconds * kRate);
            left.resize (at + n, 0.0f);
            right.resize (at + n, 0.0f);
            synth.setParams (params);
            synth.renderStereo (left.data() + at, right.data() + at, (int) n);
            return at;
        }

        /** Starts a note with the current settings in force, as the engine does.
            (The settings reach the synth before the notes of each block.) */
        void noteOn (int key, float velocity, std::int64_t duration)
        {
            synth.setParams (params);
            synth.noteOn (key, velocity, duration);
        }

        std::size_t at (double seconds) const { return (std::size_t) (seconds * kRate); }
    };

    /** Amplitude of the sine component at `hz` in s[from, to). */
    double component (const std::vector<float>& s, std::size_t from, std::size_t to, double hz)
    {
        double c = 0.0, q = 0.0;
        for (std::size_t i = from; i < to; ++i)
        {
            const double phase = 2.0 * kPi * hz * (double) i / kRate;
            c += s[i] * std::cos (phase);
            q += s[i] * std::sin (phase);
        }
        return 2.0 * std::sqrt (c * c + q * q) / (double) (to - from);
    }

    int crossings (const std::vector<float>& s, std::size_t from, std::size_t to)
    {
        int n = 0;
        for (std::size_t i = from + 1; i < to; ++i)
            if (s[i - 1] < 0.0f && s[i] >= 0.0f)
                ++n;
        return n;
    }

    double rms (const std::vector<float>& s, std::size_t from, std::size_t to)
    {
        double sum = 0.0;
        for (std::size_t i = from; i < to; ++i)
            sum += (double) s[i] * s[i];
        return std::sqrt (sum / (double) (to - from));
    }

    /** Energy of the sample-to-sample difference: a measure of high-frequency content. */
    double brightness (const std::vector<float>& s, std::size_t from, std::size_t to)
    {
        double sum = 0.0;
        for (std::size_t i = from + 1; i < to; ++i)
            sum += std::pow ((double) s[i] - s[i - 1], 2.0);
        return sum / (double) (to - from);
    }
}

TEST (the_default_synth_patch_is_mono_and_the_same_on_both_sides)
{
    Bench b;
    b.params = SynthParams {};
    b.noteOn (57, 1.0f, 0);
    b.noteOn (64, 0.7f, 0);
    b.run (0.5);
    CHECK (b.left == b.right);
    CHECK (rms (b.left, 0, b.left.size()) > 0.05);

    // The mono render is the same signal.
    PolySynth again;
    again.prepare (kRate);
    again.setParams (SynthParams {});
    again.noteOn (57, 1.0f, 0);
    again.noteOn (64, 0.7f, 0);
    std::vector<float> mono (b.left.size(), 0.0f);
    again.render (mono.data(), (int) mono.size());
    CHECK (mono == b.left);
}

TEST (the_second_oscillator_sub_and_noise_add_what_they_say)
{
    // A4 = 440 Hz. One voice at full velocity has amplitude 0.32.
    Bench plain;
    plain.noteOn (69, 1.0f, 0);
    plain.run (0.5);
    const auto from = plain.at (0.1), to = plain.at (0.5);
    CHECK_NEAR (component (plain.left, from, to, 440.0), 0.32, 0.02);
    CHECK (component (plain.left, from, to, 880.0) < 0.005);
    CHECK (component (plain.left, from, to, 220.0) < 0.005);

    Bench octave; // oscillator 2 an octave up
    octave.params.waveform2 = Waveform::sine;
    octave.params.osc2Level = 0.5f;
    octave.params.osc2Semitones = 12.0f;
    octave.noteOn (69, 1.0f, 0);
    octave.run (0.5);
    CHECK_NEAR (component (octave.left, from, to, 440.0), 0.32, 0.02);
    CHECK_NEAR (component (octave.left, from, to, 880.0), 0.16, 0.02);

    Bench detuned; // oscillator 2 fifty cents sharp, oscillator 1 off
    detuned.params.osc1Level = 0.0f;
    detuned.params.waveform2 = Waveform::sine;
    detuned.params.osc2Level = 1.0f;
    detuned.params.osc2Detune = 50.0f;
    detuned.noteOn (69, 1.0f, 0);
    detuned.run (0.5);
    CHECK_NEAR (crossings (detuned.left, from, to), 440.0 * std::pow (2.0, 50.0 / 1200.0) * 0.4, 1.5);

    Bench sub; // a sine an octave below
    sub.params.subLevel = 1.0f;
    sub.noteOn (69, 1.0f, 0);
    sub.run (0.5);
    CHECK_NEAR (component (sub.left, from, to, 220.0), 0.32, 0.02);
    CHECK_NEAR (component (sub.left, from, to, 440.0), 0.32, 0.02);

    Bench noise, noiseAgain; // noise alone: sound without a pitch, and the same every time
    for (auto* n : { &noise, &noiseAgain })
    {
        n->params.osc1Level = 0.0f;
        n->params.noiseLevel = 1.0f;
        n->noteOn (69, 1.0f, 0);
        n->run (0.5);
    }
    CHECK (rms (noise.left, from, to) > 0.05);
    CHECK (component (noise.left, from, to, 440.0) < 0.02);
    CHECK (noise.left == noiseAgain.left);
}

TEST (unison_thickens_and_widens_without_getting_louder)
{
    Bench single;
    single.params.waveform = Waveform::saw;
    single.noteOn (57, 1.0f, 0);
    single.run (1.0);

    Bench wide;
    wide.params.waveform = Waveform::saw;
    wide.params.unison = 7;
    wide.params.unisonDetune = 25.0f;
    wide.params.unisonWidth = 1.0f;
    wide.noteOn (57, 1.0f, 0);
    wide.run (1.0);
    CHECK (wide.left != wide.right); // spread across the stereo field
    const double ratio = rms (wide.left, 4800, 48000) / rms (single.left, 4800, 48000);
    CHECK (ratio > 0.4 && ratio < 1.3); // seven copies, about the loudness of one
    const th::Stereo both { wide.left, wide.right };
    CHECK (both.allFinite());

    // The copies beat against each other: the level is no longer steady.
    double lowest = 1.0e9, highest = 0.0;
    for (std::size_t i = 4800; i + 2400 <= 48000; i += 2400)
    {
        const double level = rms (wide.left, i, i + 2400);
        lowest = std::min (lowest, level);
        highest = std::max (highest, level);
    }
    CHECK (highest > 1.15 * lowest);

    Bench narrow; // no width: thick but mono
    narrow.params = wide.params;
    narrow.params.unisonWidth = 0.0f;
    narrow.noteOn (57, 1.0f, 0);
    narrow.run (0.2);
    CHECK (narrow.left == narrow.right);
}

TEST (glide_slides_from_the_previous_note)
{
    auto pitchAfterSecondNote = [] (float glide, double fromSeconds)
    {
        Bench b;
        b.params.glide = glide;
        b.noteOn (57, 1.0f, 4800); // A3 = 220 Hz, a tenth of a second
        b.run (0.2);
        b.noteOn (69, 1.0f, 0);    // A4 = 440 Hz
        const auto start = b.run (0.5);
        const auto from = start + b.at (fromSeconds);
        return crossings (b.left, from, from + b.at (0.05)) / 0.05; // Hz
    };

    CHECK_NEAR (pitchAfterSecondNote (0.0f, 0.0), 440.0, 25.0);  // no glide: there at once
    const double early = pitchAfterSecondNote (0.2f, 0.0);
    CHECK (early > 215.0 && early < 300.0);                      // gliding: still near the old note
    const double middle = pitchAfterSecondNote (0.2f, 0.09);
    CHECK (middle > early + 20.0 && middle < 420.0);             // on its way
    CHECK_NEAR (pitchAfterSecondNote (0.2f, 0.3), 440.0, 25.0);  // and arrived

    // The first note of all has nowhere to glide from.
    Bench first;
    first.params.glide = 0.5f;
    first.noteOn (69, 1.0f, 0);
    first.run (0.1);
    CHECK_NEAR (crossings (first.left, 0, first.at (0.1)) / 0.1, 440.0, 20.0);
}

TEST (mono_mode_plays_overlapping_notes_legato_on_one_voice)
{
    // A filter envelope that makes a short bright burst at every new attack.
    auto setUp = [] (Bench& b, bool mono)
    {
        b.params.waveform = Waveform::saw;
        b.params.mono = mono;
        b.params.cutoffHz = 150.0f;
        b.params.filterEnvAmount = 5.0f;
        b.params.filterAttack = 0.001f;
        b.params.filterDecay = 0.3f;
        b.params.filterSustain = 0.0f;
        b.noteOn (48, 1.0f, 0);
        b.run (0.3);
    };

    Bench mono, poly;
    setUp (mono, true);
    setUp (poly, false);
    mono.noteOn (55, 1.0f, 0);
    poly.noteOn (55, 1.0f, 0);
    CHECK_EQ (mono.synth.activeVoiceCount(), 1);
    CHECK_EQ (poly.synth.activeVoiceCount(), 2);
    const auto at = mono.run (0.02);
    poly.run (0.02);
    // Poly starts a new voice with a new burst; mono carries on without one.
    CHECK (brightness (mono.left, at, at + 960) < 0.2 * brightness (poly.left, at, at + 960));

    // The old key no longer holds the voice; the new one does.
    mono.synth.noteOff (48);
    mono.run (0.2);
    CHECK_EQ (mono.synth.activeVoiceCount(), 1);
    mono.synth.noteOff (55);
    mono.run (0.3);
    CHECK_EQ (mono.synth.activeVoiceCount(), 0);

    // After a release the next note attacks again.
    mono.noteOn (48, 1.0f, 0);
    const auto again = mono.run (0.02);
    CHECK (brightness (mono.left, again, again + 960) > 5.0 * brightness (mono.left, at, at + 960));
}

TEST (the_lfo_moves_pitch_level_and_cutoff)
{
    // A 2 Hz sine LFO is at its top around 0.125 s and at its bottom around 0.375 s.
    Bench vibrato;
    vibrato.params.lfoRate = 2.0f;
    vibrato.params.lfoPitch = 12.0f; // an octave each way
    vibrato.noteOn (69, 1.0f, 0);
    vibrato.run (0.5);
    const double high = crossings (vibrato.left, vibrato.at (0.10), vibrato.at (0.15)) / 0.05;
    const double low = crossings (vibrato.left, vibrato.at (0.35), vibrato.at (0.40)) / 0.05;
    CHECK (high > 700.0 && high < 900.0);
    CHECK (low > 200.0 && low < 280.0);

    Bench tremolo;
    tremolo.params.lfoRate = 2.0f;
    tremolo.params.lfoAmp = 1.0f;
    tremolo.noteOn (69, 1.0f, 0);
    tremolo.run (0.5);
    CHECK (rms (tremolo.left, tremolo.at (0.365), tremolo.at (0.385)) < 0.05 * rms (tremolo.left, tremolo.at (0.115), tremolo.at (0.135)));

    Bench sweep;
    sweep.params.waveform = Waveform::saw;
    sweep.params.cutoffHz = 400.0f;
    sweep.params.lfoRate = 2.0f;
    sweep.params.lfoCutoff = 3.0f;
    sweep.noteOn (45, 1.0f, 0);
    sweep.run (0.5);
    CHECK (brightness (sweep.left, sweep.at (0.10), sweep.at (0.15)) > 20.0 * brightness (sweep.left, sweep.at (0.35), sweep.at (0.40)));

    // A square LFO on the level does not click: the level is ramped between its two values.
    Bench square;
    square.params.lfoShape = LfoShape::square;
    square.params.lfoRate = 8.0f;
    square.params.lfoAmp = 1.0f;
    square.noteOn (45, 1.0f, 0); // 110 Hz: moves less than 0.005 per sample
    square.run (0.5);
    float step = 0.0f;
    for (std::size_t i = 1; i < square.left.size(); ++i)
        step = std::max (step, std::abs (square.left[i] - square.left[i - 1]));
    CHECK (step < 0.02f);

    // The random shape holds a new value each cycle, and is the same on every run.
    Bench randomA, randomB;
    for (auto* r : { &randomA, &randomB })
    {
        r->params.lfoShape = LfoShape::random;
        r->params.lfoRate = 10.0f;
        r->params.lfoPitch = 7.0f;
        r->noteOn (69, 1.0f, 0);
        r->run (1.0);
    }
    CHECK (randomA.left == randomB.left);
    std::set<int> pitches;
    for (int k = 1; k < 10; ++k)
        pitches.insert (crossings (randomA.left, randomA.at (0.1 * k + 0.01), randomA.at (0.1 * k + 0.09)) / 4);
    CHECK (pitches.size() >= 4);
}

TEST (the_filter_envelope_opens_and_closes_the_filter)
{
    auto run = [] (float amount)
    {
        Bench b;
        b.params.waveform = Waveform::saw;
        b.params.cutoffHz = 600.0f;
        b.params.filterEnvAmount = amount;
        b.params.filterAttack = 0.001f;
        b.params.filterDecay = 0.5f;
        b.params.filterSustain = 0.0f;
        b.noteOn (45, 1.0f, 0);
        b.run (0.5);
        return std::pair { brightness (b.left, b.at (0.005), b.at (0.025)), brightness (b.left, b.at (0.4), b.at (0.42)) };
    };

    const auto [flatStart, flatLater] = run (0.0f);
    CHECK (flatStart < 3.0 * flatLater && flatLater < 3.0 * flatStart); // no envelope: steady

    const auto [openStart, openLater] = run (4.0f);
    CHECK (openStart > 4.0 * openLater);               // bright attack, then back to the knob
    CHECK_NEAR (openLater / flatLater, 1.0, 0.25);

    const auto [closedStart, closedLater] = run (-3.0f);
    CHECK (closedStart < 0.2 * closedLater);           // negative amount: dull attack
}

TEST (stereo_synth_sounds_reach_the_output_in_stereo)
{
    Project p;
    p.tempo = 120.0;
    auto& c = p.addChannel (InstrumentKind::synth);
    c.synth.unison = 5;
    c.synth.unisonWidth = 1.0f;
    p.addPattern().notes[c.id].push_back (Note { 0, 8 * kTicksPerStep, 57, 1.0f });

    EngineController controller;
    controller.prepare (kRate, 512);
    controller.sync (p, 0);
    CHECK (controller.engine().play());
    const auto out = th::render (controller.engine(), 24000);
    CHECK (out.allFinite());
    CHECK (out.peak() > 0.05f);
    CHECK (out.left != out.right);
}

TEST (every_factory_preset_makes_sound_and_survives_a_round_trip)
{
    const auto& presets = synthFactoryPresets();
    CHECK (presets.size() >= 15);
    CHECK (presets.front().name == "Init");
    CHECK (presets.front().params == SynthParams {});

    std::set<std::string> names;
    for (const auto& preset : presets)
    {
        CHECK (names.insert (preset.name).second); // no two with the same name
        CHECK (! preset.category.empty());

        Bench b;
        b.params = preset.params;
        for (int key : { 48, 55, 60 }) // a chord, half a second long
            b.noteOn (key, 0.8f, 24000);
        b.run (1.0);
        const th::Stereo audio { b.left, b.right };
        if (! audio.allFinite() || audio.peak() < 0.02f || audio.peak() > 1.5f)
            tt::reportFailure (__FILE__, __LINE__, preset.name + ": peak " + std::to_string (audio.peak()));

        SynthParams loaded;
        std::string name;
        CHECK (ProjectSerializer::synthPresetFromJson (ProjectSerializer::synthPresetToJson (preset.name, preset.params), loaded, &name).empty());
        CHECK (loaded == preset.params);
        CHECK (name == preset.name);

        // And inside a project file.
        Project project;
        project.addChannel (InstrumentKind::synth).synth = preset.params;
        project.addPattern();
        Project reopened;
        CHECK (ProjectSerializer::fromJson (ProjectSerializer::toJson (project), reopened).empty());
        CHECK (reopened == project);
    }

    SynthParams untouched;
    untouched.cutoffHz = 123.0f;
    CHECK (! ProjectSerializer::synthPresetFromJson ("{\"format\":\"tactus-project\"}", untouched).empty());
    CHECK (! ProjectSerializer::synthPresetFromJson ("not json", untouched).empty());
    CHECK_NEAR (untouched.cutoffHz, 123.0, 1.0e-6);
}

TEST (projects_from_before_the_synth_upgrade_sound_as_they_did)
{
    // A version 4 synth knows nothing of the new settings: they must all load as "off".
    Project p;
    CHECK (ProjectSerializer::fromJson (
        "{\"format\":\"tactus-project\",\"version\":4,\"patterns\":[{\"id\":2}],\"channels\":[{\"id\":1,\"name\":\"Old\","
        "\"kind\":\"synth\",\"synth\":{\"waveform\":1,\"cutoffHz\":900.0,\"resonance\":0.35,"
        "\"attack\":0.01,\"decay\":0.2,\"sustain\":0.5,\"release\":0.3}}]}", p).empty());

    SynthParams expected;
    expected.waveform = Waveform::square;
    expected.cutoffHz = 900.0f;
    expected.resonance = 0.35f;
    expected.attack = 0.01f;
    expected.decay = 0.2f;
    expected.sustain = 0.5f;
    expected.release = 0.3f;
    CHECK (p.channels[0].synth == expected);
    CHECK_NEAR (expected.osc2Level + expected.subLevel + expected.noiseLevel + expected.filterEnvAmount
                    + expected.lfoPitch + expected.lfoCutoff + expected.lfoAmp + expected.glide, 0.0, 1.0e-9);
    CHECK_EQ (expected.unison, 1);
    CHECK (! expected.mono);
}

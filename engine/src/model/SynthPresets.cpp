#include "tactus/model/SynthPresets.h"

namespace tactus
{

namespace
{
    using W = Waveform;

    SynthPreset make (const char* category, const char* name, void (*shape) (SynthParams&))
    {
        SynthPreset preset { category, name, {} };
        shape (preset.params);
        return preset;
    }

    void envelope (SynthParams& p, float a, float d, float s, float r)
    {
        p.attack = a; p.decay = d; p.sustain = s; p.release = r;
    }

    void filterEnvelope (SynthParams& p, float amount, float a, float d, float s, float r)
    {
        p.filterEnvAmount = amount; p.filterAttack = a; p.filterDecay = d; p.filterSustain = s; p.filterRelease = r;
    }

    std::vector<SynthPreset> build()
    {
        return {
            make ("Basic", "Init", [] (SynthParams&) {}),

            make ("Bass", "Sub Bass", [] (SynthParams& p)
            {
                p.waveform = W::sine; p.subLevel = 0.6f; p.cutoffHz = 700.0f; p.resonance = 0.1f;
                envelope (p, 0.003f, 0.2f, 0.9f, 0.08f);
            }),
            make ("Bass", "Pluck Bass", [] (SynthParams& p)
            {
                p.waveform = W::saw; p.subLevel = 0.5f; p.cutoffHz = 180.0f; p.resonance = 0.3f;
                filterEnvelope (p, 4.0f, 0.001f, 0.16f, 0.0f, 0.1f);
                envelope (p, 0.002f, 0.3f, 0.5f, 0.1f);
            }),
            make ("Bass", "Reese Bass", [] (SynthParams& p)
            {
                p.waveform = W::saw; p.waveform2 = W::saw; p.osc2Level = 1.0f; p.osc2Detune = 22.0f;
                p.subLevel = 0.4f; p.cutoffHz = 600.0f; p.resonance = 0.2f;
                envelope (p, 0.005f, 0.2f, 0.9f, 0.12f);
            }),
            make ("Bass", "Acid Bass", [] (SynthParams& p)
            {
                p.waveform = W::saw; p.cutoffHz = 260.0f; p.resonance = 0.85f; p.mono = true; p.glide = 0.06f;
                filterEnvelope (p, 3.5f, 0.001f, 0.2f, 0.1f, 0.1f);
                envelope (p, 0.002f, 0.25f, 0.7f, 0.06f);
            }),
            make ("Bass", "Wobble Bass", [] (SynthParams& p)
            {
                p.waveform = W::square; p.waveform2 = W::saw; p.osc2Level = 0.7f; p.osc2Detune = 9.0f;
                p.subLevel = 0.5f; p.cutoffHz = 420.0f; p.resonance = 0.55f; p.mono = true;
                p.lfoShape = LfoShape::sine; p.lfoRate = 4.13f; p.lfoCutoff = 2.2f;
                envelope (p, 0.004f, 0.2f, 1.0f, 0.1f);
            }),

            make ("Lead", "Super Saw", [] (SynthParams& p)
            {
                p.waveform = W::saw; p.unison = 7; p.unisonDetune = 28.0f; p.unisonWidth = 0.9f;
                p.cutoffHz = 6500.0f; p.resonance = 0.15f;
                envelope (p, 0.006f, 0.3f, 0.8f, 0.25f);
            }),
            make ("Lead", "Square Lead", [] (SynthParams& p)
            {
                p.waveform = W::square; p.waveform2 = W::square; p.osc2Level = 0.6f; p.osc2Semitones = 12.0f; p.osc2Detune = 6.0f;
                p.cutoffHz = 3200.0f; p.resonance = 0.3f; p.lfoRate = 5.6f; p.lfoPitch = 0.12f;
                envelope (p, 0.004f, 0.2f, 0.75f, 0.15f);
            }),
            make ("Lead", "Gliding Lead", [] (SynthParams& p)
            {
                p.waveform = W::saw; p.waveform2 = W::square; p.osc2Level = 0.5f; p.osc2Detune = -7.0f;
                p.mono = true; p.glide = 0.09f; p.cutoffHz = 2600.0f; p.resonance = 0.4f;
                filterEnvelope (p, 1.5f, 0.002f, 0.3f, 0.3f, 0.2f);
                p.lfoRate = 5.2f; p.lfoPitch = 0.15f;
                envelope (p, 0.004f, 0.2f, 0.85f, 0.18f);
            }),
            make ("Lead", "Fifth Stab", [] (SynthParams& p)
            {
                p.waveform = W::saw; p.waveform2 = W::saw; p.osc2Level = 0.8f; p.osc2Semitones = 7.0f;
                p.unison = 3; p.unisonDetune = 14.0f; p.cutoffHz = 900.0f; p.resonance = 0.25f;
                filterEnvelope (p, 3.0f, 0.001f, 0.22f, 0.0f, 0.2f);
                envelope (p, 0.002f, 0.35f, 0.0f, 0.2f);
            }),

            make ("Pad", "Warm Pad", [] (SynthParams& p)
            {
                p.waveform = W::saw; p.waveform2 = W::saw; p.osc2Level = 0.9f; p.osc2Detune = 11.0f;
                p.unison = 3; p.unisonDetune = 12.0f; p.unisonWidth = 0.8f; p.cutoffHz = 1100.0f; p.resonance = 0.15f;
                filterEnvelope (p, 1.2f, 0.8f, 1.5f, 0.6f, 1.2f);
                envelope (p, 0.6f, 0.8f, 0.85f, 1.4f);
            }),
            make ("Pad", "String Ensemble", [] (SynthParams& p)
            {
                p.waveform = W::saw; p.unison = 5; p.unisonDetune = 18.0f; p.unisonWidth = 1.0f;
                p.cutoffHz = 3000.0f; p.resonance = 0.1f; p.lfoRate = 5.0f; p.lfoPitch = 0.08f;
                envelope (p, 0.25f, 0.4f, 0.9f, 0.7f);
            }),
            make ("Pad", "Glass Pad", [] (SynthParams& p)
            {
                p.waveform = W::triangle; p.waveform2 = W::sine; p.osc2Level = 0.6f; p.osc2Semitones = 12.0f; p.osc2Detune = 5.0f;
                p.unison = 3; p.unisonDetune = 9.0f; p.unisonWidth = 1.0f; p.cutoffHz = 5000.0f;
                p.lfoShape = LfoShape::triangle; p.lfoRate = 0.3f; p.lfoCutoff = 0.8f;
                envelope (p, 0.9f, 1.0f, 0.8f, 2.0f);
            }),

            make ("Keys", "Pluck", [] (SynthParams& p)
            {
                p.waveform = W::saw; p.waveform2 = W::square; p.osc2Level = 0.4f; p.osc2Semitones = 12.0f;
                p.cutoffHz = 500.0f; p.resonance = 0.3f;
                filterEnvelope (p, 4.0f, 0.001f, 0.18f, 0.0f, 0.18f);
                envelope (p, 0.001f, 0.4f, 0.0f, 0.3f);
            }),
            make ("Keys", "Electric Piano", [] (SynthParams& p)
            {
                p.waveform = W::sine; p.waveform2 = W::triangle; p.osc2Level = 0.35f; p.osc2Semitones = 12.0f;
                p.cutoffHz = 2200.0f; p.resonance = 0.05f; p.lfoRate = 4.5f; p.lfoAmp = 0.25f;
                filterEnvelope (p, 1.5f, 0.001f, 0.4f, 0.0f, 0.3f);
                envelope (p, 0.002f, 1.2f, 0.25f, 0.35f);
            }),
            make ("Keys", "Organ", [] (SynthParams& p)
            {
                p.waveform = W::sine; p.waveform2 = W::sine; p.osc2Level = 0.7f; p.osc2Semitones = 12.0f;
                p.subLevel = 0.7f; p.cutoffHz = 8000.0f; p.resonance = 0.0f; p.lfoRate = 6.5f; p.lfoPitch = 0.06f;
                envelope (p, 0.004f, 0.05f, 1.0f, 0.05f);
            }),
            make ("Keys", "Bell", [] (SynthParams& p)
            {
                p.waveform = W::sine; p.waveform2 = W::sine; p.osc2Level = 0.5f; p.osc2Semitones = 19.0f; p.osc2Detune = 4.0f;
                p.cutoffHz = 9000.0f; p.resonance = 0.0f;
                envelope (p, 0.001f, 1.6f, 0.0f, 1.2f);
            }),

            make ("FX", "Noise Sweep", [] (SynthParams& p)
            {
                p.osc1Level = 0.0f; p.noiseLevel = 1.0f; p.cutoffHz = 300.0f; p.resonance = 0.6f;
                filterEnvelope (p, 5.0f, 1.5f, 1.5f, 0.0f, 1.0f);
                envelope (p, 0.4f, 0.5f, 1.0f, 0.8f);
            }),
            make ("FX", "Laser", [] (SynthParams& p)
            {
                p.waveform = W::square; p.cutoffHz = 9000.0f; p.resonance = 0.5f;
                p.lfoShape = LfoShape::saw; p.lfoRate = 3.0f; p.lfoPitch = 12.0f;
                envelope (p, 0.001f, 0.3f, 0.0f, 0.2f);
            }),
            make ("FX", "Random Blips", [] (SynthParams& p)
            {
                p.waveform = W::triangle; p.cutoffHz = 1500.0f; p.resonance = 0.75f;
                p.lfoShape = LfoShape::random; p.lfoRate = 9.0f; p.lfoCutoff = 3.0f;
                envelope (p, 0.002f, 0.2f, 0.8f, 0.2f);
            }),
        };
    }
}

const std::vector<SynthPreset>& synthFactoryPresets()
{
    static const std::vector<SynthPreset> presets = build();
    return presets;
}

} // namespace tactus

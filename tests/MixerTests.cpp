#include "TestFramework.h"
#include "TestHelpers.h"

#include "tactus/model/ProjectSerializer.h"

using namespace tactus;

namespace
{
    constexpr double kRate = 48000.0;

    /** One sine-synth channel holding a note for the whole (2 s) pattern, plus a master. */
    struct MixFixture
    {
        Project project;
        Id channel = 0;

        MixFixture()
        {
            project.tempo = 120.0;
            auto& c = project.addChannel (InstrumentKind::synth);
            c.synth.waveform = Waveform::sine;
            c.synth.cutoffHz = 18000.0f;
            c.synth.sustain = 1.0f;
            channel = c.id;
            project.addPattern().notes[channel].push_back (Note { 0, 16 * kTicksPerStep, 57, 1.0f });
            project.ensureMaster();
        }

        Channel& ch() { return *project.findChannel (channel); }

        th::Stereo render (int samples = 24000, EngineController* keep = nullptr)
        {
            EngineController local;
            auto& controller = keep != nullptr ? *keep : local;
            controller.prepare (kRate, 512);
            controller.sync (project, 0);
            if (! controller.engine().play())
                return {};
            return th::render (controller.engine(), samples, 480);
        }
    };

    float maxDiff (const std::vector<float>& a, const std::vector<float>& b, std::size_t from)
    {
        float worst = 0.0f;
        for (std::size_t i = from; i < std::min (a.size(), b.size()); ++i)
            worst = std::max (worst, std::abs (a[i] - b[i]));
        return worst;
    }
}

TEST (routing_rules_only_allow_routes_to_the_right)
{
    Project p;
    const auto master = p.ensureMaster().id;
    const auto a = p.addInsert ("A").id;
    const auto b = p.addInsert ("B").id;
    CHECK (p.canRoute (a, 0));
    CHECK (p.canRoute (a, master));
    CHECK (p.canRoute (a, b));
    CHECK (! p.canRoute (b, a));      // leftwards: could form a loop
    CHECK (! p.canRoute (a, a));
    CHECK (! p.canRoute (master, a)); // the master feeds the sound card only
    CHECK (! p.canRoute (4242, 0));
    CHECK_EQ (p.insertIndex (master), 0);
    CHECK_EQ (p.insertIndex (b), 2);
}

TEST (an_insert_fader_and_balance_shape_the_channel)
{
    MixFixture f;
    auto& insert = f.project.addInsert ("Lead");
    f.ch().mixerInsert = insert.id;
    const auto insertId = insert.id;

    const auto unity = f.render();
    CHECK (unity.peak (4800) > 0.05f);

    f.project.findInsert (insertId)->volume = 0.5f;
    const auto half = f.render();
    float worst = 0.0f;
    for (std::size_t i = 4800; i < unity.left.size(); ++i)
        worst = std::max (worst, std::abs (half.left[i] - 0.5f * unity.left[i]));
    CHECK (worst < 1.0e-6f);

    f.project.findInsert (insertId)->volume = 1.0f;
    f.project.findInsert (insertId)->pan = -1.0f;
    const auto left = f.render();
    float rightPeak = 0.0f;
    for (float s : left.right)
        rightPeak = std::max (rightPeak, std::abs (s));
    CHECK (rightPeak < 1.0e-7f);
    CHECK (maxDiff (left.left, unity.left, 4800) < 1.0e-6f); // balance: the favoured side stays at unity
}

TEST (outputs_and_sends_reach_their_buses)
{
    MixFixture f;
    const auto a = f.project.addInsert ("Source").id;
    const auto bus = f.project.addInsert ("Bus").id;
    f.ch().mixerInsert = a;
    const auto direct = f.render();

    // Routed through the bus: the bus fader now controls it.
    f.project.findInsert (a)->output = bus;
    CHECK (maxDiff (f.render().left, direct.left, 4800) < 1.0e-6f);
    f.project.findInsert (bus)->volume = 0.0f;
    CHECK (f.render().peak (4800) < 1.0e-9f);

    // Straight to the master plus a half-level send to the bus: 1.5 times the signal.
    f.project.findInsert (a)->output = 0;
    f.project.findInsert (bus)->volume = 1.0f;
    f.project.findInsert (a)->sends.push_back ({ bus, 0.5f });
    const auto withSend = f.render();
    float worst = 0.0f;
    for (std::size_t i = 4800; i < direct.left.size(); ++i)
        worst = std::max (worst, std::abs (withSend.left[i] - 1.5f * direct.left[i]));
    CHECK (worst < 1.0e-5f);

    // Sends are post-fader.
    f.project.findInsert (a)->volume = 0.0f;
    CHECK (f.render().peak (4800) < 1.0e-9f);
}

TEST (mute_and_solo_follow_the_signal_path)
{
    MixFixture f;
    const auto a = f.project.addInsert ("A").id;
    const auto other = f.project.addInsert ("Other").id;
    const auto bus = f.project.addInsert ("Bus").id;
    f.ch().mixerInsert = a;
    f.project.findInsert (a)->sends.push_back ({ bus, 1.0f });

    f.project.findInsert (a)->muted = true;
    CHECK (f.render().peak() < 1.0e-9f);
    f.project.findInsert (a)->muted = false;

    f.project.findInsert (other)->solo = true; // something else soloed
    CHECK (f.render().peak() < 1.0e-9f);

    f.project.findInsert (other)->solo = false;
    f.project.findInsert (bus)->solo = true;   // A feeds the soloed bus, so it stays audible
    CHECK (f.render().peak (4800) > 0.05f);

    // A channel routed straight to the master is silenced while anything is soloed.
    f.ch().mixerInsert = 0;
    CHECK (f.render().peak() < 1.0e-9f);
    f.project.findInsert (bus)->solo = false;
    CHECK (f.render().peak (4800) > 0.05f);
}

TEST (effect_order_matters_and_bypass_is_transparent)
{
    MixFixture f;
    const auto a = f.project.addInsert ("FX").id;
    f.ch().mixerInsert = a;
    const auto clean = f.render();

    auto* distortion = f.project.addEffect (a, EffectType::distortion);
    distortion->params[0] = 30.0f; // drive
    distortion->params[1] = 1.0f;  // hard
    distortion->params[2] = 20000.0f;
    auto* filter = f.project.addEffect (a, EffectType::filter);
    filter->params[1] = 300.0f;    // low-pass at 300 Hz
    const auto distortThenFilter = f.render();

    auto& chain = f.project.findInsert (a)->effects;
    std::swap (chain[0], chain[1]);
    const auto filterThenDistort = f.render();
    CHECK (maxDiff (distortThenFilter.left, filterThenDistort.left, 4800) > 0.05f);

    for (auto& e : chain)
        e.bypassed = true;
    const auto bypassed = f.render();
    CHECK (maxDiff (bypassed.left, clean.left, 0) < 1.0e-6f); // bypassed from the start: never heard
}

TEST (switching_an_effect_while_playing_cross_fades_without_clicks)
{
    MixFixture f;
    f.ch().synth.waveform = Waveform::sine;
    const auto a = f.project.addInsert ("FX").id;
    f.ch().mixerInsert = a;
    auto* gain = f.project.addEffect (a, EffectType::stereoImager);
    gain->params[1] = -12.0f; // a pure level change, easy to check
    gain->bypassed = true;

    EngineController controller;
    controller.prepare (kRate, 512);
    controller.sync (f.project, 0);
    CHECK (controller.engine().play());
    th::render (controller.engine(), 9600, 480);

    f.project.findInsert (a)->effects[0].bypassed = false;
    controller.sync (f.project, 0);
    const auto out = th::render (controller.engine(), 9600, 480);
    float largestStep = 0.0f;
    for (std::size_t i = 1; i < out.left.size(); ++i)
        largestStep = std::max (largestStep, std::abs (out.left[i] - out.left[i - 1]));
    CHECK (largestStep < 0.02f);                     // a 220 Hz sine at this level moves < 0.02 per sample
    CHECK (out.peak (4800) < 0.3f * out.peak (0, 100) + 0.05f); // and it did turn down
}

TEST (insert_meters_report_their_own_level)
{
    MixFixture f;
    const auto a = f.project.addInsert ("A").id;
    f.project.addInsert ("Unused");
    f.ch().mixerInsert = a;
    EngineController controller;
    f.render (9600, &controller);
    auto& engine = controller.engine();
    CHECK (engine.takeInsertPeak (1, 0) > 0.05f);  // A
    CHECK (engine.takeInsertPeak (2, 0) == 0.0f);  // Unused
    CHECK (engine.takeInsertPeak (0, 1) > 0.05f);  // master
    CHECK (engine.takeInsertPeak (1, 0) == 0.0f);  // reading resets
    CHECK (engine.takeInsertPeak (999, 0) == 0.0f);
}

TEST (default_project_master_limiter_prevents_clipping)
{
    auto p = Project::createDefault();
    for (auto& c : p.channels)
        c.volume = 1.0f;
    for (std::size_t i = 1; i < p.mixer.size(); ++i)
        p.mixer[i].volume = MixerInsert::kMaxVolume; // every insert at +6 dB; effects are pre-fader, so the master stays at 0 dB

    EngineController controller;
    controller.prepare (kRate, 512);
    controller.sync (p, 0);
    th::render (controller.engine(), 960, 480); // a new effect fades in over 10 ms; let it settle first
    controller.engine().takeClipped();
    CHECK (controller.engine().play());
    const auto out = th::render (controller.engine(), 96000, 480);
    CHECK (out.allFinite());
    // Ceiling -0.3 dB, then the engine's fixed 0.9 output trim.
    CHECK (out.peak() <= std::pow (10.0f, -0.3f / 20.0f) * 0.9f * 1.0001f);
    CHECK (out.peak() > 0.8f); // loud, as asked
    CHECK (! controller.engine().takeClipped());
}

TEST (removing_an_insert_reroutes_everything_to_the_master)
{
    MixFixture f;
    const auto a = f.project.addInsert ("A").id;
    const auto bus = f.project.addInsert ("Bus").id;
    f.ch().mixerInsert = bus;
    f.project.findInsert (a)->output = bus;
    f.project.findInsert (a)->sends.push_back ({ bus, 0.3f });
    f.project.addTrack().mixerInsert = bus;

    CHECK (f.project.removeInsert (bus));
    CHECK_EQ (f.ch().mixerInsert, (Id) 0);
    CHECK_EQ (f.project.findInsert (a)->output, (Id) 0);
    CHECK (f.project.findInsert (a)->sends.empty());
    CHECK_EQ (f.project.arrangement.tracks[0].mixerInsert, (Id) 0);
    CHECK (! f.project.removeInsert (f.project.mixer[0].id)); // never the master
}

TEST (mixer_survives_save_and_load)
{
    auto p = Project::createDefault();
    const auto bus = p.addInsert ("Reverb bus").id;
    auto* reverb = p.addEffect (bus, EffectType::reverb);
    reverb->params[0] = 0.9f;
    reverb->bypassed = true;
    p.addEffect (bus, EffectType::eq);
    p.mixer[1].sends.push_back ({ bus, 0.25f });
    p.mixer[1].pan = -0.4f;
    p.mixer[2].solo = true;
    p.mixer[3].output = bus;
    p.arrangement.tracks[2].mixerInsert = bus;

    Project loaded;
    const auto error = ProjectSerializer::fromJson (ProjectSerializer::toJson (p), loaded);
    CHECK (error.empty());
    if (! error.empty())
        std::printf ("    %s\n", error.c_str());
    CHECK (loaded == p);
}

TEST (broken_mixers_are_rejected_with_a_reason)
{
    auto p = Project::createDefault();
    auto json = ProjectSerializer::toJson (p);
    Project out;

    // An insert routed to its left.
    auto leftwards = p;
    leftwards.mixer[3].output = leftwards.mixer[2].id;
    auto error = ProjectSerializer::fromJson (ProjectSerializer::toJson (leftwards), out);
    CHECK (error.find ("not to its right") != std::string::npos);

    // An unknown effect type or parameter.
    auto withEq = p;
    withEq.addEffect (withEq.mixer[1].id, EffectType::eq);
    json = ProjectSerializer::toJson (withEq);
    auto unknownType = json;
    unknownType.replace (unknownType.find ("\"eq\""), 4, "\"wah\"");
    CHECK (ProjectSerializer::fromJson (unknownType, out).find ("wah") != std::string::npos);
    auto unknownParam = json;
    unknownParam.replace (unknownParam.find ("\"lowGain\""), 9, "\"wobble\"");
    CHECK (ProjectSerializer::fromJson (unknownParam, out).find ("wobble") != std::string::npos);

    // A channel playing into an insert that is not there.
    auto dangling = p;
    dangling.channels[0].mixerInsert = 99999;
    CHECK (ProjectSerializer::fromJson (ProjectSerializer::toJson (dangling), out).find ("does not exist") != std::string::npos);
}

TEST (projects_from_before_the_mixer_still_open)
{
    Project old;
    const auto error = ProjectSerializer::fromJson (
        "{\"format\":\"tactus-project\",\"version\":2,\"nextId\":3,"
        "\"channels\":[{\"id\":1,\"name\":\"Kick\",\"kind\":\"kick\"}],"
        "\"patterns\":[{\"id\":2,\"name\":\"P\",\"tracks\":[{\"channel\":1,\"notes\":[[0,240,60,0.8]]}]}]}", old);
    CHECK (error.empty());
    CHECK (old.mixer.empty());             // left as it was; the application adds the master
    CHECK_EQ (old.channels[0].mixerInsert, (Id) 0);
    const auto master = old.ensureMaster().id;
    CHECK (master > 2);                    // a fresh id, not one already in use
}

TEST (effect_chain_presets_round_trip_with_fresh_ids)
{
    auto p = Project::createDefault();
    const auto a = p.mixer[1].id;
    auto* comp = p.addEffect (a, EffectType::compressor);
    comp->params[0] = -25.0f;
    p.addEffect (a, EffectType::delay)->bypassed = true;
    const auto chain = p.findInsert (a)->effects;

    const auto json = ProjectSerializer::effectChainToJson (chain);
    std::vector<EffectSlot> loaded;
    CHECK (ProjectSerializer::effectChainFromJson (json, p, loaded).empty());
    CHECK_EQ ((int) loaded.size(), 2);
    CHECK (loaded[0].type == EffectType::compressor);
    CHECK_NEAR (loaded[0].params[0], -25.0, 1.0e-6);
    CHECK (loaded[1].bypassed);
    CHECK (loaded[0].id != chain[0].id && loaded[1].id != chain[1].id && loaded[0].id != loaded[1].id);

    std::vector<EffectSlot> untouched;
    CHECK (! ProjectSerializer::effectChainFromJson ("{\"format\":\"something\"}", p, untouched).empty());
    CHECK (untouched.empty());
}

TEST (editing_the_mixer_while_playing_is_safe)
{
    auto p = Project::createDefault();
    EngineController controller;
    controller.prepare (kRate, 256);
    controller.sync (p, 0);
    CHECK (controller.engine().play());

    std::uint32_t seed = 5;
    for (int step = 0; step < 600; ++step)
    {
        seed = seed * 1664525u + 1013904223u;
        auto& m = p.mixer[1 + seed % (p.mixer.size() - 1)];
        switch ((seed >> 8) % 5)
        {
            case 0: p.addEffect (m.id, (EffectType) ((seed >> 12) % 12)); break;
            case 1: if (! m.effects.empty()) m.effects.erase (m.effects.begin()); break;
            case 2: if (! m.effects.empty()) m.effects.back().bypassed = ! m.effects.back().bypassed; break;
            case 3: m.volume = (float) ((seed >> 16) % 200) / 100.0f; break;
            case 4: if (! m.effects.empty()) std::swap (m.effects.front(), m.effects.back()); break;
        }
        controller.sync (p, 0);
        const auto out = th::render (controller.engine(), 256, 256);
        if (! out.allFinite())
        {
            CHECK (out.allFinite());
            break;
        }
    }
    controller.engine().service();
    CHECK (controller.engine().getNumSnapshotsFreed() > 500);
}

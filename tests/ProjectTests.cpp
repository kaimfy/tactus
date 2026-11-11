#include "TestFramework.h"

#include "tactus/model/ProjectSerializer.h"

#include <juce_core/juce_core.h>

using namespace tactus;

namespace
{
    juce::File tempDir()
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("tactus_tests");
        dir.createDirectory();
        return dir;
    }

    Project richProject()
    {
        auto p = Project::createDefault();
        p.name = "Round trip \xC3\xA9\xC3\xA0 \"quoted\""; // non-ASCII and characters JSON must escape
        p.tempo = 133.25;
        p.timeSigNumerator = 3;
        p.channels[1].muted = true;
        p.channels[2].pan = -0.35f;
        p.channels[4].synth.waveform = Waveform::square;
        p.channels[4].synth.cutoffHz = 1234.5f;
        auto& second = p.addPattern ("Fill");
        second.lengthSteps = 32;
        second.swing = 0.4f;
        second.notes[p.channels[4].id].push_back (Note { 37, 1111, 72, 0.33f });
        return p;
    }
}

TEST (step_toggling_edits_notes)
{
    Pattern p;
    CHECK (! p.stepActive (1, 3));
    CHECK (p.toggleStep (1, 3));
    CHECK (p.stepActive (1, 3));
    CHECK_EQ ((int) p.notes[1].size(), 1);
    CHECK_EQ (p.notes[1][0].start, (std::int64_t) 3 * kTicksPerStep);

    p.setStep (1, 3, true); // already on: no duplicate
    CHECK_EQ ((int) p.notes[1].size(), 1);

    p.setStep (1, 99, true); // outside the pattern: ignored
    CHECK_EQ ((int) p.notes[1].size(), 1);

    CHECK (! p.toggleStep (1, 3));
    CHECK (p.notes[1].empty());
}

TEST (removing_a_channel_removes_its_notes_everywhere)
{
    auto p = Project::createDefault();
    const auto kick = p.channels[0].id;
    CHECK (! p.patterns[0].notes[kick].empty());
    CHECK (p.removeChannel (kick));
    CHECK (p.findChannel (kick) == nullptr);
    CHECK (p.patterns[0].notes.count (kick) == 0);
    CHECK (! p.removeChannel (kick));
}

TEST (duplicated_pattern_is_independent)
{
    auto p = Project::createDefault();
    const auto kick = p.channels[0].id;
    const int index = p.duplicatePattern (p.patterns[0].id);
    CHECK_EQ (index, 1);
    CHECK (p.patterns[1].id != p.patterns[0].id);
    p.patterns[1].setStep (kick, 0, false);
    CHECK (p.patterns[0].stepActive (kick, 0));
    CHECK (! p.patterns[1].stepActive (kick, 0));
}

TEST (project_survives_json_round_trip)
{
    const auto original = richProject();
    Project loaded;
    const auto error = ProjectSerializer::fromJson (ProjectSerializer::toJson (original), loaded);
    CHECK (error.empty());
    if (! error.empty())
        std::printf ("    %s\n", error.c_str());
    CHECK (loaded == original);

    // New ids must not collide with loaded ones.
    const auto newId = loaded.addChannel (InstrumentKind::synth).id;
    for (std::size_t i = 0; i + 1 < loaded.channels.size(); ++i)
        CHECK (loaded.channels[i].id != newId);
}

TEST (project_survives_file_round_trip_and_overwrite)
{
    const auto file = tempDir().getChildFile ("roundtrip.tactus");
    file.deleteFile();
    const auto path = file.getFullPathName().toStdString();

    auto project = richProject();
    CHECK (ProjectSerializer::saveToFile (project, path).empty());

    project.tempo = 90.0; // save again over the existing file
    CHECK (ProjectSerializer::saveToFile (project, path).empty());

    Project loaded;
    CHECK (ProjectSerializer::loadFromFile (path, loaded).empty());
    CHECK (loaded == project);

    // No temporary files are left behind next to the project.
    CHECK_EQ (tempDir().getNumberOfChildFiles (juce::File::findFiles, "roundtrip*"), 1);
    file.deleteFile();
}

TEST (bad_project_files_are_rejected_with_a_reason)
{
    Project untouched;
    untouched.name = "keep me";
    Project target = untouched;

    CHECK (! ProjectSerializer::fromJson ("this is not json {", target).empty());
    CHECK (! ProjectSerializer::fromJson ("{\"format\":\"something-else\",\"version\":1}", target).empty());
    CHECK (! ProjectSerializer::fromJson ("[1,2,3]", target).empty());

    const auto newer = ProjectSerializer::fromJson ("{\"format\":\"tactus-project\",\"version\":999}", target);
    CHECK (newer.find ("newer version") != std::string::npos);

    const auto orphan = ProjectSerializer::fromJson (
        "{\"format\":\"tactus-project\",\"version\":1,\"channels\":[],"
        "\"patterns\":[{\"id\":2,\"name\":\"P\",\"tracks\":[{\"channel\":77,\"notes\":[[0,240,60,0.8]]}]}]}", target);
    CHECK (orphan.find ("does not exist") != std::string::npos);

    const auto unknownKind = ProjectSerializer::fromJson (
        "{\"format\":\"tactus-project\",\"version\":1,"
        "\"channels\":[{\"id\":1,\"name\":\"X\",\"kind\":\"theremin\"}],\"patterns\":[{\"id\":2}]}", target);
    CHECK (unknownKind.find ("theremin") != std::string::npos);

    CHECK (! ProjectSerializer::loadFromFile (tempDir().getChildFile ("missing.tactus").getFullPathName().toStdString(), target).empty());

    CHECK (target == untouched); // a failed load never half-fills the project
}

TEST (out_of_range_values_in_a_file_are_clamped)
{
    Project p;
    const auto error = ProjectSerializer::fromJson (
        "{\"format\":\"tactus-project\",\"version\":1,\"tempo\":99999,"
        "\"channels\":[{\"id\":1,\"name\":\"X\",\"kind\":\"synth\",\"volume\":50,\"pan\":-9,"
        "\"synth\":{\"waveform\":42,\"cutoffHz\":-5}}],"
        "\"patterns\":[{\"id\":2,\"name\":\"P\",\"lengthSteps\":0,"
        "\"tracks\":[{\"channel\":1,\"notes\":[[-10,0,400,7]]}]}]}", p);
    CHECK (error.empty());
    CHECK_NEAR (p.tempo, kMaxTempo, 1.0e-9);
    CHECK_NEAR (p.channels[0].volume, 1.0, 1.0e-9);
    CHECK_NEAR (p.channels[0].pan, -1.0, 1.0e-9);
    CHECK_EQ ((int) p.channels[0].synth.waveform, 3);
    CHECK_NEAR (p.channels[0].synth.cutoffHz, 40.0, 1.0e-6);
    CHECK_EQ (p.patterns[0].lengthSteps, 1);
    const auto& n = p.patterns[0].notes[1][0];
    CHECK_EQ (n.start, (std::int64_t) 0);
    CHECK_EQ (n.length, (std::int64_t) 1);
    CHECK_EQ (n.key, 127);
    CHECK_NEAR (n.velocity, 1.0, 1.0e-9);
    CHECK (p.nextId > 2);
}

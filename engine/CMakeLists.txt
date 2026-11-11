# tactus_engine: everything that makes sound or stores music.
# The UI depends on the engine, never the other way round: no file in here may
# include a juce_gui_* / juce_graphics header (checked in the root CMakeLists).
add_library(tactus_engine STATIC
    src/model/Project.cpp
    src/model/EffectInfo.cpp
    src/model/SynthPresets.cpp
    src/model/ProjectSerializer.cpp
    src/model/UndoHistory.cpp
    src/model/Editing.cpp
    src/model/ProjectFiles.cpp
    src/engine/AudioPool.cpp
    src/engine/Recorder.cpp
    src/engine/SampleSlicer.cpp
    src/dsp/PolySynth.cpp
    src/dsp/DrumSynth.cpp
    src/dsp/Sampler.cpp
    src/dsp/Effects.cpp
    src/engine/Snapshot.cpp
    src/engine/AudioEngine.cpp
    src/engine/EngineController.cpp
    src/engine/DeviceHost.cpp)

target_include_directories(tactus_engine PUBLIC include)
target_link_libraries(tactus_engine PUBLIC tactus_juce PRIVATE tactus_warnings)

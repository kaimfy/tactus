# Headless tests: they link the engine only, which proves it runs without the UI.
add_executable(tactus_tests
    TestMain.cpp
    SequencerTests.cpp
    ProjectTests.cpp
    EngineTests.cpp
    ArrangementTests.cpp
    EditingTests.cpp
    AudioClipTests.cpp
    RecorderTests.cpp
    ProjectFilesTests.cpp
    EffectTests.cpp
    MixerTests.cpp
    SamplerTests.cpp
    SynthTests.cpp)

target_link_libraries(tactus_tests PRIVATE tactus_engine tactus_warnings)

add_test(NAME engine_tests COMMAND tactus_tests)

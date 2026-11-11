juce_add_gui_app(Tactus
    PRODUCT_NAME "Tactus"
    COMPANY_NAME "Tactus"
    VERSION ${PROJECT_VERSION})

target_sources(Tactus PRIVATE
    src/Main.cpp
    src/Session.cpp
    src/Theme.cpp
    src/MainComponent.cpp
    src/TransportBar.cpp
    src/ChannelRack.cpp
    src/PianoRoll.cpp
    src/ArrangementView.cpp
    src/MixerView.cpp
    src/InstrumentPanel.cpp)

target_compile_definitions(Tactus PRIVATE
    TACTUS_VERSION_STRING="${PROJECT_VERSION}")

target_link_libraries(Tactus PRIVATE tactus_engine tactus_juce tactus_warnings)

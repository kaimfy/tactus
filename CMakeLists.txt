cmake_minimum_required(VERSION 3.24)

project(Tactus VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Static MSVC runtime so the built executable runs without a redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# JUCE: use the local checkout made by `dev.ps1 bootstrap` if present,
# otherwise download the pinned tag.
set(TACTUS_JUCE_TAG 9.0.5)
if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/JUCE/CMakeLists.txt")
    add_subdirectory(third_party/JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG ${TACTUS_JUCE_TAG}
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)
endif()

# All JUCE modules are compiled exactly once, into this static library.
# (JUCE modules are source-only INTERFACE targets; linking them into more than
# one of our targets would compile and define them twice.)
add_library(tactus_juce STATIC)
target_link_libraries(tactus_juce
    PRIVATE
        juce::juce_core
        juce::juce_events
        juce::juce_data_structures
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags)
target_compile_definitions(tactus_juce
    PUBLIC
        JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
        JUCE_USE_CURL=0
        JUCE_USE_MP3AUDIOFORMAT=1
        JUCE_WEB_BROWSER=0
        $<$<PLATFORM_ID:Windows>:JUCE_WASAPI=1>
        $<$<PLATFORM_ID:Windows>:JUCE_DIRECTSOUND=1>
        $<$<PLATFORM_ID:Linux>:JUCE_ALSA=1>
        $<$<PLATFORM_ID:Linux>:JUCE_JACK=1>
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_MODAL_LOOPS_PERMITTED=0
    INTERFACE
        $<TARGET_PROPERTY:tactus_juce,COMPILE_DEFINITIONS>)
target_include_directories(tactus_juce
    INTERFACE
        $<TARGET_PROPERTY:tactus_juce,INCLUDE_DIRECTORIES>)

# Our own code is held to a stricter warning level than the vendored framework.
add_library(tactus_warnings INTERFACE)
if(MSVC)
    target_compile_options(tactus_warnings INTERFACE /W4 /permissive- /utf-8)
else()
    target_compile_options(tactus_warnings INTERFACE -Wall -Wextra)
endif()

# Architectural rule, enforced at configure time: the engine must not see the GUI.
file(GLOB_RECURSE TACTUS_ENGINE_FILES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/engine/*.h" "${CMAKE_SOURCE_DIR}/engine/*.cpp")
foreach(engine_file IN LISTS TACTUS_ENGINE_FILES)
    file(STRINGS "${engine_file}" gui_includes REGEX "#include.*juce_(gui|graphics|audio_utils|opengl)")
    if(gui_includes)
        message(FATAL_ERROR "Engine file includes a GUI module: ${engine_file}\n${gui_includes}")
    endif()
endforeach()

add_subdirectory(engine)
add_subdirectory(app)

enable_testing()
add_subdirectory(tests)

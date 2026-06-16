cmake_minimum_required(VERSION 3.22)

project(FeedbackKiller VERSION 0.4.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE 8: use a local checkout if FK_JUCE_PATH is set, otherwise fetch it.
set(FK_JUCE_PATH "" CACHE PATH
    "Path to a local JUCE 8 checkout (leave empty to download JUCE automatically)")
set(FK_JUCE_TAG "8.0.4" CACHE STRING
    "JUCE git tag/branch to fetch when FK_JUCE_PATH is empty")

if(FK_JUCE_PATH)
    add_subdirectory("${FK_JUCE_PATH}" "${CMAKE_BINARY_DIR}/JUCE")
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        "${FK_JUCE_TAG}"
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(FeedbackKiller
    COMPANY_NAME              "Bytemixer"
    PRODUCT_NAME              "Feedback Resonance Killer"
    PLUGIN_MANUFACTURER_CODE  Btmx
    PLUGIN_CODE               Fbk1
    FORMATS                   VST3 Standalone
    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          FALSE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD   FALSE)

target_sources(FeedbackKiller PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/SpectrumAnalyzer.cpp
    Source/FeedbackKillerDSP.cpp)

target_compile_definitions(FeedbackKiller PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(FeedbackKiller
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

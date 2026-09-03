cmake_minimum_required(VERSION 3.22)

project(SampleTrap VERSION 0.4.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.9
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(SampleTrap
    COMPANY_NAME "amphiox"
    COMPANY_COPYRIGHT "Copyright (c) 2026 Jesse Manser"
    BUNDLE_ID "com.amphiox.sampletrap"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "SampleTrap needs microphone access to record samples."
    PLUGIN_MANUFACTURER_CODE Amph
    PLUGIN_CODE SmTp
    FORMATS VST3 Standalone
    PRODUCT_NAME "SampleTrap"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
)

target_sources(SampleTrap PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
)

juce_add_binary_data(SampleTrapAssets
    SOURCES Assets/sampletrap-logo.png
)

target_compile_definitions(SampleTrap PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(SampleTrap PRIVATE
    SampleTrapAssets
    juce::juce_audio_utils
    juce::juce_dsp
PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

juce_generate_juce_header(SampleTrap)

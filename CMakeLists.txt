cmake_minimum_required(VERSION 3.15)
project(AllSynth VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add JUCE from local source
add_subdirectory(/Users/shaiperelman/JUCE JUCE)

# Plugin target
juce_add_plugin(AllSynthPlugin
    PLUGIN_MANUFACTURER_CODE SPER
    PLUGIN_CODE ASYN
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "AllSynth"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    COPY_PLUGIN_AFTER_BUILD TRUE
)
juce_generate_juce_header(AllSynthPlugin)

target_sources(AllSynthPlugin PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/SynthSound.h
    Source/SynthVoice.cpp
    Source/SynthVoice.h
    Source/DelayLine.cpp
    Source/AnalogueDrive.h
)

target_compile_definitions(AllSynthPlugin
    PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(AllSynthPlugin PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_gui_basics
) 
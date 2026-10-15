cmake_minimum_required(VERSION 3.22)
project(Dereverb VERSION 0.1.0)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

set(DEREVERB_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND DEREVERB_FORMATS AU)
endif()

juce_add_plugin(Dereverb
    COMPANY_NAME "MiEstudio"
    PLUGIN_MANUFACTURER_CODE Mest
    PLUGIN_CODE Drvb
    FORMATS ${DEREVERB_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    PRODUCT_NAME "Dereverb")

# Ilustracion y mascara de efectos de la interfaz, incrustadas en el binario
juce_add_binary_data(DereverbAssets SOURCES assets/skin.jpg assets/fxmask.png)
set_target_properties(DereverbAssets PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

target_sources(Dereverb PRIVATE
    PluginProcessor.cpp PluginProcessor.h
    PluginEditor.cpp PluginEditor.h)

target_compile_definitions(Dereverb PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Dereverb
    PRIVATE
        DereverbAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

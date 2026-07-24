cmake_minimum_required(VERSION 3.22)
project(MixMind VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Fetch JUCE ────────────────────────────────────────────────────────────────
# Include JUCE from the repository root
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../JUCE" JUCE)

# ── Plugin target ─────────────────────────────────────────────────────────────
juce_add_plugin(MixMind
    VERSION                     1.0.0
    COMPANY_NAME                "YourStudio"
    PLUGIN_MANUFACTURER_CODE    MXMN
    PLUGIN_CODE                 Mxmn
    FORMATS                     VST3 AU Standalone
    PRODUCT_NAME                "MixMind"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES             Tools
    AU_MAIN_TYPE                kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD     TRUE
)

# ── Source files ──────────────────────────────────────────────────────────────
target_sources(MixMind PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/ApiClient.cpp
    Source/ApiClient.h
    Source/ChatComponent.cpp
    Source/ChatComponent.h
    Source/ContextPanel.cpp
    Source/ContextPanel.h
    Source/AudioAnalyzer.cpp
    Source/AudioAnalyzer.h
    Source/LookAndFeel.h
)

# ── JUCE modules & Definitions ────────────────────────────────────────────────
target_compile_definitions(MixMind PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
)

target_link_libraries(MixMind PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_gui_extra
    juce::juce_core
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# Explicitly set include directories for JUCE modules
target_include_directories(MixMind PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../JUCE/modules"
)

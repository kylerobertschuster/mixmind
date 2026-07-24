cmake_minimum_required(VERSION 3.22)
project(MixMind VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Company / branding (change these before shipping) ───────────────────────
set(MIXMIND_COMPANY         "MixMind Audio")
set(MIXMIND_MANUFACTURER    MXMN)
set(MIXMIND_PLUGIN_CODE     Mxmn)

# ── Fetch JUCE automatically (no manual git clone required) ─────────────────
include(FetchContent)

FetchContent_Declare(JUCE
    GIT_REPOSITORY  https://github.com/juce-framework/JUCE.git
    GIT_TAG         8.0.6
    GIT_SHALLOW     TRUE
)

FetchContent_GetProperties(JUCE)
if(NOT juce_POPULATED)
    FetchContent_Populate(JUCE)
    add_subdirectory(${juce_SOURCE_DIR} ${juce_BINARY_DIR})
endif()

# ── Plugin target ─────────────────────────────────────────────────────────
juce_add_plugin(MixMind
    VERSION                     1.0.0
    COMPANY_NAME                "${MIXMIND_COMPANY}"
    PLUGIN_MANUFACTURER_CODE    ${MIXMIND_MANUFACTURER}
    PLUGIN_CODE                 ${MIXMIND_PLUGIN_CODE}
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

# ── Source files ──────────────────────────────────────────────────────────
target_sources(MixMind PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/ApiClient.cpp
    Source/ApiClient.h
    Source/AudioAnalyzer.cpp
    Source/AudioAnalyzer.h
    Source/ChatComponent.cpp
    Source/ChatComponent.h
    Source/ContextPanel.cpp
    Source/ContextPanel.h
    Source/LookAndFeel.h
)

# ── Compile definitions ───────────────────────────────────────────────────
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

# ── Signing & notarization is handled by build_pkg.sh ─────────────────────
# Run ./build_pkg.sh after building to codesign, notarize, and package.
# Set these environment variables before running:
#   export MIXMIND_SIGN_IDENTITY="Developer ID Application: Your Name (TEAMID)"
#   export MIXMIND_NOTARIZE=true

cmake_minimum_required(VERSION 3.22)
project(MixMind VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Company / branding (change these before shipping) ───────────────────────
set(MIXMIND_COMPANY         "JuicePipe")
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
    Source/AudioAnalyzer.cpp
    Source/AudioAnalyzer.h
    Source/LoudnessMeter.cpp
    Source/LoudnessMeter.h
    Source/ShaperProcessor.cpp
    Source/ShaperProcessor.h
    Source/LookAndFeel.h
    Source/LookAndFeel.cpp
    Source/PresetManager.cpp
    Source/PresetManager.h
    Source/HostTheme.cpp
    Source/HostTheme.h
    Source/AnalyzerCanvas.cpp
    Source/AnalyzerCanvas.h
    Source/FocusModel.h
    Source/ReferenceAnalyzer.cpp
    Source/ReferenceAnalyzer.h
    Source/TelemetryCanvas.cpp
    Source/TelemetryCanvas.h
)

# ── Compile definitions ───────────────────────────────────────────────────
target_compile_definitions(MixMind PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_MP3AUDIOFORMAT=1
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

# ═══════════════════════════════════════════════════════════════════════════
#  Scope — Goniometer / Phase Scope
# ═══════════════════════════════════════════════════════════════════════════
juce_add_plugin(Scope
    VERSION                     1.0.0
    COMPANY_NAME                "${MIXMIND_COMPANY}"
    PLUGIN_MANUFACTURER_CODE    ${MIXMIND_MANUFACTURER}
    PLUGIN_CODE                 Scop
    FORMATS                     VST3 AU Standalone
    PRODUCT_NAME                "Scope"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    COPY_PLUGIN_AFTER_BUILD     TRUE
    VST3_CATEGORIES             Tools
    AU_MAIN_TYPE                kAudioUnitType_Effect
)

target_sources(Scope PRIVATE
    Source/ScopeProcessor.cpp Source/ScopeProcessor.h
    Source/ScopeEditor.cpp Source/ScopeEditor.h
    Source/AudioAnalyzer.cpp Source/AudioAnalyzer.h
    Source/LoudnessMeter.cpp Source/LoudnessMeter.h
    Source/LookAndFeel.h Source/LookAndFeel.cpp
    Source/LicenseManager.cpp Source/LicenseManager.h
    Source/HostTheme.cpp Source/HostTheme.h
)

target_compile_definitions(Scope PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
)

target_link_libraries(Scope PRIVATE
    juce::juce_audio_utils juce::juce_audio_processors
    juce::juce_gui_extra juce::juce_core juce::juce_dsp
    juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# ═══════════════════════════════════════════════════════════════════════════
#  Meter — LUFS / Dynamics Metering
# ═══════════════════════════════════════════════════════════════════════════
juce_add_plugin(Meter
    VERSION                     1.0.0
    COMPANY_NAME                "${MIXMIND_COMPANY}"
    PLUGIN_MANUFACTURER_CODE    ${MIXMIND_MANUFACTURER}
    PLUGIN_CODE                 Metr
    FORMATS                     VST3 AU Standalone
    PRODUCT_NAME                "Meter"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    COPY_PLUGIN_AFTER_BUILD     TRUE
    VST3_CATEGORIES             Tools
    AU_MAIN_TYPE                kAudioUnitType_Effect
)

target_sources(Meter PRIVATE
    Source/MeterProcessor.cpp Source/MeterProcessor.h
    Source/MeterEditor.cpp Source/MeterEditor.h
    Source/AudioAnalyzer.cpp Source/AudioAnalyzer.h
    Source/LoudnessMeter.cpp Source/LoudnessMeter.h
    Source/LookAndFeel.h Source/LookAndFeel.cpp
    Source/LicenseManager.cpp Source/LicenseManager.h
    Source/HostTheme.cpp Source/HostTheme.h
)

target_compile_definitions(Meter PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
)

target_link_libraries(Meter PRIVATE
    juce::juce_audio_utils juce::juce_audio_processors
    juce::juce_gui_extra juce::juce_core juce::juce_dsp
    juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# ═══════════════════════════════════════════════════════════════════════════
#  EQT — Parametric EQ
# ═══════════════════════════════════════════════════════════════════════════
juce_add_plugin(EQT
    VERSION                     1.0.0
    COMPANY_NAME                "${MIXMIND_COMPANY}"
    PLUGIN_MANUFACTURER_CODE    ${MIXMIND_MANUFACTURER}
    PLUGIN_CODE                 Equt
    FORMATS                     VST3 AU Standalone
    PRODUCT_NAME                "EQT"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    COPY_PLUGIN_AFTER_BUILD     TRUE
    VST3_CATEGORIES             Tools
    AU_MAIN_TYPE                kAudioUnitType_Effect
)

target_sources(EQT PRIVATE
    Source/EQTProcessor.cpp Source/EQTProcessor.h
    Source/EQTEditor.cpp Source/EQTEditor.h
    Source/AudioAnalyzer.cpp Source/AudioAnalyzer.h
    Source/LoudnessMeter.cpp Source/LoudnessMeter.h
    Source/LookAndFeel.h Source/LookAndFeel.cpp
    Source/LicenseManager.cpp Source/LicenseManager.h
    Source/HostTheme.cpp Source/HostTheme.h
)

target_compile_definitions(EQT PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
)

target_link_libraries(EQT PRIVATE
    juce::juce_audio_utils juce::juce_audio_processors
    juce::juce_gui_extra juce::juce_core juce::juce_dsp
    juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

juce_add_plugin(Reflex
    VERSION 1.0.0 COMPANY_NAME "${MIXMIND_COMPANY}"
    PLUGIN_MANUFACTURER_CODE ${MIXMIND_MANUFACTURER} PLUGIN_CODE Rflx
    FORMATS VST3 AU Standalone PRODUCT_NAME "Reflex"
    IS_SYNTH FALSE NEEDS_MIDI_INPUT FALSE NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE COPY_PLUGIN_AFTER_BUILD TRUE
    VST3_CATEGORIES Tools AU_MAIN_TYPE kAudioUnitType_Effect
)
target_sources(Reflex PRIVATE
    Source/ReflexProcessor.cpp Source/ReflexProcessor.h
    Source/ReflexEditor.cpp Source/ReflexEditor.h
    Source/LookAndFeel.h Source/LookAndFeel.cpp
    Source/LicenseManager.cpp Source/LicenseManager.h
    Source/HostTheme.cpp Source/HostTheme.h
)
target_compile_definitions(Reflex PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0)
target_link_libraries(Reflex PRIVATE juce::juce_audio_utils juce::juce_audio_processors juce::juce_gui_extra juce::juce_core juce::juce_dsp juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

juce_add_plugin(ThreeFX VERSION 1.0.0 COMPANY_NAME "${MIXMIND_COMPANY}" PLUGIN_MANUFACTURER_CODE ${MIXMIND_MANUFACTURER} PLUGIN_CODE Tfxf FORMATS VST3 AU Standalone PRODUCT_NAME "3FX" IS_SYNTH FALSE NEEDS_MIDI_INPUT FALSE NEEDS_MIDI_OUTPUT FALSE IS_MIDI_EFFECT FALSE COPY_PLUGIN_AFTER_BUILD TRUE VST3_CATEGORIES Tools AU_MAIN_TYPE kAudioUnitType_Effect)
target_sources(ThreeFX PRIVATE Source/ThreeFXProcessor.cpp Source/ThreeFXProcessor.h Source/ThreeFXEditor.cpp Source/ThreeFXEditor.h Source/LookAndFeel.h Source/LookAndFeel.cpp Source/LicenseManager.cpp Source/LicenseManager.h Source/HostTheme.cpp Source/HostTheme.h)
target_compile_definitions(ThreeFX PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0)
target_link_libraries(ThreeFX PRIVATE juce::juce_audio_utils juce::juce_audio_processors juce::juce_gui_extra juce::juce_core juce::juce_dsp juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

juce_add_plugin(Neat VERSION 1.0.0 COMPANY_NAME "${MIXMIND_COMPANY}" PLUGIN_MANUFACTURER_CODE ${MIXMIND_MANUFACTURER} PLUGIN_CODE Nttt FORMATS VST3 AU Standalone PRODUCT_NAME "Neat Neat Neat" IS_SYNTH FALSE NEEDS_MIDI_INPUT FALSE NEEDS_MIDI_OUTPUT FALSE IS_MIDI_EFFECT FALSE COPY_PLUGIN_AFTER_BUILD TRUE VST3_CATEGORIES Tools AU_MAIN_TYPE kAudioUnitType_Effect)
target_sources(Neat PRIVATE Source/NeatProcessor.cpp Source/NeatProcessor.h Source/NeatEditor.cpp Source/NeatEditor.h Source/LookAndFeel.h Source/LookAndFeel.cpp Source/LicenseManager.cpp Source/LicenseManager.h Source/HostTheme.cpp Source/HostTheme.h)
target_compile_definitions(Neat PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0)
target_link_libraries(Neat PRIVATE juce::juce_audio_utils juce::juce_audio_processors juce::juce_gui_extra juce::juce_core juce::juce_dsp juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

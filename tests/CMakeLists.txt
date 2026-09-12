# ── MixMind test suite (Catch2) ───────────────────────────────────────────
# Runtime verification for the parts that are pure data-in/data-out: the
# meters, the FIR designer, the per-channel routing and its latency, the
# trace→target map, and session recall of the match data.
#
# The tests build the plugin's own .cpp files rather than copies, so they cannot
# drift from what ships. Anything needing an editor or a window is tested
# through its pure half instead of a GUI.

juce_add_console_app(MixMind_Tests PRODUCT_NAME "MixMind Tests")

target_sources(MixMind_Tests PRIVATE
    TestSignals.h
    LoudnessTests.cpp
    ShaperTests.cpp
    MatchStateTests.cpp

    ${CMAKE_SOURCE_DIR}/Source/LoudnessMeter.cpp
    ${CMAKE_SOURCE_DIR}/Source/ShaperProcessor.cpp
    ${CMAKE_SOURCE_DIR}/Source/ReferenceAnalyzer.cpp
)

target_include_directories(MixMind_Tests PRIVATE ${CMAKE_SOURCE_DIR}/Source)

target_compile_definitions(MixMind_Tests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
)

target_link_libraries(MixMind_Tests PRIVATE
    Catch2::Catch2WithMain
    juce::juce_audio_basics
    juce::juce_audio_formats
    juce::juce_dsp
    juce::juce_core
    juce::juce_data_structures
    juce::juce_graphics
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

add_test(NAME MixMind_Tests COMMAND MixMind_Tests)

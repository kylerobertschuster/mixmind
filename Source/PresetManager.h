#pragma once
#include <juce_core/juce_core.h>
#include <juce_audio_processors/juce_audio_processors.h>

// ─────────────────────────────────────────────────────────────────────────────
//  PresetManager
//  Detects where the plugin is placed in the DAW (master, audio track, MIDI
//  track) and provides genre/focus presets and template plugin chains.
// ─────────────────────────────────────────────────────────────────────────────
class PresetManager
{
public:
    enum class TrackType
    {
        Master          = 0,
        AudioVocal,
        AudioDrums,
        AudioGuitar,
        AudioBass,
        AudioSynth,
        AudioVoiceMemo,
        MidiSynth,
        MidiDrums,
        MidiOther
    };

    enum class Focus
    {
        Clean           = 0,
        LoFi,
        BlownOut
    };

    struct Preset
    {
        juce::String name;
        juce::String systemPrompt;
        juce::String quickPrompts;   // comma-separated suggestions
    };

    PresetManager();

    // Track type
    void setTrackType (TrackType type);
    TrackType getTrackType() const { return trackType; }
    juce::String getTrackTypeName() const;
    static juce::StringArray getAllTrackTypeNames();

    // Focus
    void setFocus (Focus f)           { focus = f; }
    Focus getFocus() const            { return focus; }
    juce::String getFocusName() const;
    static juce::StringArray getAllFocusNames();

    // Genre
    void setGenre (const juce::String& g)  { genre = g; }
    juce::String getGenre() const          { return genre; }
    static juce::StringArray getGenresForTrackType (TrackType type);

    // Get the current preset prompt modifier
    juce::String buildPresetPrompt() const;

    // Quick prompt suggestions for current track type
    juce::StringArray getQuickPrompts() const;

    // Template plugin chain suggestions
    juce::StringArray getTemplateChain() const;

private:
    TrackType    trackType { TrackType::Master };
    Focus        focus     { Focus::Clean };
    juce::String genre;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetManager)
};

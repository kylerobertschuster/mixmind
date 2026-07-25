#include "PresetManager.h"

PresetManager::PresetManager() = default;

void PresetManager::setTrackType (TrackType type)
{
    trackType = type;
}

juce::String PresetManager::getTrackTypeName() const
{
    switch (trackType)
    {
        case TrackType::Master:         return "Master Bus";
        case TrackType::AudioVocal:     return "Vocal (Audio)";
        case TrackType::AudioDrums:     return "Drums (Audio)";
        case TrackType::AudioGuitar:    return "Guitar (Audio)";
        case TrackType::AudioBass:      return "Bass (Audio)";
        case TrackType::AudioSynth:     return "Synth (Audio)";
        case TrackType::AudioVoiceMemo: return "Voice Memo";
        case TrackType::MidiSynth:      return "Synth (MIDI)";
        case TrackType::MidiDrums:      return "Drums (MIDI)";
        case TrackType::MidiOther:      return "MIDI";
    }
    return "Unknown";
}

juce::StringArray PresetManager::getAllTrackTypeNames()
{
    return {
        "Master Bus",
        "Vocal (Audio)",
        "Drums (Audio)",
        "Guitar (Audio)",
        "Bass (Audio)",
        "Synth (Audio)",
        "Voice Memo",
        "Synth (MIDI)",
        "Drums (MIDI)",
        "MIDI"
    };
}

juce::String PresetManager::getFocusName() const
{
    switch (focus)
    {
        case Focus::Clean:    return "Clean";
        case Focus::LoFi:     return "Lo-Fi";
        case Focus::BlownOut: return "Blown Out";
    }
    return "Clean";
}

juce::StringArray PresetManager::getAllFocusNames()
{
    return { "Clean", "Lo-Fi", "Blown Out" };
}

juce::StringArray PresetManager::getGenresForTrackType (TrackType type)
{
    switch (type)
    {
        case TrackType::Master:
            return { "Hip-Hop / Trap", "House / Techno", "Lo-fi / Chill", "Pop",
                     "R&B / Soul", "Drum & Bass", "Rock", "Ambient", "Jazz", "Metal", "Other" };
        case TrackType::AudioVocal:
            return { "Pop", "Hip-Hop", "R&B", "Rock", "Singer-Songwriter",
                     "Lo-fi", "Indie", "Podcast / Voiceover", "Other" };
        case TrackType::AudioDrums:
        case TrackType::MidiDrums:
            return { "Hip-Hop / Trap", "House / Techno", "Drum & Bass",
                     "Rock", "Lo-fi", "Pop", "Jazz", "Breakbeat", "Other" };
        case TrackType::AudioGuitar:
            return { "Rock", "Indie", "Lo-fi", "Shoegaze", "Funk",
                     "Blues", "Pop", "Metal", "Other" };
        case TrackType::AudioBass:
            return { "Hip-Hop", "Drum & Bass", "House", "Funk",
                     "Rock", "Jazz", "Pop", "Other" };
        case TrackType::AudioSynth:
        case TrackType::MidiSynth:
            return { "House / Techno", "Lo-fi", "Synthwave", "Trap",
                     "Ambient", "Pop", "Drum & Bass", "Experimental", "Other" };
        case TrackType::AudioVoiceMemo:
            return { "Voice Note", "Field Recording", "Demo Sketch",
                     "Voiceover", "Sample Flip", "Other" };
        case TrackType::MidiOther:
            return { "Hip-Hop / Trap", "House / Techno", "Lo-fi", "Pop",
                     "R&B", "Drum & Bass", "Ambient", "Other" };
    }
    return { "Other" };
}

juce::String PresetManager::buildPresetPrompt() const
{
    juce::String prompt;

    prompt << "You are placed on a " << getTrackTypeName() << " in a " << genre << " session. ";

    switch (focus)
    {
        case Focus::Clean:
            prompt << "Focus on clean, professional, balanced sound. Industry-standard mix quality.";
            break;
        case Focus::LoFi:
            prompt << "Focus on a lo-fi aesthetic: warm saturation, tape wobble, reduced high-end, "
                      "vintage character. Imperfection is the goal.";
            break;
        case Focus::BlownOut:
            prompt << "Focus on a blown-out, distorted, aggressive, overdriven sound. "
                      "Push everything into the red. Clipping, distortion, and chaos are features, not bugs.";
            break;
    }

    prompt << "\n\nTRACK CONTEXT:\n";
    prompt << "- This is a " << getTrackTypeName() << " in a " << genre << " session.\n";
    prompt << "- Focus: " << getFocusName() << "\n";

    // Add track-specific guidance
    switch (trackType)
    {
        case TrackType::Master:
            prompt << "- You are on the master bus. Focus on overall balance, glue, loudness, and translation.\n";
            break;
        case TrackType::AudioVocal:
            prompt << "- Vocal track. Focus on clarity, presence, de-essing, compression, and sitting in the mix.\n";
            break;
        case TrackType::AudioDrums:
        case TrackType::MidiDrums:
            prompt << "- Drums. Focus on punch, transient shaping, stereo placement, and groove.\n";
            break;
        case TrackType::AudioGuitar:
            prompt << "- Guitar. Focus on tone shaping, amp character, saturation, and placement in the stereo field.\n";
            break;
        case TrackType::AudioBass:
            prompt << "- Bass. Focus on low-end control, saturation, mid presence for translation on small speakers.\n";
            break;
        case TrackType::AudioSynth:
        case TrackType::MidiSynth:
            prompt << "- Synth. Focus on sound design texture, stereo width, and frequency carving to fit the arrangement.\n";
            break;
        case TrackType::AudioVoiceMemo:
            prompt << "- Voice memo or field recording. Focus on cleanup, noise reduction, EQ rescue, and making the source usable.\n";
            break;
        case TrackType::MidiOther:
            prompt << "- MIDI instrument. Focus on sound selection, articulation, velocity shaping, and mix placement.\n";
            break;
    }

    return prompt;
}

juce::StringArray PresetManager::getQuickPrompts() const
{
    juce::StringArray prompts;

    switch (trackType)
    {
        case TrackType::Master:
            prompts.addArray ({
                "Is my low end balanced?",
                "How's my stereo width?",
                "Check my LUFS and dynamics",
                "What's eating headroom?",
                "Give me a master chain for " + genre,
                "How do I get more loudness?",
                "Is my mix translating?",
                "Diagnose my overall balance"
            });
            break;
        case TrackType::AudioVocal:
            prompts.addArray ({
                "Clean up this vocal",
                "How do I get vocals to sit in the mix?",
                "De-essing and compression tips",
                "Vocal EQ guide for " + genre,
                "How to add presence without harshness",
                "Double tracking and widening tips",
                "Vocal reverb/delay settings",
                "Fix boxy/muddy vocals"
            });
            break;
        case TrackType::AudioDrums:
        case TrackType::MidiDrums:
            prompts.addArray ({
                "Make my kick punch through",
                "Snare compression and EQ",
                "Stereo drum placement",
                "Parallel compression setup",
                "Tame cymbal harshness",
                "Get my drums to knock harder",
                "Drum bus processing chain",
                "Fix phase issues in drums"
            });
            break;
        case TrackType::AudioGuitar:
            prompts.addArray ({
                "Dial in the perfect guitar tone",
                "Clean up muddy guitar",
                "Saturation and amp character",
                "Widen guitars without phase issues",
                "Guitar EQ carve for " + genre,
                "How to layer guitars properly",
                "Make my DI guitar sound like an amp",
                "Shoegaze/textural guitar tips"
            });
            break;
        case TrackType::AudioBass:
            prompts.addArray ({
                "Get my bass to translate on small speakers",
                "Bass saturation and distortion",
                "Sub vs mid bass balance",
                "Sidechain bass to kick",
                "Bass compression settings",
                "EQ carve for bass in a busy mix",
                "808 processing tips",
                "Make my bassline groove harder"
            });
            break;
        case TrackType::AudioSynth:
        case TrackType::MidiSynth:
            prompts.addArray ({
                "Make this synth sit in the mix",
                "Stereo width for synths",
                "Sound design texture tips",
                "Layering synths properly",
                "EQ carve for synth pads",
                "Give this synth more character",
                "Clean up muddy synth low end",
                "How to make synths punchy"
            });
            break;
        case TrackType::AudioVoiceMemo:
            prompts.addArray ({
                "Clean up this recording",
                "Remove background noise",
                "Make this voice memo usable",
                "EQ rescue for bad recordings",
                "Add body to a thin recording",
                "Sample flip ideas for this recording",
                "Fix clipping/overloaded recording",
                "Match this to a professional vocal sound"
            });
            break;
        case TrackType::MidiOther:
            prompts.addArray ({
                "Sound selection advice for " + genre,
                "Velocity and dynamics tips",
                "How to humanize MIDI",
                "Layer this with other sounds",
                "Make my MIDI sound realistic",
                "Mix placement for this part",
                "Articulation and expression tips",
                "Get this part to stand out"
            });
            break;
    }

    return prompts;
}

juce::StringArray PresetManager::getTemplateChain() const
{
    juce::StringArray chain;

    chain.add ("1. Subtractive EQ — cut problem frequencies first");

    switch (trackType)
    {
        case TrackType::Master:
            chain.addArray ({
                "2. Gentle bus compression (2:1, slow attack)",
                "3. Mid-side EQ for stereo balance",
                "4. Saturation/exciter for glue",
                "5. Limiter with 2-3dB gain reduction max",
                "6. LUFS meter — target -14 to -8 depending on genre"
            });
            break;
        case TrackType::AudioVocal:
            chain.addArray ({
                "2. De-esser (5-8 kHz)",
                "3. Compressor (4:1, medium attack) — 3-6dB reduction",
                "4. Sweetening EQ — boost air (10k+) and presence (2-4k)",
                "5. Saturation for character",
                "6. Reverb/delay sends (not inserts)"
            });
            break;
        case TrackType::AudioDrums:
        case TrackType::MidiDrums:
            chain.addArray ({
                "2. Gate/expander for cleanup",
                "3. Transient shaper",
                "4. Compressor (4:1, fast attack for punch)",
                "5. EQ — boost punch (60-100Hz on kick, 200Hz on snare)",
                "6. Saturation for warmth"
            });
            break;
        case TrackType::AudioGuitar:
            chain.addArray ({
                "2. Amp sim or saturation",
                "3. EQ — high-pass at 80-120Hz",
                "4. Compressor (3:1, medium attack)",
                "5. Reverb/delay for space",
                "6. Doubler/widener for stereo"
            });
            break;
        case TrackType::AudioBass:
            chain.addArray ({
                "2. Saturation/distortion for harmonics",
                "3. Compressor (4:1, fast attack) — 5-8dB reduction",
                "4. EQ — boost 60-100Hz, cut 200-400Hz for clarity",
                "5. Sidechain compressor from kick",
                "6. Limiter to tame peaks"
            });
            break;
        case TrackType::AudioSynth:
        case TrackType::MidiSynth:
            chain.addArray ({
                "2. Filter/envelope shaping",
                "3. EQ carve to fit arrangement",
                "4. Stereo widener or chorus",
                "5. Reverb/delay for depth",
                "6. Saturation for analog warmth"
            });
            break;
        case TrackType::AudioVoiceMemo:
            chain.addArray ({
                "2. Noise gate/denoiser",
                "3. Aggressive EQ cleanup — high-pass, notch resonances",
                "4. Compressor to even out levels",
                "5. Saturation to add missing harmonics",
                "6. Reverb to place in a space"
            });
            break;
        case TrackType::MidiOther:
            chain.addArray ({
                "2. Instrument/sound selection",
                "3. Velocity and timing humanization",
                "4. EQ to fit the arrangement",
                "5. Reverb/delay for space",
                "6. Light compression for consistency"
            });
            break;
    }

    return chain;
}

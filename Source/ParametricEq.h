#pragma once
#include <juce_core/juce_core.h>
#include <array>

// ─────────────────────────────────────────────────────────────────────────────
//  ParametricEq — the interactive bands that sit after the match EQ.
//
//  Each band is a cascade of analog-matched biquads: poles are the analog
//  prototype's poles mapped by impulse invariance, zeros are fitted to the
//  analog magnitude at DC, the band frequency and Nyquist (Vicanek, "Matched
//  Second Order Digital Filters"). Unlike bilinear (RBJ) filters, a bell at
//  16 kHz keeps its analog width instead of cramping toward Nyquist.
//
//  Per-band placement: Stereo (L and R), Mid or Side. When any band is
//  audible the signal is processed as mid/side; when none is, process() does
//  not touch the audio (bit-exact bypass).
//
//  Each designed section runs as a TPT state-variable filter (Simper): any
//  stable biquad maps exactly onto an SVF's g, k and three output mixes, and
//  an SVF's states are integrator states, so sweeping it behaves like an
//  analog filter instead of re-interpreting stored energy (the swell a Direct
//  Form biquad produces when a resonant band is dragged).
//
//  Continuous parameters (frequency, gain, Q) glide over ~20 ms, are
//  re-designed every kSubBlock samples and interpolated per sample; discrete
//  changes (type, slope, placement, on/off) fade the band out and back in,
//  so nothing clicks.
// ─────────────────────────────────────────────────────────────────────────────
class ParametricEq
{
public:
    static constexpr int kNumBands    = 8;
    static constexpr int kMaxSections = 4;     // 48 dB/oct cuts
    static constexpr int kSubBlock    = 16;
    static constexpr float kMinHz = 20.0f, kMaxHz = 20000.0f;
    static constexpr float kMinQ  = 0.1f,  kMaxQ  = 18.0f;
    static constexpr float kMaxGainDb = 24.0f;

    enum class Type { bell, lowShelf, highShelf, lowCut, highCut, notch };
    enum class Placement { stereo, mid, side };

    struct Band
    {
        bool      on        { false };
        Type      type      { Type::bell };
        float     freq      { 1000.0f };   // Hz
        float     gainDb    { 0.0f };      // bell / shelves only
        float     q         { 1.0f };
        int       slope     { 12 };        // cuts: 12, 24 or 48 dB/oct
        Placement placement { Placement::stereo };
    };

    static bool hasGain (Type t) { return t == Type::bell || t == Type::lowShelf || t == Type::highShelf; }
    static juce::StringArray typeNames()      { return { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch" }; }
    static juce::StringArray slopeNames()     { return { "12 dB/oct", "24 dB/oct", "48 dB/oct" }; }
    static juce::StringArray placementNames() { return { "Stereo", "Mid", "Side" }; }
    static int slopeFromIndex (int i)         { return i <= 0 ? 12 : (i == 1 ? 24 : 48); }
    static int indexFromSlope (int s)         { return s <= 12 ? 0 : (s <= 24 ? 1 : 2); }

    struct Biquad { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };

    ParametricEq() = default;

    // Matched design of a band at `sampleRate`; returns the section count.
    static int design (const Band&, double sampleRate, Biquad* sections);

    // Magnitude (dB) of the designed digital band, and of its analog prototype.
    static double responseDb (const Band&, double hz, double sampleRate);
    static double magnitudeDb (const Biquad* sections, int numSections, double hz, double sampleRate);
    static double analogResponseDb (const Band&, double hz);

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    // Latest requested settings for a band (audio thread, once per block).
    void setBand (int index, const Band& target);

    // In place. R == nullptr for a mono stream (Stereo and Mid bands apply).
    void process (float* L, float* R, int numSamples);

    // True while any band is on or still fading out.
    bool isActive() const;

    // SVF realisation of a biquad: y = mH·hp + mB·bp + mL·lp.
    struct Svf { double g = 1, k = 2, mH = 1, mB = 2, mL = 1; };   // default: pass-through
    static Svf toSvf (const Biquad&);

private:
    struct Voice
    {
        Band   target, current;
        Svf    from[kMaxSections], to[kMaxSections];   // per-sample interpolation within a sub-block
        int    numSections { 0 };
        double ic1[2][kMaxSections] {}, ic2[2][kMaxSections] {};   // mid (or mono), side
        float  mix { 0.0f }, mixTarget { 0.0f };
        double logFreq { 0.0 }, logQ { 0.0 };
        bool   needsDesign { true }, fresh { true };
    };

    void updateVoice (Voice&);
    static void clearState (Voice&);

    std::array<Voice, kNumBands> voices;
    double sampleRate { 48000.0 };
    double glide { 1.0 };      // per-sub-block smoothing coefficient
    float  mixStep { 0.01f };  // per-sample fade step

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParametricEq)
};

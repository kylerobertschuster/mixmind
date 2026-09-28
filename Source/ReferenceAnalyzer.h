#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <functional>
#include <vector>
#include "AudioAnalyzer.h"

// ─────────────────────────────────────────────────────────────────────────────
//  ReferenceAnalyzer — decodes a reference track and measures it the same way
//  AudioAnalyzer measures the live signal, so REF and YOU are comparable:
//
//    · long-term average spectra of the mid and side signals (Hann, 50 %
//      overlap, frames whose mid is below −70 dBFS skipped — same gate as the
//      live long-term),
//    · BS.1770 integrated loudness and true peak (LoudnessMeter),
//    · whole-file RMS, stereo width and phase correlation.
//
//  The spectrum is analysed at the file's own sample rate, with an FFT size
//  chosen so the bin spacing stays ≈ 22 Hz, and mapped onto the live analyzer
//  grid with mapToGrid(). Nothing is resampled, so there is no interpolator
//  droop or aliasing, and a session sample-rate change only needs a re-map.
//
//  analyse() blocks for a second or two on long files — call it from a
//  background thread. It streams the file in chunks (no whole-file buffer).
// ─────────────────────────────────────────────────────────────────────────────
class ReferenceAnalyzer
{
public:
    struct Result
    {
        juce::String name;              // file name without extension
        juce::String path;              // full path, for session recall
        double sampleRate { 0.0 };      // native rate of `spectrum`
        int    fftSize    { 0 };        // native FFT size of `spectrum`
        std::vector<float> spectrum;    // fftSize/2 bins, mean magnitude of the mid (AudioAnalyzer scaling)
        std::vector<float> sideSpectrum;   // same for the side (L−R)/2; empty in pre-M/S sessions
        double durationSeconds { 0.0 };

        float lufs        { LoudnessMeter::kSilenceDb };   // integrated
        float truePeakDb  { LoudnessMeter::kSilenceDb };
        float rmsDb       { LoudnessMeter::kSilenceDb };
        float stereoWidth { 0.0f };
        float phaseCorr   { 1.0f };

        // A Result can come from a session file someone else wrote, so this
        // checks rather than trusts: a plausible rate, the FFT size analyse()
        // uses for it, one magnitude per bin. (A subnormal rate used to pass
        // and make mapToGrid index with (int) NaN.)
        bool  isValid() const
        {
            return sampleRate >= kMinSampleRate && sampleRate <= kMaxSampleRate
                && fftSize == fftSizeForRate (sampleRate)
                && (int) spectrum.size() == fftSize / 2;
        }
        bool  hasSide() const { return isValid() && sideSpectrum.size() == spectrum.size(); }
        float getCrestFactor() const
        {
            if (truePeakDb <= LoudnessMeter::kSilenceDb || rmsDb <= LoudnessMeter::kSilenceDb) return 0.0f;
            return juce::jmax (0.0f, truePeakDb - rmsDb);
        }
    };

    ReferenceAnalyzer();

    // Decode + analyse. Returns false with a user-facing `error` on failure.
    // `shouldAbort` is polled between chunks; returning true cancels.
    bool analyse (const juce::File& file, Result& out, juce::String& error,
                  const std::function<bool()>& shouldAbort = {});

    // Longest stretch of a file that is analysed (keeps a pathological
    // multi-hour file from tying up the loader).
    static constexpr double kMaxSeconds = 30.0 * 60.0;

    // Native FFT size used for a given file sample rate.
    static int fftSizeForRate (double sampleRate);

    // File sample rates analyse() accepts.
    static constexpr double kMinSampleRate = 1000.0, kMaxSampleRate = 768000.0;

    // Resamples `ref.spectrum` (or `sideSpectrum`) onto the live grid:
    // `numBins` bins spaced liveSampleRate / AudioAnalyzer::fftSize. Bins
    // above the reference's Nyquist are 0 ("no data"), as is everything when
    // a side spectrum is asked for and there is none. Includes the
    // noise-bandwidth correction sqrt(liveBinHz / refBinHz) so broadband
    // content lines up in level.
    static void mapToGrid (const Result& ref, double liveSampleRate, float* outBins, int numBins,
                           bool side = false);

    bool canRead (const juce::File& file) const;
    juce::String getWildcard() const { return formatManager.getWildcardForAllFormats(); }

private:
    juce::AudioFormatManager formatManager;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReferenceAnalyzer)
};

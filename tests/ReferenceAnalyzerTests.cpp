#include "ReferenceAnalyzer.h"
#include "TestSignals.h"

using namespace TestSignals;

namespace
{
    constexpr int kBins = AudioAnalyzer::numBins;

    // Mean magnitude (dB) over the live-grid bins between lo and hi Hz.
    double bandDb (const float* bins, double fs, double lo, double hi)
    {
        double s = 0.0; int n = 0;
        for (int i = 1; i < kBins; ++i)
        {
            const double f = i * fs / AudioAnalyzer::fftSize;
            if (f >= lo && f <= hi) { s += bins[i]; ++n; }
        }
        return 20.0 * std::log10 (juce::jmax (1.0e-12, s / juce::jmax (1, n)));
    }

    std::vector<float> liveLongTerm (const std::vector<float>& l, const std::vector<float>& r, double fs)
    {
        AudioAnalyzer a;
        a.prepare (fs, 512);
        juce::AudioBuffer<float> buf (2, 512);
        for (size_t i = 0; i + 512 <= l.size(); i += 512)
        {
            buf.copyFrom (0, 0, l.data() + i, 512);
            buf.copyFrom (1, 0, r.data() + i, 512);
            a.process (buf);
        }
        return { a.getLongTermBins(), a.getLongTermBins() + kBins };
    }
}

class ReferenceAnalyzerTests : public juce::UnitTest
{
public:
    ReferenceAnalyzerTests() : juce::UnitTest ("ReferenceAnalyzer", "Reference") {}

    void runTest() override
    {
        ReferenceAnalyzer analyzer;

        beginTest ("Stereo WAV: loudness, peak and spectrum agree with the live analyzer");
        {
            constexpr double fs = 48000.0;
            const auto l = pinkNoise (fs, 12.0, 0.05, 1);
            const auto r = pinkNoise (fs, 12.0, 0.05, 2);
            TempWav wav ("ref-stereo");
            expect (writeWav (wav.file, l, r, fs));

            ReferenceAnalyzer::Result res;
            juce::String err;
            expect (analyzer.analyse (wav.file, res, err), err);
            expect (res.isValid());
            expectEquals (res.name, juce::String ("ref-stereo"));
            expectEquals (res.fftSize, 2048);
            expectWithinAbsoluteError (res.durationSeconds, 12.0, 0.01);

            LoudnessMeter m; m.prepare (fs, 512);
            for (size_t i = 0; i < l.size(); i += 512)
                m.process (l.data() + i, r.data() + i, (int) juce::jmin ((size_t) 512, l.size() - i));
            expectWithinAbsoluteError (res.lufs, m.getIntegratedLufs(), 0.05f);   // 24-bit file vs float
            expectWithinAbsoluteError (res.truePeakDb, m.getTruePeakDb(), 0.05f);
            expect (res.getCrestFactor() > 6.0f && res.getCrestFactor() < 20.0f);
            expect (res.phaseCorr > -0.2f && res.phaseCorr < 0.2f);            // independent channels

            // Same rate → identity map; compare with the live long-term spectrum.
            std::vector<float> refBins ((size_t) kBins);
            ReferenceAnalyzer::mapToGrid (res, fs, refBins.data(), kBins);
            const auto live = liveLongTerm (l, r, fs);
            for (auto [lo, hi] : { std::pair { 60.0, 120.0 }, { 250.0, 500.0 }, { 1000.0, 2000.0 }, { 4000.0, 8000.0 }, { 10000.0, 16000.0 } })
                expectWithinAbsoluteError (bandDb (refBins.data(), fs, lo, hi), bandDb (live.data(), fs, lo, hi), 1.0,
                                           juce::String (lo) + "-" + juce::String (hi) + " Hz");
        }

        beginTest ("Sample-rate independent: 44.1 k and 96 k files land on the 48 k grid at the same level");
        {
            std::vector<float> at48[2];
            for (double fs : { 44100.0, 96000.0 })
            {
                // Same power per Hz at both rates (white noise at a fixed RMS
                // would put half as much energy in each Hz at 96 k).
                auto x = whiteNoise (fs, 8.0, -20.0 + 10.0 * std::log10 (fs / 44100.0), 7);
                const auto tone = sine (fs, 1000.0, 0.25, 8.0);
                for (size_t i = 0; i < x.size(); ++i) x[i] += tone[i];

                TempWav wav ("ref-" + juce::String ((int) fs));
                expect (writeWav (wav.file, x, x, fs));

                ReferenceAnalyzer::Result res;
                juce::String err;
                expect (analyzer.analyse (wav.file, res, err), err);
                expectEquals (res.fftSize, ReferenceAnalyzer::fftSizeForRate (fs));

                auto& out = at48[fs < 50000.0 ? 0 : 1];
                out.resize ((size_t) kBins);
                ReferenceAnalyzer::mapToGrid (res, 48000.0, out.data(), kBins);

                // The 1 kHz tone lands on the right 48 k bin (1000 / 23.44 ≈ 42.7).
                int peak = 1;
                for (int i = 1; i < kBins; ++i) if (out[(size_t) i] > out[(size_t) peak]) peak = i;
                expect (peak == 42 || peak == 43, "tone at bin " + juce::String (peak));

                // Content above the file's Nyquist: none (44.1 k → 0 above 22.05 kHz).
                if (fs < 50000.0) expectEquals (out[(size_t) kBins - 1], 0.0f);
            }

            // Broadband noise level agrees between the two source rates.
            for (auto [lo, hi] : { std::pair { 100.0, 400.0 }, { 2000.0, 6000.0 }, { 8000.0, 16000.0 } })
                expectWithinAbsoluteError (bandDb (at48[0].data(), 48000.0, lo, hi),
                                           bandDb (at48[1].data(), 48000.0, lo, hi), 1.0,
                                           juce::String (lo) + "-" + juce::String (hi) + " Hz");
        }

        beginTest ("Mono file is measured as dual-mono (as it plays on a stereo bus)");
        {
            constexpr double fs = 48000.0;
            const auto x = sine (fs, 1000.0, dbToGain (-23.0), 10.0);
            TempWav mono ("ref-mono"), dual ("ref-dual");
            expect (writeWav (mono.file, x, {}, fs));
            expect (writeWav (dual.file, x, x, fs));

            ReferenceAnalyzer::Result a, b;
            juce::String err;
            expect (analyzer.analyse (mono.file, a, err), err);
            expect (analyzer.analyse (dual.file, b, err), err);
            expectWithinAbsoluteError (a.lufs, -23.0f, 0.1f);
            expectWithinAbsoluteError (a.lufs, b.lufs, 0.01f);
            expectWithinAbsoluteError (a.stereoWidth, 0.0f, 1.0e-4f);
            expectWithinAbsoluteError (a.phaseCorr, 1.0f, 1.0e-4f);
        }

        beginTest ("A file shorter than one gating block still gives a spectrum; loudness is unmeasured");
        {
            const auto x = sine (48000.0, 1000.0, dbToGain (-20.0), 0.3);
            TempWav wav ("ref-short");
            expect (writeWav (wav.file, x, x, 48000.0));
            ReferenceAnalyzer::Result res;
            juce::String err;
            expect (analyzer.analyse (wav.file, res, err), err);
            expect (res.isValid());
            expectEquals (res.lufs, LoudnessMeter::kSilenceDb);
        }

        beginTest ("Failures are reported, not thrown or crashed");
        {
            ReferenceAnalyzer::Result res;
            juce::String err;

            expect (! analyzer.analyse (juce::File ("/definitely/not/here.wav"), res, err));
            expect (err.containsIgnoreCase ("not found"), err);

            TempWav silent ("ref-silent");
            const std::vector<float> zeros (48000 * 2, 0.0f);
            expect (writeWav (silent.file, zeros, zeros, 48000.0));
            err = {};
            expect (! analyzer.analyse (silent.file, res, err));
            expect (err.containsIgnoreCase ("silent"), err);

            const auto junk = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("mixmind-junk.wav");
            junk.replaceWithText ("this is not audio");
            err = {};
            expect (! analyzer.analyse (junk, res, err));
            expect (err.isNotEmpty());
            junk.deleteFile();
        }

        beginTest ("Analysis can be cancelled");
        {
            const auto x = whiteNoise (48000.0, 10.0, -20.0, 3);
            TempWav wav ("ref-cancel");
            expect (writeWav (wav.file, x, x, 48000.0));
            ReferenceAnalyzer::Result res;
            juce::String err;
            expect (! analyzer.analyse (wav.file, res, err, [] { return true; }));
            expectEquals (err, juce::String ("Cancelled."));
        }

        beginTest ("Supported formats include WAV, AIFF, FLAC and MP3");
        {
            for (auto ext : { "wav", "aiff", "flac", "mp3" })
                expect (analyzer.canRead (juce::File ("/x/y." + juce::String (ext))), ext);
            expect (! analyzer.canRead (juce::File ("/x/y.png")));
        }
    }
};

static ReferenceAnalyzerTests referenceAnalyzerTests;

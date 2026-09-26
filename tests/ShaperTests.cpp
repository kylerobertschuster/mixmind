#include "ShaperProcessor.h"
#include "AudioAnalyzer.h"
#include "TestSignals.h"

using namespace TestSignals;

namespace
{
    constexpr double kFs = 48000.0;
    constexpr int    kBins = AudioAnalyzer::numBins;

    double binFreq (int i) { return (double) i * kFs / (double) ShaperProcessor::kDesignSize; }

    std::vector<float> flat (float value) { return std::vector<float> ((size_t) kBins, value); }

    // Octave-weighted mean response (dB) between 40 Hz and 16 kHz.
    double meanResponseDb (const std::vector<float>& taps)
    {
        double s = 0.0, w = 0.0;
        for (double f = 40.0; f <= 16000.0; f *= 1.05)
        {
            s += ShaperProcessor::responseDb (taps, f, kFs);
            w += 1.0;
        }
        return s / w;
    }

    // Runs a buffer through the shaper in 256-sample blocks.
    void run (ShaperProcessor& s, std::vector<float>& l, std::vector<float>& r)
    {
        for (size_t i = 0; i < l.size(); i += 256)
        {
            const int n = (int) juce::jmin ((size_t) 256, l.size() - i);
            s.process (l.data() + i, r.data() + i, n);
        }
    }
}

class ShaperTests : public juce::UnitTest
{
public:
    ShaperTests() : juce::UnitTest ("ShaperProcessor (linear-phase match EQ)", "Shaper") {}

    void runTest() override
    {
        std::vector<float> taps;

        beginTest ("Target == live designs the identity (unit impulse at kLatency)");
        {
            const auto live = flat (0.01f);
            ShaperProcessor::buildMatchFilter (live.data(), live.data(), kBins, kFs, 1.0f, taps);
            expectEquals ((int) taps.size(), ShaperProcessor::kTapCount);
            expectWithinAbsoluteError (taps[(size_t) ShaperProcessor::kLatency], 1.0f, 1.0e-4f);
            float offPeak = 0.0f;
            for (size_t i = 0; i < taps.size(); ++i)
                if ((int) i != ShaperProcessor::kLatency) offPeak = juce::jmax (offPeak, std::abs (taps[i]));
            expectLessThan (offPeak, 1.0e-4f);
        }

        beginTest ("Level-neutral: a reference 20 dB louder does not change the level");
        {
            const auto live = flat (0.01f), target = flat (0.1f);
            ShaperProcessor::buildMatchFilter (target.data(), live.data(), kBins, kFs, 1.0f, taps);
            for (double f : { 50.0, 200.0, 1000.0, 5000.0, 15000.0 })
                expectWithinAbsoluteError (ShaperProcessor::responseDb (taps, f, kFs), 0.0f, 0.1f);
        }

        beginTest ("Amount 0 and missing data design the identity");
        {
            auto live = flat (0.01f);
            auto target = flat (0.01f);
            for (int i = 0; i < kBins; ++i) if (binFreq (i) > 2000.0) target[(size_t) i] *= 2.0f;

            ShaperProcessor::buildMatchFilter (target.data(), live.data(), kBins, kFs, 0.0f, taps);
            expectWithinAbsoluteError (taps[(size_t) ShaperProcessor::kLatency], 1.0f, 1.0e-6f);

            const auto silent = flat (0.0f);
            ShaperProcessor::buildMatchFilter (target.data(), silent.data(), kBins, kFs, 1.0f, taps);
            expectWithinAbsoluteError (taps[(size_t) ShaperProcessor::kLatency], 1.0f, 1.0e-6f);

            ShaperProcessor::buildMatchFilter (silent.data(), live.data(), kBins, kFs, 1.0f, taps);
            expectWithinAbsoluteError (taps[(size_t) ShaperProcessor::kLatency], 1.0f, 1.0e-6f);
        }

        beginTest ("A +6 dB high shelf in the target becomes a +6 dB tilt, centred on 0 dB");
        {
            auto live = flat (0.01f);
            auto target = flat (0.01f);
            for (int i = 0; i < kBins; ++i) if (binFreq (i) > 2000.0) target[(size_t) i] *= 2.0f;

            std::vector<float> curve;
            ShaperProcessor::buildMatchFilter (target.data(), live.data(), kBins, kFs, 1.0f, taps, &curve);

            const float lo = ShaperProcessor::responseDb (taps, 250.0, kFs);
            const float hi = ShaperProcessor::responseDb (taps, 8000.0, kFs);
            expectWithinAbsoluteError (hi - lo, 6.02f, 0.5f);
            expectWithinAbsoluteError ((float) meanResponseDb (taps), 0.0f, 0.5f);

            // The published curve is what the filter realises.
            expectEquals ((int) curve.size(), kBins);
            const int bin8k = (int) std::lround (8000.0 / binFreq (1));
            expectWithinAbsoluteError (curve[(size_t) bin8k], hi, 0.5f);

            // Amount scales the correction.
            ShaperProcessor::buildMatchFilter (target.data(), live.data(), kBins, kFs, 0.5f, taps);
            expectWithinAbsoluteError (ShaperProcessor::responseDb (taps, 8000.0, kFs)
                                         - ShaperProcessor::responseDb (taps, 250.0, kFs), 3.01f, 0.3f);
        }

        beginTest ("Designed taps are exactly linear-phase about kLatency");
        {
            auto live = flat (0.01f);
            auto target = flat (0.01f);
            for (int i = 0; i < kBins; ++i) target[(size_t) i] *= (float) std::pow (binFreq (i) / 1000.0 + 0.01, -0.25);

            ShaperProcessor::buildMatchFilter (target.data(), live.data(), kBins, kFs, 1.0f, taps);
            constexpr int c = ShaperProcessor::kLatency;
            float asym = 0.0f;
            for (int m = 1; m < c; ++m)
                asym = juce::jmax (asym, std::abs (taps[(size_t) (c + m)] - taps[(size_t) (c - m)]));
            expectLessThan (asym, 1.0e-6f);
            expectEquals (taps[0], 0.0f);
        }

        beginTest ("Bypassed = pure kLatency delay; latency is constant");
        {
            ShaperProcessor s; s.prepare (kFs, 256);
            std::vector<float> l (4096, 0.0f), r (4096, 0.0f);
            l[100] = 1.0f; r[200] = -0.5f;
            run (s, l, r);
            expectEquals (l[100 + ShaperProcessor::kLatency], 1.0f);
            expectEquals (r[200 + ShaperProcessor::kLatency], -0.5f);
            float other = 0.0f;
            for (size_t i = 0; i < l.size(); ++i)
                if ((int) i != 100 + ShaperProcessor::kLatency) other = juce::jmax (other, std::abs (l[i]));
            expectEquals (other, 0.0f);
            expectEquals (ShaperProcessor::getLatencySamples(), 512);
        }

        beginTest ("Enabled: the runtime impulse response equals the designed taps");
        {
            auto live = flat (0.01f);
            auto target = flat (0.01f);
            for (int i = 0; i < kBins; ++i) if (binFreq (i) > 2000.0) target[(size_t) i] *= 2.0f;
            ShaperProcessor::buildMatchFilter (target.data(), live.data(), kBins, kFs, 1.0f, taps);

            ShaperProcessor s; s.prepare (kFs, 256);
            s.setFilter (taps.data(), (int) taps.size());
            s.setEnabled (true);

            std::vector<float> l (ShaperProcessor::kFadeSamples + 4096, 0.0f), r (l.size(), 0.0f);
            const size_t at = (size_t) ShaperProcessor::kFadeSamples + 256;   // after the fade-in
            l[at] = 1.0f; r[at] = 1.0f;
            run (s, l, r);

            float err = 0.0f;
            for (size_t k = 0; k < taps.size(); ++k)
                err = juce::jmax (err, std::abs (l[at + k] - taps[k]), std::abs (r[at + k] - taps[k]));
            expectLessThan (err, 1.0e-6f);
        }

        beginTest ("Stepped sine sweep: measured gain matches the designed response");
        {
            auto live = flat (0.01f);
            auto target = flat (0.01f);
            for (int i = 0; i < kBins; ++i)
                target[(size_t) i] *= (float) std::pow (binFreq (i) / 1000.0 + 0.01, 0.3);   // rising tilt
            ShaperProcessor::buildMatchFilter (target.data(), live.data(), kBins, kFs, 1.0f, taps);

            for (double f : { 60.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0 })
            {
                ShaperProcessor s; s.prepare (kFs, 256);
                s.setFilter (taps.data(), (int) taps.size());
                s.setEnabled (true);

                auto l = sine (kFs, f, 0.25, 0.5);
                auto r = l;
                const auto in = l;
                run (s, l, r);

                const size_t skip = (size_t) (ShaperProcessor::kFadeSamples + ShaperProcessor::kTapCount);
                const double gain = rmsDb (l.data() + skip, l.size() - skip) - rmsDb (in.data() + skip, in.size() - skip);
                expectWithinAbsoluteError ((float) gain, ShaperProcessor::responseDb (taps, f, kFs), 0.05f,
                                           "at " + juce::String (f) + " Hz");
            }
        }

        beginTest ("Filter swaps and SHAPE toggles crossfade (no clicks)");
        {
            auto live = flat (0.01f);
            auto target = flat (0.01f);
            for (int i = 0; i < kBins; ++i) if (binFreq (i) < 800.0) target[(size_t) i] *= 3.0f;   // big low boost
            ShaperProcessor::buildMatchFilter (target.data(), live.data(), kBins, kFs, 1.0f, taps);

            ShaperProcessor s; s.prepare (kFs, 256);
            auto l = sine (kFs, 440.0, 0.5, 2.0);
            auto r = l;

            // Worst sample-to-sample step of a 440 Hz sine with the boosted gain.
            const float boost = juce::Decibels::decibelsToGain (ShaperProcessor::responseDb (taps, 440.0, kFs));
            const float maxStep = 0.5f * juce::jmax (1.0f, boost) * (float) (2.0 * juce::MathConstants<double>::pi * 440.0 / kFs);

            for (size_t i = 0; i < l.size(); i += 256)
            {
                if (i == 24000) { s.setFilter (taps.data(), (int) taps.size()); s.setEnabled (true); }
                if (i == 60000) s.setEnabled (false);
                s.process (l.data() + i, r.data() + i, (int) juce::jmin ((size_t) 256, l.size() - i));
            }

            float worst = 0.0f;
            for (size_t i = (size_t) ShaperProcessor::kLatency + 1; i < l.size(); ++i)
                worst = juce::jmax (worst, std::abs (l[i] - l[i - 1]));
            expectLessThan (worst, maxStep * 1.05f);
        }

        beginTest ("Trace -> target: log-frequency interpolation in dB, explicit dB -> linear");
        {
            std::vector<ShaperProcessor::TracePoint> curve { { 100.0f, -40.0f }, { 10000.0f, -60.0f } };
            std::vector<float> t;
            ShaperProcessor::buildTargetFromCurve (curve, kBins, kFs, t);
            expectEquals ((int) t.size(), kBins);

            const int i1k = 43;   // 1007.8 Hz
            const double expectedDb = -40.0 - 20.0 * std::log (binFreq (i1k) / 100.0) / std::log (100.0);
            expectWithinAbsoluteError (20.0f * std::log10 (t[(size_t) i1k]), (float) expectedDb, 0.01f);

            expectEquals (t[2], 0.0f);                                   // 47 Hz: outside the trace
            expectEquals (t[(size_t) (kBins - 1)], 0.0f);                // 24 kHz: outside the trace
            expectWithinAbsoluteError (20.0f * std::log10 (t[5]), -40.0f - 20.0f * (float) (std::log (binFreq (5) / 100.0) / std::log (100.0)), 0.01f);
        }

        beginTest ("Manual mode end to end: the trace's slope is what the filter applies");
        {
            const auto live = flat (0.01f);   // −40 dB, flat
            std::vector<ShaperProcessor::TracePoint> curve { { 50.0f, -46.0f }, { 15000.0f, -34.0f } };
            std::vector<float> target;
            ShaperProcessor::buildTargetFromCurve (curve, kBins, kFs, target);
            ShaperProcessor::buildMatchFilter (target.data(), live.data(), kBins, kFs, 1.0f, taps);

            const double expected = 12.0 * std::log (8000.0 / 300.0) / std::log (15000.0 / 50.0);
            const float measured = ShaperProcessor::responseDb (taps, 8000.0, kFs) - ShaperProcessor::responseDb (taps, 300.0, kFs);
            expectWithinAbsoluteError (measured, (float) expected, 0.75f);
        }

        beginTest ("Outside the data the correction holds its edge value (no step back to 0 dB)");
        {
            const auto live = flat (0.01f);
            std::vector<ShaperProcessor::TracePoint> curve { { 200.0f, -46.0f }, { 5000.0f, -34.0f } };
            std::vector<float> target, curveDb;
            ShaperProcessor::buildTargetFromCurve (curve, kBins, kFs, target);
            ShaperProcessor::buildMatchFilter (target.data(), live.data(), kBins, kFs, 1.0f, taps, &curveDb);

            const auto at = [&] (double hz) { return curveDb[(size_t) std::lround (hz / binFreq (1))]; };
            expectGreaterThan (at (5000.0) - at (200.0), 6.0f);
            expectWithinAbsoluteError (at (12000.0), at (5000.0), 0.5f);
            expectWithinAbsoluteError (at (20000.0), at (5000.0), 0.5f);
            expectWithinAbsoluteError (at (80.0), at (200.0), 0.5f);
            expectEquals (curveDb[0], 0.0f);   // DC: never corrected
        }
    }
};

static ShaperTests shaperTests;

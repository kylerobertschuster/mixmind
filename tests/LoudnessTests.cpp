#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "LoudnessMeter.h"
#include "TestSignals.h"

// ─────────────────────────────────────────────────────────────────────────────
//  BS.1770 loudness + true peak.
//
//  Every expected number below comes from an independent implementation of
//  ITU-R BS.1770-4 (the standard's published 48 kHz K-weighting biquads, per
//  channel, energy-domain gating, 400 ms blocks), evaluated in Python for these
//  exact signals. They are not read back off the plugin.
//
//  Two things the reference pins down that are easy to get wrong:
//    · the channels are SUMMED, not averaged — a −23 dBFS tone in both channels
//      is 3.01 dB louder than the same tone in one;
//    · with K-weighting, a 1 kHz tone reads its peak level in LUFS, which is why
//      the EBU reference signal (−23 dBFS, 1 kHz) reads −23.0 LUFS.
// ─────────────────────────────────────────────────────────────────────────────
namespace
{
    using namespace TestSignals;

    constexpr double kSr = TestSignals::kSampleRate;

    struct Reading
    {
        float integrated, momentary, shortTerm, truePeakDb, rmsDb;
    };

    // Feeds a buffer the way a host would — in blocks — and reads the meter.
    // `R == nullptr` for a genuinely mono source: one channel of loudness.
    inline Reading measure (const juce::AudioBuffer<float>& b, int blockSize = 512)
    {
        LoudnessMeter m;
        m.prepare (kSr, blockSize);

        const bool stereo = b.getNumChannels() > 1;

        for (int start = 0; start < b.getNumSamples(); start += blockSize)
        {
            const int n = juce::jmin (blockSize, b.getNumSamples() - start);
            m.process (b.getReadPointer (0, start),
                       stereo ? b.getReadPointer (1, start) : nullptr,
                       n);
        }

        return { m.getIntegratedLufs(), m.getMomentaryLufs(), m.getShortTermLufs(),
                 m.getTruePeakDb(), m.getRmsDb() };
    }

    inline float ampForPeakDb (float db) { return std::pow (10.0f, db / 20.0f); }

    // `seconds` of a 1 kHz tone at `peakDb` per channel.
    inline juce::AudioBuffer<float> tone (double seconds, float peakDb,
                                         float lGain = 1.0f, float rGain = 1.0f)
    {
        const int n = (int) std::lround (seconds * kSr);
        const float a = ampForPeakDb (peakDb);
        return stereoSines (n, 1000.0, 1000.0, a * lGain, a * rGain);
    }
}

TEST_CASE ("a 1 kHz tone reads its peak level in LUFS", "[loudness]")
{
    // EBU Tech 3341 test signal 1: −23 dBFS, 1 kHz, stereo → −23.0 LUFS.
    // K-weighting at 1 kHz (+0.70 dB) and the −0.691 offset cancel, so the scale
    // is anchored to the peak level, not the RMS.
    const auto a = measure (tone (5.0, -23.0f));
    INFO ("-23 dBFS stereo tone → " << a.integrated << " LUFS");
    REQUIRE (a.integrated == Catch::Approx (-22.99f).margin (0.2f));

    const auto b = measure (tone (5.0, -20.0f));
    INFO ("-20 dBFS stereo tone → " << b.integrated << " LUFS");
    REQUIRE (b.integrated == Catch::Approx (-19.99f).margin (0.2f));

    // …and the offset is level-linear, i.e. it is a level, not a fudge.
    REQUIRE ((b.integrated - a.integrated) == Catch::Approx (3.0f).margin (0.1f));
}

TEST_CASE ("loudness sums the channels instead of averaging them", "[loudness][regression]")
{
    // Regression: K-weighting the mono average (L+R)·0.5 before measuring reads
    // 3.01 dB low for correlated stereo and 6 dB low for hard-panned material.
    // BS.1770 sums the per-channel weighted mean squares.
    const auto stereo   = measure (tone (5.0, -23.0f));
    const auto leftOnly = measure (tone (5.0, -23.0f, 1.0f, 0.0f));

    INFO ("stereo " << stereo.integrated << " LUFS, L-only " << leftOnly.integrated << " LUFS");

    REQUIRE (stereo.integrated   == Catch::Approx (-22.99f).margin (0.2f));
    REQUIRE (leftOnly.integrated == Catch::Approx (-26.00f).margin (0.2f));

    // The second channel must add exactly its own energy (+3.01 dB), not halve
    // the first one's.
    const float delta = stereo.integrated - leftOnly.integrated;
    INFO ("channel gain = " << delta << " dB (expected +3.01)");
    REQUIRE (delta == Catch::Approx (3.01f).margin (0.15f));
}

TEST_CASE ("a mono source counts as one channel", "[loudness]")
{
    // A single-channel buffer is handed to the meter as (L, nullptr) — one
    // channel of loudness. A stereo buffer with a silent right channel carries
    // the same energy, so both must read the same.
    juce::AudioBuffer<float> oneCh (1, (int) (5.0 * kSr));
    oneCh.copyFrom (0, 0, tone (5.0, -23.0f), 0, 0, oneCh.getNumSamples());

    const auto trueMono    = measure (oneCh);
    const auto silentRight = measure (tone (5.0, -23.0f, 1.0f, 0.0f));

    INFO ("single-channel buffer " << trueMono.integrated
          << " LUFS, stereo with silent R " << silentRight.integrated << " LUFS");
    REQUIRE (trueMono.integrated    == Catch::Approx (-26.00f).margin (0.2f));
    REQUIRE (silentRight.integrated == Catch::Approx (-26.00f).margin (0.2f));
    REQUIRE (trueMono.integrated == Catch::Approx (silentRight.integrated).margin (0.05f));
}

TEST_CASE ("true peak is measured per channel", "[loudness][regression]")
{
    // Regression: oversampling the mono average halves a hard-panned signal, so
    // a full-scale left-only tone used to read −6 dBTP. BS.1770 measures true
    // peak per channel and reports the loudest.
    const auto both = measure (tone (2.0, 0.0f));
    const auto left = measure (tone (2.0, 0.0f, 1.0f, 0.0f));
    const auto right = measure (tone (2.0, 0.0f, 0.0f, 1.0f));

    INFO ("stereo " << both.truePeakDb << " dBTP, L " << left.truePeakDb
          << ", R " << right.truePeakDb);

    REQUIRE (both.truePeakDb  == Catch::Approx (0.0f).margin (0.3f));
    REQUIRE (left.truePeakDb  == Catch::Approx (0.0f).margin (0.3f));
    REQUIRE (right.truePeakDb == Catch::Approx (0.0f).margin (0.3f));

    // A −6 dB reading here is the signature of the mono-average bug.
    REQUIRE (left.truePeakDb > -1.0f);
}

TEST_CASE ("RMS follows the loudest channel, so the crest factor stays honest", "[loudness]")
{
    // Crest factor is truePeak − rms (AudioAnalyzer::getCrestFactor), so both have
    // to come from the same per-channel basis or hard-panned material reports a
    // crest 6 dB too high. A full-scale sine is 3.01 dB rms-to-peak.
    const auto left = measure (tone (2.0, 0.0f, 1.0f, 0.0f));

    INFO ("L-only rms " << left.rmsDb << " dB, peak " << left.truePeakDb << " dBTP");
    REQUIRE (left.rmsDb == Catch::Approx (-3.01f).margin (0.2f));

    const float crest = left.truePeakDb - left.rmsDb;
    INFO ("crest factor " << crest << " dB (a sine is 3.01)");
    REQUIRE (crest == Catch::Approx (3.01f).margin (0.3f));
}

TEST_CASE ("the relative gate discards a quiet section", "[loudness][gating]")
{
    // 4 s at −23 then 4 s at −50: the quiet part is 27 LU down, well past the
    // −10 LU relative gate, so the integrated figure must stay at the loud level.
    // Reference: −23.16 (75 % overlap) / −22.99 (non-overlapping blocks).
    // Averaging the two halves' energies instead would give −26.69.
    auto b = tone (4.0, -23.0f);
    const auto quiet = tone (4.0, -50.0f);
    juce::AudioBuffer<float> mixed (2, b.getNumSamples() * 2);

    for (int ch = 0; ch < 2; ++ch)
    {
        mixed.copyFrom (ch, 0, b, ch, 0, b.getNumSamples());
        mixed.copyFrom (ch, b.getNumSamples(), quiet, ch, 0, quiet.getNumSamples());
    }

    const auto r = measure (mixed);
    INFO ("gated " << r.integrated << " LUFS (ungated would be −26.69)");
    REQUIRE (r.integrated == Catch::Approx (-23.0f).margin (0.3f));
    REQUIRE (r.integrated > -24.5f);
}

TEST_CASE ("material below the absolute gate is not reported", "[loudness][gating]")
{
    // −75 dBFS sits under the −70 LUFS absolute gate, so there is no valid
    // integrated figure at all — the meter must say so rather than report −75.
    const auto r = measure (tone (5.0, -75.0f));

    INFO ("integrated " << r.integrated << ", true peak " << r.truePeakDb);
    REQUIRE (r.integrated <= -70.0f);
    REQUIRE (std::isfinite (r.integrated));
    REQUIRE (r.truePeakDb == Catch::Approx (-75.0f).margin (0.3f));   // still measurable
}

TEST_CASE ("silence reads as silence", "[loudness][robustness]")
{
    juce::AudioBuffer<float> silent (2, (int) (1.0 * kSr));
    silent.clear();
    const auto r = measure (silent);

    REQUIRE (r.integrated <= -70.0f);
    REQUIRE (r.momentary  <= -70.0f);
    REQUIRE (r.shortTerm  <= -70.0f);
    REQUIRE (r.truePeakDb <= -60.0f);
    REQUIRE (std::isfinite (r.rmsDb));
}

TEST_CASE ("a steady tone gives matching momentary, short-term and integrated", "[loudness]")
{
    const auto r = measure (tone (5.0, -23.0f));

    INFO ("I " << r.integrated << ", M " << r.momentary << ", S " << r.shortTerm);
    REQUIRE (r.momentary == Catch::Approx (r.integrated).margin (0.3f));
    REQUIRE (r.shortTerm == Catch::Approx (r.integrated).margin (0.3f));
}

TEST_CASE ("the reading does not depend on the host's block size", "[loudness]")
{
    const auto coarse = measure (tone (3.0, -23.0f), 4096);
    const auto fine   = measure (tone (3.0, -23.0f), 37);      // deliberately not a divisor

    INFO ("4096-sample blocks " << coarse.integrated << " LUFS, 37-sample blocks " << fine.integrated);
    REQUIRE (coarse.integrated == Catch::Approx (fine.integrated).margin (0.2f));
    REQUIRE (coarse.truePeakDb  == Catch::Approx (fine.truePeakDb).margin (0.3f));
}

TEST_CASE ("degenerate input is ignored, not measured", "[loudness][robustness]")
{
    LoudnessMeter m;
    m.prepare (kSr, 512);

    const float one = 1.0f;
    m.process (nullptr, nullptr, 512);      // no data
    m.process (&one, nullptr, 0);           // no samples
    m.process (&one, &one, 1);              // a single sample

    REQUIRE (std::isfinite (m.getIntegratedLufs()));
    REQUIRE (std::isfinite (m.getTruePeakDb()));
    REQUIRE (m.getTruePeakDb() <= 0.1f);    // one sample of DC must not overshoot
}

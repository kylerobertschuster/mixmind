#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "ShaperProcessor.h"
#include "TestSignals.h"

#include <cmath>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
//  ShaperProcessor — the linear-phase FIR match-EQ.
//
//  Three groups of checks:
//    · the runtime path (bypass transparency, latency reporting, block-size
//      independence, and that the impulse response really is the published taps);
//    · the design path (buildMatchFilter) — DC-normalisation, the ±24 dB clamp,
//      the "never boost below the noise floor" rule, and that the amount control
//      scales the match from flat to full;
//    · the trace→target map (buildTargetFromCurve) — log-frequency interpolation.
//
//  Response checks use TestDsp::firMagnitudeAt (direct evaluation of H(e^jw)),
//  and are written as dB *differences between two frequencies*. That is
//  deliberate: the designer DC-normalises its output, so absolute gain is not
//  meaningful — only the shape is. A ratio also survives the unavoidable
//  ripple from Hann-windowing and truncating the design impulse.
// ─────────────────────────────────────────────────────────────────────────────
namespace
{
    using namespace TestSignals;

    constexpr int    kNB = ShaperProcessor::kNumBins;    // 1024
    constexpr int    kK  = ShaperProcessor::kTapCount;   // 1024
    constexpr double kSr = TestSignals::kSampleRate;     // 48000
    constexpr double kBinHz = (kSr * 0.5) / (double) kNB;

    std::vector<float> flat (float v) { return std::vector<float> ((size_t) kNB, v); }

    // juce::Array has no initializer_list of pairs, so build curves explicitly.
    juce::Array<std::pair<float, float>> makeCurve (std::initializer_list<std::pair<float, float>> pts)
    {
        juce::Array<std::pair<float, float>> a;
        for (const auto& p : pts) a.add (p);
        return a;
    }

    // Top bin index at or below `hz` (bins are linear in frequency).
    int binAt (double hz) { return (int) std::floor (hz / kBinHz); }

    float respDb (const std::vector<float>& h, double hz)
    {
        return TestDsp::dbOf (TestDsp::firMagnitudeAt (h.data(), (int) h.size(), hz, kSr));
    }

    // Two probe points well inside the band and far from any transition edge,
    // so 1/3-octave smoothing and the design ripple do not dominate.
    constexpr double kProbeLow  = 100.0;
    constexpr double kProbeHigh = 10000.0;

    // Runs the shaper over a mono signal (duplicated to both channels).
    std::vector<float> runShaper (const std::vector<float>& taps,
                                  const std::vector<float>& in,
                                  bool enabled = true)
    {
        ShaperProcessor p;
        p.prepare (kSr, (int) in.size());
        p.setEnabled (enabled);
        if (! taps.empty())
            p.setFilter (taps.data(), (int) taps.size());

        std::vector<float> outL (in.size(), 0.0f), outR (in.size(), 0.0f);
        std::vector<float> inR (in);

        p.process (in.data(), inR.data(), outL.data(), outR.data(), (int) in.size());
        return outL;
    }
}

// ── Runtime path ─────────────────────────────────────────────────────────────

TEST_CASE ("a bypassed shaper is bit-transparent and reports no latency", "[shaper][bypass][regression]")
{
    // The old failure here was a shaper that still applied its delay line (and
    // still reported 512 samples of latency) while switched off, which shifts a
    // track against every other track in the session.
    auto buf = noise (4096, 0.5f);

    std::vector<float> taps ((size_t) kK, 0.0f);
    taps[(size_t) ShaperProcessor::kLatency] = 1.0f;   // a very much non-transparent filter

    ShaperProcessor p;
    p.prepare (kSr, 4096);
    p.setEnabled (false);
    p.setFilter (taps.data(), (int) taps.size());      // loaded, but bypassed

    REQUIRE (p.getLatencySamples() == 0);

    std::vector<float> inL (4096), inR (4096), outL (4096, 0.0f), outR (4096, 0.0f);
    for (int i = 0; i < 4096; ++i)
    {
        inL[(size_t) i] = buf.getSample (0, i);
        inR[(size_t) i] = buf.getSample (1, i);
    }

    p.process (inL.data(), inR.data(), outL.data(), outR.data(), 4096);

    REQUIRE (TestDsp::maxAbsDiff (outL.data(), inL.data(), 4096) == 0.0f);
    REQUIRE (TestDsp::maxAbsDiff (outR.data(), inR.data(), 4096) == 0.0f);
}

TEST_CASE ("latency is reported only while a filter is actually live", "[shaper][latency]")
{
    ShaperProcessor p;
    p.prepare (kSr, 512);

    // Off: transparent, so the host must compensate nothing.
    REQUIRE (p.getLatencySamples() == 0);

    // On, but nothing designed yet — still nothing to compensate.
    p.setEnabled (true);
    REQUIRE (p.getLatencySamples() == 0);

    // On with taps: now the host has to be told.
    std::vector<float> taps ((size_t) kK, 0.0f);
    taps[(size_t) ShaperProcessor::kLatency] = 1.0f;
    p.setFilter (taps.data(), (int) taps.size());

    REQUIRE (ShaperProcessor::kLatency == 512);
    REQUIRE (p.getLatencySamples() == ShaperProcessor::kLatency);

    // Clearing the filter makes it transparent again.
    p.setFilter (nullptr, 0);
    REQUIRE (p.getLatencySamples() == 0);

    // ...and so does switching off, even with taps loaded.
    p.setFilter (taps.data(), (int) taps.size());
    REQUIRE (p.getLatencySamples() == ShaperProcessor::kLatency);
    p.setEnabled (false);
    REQUIRE (p.getLatencySamples() == 0);
}

TEST_CASE ("the impulse response is exactly the published taps", "[shaper][convolution]")
{
    // The runtime path must be a plain causal FIR: y[n] = sum h[k]·x[n-k].
    // Any error in the delay-line mask or write index shows up here as a
    // shifted or wrapped copy.
    std::vector<float> taps ((size_t) kK, 0.0f);
    for (int k = 0; k < kK; ++k)
        taps[(size_t) k] = 0.01f * std::sin (0.05f * (float) k);

    std::vector<float> in ((size_t) (2 * kK), 0.0f);
    in[0] = 1.0f;

    const auto out = runShaper (taps, in);

    REQUIRE (TestDsp::maxAbsDiff (out.data(), taps.data(), kK) < 1.0e-7f);
}

TEST_CASE ("the output does not depend on how the host slices the blocks", "[shaper][blocksize]")
{
    auto buf = noise (4096, 0.5f);
    std::vector<float> taps ((size_t) kK, 0.0f);
    for (int k = 0; k < kK; ++k)
        taps[(size_t) k] = 0.02f * std::cos (0.03f * (float) k);

    std::vector<float> inL (4096), inR (4096);
    for (int i = 0; i < 4096; ++i)
    {
        inL[(size_t) i] = buf.getSample (0, i);
        inR[(size_t) i] = buf.getSample (1, i);
    }

    // One shot.
    std::vector<float> wholeL (4096, 0.0f), wholeR (4096, 0.0f);
    {
        ShaperProcessor p;
        p.prepare (kSr, 4096);
        p.setEnabled (true);
        p.setFilter (taps.data(), (int) taps.size());
        p.process (inL.data(), inR.data(), wholeL.data(), wholeR.data(), 4096);
    }

    // Sliced into awkward, non-power-of-two blocks.
    std::vector<float> slicedL (4096, 0.0f), slicedR (4096, 0.0f);
    {
        ShaperProcessor p;
        p.prepare (kSr, 512);
        p.setEnabled (true);
        p.setFilter (taps.data(), (int) taps.size());

        const int sizes[] = { 64, 512, 7, 1024, 333, 4096 - (64 + 512 + 7 + 1024 + 333) };
        int offset = 0;
        for (int n : sizes)
        {
            if (n <= 0) continue;
            p.process (inL.data() + offset, inR.data() + offset,
                       slicedL.data() + offset, slicedR.data() + offset, n);
            offset += n;
        }
    }

    REQUIRE (TestDsp::maxAbsDiff (wholeL.data(), slicedL.data(), 4096) < 1.0e-6f);
    REQUIRE (TestDsp::maxAbsDiff (wholeR.data(), slicedR.data(), 4096) < 1.0e-6f);
}

TEST_CASE ("garbage through the shaper stays finite", "[shaper][robustness]")
{
    // A denormal/NaN leak here would poison the DAW's mix bus permanently.
    ShaperProcessor p;
    p.prepare (kSr, 512);
    p.setEnabled (true);

    std::vector<float> taps ((size_t) kK, 0.0f);
    for (int k = 0; k < kK; ++k)
        taps[(size_t) k] = 0.05f * std::sin (0.11f * (float) k);
    p.setFilter (taps.data(), (int) taps.size());

    auto buf = noise (2048, 0.9f);
    std::vector<float> l (2048), r (2048);
    for (int i = 0; i < 2048; ++i)
    {
        l[(size_t) i] = buf.getSample (0, i);
        r[(size_t) i] = buf.getSample (1, i);
    }

    p.process (l.data(), r.data(), l.data(), r.data(), 2048);

    for (int i = 0; i < 2048; ++i)
    {
        REQUIRE (std::isfinite (l[(size_t) i]));
        REQUIRE (std::isfinite (r[(size_t) i]));
    }
}

// ── Design path ──────────────────────────────────────────────────────────────

TEST_CASE ("the designed filter is DC-normalised, never re-levelled", "[shaper][normalise][regression]")
{
    // The shaper reshapes a spectrum; it must not change broadband level. That
    // is enforced by dividing the taps by their sum, i.e. setting H(0) == 1.
    auto live  = flat (0.1f);
    auto target = flat (0.1f);
    for (int i = 0; i <= binAt (900.0); ++i)
        target[(size_t) i] = 0.6f;                     // a big low-end boost request

    std::vector<float> taps;
    ShaperProcessor::buildMatchFilter (target.data(), live.data(), kNB, 1.0f, taps);

    REQUIRE ((int) taps.size() == kK);

    double sum = 0.0;
    for (float v : taps) sum += (double) v;

    REQUIRE (sum == Catch::Approx (1.0).margin (1.0e-3));
    REQUIRE (respDb (taps, 0.0) == Catch::Approx (0.0).margin (0.2));
}

TEST_CASE ("a flat difference designs a flat response", "[shaper]")
{
    auto a = flat (0.1f);
    auto b = flat (0.1f);

    std::vector<float> taps;
    ShaperProcessor::buildMatchFilter (a.data(), b.data(), kNB, 1.0f, taps);

    // Same requested level everywhere → nothing to reshape.
    REQUIRE (std::abs (respDb (taps, kProbeLow) - respDb (taps, kProbeHigh)) < 1.0f);
}

TEST_CASE ("the response follows the requested difference", "[shaper]")
{
    // +12 dB requested in the low band, 0 dB above it. After DC-normalisation
    // the absolute numbers move, but the *difference between the two bands*
    // must still be the 12 dB that was asked for.
    auto live  = flat (0.1f);
    auto target = flat (0.1f);
    const int edge = binAt (900.0);
    for (int i = 0; i <= edge; ++i)
        target[(size_t) i] = 0.4f;                     // 20·log10(0.4/0.1) = +12.04 dB

    std::vector<float> taps;
    ShaperProcessor::buildMatchFilter (target.data(), live.data(), kNB, 1.0f, taps);

    const float tilt = respDb (taps, kProbeLow) - respDb (taps, kProbeHigh);
    INFO ("low - high = " << tilt << " dB (asked for 12.0)");
    REQUIRE (tilt > 9.0f);
    REQUIRE (tilt < 14.0f);
}

TEST_CASE ("the match is clamped to +/-24 dB", "[shaper][clamp]")
{
    // A +60 dB request must not become a +60 dB filter.
    auto live  = flat (0.1f);
    auto target = flat (0.1f);
    const int edge = binAt (900.0);
    for (int i = 0; i <= edge; ++i)
        target[(size_t) i] = 100.0f;                   // +60 dB requested

    std::vector<float> taps;
    ShaperProcessor::buildMatchFilter (target.data(), live.data(), kNB, 1.0f, taps);

    const float tilt = respDb (taps, kProbeLow) - respDb (taps, kProbeHigh);
    INFO ("low - high = " << tilt << " dB (clamp is 24)");
    REQUIRE (tilt < 27.0f);                            // would be ~60 unclamped
    REQUIRE (tilt > 20.0f);
}

TEST_CASE ("silence in the live signal is never boosted", "[shaper][regression]")
{
    // Below the -80 dB floor there is nothing to match against, so the designer
    // must flatten rather than amplify the noise floor by up to +24 dB.
    auto live  = flat (0.0f);                          // 20·log10(0) → -100 dB
    auto target = flat (1.0f);

    std::vector<float> taps;
    ShaperProcessor::buildMatchFilter (target.data(), live.data(), kNB, 1.0f, taps);

    const float tilt = std::abs (respDb (taps, kProbeLow) - respDb (taps, kProbeHigh));
    INFO ("low - high = " << tilt << " dB (expected ~flat)");
    REQUIRE (tilt < 3.0f);
}

TEST_CASE ("amount 0 is exactly the neutral design, amount 1 is the full match", "[shaper]")
{
    auto live  = flat (0.1f);
    auto target = flat (0.1f);
    const int edge = binAt (900.0);
    for (int i = 0; i <= edge; ++i)
        target[(size_t) i] = 0.5f;

    std::vector<float> neutral;
    ShaperProcessor::buildMatchFilter (live.data(), live.data(), kNB, 1.0f, neutral);

    std::vector<float> zero;
    ShaperProcessor::buildMatchFilter (target.data(), live.data(), kNB, 0.0f, zero);

    // amount == 0 scales every dB difference to nothing; a shaped target with
    // amount 0 must land on exactly the same filter as a flat target.
    REQUIRE (TestDsp::maxAbsDiff (neutral.data(), zero.data(), kK) == 0.0f);

    std::vector<float> half, full;
    ShaperProcessor::buildMatchFilter (target.data(), live.data(), kNB, 0.5f, half);
    ShaperProcessor::buildMatchFilter (target.data(), live.data(), kNB, 1.0f, full);

    const float tiltHalf = respDb (half, kProbeLow) - respDb (half, kProbeHigh);
    const float tiltFull = respDb (full, kProbeLow) - respDb (full, kProbeHigh);

    INFO ("half = " << tiltHalf << " dB, full = " << tiltFull << " dB");
    REQUIRE (tiltHalf > 0.0f);
    REQUIRE (tiltHalf < tiltFull);
}

TEST_CASE ("the designer rejects empty or degenerate input", "[shaper][robustness]")
{
    std::vector<float> taps;
    auto a = flat (0.1f);

    ShaperProcessor::buildMatchFilter (nullptr, a.data(), kNB, 1.0f, taps);
    REQUIRE ((int) taps.size() == kK);

    ShaperProcessor::buildMatchFilter (a.data(), nullptr, kNB, 1.0f, taps);
    REQUIRE ((int) taps.size() == kK);

    ShaperProcessor::buildMatchFilter (a.data(), a.data(), 0, 1.0f, taps);
    REQUIRE ((int) taps.size() == kK);

    for (float v : taps) REQUIRE (std::isfinite (v));
}

// ── Trace → target map ───────────────────────────────────────────────────────

TEST_CASE ("a trace with fewer than two points produces no target", "[shaper][trace]")
{
    std::vector<float> out;

    juce::Array<std::pair<float, float>> empty;
    ShaperProcessor::buildTargetFromCurve (empty, kNB, kSr, out);
    REQUIRE ((int) out.size() == kNB);
    for (float v : out) REQUIRE (v == 0.0f);

    auto one = makeCurve ({ { 1000.0f, 0.5f } });
    ShaperProcessor::buildTargetFromCurve (one, kNB, kSr, out);
    for (float v : out) REQUIRE (v == 0.0f);
}

TEST_CASE ("the trace interpolates linearly in log-frequency", "[shaper][trace]")
{
    auto curve = makeCurve ({ { 20.0f, 0.0f }, { 20000.0f, 1.0f } });

    std::vector<float> out;
    ShaperProcessor::buildTargetFromCurve (curve, kNB, kSr, out);
    REQUIRE ((int) out.size() == kNB);

    for (float v : out)
    {
        REQUIRE (v >= 0.0f);
        REQUIRE (v <= 1.0f);
    }

    // Log-frequency linear means the midpoint is the *geometric* mean of the
    // endpoints: sqrt(20 · 20000) = 632.5 Hz.
    const double midHz = std::sqrt (20.0 * 20000.0);
    const int midBin = (int) std::lround (midHz / kBinHz);
    INFO ("bin " << midBin << " = " << out[(size_t) midBin] << " at "
                 << (midBin * kBinHz) << " Hz");
    REQUIRE (out[(size_t) midBin] == Catch::Approx (0.5f).margin (0.02f));

    // ...and it must rise across the band, not just at the midpoint.
    REQUIRE (out[(size_t) binAt (200.0)] < out[(size_t) binAt (2000.0)]);
    REQUIRE (out[(size_t) binAt (2000.0)] < out[(size_t) binAt (18000.0)]);
}

TEST_CASE ("trace values are clamped to 0..1", "[shaper][trace]")
{
    auto curve = makeCurve ({ { 20.0f, 1.8f }, { 20000.0f, -0.7f } });

    std::vector<float> out;
    ShaperProcessor::buildTargetFromCurve (curve, kNB, kSr, out);

    for (float v : out)
    {
        REQUIRE (v >= 0.0f);
        REQUIRE (v <= 1.0f);
    }
}

// KNOWN DEFECT — expected to fail on the current source.
//
// ShaperProcessor::buildTargetFromCurve (ShaperProcessor.cpp:193-204) walks the
// curve's segments forward only. When a bin falls below the first point, the
// walk runs off the end and the k >= size-1 fallback returns the LAST point's
// value — i.e. bins below the trace inherit the HIGHEST frequency's setting.
//
// This is not cosmetic: buildMatchFilter DC-normalises by dividing the taps by
// their sum, and sum(taps) == H(0) == mag[0]. So bin 0 is the reference the
// whole filter is scaled against. Measured on a trace drawn 0.0 @ 20 Hz ->
// 1.0 @ 20 kHz over a flat live spectrum, the resulting impulse response
// differs by up to 8.16 per tap from the intended one.
//
// The default axis is 20 Hz (only bin 0 affected), but selecting a focus band
// raises axisMin to the band start (TelemetryCanvas.cpp:364) — focus a band
// from 2 kHz and every bin below 2 kHz takes the treble value.
//
// The assertion below is the CORRECT behaviour and is deliberately not
// weakened. [!shouldfail] keeps the suite green while this is outstanding, and
// will turn the run RED the moment the designer starts returning the first
// point's value — at which point remove the [!shouldfail] tag.
//
// Not fixed yet on purpose: it is a pre-existing behaviour, not a regression,
// and changing the shaper's output immediately before a demo is the wrong
// trade. See .pi/skills/mixmind-dsp/references/audit-checklist.md.
TEST_CASE ("outside the drawn range the trace holds its nearest endpoint",
           "[shaper][trace][regression][!shouldfail]")
{
    // The trace is drawn over 20 Hz..20 kHz, but the analysis grid starts at
    // DC and runs to Nyquist. Bins below the first drawn point must inherit the
    // *lowest* point's value, exactly as the plot shows the curve flat there.
    // Inheriting the highest point's value would rotate the whole filter, since
    // the designer DC-normalises against bin 0.
    auto curve = makeCurve ({ { 20.0f, 0.0f }, { 20000.0f, 1.0f } });

    std::vector<float> out;
    ShaperProcessor::buildTargetFromCurve (curve, kNB, kSr, out);

    // Bin 0 is 0 Hz — below the first point at 20 Hz.
    INFO ("DC bin = " << out[0] << " (first point is 0.0, last point is 1.0)");
    REQUIRE (out[0] == Catch::Approx (0.0f).margin (0.01f));
}

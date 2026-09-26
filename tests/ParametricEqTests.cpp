#include "ParametricEq.h"
#include "TestSignals.h"
#include <complex>

using namespace TestSignals;

namespace
{
    using Band = ParametricEq::Band;
    using Type = ParametricEq::Type;
    using Placement = ParametricEq::Placement;

    Band band (Type t, float hz, float gainDb, float q, int slope = 12, Placement p = Placement::stereo)
    {
        Band b;
        b.on = true; b.type = t; b.freq = hz; b.gainDb = gainDb; b.q = q; b.slope = slope; b.placement = p;
        return b;
    }

    // Largest |digital − analog| (dB) over 1/12-octave points from 20 Hz to
    // maxHz. Points where the analog response is below −60 dB are skipped: a
    // 48 dB/oct cut is −100 dB there and a fraction of a dB of difference at
    // that depth is not a property anyone can hear.
    double worstError (const Band& b, double fs, double maxHz)
    {
        double worst = 0.0;
        for (double f = 20.0; f <= maxHz; f *= std::pow (2.0, 1.0 / 12.0))
        {
            const double analog = ParametricEq::analogResponseDb (b, f);
            if (analog < -60.0) continue;
            worst = juce::jmax (worst, std::abs (ParametricEq::responseDb (b, f, fs) - analog));
        }
        return worst;
    }

    // Classic bilinear (RBJ) peaking EQ, for comparison.
    double rbjBellDb (double f0, double gainDb, double q, double fs, double f)
    {
        const double A = std::pow (10.0, gainDb / 40.0), w0 = 2.0 * juce::MathConstants<double>::pi * f0 / fs;
        const double alpha = std::sin (w0) / (2.0 * q), c = std::cos (w0);
        const double b0 = 1 + alpha * A, b1 = -2 * c, b2 = 1 - alpha * A;
        const double a0 = 1 + alpha / A, a1 = -2 * c, a2 = 1 - alpha / A;
        const auto z1 = std::polar (1.0, -2.0 * juce::MathConstants<double>::pi * f / fs), z2 = z1 * z1;
        return 20.0 * std::log10 (std::abs ((b0 + b1 * z1 + b2 * z2) / (a0 + a1 * z1 + a2 * z2)));
    }

    // Steady-state gain (dB) of the EQ on a sine, L = R (or L = −R for side).
    double measuredGainDb (ParametricEq& eq, double fs, double hz, bool sideSignal = false)
    {
        auto l = sine (fs, hz, 0.1, 1.0);
        auto r = l;
        if (sideSignal) for (auto& v : r) v = -v;
        const auto in = l;
        for (size_t i = 0; i < l.size(); i += 256)
            eq.process (l.data() + i, r.data() + i, (int) juce::jmin ((size_t) 256, l.size() - i));
        const size_t skip = l.size() / 2;
        return rmsDb (l.data() + skip, l.size() - skip) - rmsDb (in.data() + skip, in.size() - skip);
    }
}

class ParametricEqTests : public juce::UnitTest
{
public:
    ParametricEqTests() : juce::UnitTest ("ParametricEq (matched biquads)", "Equalizer") {}

    void runTest() override
    {
        beginTest ("Matched bells follow the analog prototype up to 0.45 fs, where bilinear bells cramp");
        {
            for (double fs : { 44100.0, 48000.0, 96000.0 })
                for (float f0 : { 100.0f, 1000.0f, 5000.0f, 12000.0f, 16000.0f })
                    for (float g : { -12.0f, 12.0f })
                        for (float q : { 0.7f, 2.0f, 6.0f })
                        {
                            const auto b = band (Type::bell, f0, g, q);
                            expectLessThan (worstError (b, fs, 0.45 * fs), 0.75,
                                            juce::String (f0) + " Hz " + juce::String (g) + " dB Q" + juce::String (q)
                                            + " @ " + juce::String (fs));
                            expectWithinAbsoluteError (ParametricEq::responseDb (b, f0, fs), (double) g, 0.01);
                        }

            // At 16 kHz / 44.1 kHz the bilinear bell is visibly narrower a
            // sixth of an octave above the centre (18 kHz); the matched one is not.
            const double f = 16000.0 * std::pow (2.0, 1.0 / 6.0);
            const auto b = band (Type::bell, 16000.0f, 12.0f, 2.0f);
            const double analog = ParametricEq::analogResponseDb (b, f);
            expectGreaterThan (std::abs (rbjBellDb (16000.0, 12.0, 2.0, 44100.0, f) - analog), 2.0);
            expectLessThan (std::abs (ParametricEq::responseDb (b, f, 44100.0) - analog), 0.75);
        }

        beginTest ("Shelves (boost and cut) follow the analog prototype");
        {
            for (auto t : { Type::lowShelf, Type::highShelf })
                for (float f0 : { 80.0f, 1000.0f, 8000.0f, 15000.0f })
                    for (float g : { -12.0f, -3.0f, 6.0f, 12.0f })
                    {
                        const auto b = band (t, f0, g, 0.7071f);
                        expectLessThan (worstError (b, 48000.0, 0.45 * 48000.0), 0.75,
                                        ParametricEq::typeNames()[(int) t] + " " + juce::String (f0) + " Hz " + juce::String (g) + " dB");
                    }
        }

        beginTest ("Cuts: 12/24/48 dB/oct slopes, -3 dB at the corner, match the analog prototype");
        {
            for (int slope : { 12, 24, 48 })
            {
                const auto lc = band (Type::lowCut, 200.0f, 0.0f, 0.7071f, slope);
                const auto hc = band (Type::highCut, 4000.0f, 0.0f, 0.7071f, slope);
                expectWithinAbsoluteError (ParametricEq::responseDb (lc, 200.0, 48000.0), -3.01, 0.05);
                expectWithinAbsoluteError (ParametricEq::responseDb (hc, 4000.0, 48000.0), -3.01, 0.05);

                // Two octaves out the slope is (almost) fully developed.
                const double lcSlope = ParametricEq::responseDb (lc, 100.0, 48000.0) - ParametricEq::responseDb (lc, 50.0, 48000.0);
                expectWithinAbsoluteError (lcSlope, (double) slope, 0.5, juce::String (slope) + " dB/oct");

                expectLessThan (worstError (lc, 48000.0, 0.45 * 48000.0), 0.75);
                expectLessThan (worstError (hc, 48000.0, 0.45 * 48000.0), 0.75);
            }
        }

        beginTest ("Notch nulls its frequency");
        {
            const auto b = band (Type::notch, 1000.0f, 0.0f, 4.0f);
            expectLessThan (ParametricEq::responseDb (b, 1000.0, 48000.0), -60.0);
            expectWithinAbsoluteError (ParametricEq::responseDb (b, 100.0, 48000.0), 0.0, 0.1);
        }

        beginTest ("Every designed section is stable");
        {
            juce::Random rng (42);
            ParametricEq::Biquad s[ParametricEq::kMaxSections];
            for (int i = 0; i < 2000; ++i)
            {
                Band b;
                b.type   = (Type) rng.nextInt (6);
                b.freq   = ParametricEq::kMinHz * std::pow (ParametricEq::kMaxHz / ParametricEq::kMinHz, rng.nextFloat());
                b.gainDb = (rng.nextFloat() * 2.0f - 1.0f) * ParametricEq::kMaxGainDb;
                b.q      = ParametricEq::kMinQ * std::pow (ParametricEq::kMaxQ / ParametricEq::kMinQ, rng.nextFloat());
                b.slope  = ParametricEq::slopeFromIndex (rng.nextInt (3));
                const double fs = rng.nextBool() ? 44100.0 : 96000.0;

                const int n = ParametricEq::design (b, fs, s);
                for (int k = 0; k < n; ++k)
                {
                    // Poles of z² + a1 z + a2 inside the unit circle.
                    const std::complex<double> disc = std::sqrt (std::complex<double> (s[k].a1 * s[k].a1 - 4.0 * s[k].a2, 0.0));
                    const double r = juce::jmax (std::abs ((-s[k].a1 + disc) * 0.5), std::abs ((-s[k].a1 - disc) * 0.5));
                    if (r >= 1.0)
                        expectLessThan (r, 1.0, ParametricEq::typeNames()[(int) b.type] + " " + juce::String (b.freq)
                                                + " Hz Q" + juce::String (b.q) + " " + juce::String (b.gainDb) + " dB");
                }
            }
            expect (true);
        }

        beginTest ("Processing: measured gain matches the design; bands cascade");
        {
            ParametricEq eq;
            eq.setBand (0, band (Type::bell, 1000.0f, 9.0f, 1.0f));
            eq.setBand (1, band (Type::highShelf, 6000.0f, -6.0f, 0.7071f));
            eq.prepare (48000.0, 256);

            for (double f : { 100.0, 1000.0, 3000.0, 12000.0 })
            {
                const double expected = ParametricEq::responseDb (band (Type::bell, 1000.0f, 9.0f, 1.0f), f, 48000.0)
                                      + ParametricEq::responseDb (band (Type::highShelf, 6000.0f, -6.0f, 0.7071f), f, 48000.0);
                expectWithinAbsoluteError (measuredGainDb (eq, 48000.0, f), expected, 0.05, juce::String (f) + " Hz");
            }
        }

        beginTest ("Placement: Mid bands only touch L+R, Side bands only L-R; mono takes Stereo + Mid");
        {
            for (auto place : { Placement::mid, Placement::side, Placement::stereo })
            {
                ParametricEq eq;
                eq.setBand (0, band (Type::bell, 1000.0f, 12.0f, 1.0f, 12, place));
                eq.prepare (48000.0, 256);
                const double onMid  = measuredGainDb (eq, 48000.0, 1000.0, false);
                const double onSide = measuredGainDb (eq, 48000.0, 1000.0, true);
                expectWithinAbsoluteError (onMid,  place != Placement::side ? 12.0 : 0.0, 0.05);
                expectWithinAbsoluteError (onSide, place != Placement::mid  ? 12.0 : 0.0, 0.05);

                ParametricEq mono;
                mono.setBand (0, band (Type::bell, 1000.0f, 12.0f, 1.0f, 12, place));
                mono.prepare (48000.0, 256);
                auto x = sine (48000.0, 1000.0, 0.1, 1.0);
                const auto in = x;
                mono.process (x.data(), nullptr, (int) x.size());
                const double g = rmsDb (x.data() + 24000, 24000) - rmsDb (in.data() + 24000, 24000);
                expectWithinAbsoluteError (g, place != Placement::side ? 12.0 : 0.0, 0.05);
            }
        }

        beginTest ("All bands off: bit-exact pass-through");
        {
            ParametricEq eq;
            eq.prepare (48000.0, 256);
            expect (! eq.isActive());
            auto l = whiteNoise (48000.0, 0.2, -6.0, 1), r = whiteNoise (48000.0, 0.2, -6.0, 2);
            const auto inL = l, inR = r;
            eq.process (l.data(), r.data(), (int) l.size());
            bool exact = true;
            for (size_t i = 0; i < l.size(); ++i)
                exact = exact && juce::exactlyEqual (l[i], inL[i]) && juce::exactlyEqual (r[i], inR[i]);
            expect (exact);
        }

        beginTest ("Switching bands on/off, type and placement mid-stream does not click");
        {
            ParametricEq eq;
            eq.prepare (48000.0, 256);
            auto l = sine (48000.0, 440.0, 0.25, 2.0);
            auto r = l;

            auto b = band (Type::bell, 440.0f, 12.0f, 1.0f);
            const double w = 2.0 * juce::MathConstants<double>::pi * 440.0 / 48000.0;
            const float boosted = 0.25f * juce::Decibels::decibelsToGain (12.0f);

            for (size_t i = 0; i < l.size(); i += 256)
            {
                if (i == 12032) eq.setBand (0, b);                                    // on
                if (i == 30208) { b.type = Type::lowShelf; eq.setBand (0, b); }      // type
                if (i == 48128) { b.placement = Placement::mid; eq.setBand (0, b); } // placement
                if (i == 66048) { b.freq = 3000.0f; eq.setBand (0, b); }             // frequency jump (glides)
                if (i == 84224) { b.on = false; eq.setBand (0, b); }                  // off
                eq.process (l.data() + i, r.data() + i, (int) juce::jmin ((size_t) 256, l.size() - i));
            }

            // A click is a discontinuity: it shows up in the second difference,
            // which for a sine of amplitude A is at most A·ω² (a 0.01 step alone
            // would be ~4× this bound). Smooth level changes do not trip it.
            float peak = 0.0f, d2 = 0.0f;
            for (size_t i = 2; i < l.size(); ++i)
            {
                peak = juce::jmax (peak, std::abs (l[i]));
                d2   = juce::jmax (d2, std::abs (l[i] - 2.0f * l[i - 1] + l[i - 2]));
            }
            expectLessThan (d2, 1.5f * peak * (float) (w * w));

            // Sweeping a resonant band re-radiates its stored energy (as an
            // analog one does); even a 2.8-octave jump swells less than 1 dB.
            expectLessThan (peak, boosted * juce::Decibels::decibelsToGain (1.0f));
            expect (! eq.isActive());
        }
    }
};

static ParametricEqTests parametricEqTests;

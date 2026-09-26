#include "LoudnessMeter.h"
#include "TestSignals.h"

using namespace TestSignals;

namespace
{
    void feed (LoudnessMeter& m, const std::vector<float>& l, const std::vector<float>* r, int block = 512)
    {
        for (size_t i = 0; i < l.size(); i += (size_t) block)
        {
            const int n = (int) juce::jmin ((size_t) block, l.size() - i);
            m.process (l.data() + i, r != nullptr ? r->data() + i : nullptr, n);
        }
    }

    // EBU Tech 3341 stereo test signal: the same 1 kHz tone on L and R, as a
    // sequence of (level dBFS, seconds) segments.
    std::vector<float> tone1k (double fs, std::initializer_list<std::pair<double, double>> segments)
    {
        std::vector<float> out;
        double phase = 0.0;
        for (const auto& [db, secs] : segments)
        {
            const auto seg = sine (fs, 1000.0, dbToGain (db), secs, phase);
            phase += 2.0 * juce::MathConstants<double>::pi * 1000.0 / fs * (double) seg.size();
            out.insert (out.end(), seg.begin(), seg.end());
        }
        return out;
    }
}

class LoudnessMeterTests : public juce::UnitTest
{
public:
    LoudnessMeterTests() : juce::UnitTest ("LoudnessMeter (BS.1770 / EBU R128)", "Metering") {}

    void runTest() override
    {
        beginTest ("K-weighting coefficients match the BS.1770 table at 48 kHz");
        {
            LoudnessMeter::Biquad s, h;
            LoudnessMeter::designKWeighting (48000.0, s, h);
            expectWithinAbsoluteError (s.b0,  1.53512485958697, 1e-9);
            expectWithinAbsoluteError (s.b1, -2.69169618940638, 1e-9);
            expectWithinAbsoluteError (s.b2,  1.19839281085285, 1e-9);
            expectWithinAbsoluteError (s.a1, -1.69065929318241, 1e-9);
            expectWithinAbsoluteError (s.a2,  0.73248077421585, 1e-9);
            expectWithinAbsoluteError (h.b0,  1.0, 1e-12);
            expectWithinAbsoluteError (h.b1, -2.0, 1e-12);
            expectWithinAbsoluteError (h.b2,  1.0, 1e-12);
            expectWithinAbsoluteError (h.a1, -1.99004745483398, 1e-9);
            expectWithinAbsoluteError (h.a2,  0.99007225036621, 1e-9);
        }

        for (double fs : { 48000.0, 44100.0 })
        {
            const juce::String at = " @ " + juce::String (fs / 1000.0, 1) + " kHz";

            beginTest ("EBU 3341 case 1: -23 dBFS 1 kHz stereo reads -23.0 LUFS (M, S, I)" + at);
            {
                LoudnessMeter m; m.prepare (fs, 512);
                const auto x = tone1k (fs, { { -23.0, 20.0 } });
                feed (m, x, &x);
                expectWithinAbsoluteError (m.getIntegratedLufs(), -23.0f, 0.1f);
                expectWithinAbsoluteError (m.getShortTermLufs(),  -23.0f, 0.1f);
                expectWithinAbsoluteError (m.getMomentaryLufs(),  -23.0f, 0.1f);
            }

            beginTest ("EBU 3341 case 2: -33 dBFS reads -33.0 LUFS" + at);
            {
                LoudnessMeter m; m.prepare (fs, 512);
                const auto x = tone1k (fs, { { -33.0, 20.0 } });
                feed (m, x, &x);
                expectWithinAbsoluteError (m.getIntegratedLufs(), -33.0f, 0.1f);
            }

            beginTest ("EBU 3341 case 3: relative gate (-36/-23/-36) -> -23.0" + at);
            {
                LoudnessMeter m; m.prepare (fs, 512);
                const auto x = tone1k (fs, { { -36.0, 10.0 }, { -23.0, 60.0 }, { -36.0, 10.0 } });
                feed (m, x, &x);
                expectWithinAbsoluteError (m.getIntegratedLufs(), -23.0f, 0.1f);
            }

            beginTest ("EBU 3341 case 4: absolute + relative gate (-72/-36/-23/-36/-72) -> -23.0" + at);
            {
                LoudnessMeter m; m.prepare (fs, 512);
                const auto x = tone1k (fs, { { -72.0, 10.0 }, { -36.0, 10.0 }, { -23.0, 60.0 },
                                             { -36.0, 10.0 }, { -72.0, 10.0 } });
                feed (m, x, &x);
                expectWithinAbsoluteError (m.getIntegratedLufs(), -23.0f, 0.1f);
            }

            beginTest ("EBU 3341 case 5: energy-domain gating (-26/-20/-26) -> -23.0" + at);
            {
                LoudnessMeter m; m.prepare (fs, 512);
                const auto x = tone1k (fs, { { -26.0, 20.0 }, { -20.0, 20.1 }, { -26.0, 20.0 } });
                feed (m, x, &x);
                expectWithinAbsoluteError (m.getIntegratedLufs(), -23.0f, 0.1f);
            }
        }

        beginTest ("Block size does not change the reading");
        {
            const auto x = tone1k (48000.0, { { -26.0, 20.0 }, { -20.0, 20.1 }, { -26.0, 20.0 } });
            float readings[3] {};
            const int blocks[3] = { 64, 441, 4096 };
            for (int i = 0; i < 3; ++i)
            {
                LoudnessMeter m; m.prepare (48000.0, blocks[i]);
                feed (m, x, &x, blocks[i]);
                readings[i] = m.getIntegratedLufs();
            }
            expectWithinAbsoluteError (readings[1], readings[0], 0.01f);
            expectWithinAbsoluteError (readings[2], readings[0], 0.01f);
        }

        beginTest ("Channel energies are summed: mono programme reads 3.01 dB below dual-mono");
        {
            const auto x = tone1k (48000.0, { { -20.0, 10.0 } });
            LoudnessMeter mono;   mono.prepare (48000.0, 512);   feed (mono, x, nullptr);
            LoudnessMeter stereo; stereo.prepare (48000.0, 512); feed (stereo, x, &x);
            expectWithinAbsoluteError (stereo.getIntegratedLufs() - mono.getIntegratedLufs(), 3.01f, 0.02f);
        }

        beginTest ("Out-of-phase stereo: same loudness as in-phase, true peak still measured");
        {
            const auto x = tone1k (48000.0, { { -6.0, 5.0 } });
            std::vector<float> inv (x.size());
            for (size_t i = 0; i < x.size(); ++i) inv[i] = -x[i];

            LoudnessMeter inPhase;  inPhase.prepare (48000.0, 512);  feed (inPhase, x, &x);
            LoudnessMeter outPhase; outPhase.prepare (48000.0, 512); feed (outPhase, x, &inv);

            expectWithinAbsoluteError (outPhase.getIntegratedLufs(), inPhase.getIntegratedLufs(), 0.01f);
            expectWithinAbsoluteError (outPhase.getTruePeakDb(), -6.0f, 0.2f);
        }

        beginTest ("True peak catches inter-sample peaks");
        {
            // fs/4 sine at 45°: every sample sits at ±0.707·A, the waveform peaks at A.
            const auto x = sine (48000.0, 12000.0, 0.5, 2.0, juce::MathConstants<double>::pi / 4.0);
            float samplePeak = 0.0f;
            for (float v : x) samplePeak = juce::jmax (samplePeak, std::abs (v));

            LoudnessMeter m; m.prepare (48000.0, 512); feed (m, x, &x);
            expectWithinAbsoluteError (juce::Decibels::gainToDecibels (samplePeak), -9.03f, 0.05f);
            expectWithinAbsoluteError (m.getTruePeakDb(), -6.02f, 0.5f);
        }

        beginTest ("Crest factor of a sine is 3 dB");
        {
            const auto x = tone1k (48000.0, { { -12.0, 5.0 } });
            LoudnessMeter m; m.prepare (48000.0, 512); feed (m, x, &x);
            expectWithinAbsoluteError (m.getCrestFactorDb(), 3.01f, 0.2f);
            expectWithinAbsoluteError (m.getRmsDb(), -15.01f, 0.1f);
        }

        beginTest ("Silence and the absolute gate leave integrated unset");
        {
            LoudnessMeter m; m.prepare (48000.0, 512);
            const std::vector<float> silence (48000 * 5, 0.0f);
            feed (m, silence, &silence);
            expectEquals (m.getIntegratedLufs(), LoudnessMeter::kSilenceDb);

            const auto quiet = tone1k (48000.0, { { -75.0, 5.0 } });   // ≈ −75 LUFS: below −70
            feed (m, quiet, &quiet);
            expectEquals (m.getIntegratedLufs(), LoudnessMeter::kSilenceDb);
        }

        beginTest ("requestReset() restarts the measurement on the next block");
        {
            LoudnessMeter m; m.prepare (48000.0, 512);
            const auto loud  = tone1k (48000.0, { { -10.0, 5.0 } });
            const auto quiet = tone1k (48000.0, { { -30.0, 5.0 } });
            feed (m, loud, &loud);
            m.requestReset();
            feed (m, quiet, &quiet);
            expectWithinAbsoluteError (m.getIntegratedLufs(), -30.0f, 0.1f);
            expectWithinAbsoluteError (m.getTruePeakDb(), -30.0f, 0.2f);
        }
    }
};

static LoudnessMeterTests loudnessMeterTests;

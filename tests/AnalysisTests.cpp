#include "PluginProcessor.h"
#include "MeasurementHistory.h"
#include "SnapshotBuffer.h"
#include "TestSignals.h"
#include <atomic>
#include <memory>

using namespace TestSignals;

namespace
{
    // Feeds l/r through an analyzer in blocks (blockSize 0 = random 1…3000).
    void feed (AudioAnalyzer& a, const std::vector<float>& l, const std::vector<float>& r, int blockSize, juce::int64 seed = 1)
    {
        juce::Random rng (seed);
        juce::AudioBuffer<float> buf (2, 4096);
        for (size_t i = 0; i < l.size();)
        {
            const int n = (int) juce::jmin ((size_t) (blockSize > 0 ? blockSize : 1 + rng.nextInt (3000)), l.size() - i);
            buf.setSize (2, n, false, false, true);
            buf.copyFrom (0, 0, l.data() + i, n);
            buf.copyFrom (1, 0, r.data() + i, n);
            a.process (buf);
            i += (size_t) n;
        }
    }

    std::vector<MeasurementFrame> drainAll (AudioAnalyzer& a)
    {
        std::vector<MeasurementFrame> out;
        MeasurementFrame f;
        while (a.popFrame (f)) out.push_back (f);
        return out;
    }

    std::vector<float> scaled (std::vector<float> x, float g) { for (auto& v : x) v *= g; return x; }
    std::vector<float> negated (std::vector<float> x) { for (auto& v : x) v = -v; return x; }

    // Stereo 1 kHz sine at `dbfs` peak, both channels (EBU Tech 3342 test signals).
    std::vector<float> tone (double fs, double dbfs, double seconds)
    {
        return sine (fs, 1000.0, std::pow (10.0, dbfs / 20.0), seconds);
    }

    // A sine at every octave-band centre up to Nyquist, 0.05 each.
    std::vector<float> octaveTones (double fs, double seconds)
    {
        std::vector<float> x ((size_t) (fs * seconds), 0.0f);
        for (int b = 0; b < MeasurementFrame::kBands; ++b)
        {
            const double hz = MeasurementFrame::bandCentreHz (b);
            if (hz >= fs * 0.45) break;
            const auto s = sine (fs, hz, 0.05, seconds);
            for (size_t i = 0; i < x.size(); ++i) x[i] += s[i];
        }
        return x;
    }

    struct Payload { juce::uint32 seq; std::array<juce::uint32, 2048> v; };
}

class AnalysisTests : public juce::UnitTest
{
public:
    AnalysisTests() : juce::UnitTest ("Analysis engine (snapshots, frames, history)", "Analysis") {}

    void runTest() override
    {
        constexpr double kFs = 48000.0;
        const int step = (int) std::lround (kFs * 0.1);

        beginTest ("SnapshotBuffer: the reader only ever sees whole values, newest last");
        {
            auto buf = std::make_unique<SnapshotBuffer<Payload>>();
            constexpr juce::uint32 kWrites = 200000;
            std::atomic<bool> done { false };
            juce::WaitableEvent finished;
            juce::Thread::launch ([&]
            {
                for (juce::uint32 seq = 1; seq <= kWrites; ++seq)
                {
                    auto& p = buf->back();
                    p.seq = seq;
                    p.v.fill (seq);
                    buf->publish();
                }
                done = true;
                finished.signal();
            });

            juce::uint32 last = 0, torn = 0, backwards = 0, reads = 0;
            for (;;)
            {
                const bool writerDone = done.load();
                const auto& p = buf->latest();
                ++reads;
                if (p.seq < last) ++backwards;
                last = p.seq;
                for (auto v : p.v) if (v != p.seq) { ++torn; break; }
                if (writerDone && p.seq == kWrites) break;
            }
            expect (finished.wait (10000));
            logMessage ("snapshot reads: " + juce::String (reads));
            expectEquals ((int) torn, 0);
            expectEquals ((int) backwards, 0);
            expectEquals ((int) last, (int) kWrites);
        }

        beginTest ("Frames tile the audio in exact 100 ms steps, whatever the host block size");
        {
            const auto l = whiteNoise (kFs, 12.0, -18.0, 11), r = whiteNoise (kFs, 12.0, -21.0, 12);
            std::vector<std::vector<MeasurementFrame>> runs;
            for (int bs : { 64, 441, 4096, 0 })
            {
                AudioAnalyzer a;
                a.prepare (kFs, 4096);
                feed (a, l, r, bs, 7);
                runs.push_back (drainAll (a));
            }
            const auto& ref = runs.front();
            expectEquals ((int) ref.size(), (int) l.size() / step);
            bool contiguous = true, fullSteps = true;
            for (size_t i = 0; i < ref.size(); ++i)
            {
                contiguous = contiguous && ref[i].index == (juce::uint32) i;
                fullSteps  = fullSteps && ref[i].samples == step;
            }
            expect (contiguous && fullSteps);

            int mismatches = 0;
            const auto near = [] (double a, double b) { return std::abs (a - b) <= 1.0e-9 * juce::jmax (1.0, std::abs (a)); };
            for (size_t k = 1; k < runs.size(); ++k)
            {
                expectEquals (runs[k].size(), ref.size());
                for (size_t i = 0; i < juce::jmin (ref.size(), runs[k].size()); ++i)
                {
                    const auto& x = ref[i]; const auto& y = runs[k][i];
                    bool same = near (x.kEnergy, y.kEnergy) && near (x.energy, y.energy) && juce::exactlyEqual (x.truePeak, y.truePeak)
                             && near (x.ll, y.ll) && near (x.rr, y.rr) && near (x.lr, y.lr) && x.fftFrames == y.fftFrames;
                    for (int b = 0; b < MeasurementFrame::kBands; ++b)
                        same = same && juce::exactlyEqual (x.bandMag[(size_t) b], y.bandMag[(size_t) b])
                                    && juce::exactlyEqual (x.bandSidePow[(size_t) b], y.bandSidePow[(size_t) b]);
                    if (! same) ++mismatches;
                }
            }
            expectEquals (mismatches, 0);
        }

        beginTest ("Window loudness and true peak agree with the BS.1770 meter exactly");
        {
            for (double fs : { 44100.0, 48000.0 })
            {
                const int stepN = (int) std::lround (fs * 0.1);
                const double seconds = 120.0 * stepN / fs;   // a whole number of steps
                const auto l = concat ({ pinkNoise (fs, seconds * 0.5, 0.3, 21), scaled (pinkNoise (fs, seconds * 0.5, 0.3, 22), 0.25f) });
                const auto r = concat ({ pinkNoise (fs, seconds * 0.5, 0.3, 23), scaled (pinkNoise (fs, seconds * 0.5, 0.3, 24), 0.25f) });
                AudioAnalyzer a;
                a.prepare (fs, 512);
                feed (a, l, r, 0, 3);
                MeasurementHistory h (60.0);
                h.drain (a);
                const auto s = h.statsSince (0);
                expectEquals (s.frames, 120);
                expectEquals (s.missingFrames, 0);
                expectWithinAbsoluteError (s.integratedLufs, a.getLufs(), 0.02f);
                expectWithinAbsoluteError (s.truePeakDb, a.getTruePeakDb(), 1.0e-5f);
                expectWithinAbsoluteError (s.seconds, seconds, 1.0e-9);
            }
        }

        beginTest ("Loudness range matches EBU Tech 3342 test cases 1-4");
        {
            struct Case { const char* name; std::vector<std::pair<double, double>> parts; float lra; };
            const std::vector<Case> cases {
                { "1: -20 / -30 dBFS",           { { -20, 20 }, { -30, 20 } }, 10.0f },
                { "2: -20 / -15 dBFS",           { { -20, 20 }, { -15, 20 } }, 5.0f },
                { "3: -40 / -20 dBFS",           { { -40, 20 }, { -20, 20 } }, 20.0f },
                { "4: -50 / -35 / -20 / -35 / -50", { { -50, 20 }, { -35, 20 }, { -20, 20 }, { -35, 20 }, { -50, 20 } }, 15.0f },
            };
            for (const auto& c : cases)
            {
                std::vector<float> x;
                for (const auto& p : c.parts)
                {
                    const auto t = tone (kFs, p.first, p.second);
                    x.insert (x.end(), t.begin(), t.end());
                }
                AudioAnalyzer a;
                a.prepare (kFs, 1024);
                MeasurementHistory h (120.0);
                for (size_t i = 0; i < x.size(); i += (size_t) kFs)   // drain every second, like the design loop
                {
                    const std::vector<float> chunk (x.begin() + (long) i, x.begin() + (long) juce::jmin (x.size(), i + (size_t) kFs));
                    feed (a, chunk, chunk, 1024);
                    h.drain (a);
                }
                const auto s = h.statsSince (0);
                logMessage (juce::String ("case ") + c.name + ": LRA " + juce::String (s.loudnessRangeLu, 2) + " LU");
                expectWithinAbsoluteError (s.loudnessRangeLu, c.lra, 1.0f, c.name);
            }
        }

        beginTest ("Silence is gated out of RMS, bands and stereo; true peak still counts");
        {
            const auto noise = whiteNoise (kFs, 4.0, -20.0, 31);
            const std::vector<float> silence ((size_t) (kFs * 4.0), 0.0f);
            const auto withGap = concat ({ noise, silence, whiteNoise (kFs, 4.0, -20.0, 32) });
            const auto solid   = concat ({ noise, whiteNoise (kFs, 4.0, -20.0, 32) });

            AudioAnalyzer a, b;
            a.prepare (kFs, 512); b.prepare (kFs, 512);
            feed (a, withGap, withGap, 512);
            feed (b, solid, solid, 512);
            MeasurementHistory ha, hb;
            ha.drain (a); hb.drain (b);
            const auto sa = ha.statsSince (0), sb = hb.statsSince (0);
            expectWithinAbsoluteError (sa.seconds, 12.0, 1.0e-9);
            expectWithinAbsoluteError (sa.gatedSeconds, 8.0, 0.11);
            expectWithinAbsoluteError (sa.rmsDb, -20.0f, 0.1f);
            expectWithinAbsoluteError (sa.rmsDb, sb.rmsDb, 0.01f);   // the silent frames add nothing
            // Integrated loudness is BS.1770's, gaps and all: the 400 ms blocks
            // straddling the edges carry part of the noise and pass both gates,
            // exactly as they do in the meter.
            expectWithinAbsoluteError (sa.integratedLufs, a.getLufs(), 0.02f);
            for (int k = 0; k < MeasurementFrame::kBands; ++k)
                if (sb.bandHasData[(size_t) k])
                    expectWithinAbsoluteError (sa.bandDb[(size_t) k], sb.bandDb[(size_t) k], 0.3f, "band " + juce::String (k));
        }

        beginTest ("Stereo image and mono fold-down: identical, anti-phase and uncorrelated channels");
        {
            const auto n1 = whiteNoise (kFs, 8.0, -18.0, 41), n2 = whiteNoise (kFs, 8.0, -18.0, 42);
            const auto run = [&] (const std::vector<float>& l, const std::vector<float>& r)
            {
                AudioAnalyzer a;
                a.prepare (kFs, 512);
                feed (a, l, r, 512);
                MeasurementHistory h;
                h.drain (a);
                return h.statsSince (0);
            };

            const auto same = run (n1, n1);
            expectWithinAbsoluteError (same.correlation, 1.0f, 1.0e-5f);
            expectWithinAbsoluteError (same.width, 0.0f, 1.0e-3f);
            for (int k = 0; k < MeasurementFrame::kBands; ++k)
                if (same.bandHasData[(size_t) k]) expectWithinAbsoluteError (same.monoLossDb[(size_t) k], 0.0f, 0.01f);

            const auto anti = run (n1, negated (n1));
            expectWithinAbsoluteError (anti.correlation, -1.0f, 1.0e-5f);
            for (int k = 0; k < MeasurementFrame::kBands; ++k)
                if (anti.bandHasData[(size_t) k]) expectEquals (anti.monoLossDb[(size_t) k], MeasurementHistory::kMonoFloorDb);

            const auto wide = run (n1, n2);
            expectWithinAbsoluteError (wide.correlation, 0.0f, 0.03f);
            expectWithinAbsoluteError (wide.width, 1.0f, 0.03f);
            for (int k = 1; k < MeasurementFrame::kBands; ++k)   // band 0 has a single bin: noisier
                if (wide.bandHasData[(size_t) k]) expectWithinAbsoluteError (wide.monoLossDb[(size_t) k], -3.01f, 0.25f, "band " + juce::String (k));
        }

        beginTest ("Octave-band levels equal the long-term spectrum's band means (44.1 / 48 / 96 kHz)");
        {
            for (double fs : { 44100.0, 48000.0, 96000.0 })
            {
                const auto x = octaveTones (fs, 8.0);
                AudioAnalyzer a;
                a.prepare (fs, 512);
                feed (a, x, x, 512);
                MeasurementHistory h;
                h.drain (a);
                const auto s = h.statsSince (0);
                float ref[MeasurementFrame::kBands];
                AudioAnalyzer::bandMagnitudes (a.getSpectra().longTerm, fs, ref);
                for (int k = 0; k < MeasurementFrame::kBands; ++k)
                {
                    const bool bins = AudioAnalyzer::bandBinCount (k, fs) > 0;
                    expect (s.bandHasData[(size_t) k] == bins, "band " + juce::String (k) + " @ " + juce::String (fs));
                    if (! bins || MeasurementFrame::bandCentreHz (k) >= fs * 0.45) continue;
                    expectWithinAbsoluteError (s.bandDb[(size_t) k], 20.0f * std::log10 (ref[k]), 0.1f,
                                               "band " + juce::String (k) + " @ " + juce::String (fs));
                }
            }
        }

        beginTest ("Frames nobody read are dropped, never waited for; blocks never span the gap");
        {
            AudioAnalyzer a;
            a.prepare (kFs, 512);
            const auto x = pinkNoise (kFs, 40.0, 0.3, 51);
            feed (a, x, x, 512);   // 400 frames into a 256-slot queue, nobody draining
            MeasurementHistory h;
            h.drain (a);
            const auto kept = h.latestIndex();
            expect (kept < 399 && kept >= 200);
            const auto more = pinkNoise (kFs, 10.0, 0.3, 52);
            feed (a, more, more, 512);
            h.drain (a);
            const auto s = h.statsSince (0);
            expectEquals (s.frames, (int) kept + 1 + 100);
            expectEquals (s.missingFrames, 500 - s.frames);
            expect (s.integratedLufs > -40.0f && s.integratedLufs < 0.0f);
        }

        beginTest ("prepare() starts a new history; a loudness reset keeps frames whole and aligned");
        {
            AudioAnalyzer a;
            a.prepare (kFs, 512);
            const auto x = whiteNoise (kFs, 3.05, -20.0, 61);
            feed (a, x, x, 512);
            MeasurementHistory h;
            h.drain (a);
            const auto before = h.latestIndex();

            a.resetLoudness();
            const auto y = whiteNoise (kFs, 6.0, -26.0, 62);
            feed (a, y, y, 512);
            std::vector<MeasurementFrame> frames = drainAll (a);
            bool whole = true, contiguous = true;
            for (size_t i = 0; i < frames.size(); ++i)
            {
                whole = whole && frames[i].samples == step;
                contiguous = contiguous && frames[i].index == before + 1 + (juce::uint32) i;
                h.add (frames[i]);
            }
            expect (whole && contiguous);
            expectWithinAbsoluteError (h.statsSince (before + 1).integratedLufs, a.getLufs(), 0.02f);

            a.prepare (kFs, 512);
            const auto z = whiteNoise (kFs, 2.0, -20.0, 63);
            feed (a, z, z, 512);
            h.drain (a);
            expectWithinAbsoluteError (h.secondsAvailable(), 2.0, 1.0e-9);
            expectEquals ((int) h.latestIndex(), 19);
        }

        if (juce::SystemStats::getEnvironmentVariable ("MIXMIND_BENCH", {}).isNotEmpty())
        {
            beginTest ("Analyzer cost (opt-in: MIXMIND_BENCH=1)");
            const auto l = pinkNoise (kFs, 20.0, 0.3, 81), r = pinkNoise (kFs, 20.0, 0.3, 82);
            AudioAnalyzer a;
            a.prepare (kFs, 512);
            MeasurementHistory h;
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            feed (a, l, r, 512);
            const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
            h.drain (a);
            logMessage ("AudioAnalyzer: " + juce::String (100.0 * ms / 20000.0, 2) + " % of one core (stereo, 48 kHz, 512-sample blocks)");
            expect (h.secondsAvailable() > 19.0);
        }

        beginTest ("The processor keeps the output's history; the audio thread runs while it is read");
        {
            MixMindProcessor p;
            p.setRateAndBufferSizeDetails (kFs, 512);
            p.prepareToPlay (kFs, 512);

            const auto l = pinkNoise (kFs, 12.0, 0.3, 71), r = pinkNoise (kFs, 12.0, 0.3, 72);
            juce::AudioBuffer<float> buf (2, 512);
            juce::MidiBuffer midi;
            std::atomic<bool> stop { false };
            juce::WaitableEvent audioDone;
            juce::Thread::launch ([&]
            {
                for (size_t i = 0; i + 512 <= l.size() && ! stop.load(); i += 512)
                {
                    buf.copyFrom (0, 0, l.data() + i, 512);
                    buf.copyFrom (1, 0, r.data() + i, 512);
                    p.processBlock (buf, midi);
                }
                audioDone.signal();
            });

            // Meanwhile the message thread reads spectra and drains frames.
            int odd = 0, lastFrames = 0, reads = 0;
            while (! audioDone.wait (0))
            {
                const auto& s = p.outputAnalyzer.getSpectra();
                if (s.longTermFrames < lastFrames) ++odd;
                lastFrames = s.longTermFrames;
                for (int i = 0; i < AudioAnalyzer::numBins; ++i)
                    if (! std::isfinite (s.display[i]) || s.display[i] > 4.0f) { ++odd; break; }
                ++reads;
                juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
            }
            juce::MessageManager::getInstance()->runDispatchLoopUntil (100);   // last design-loop drains
            logMessage ("spectra reads during playback: " + juce::String (reads));
            expectEquals (odd, 0);

            const auto st = p.getMeasurementHistory().statsForLast (10.0);
            expectWithinAbsoluteError (st.seconds, 10.0, 0.051);
            expectEquals (st.missingFrames, 0);
            expectWithinAbsoluteError (st.integratedLufs, p.outputAnalyzer.getLufs(), 0.15f);
            expect (p.getMeasurementHistory().secondsAvailable() > 11.0);
        }
    }
};

static AnalysisTests analysisTests;

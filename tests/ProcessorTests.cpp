#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "TestSignals.h"
#include <algorithm>
#include <limits>

using namespace TestSignals;

namespace
{
    constexpr double kFs = 48000.0;

    struct Harness
    {
        Harness()
        {
            p.setRateAndBufferSizeDetails (kFs, 512);
            p.prepareToPlay (kFs, 512);
        }

        void setParam (const char* id, float normalised)
        {
            p.parameters.getParameter (id)->setValueNotifyingHost (normalised);
        }

        // Plays l/r through processBlock; returns the output left channel.
        std::vector<float> play (const std::vector<float>& l, const std::vector<float>& r, bool bypassed = false)
        {
            std::vector<float> out (l.size(), 0.0f);
            juce::AudioBuffer<float> buf (2, 512);
            juce::MidiBuffer midi;
            for (size_t i = 0; i < l.size(); i += 512)
            {
                const int n = (int) juce::jmin ((size_t) 512, l.size() - i);
                buf.setSize (2, n, false, false, true);
                buf.copyFrom (0, 0, l.data() + i, n);
                buf.copyFrom (1, 0, r.data() + i, n);
                if (bypassed) p.processBlockBypassed (buf, midi);
                else          p.processBlock (buf, midi);
                std::copy (buf.getReadPointer (0), buf.getReadPointer (0) + n, out.begin() + (long) i);
            }
            return out;
        }

        MixMindProcessor p;
    };

    bool pumpUntil (const std::function<bool()>& done, int timeoutMs)
    {
        const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
        while (! done())
        {
            if (juce::Time::getMillisecondCounter() > end) return false;
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        }
        return true;
    }

    void setBand (MixMindProcessor& p, int band, const ParametricEq::Band& b)
    {
        const auto& bp = p.getBandParams (band);
        const auto set = [] (juce::RangedAudioParameter* prm, float v) { prm->setValueNotifyingHost (prm->convertTo0to1 (v)); };
        set (bp.on, b.on ? 1.0f : 0.0f);
        set (bp.type, (float) (int) b.type);
        set (bp.freq, b.freq);
        set (bp.gain, b.gainDb);
        set (bp.q, b.q);
        set (bp.slope, (float) ParametricEq::indexFromSlope (b.slope));
        set (bp.placement, (float) (int) b.placement);
    }

    float correctionSideAt (const MixMindProcessor& p, double hz)
    {
        const auto& c = p.getCorrectionSideDb();
        const auto i = (size_t) std::lround (hz / (kFs / AudioAnalyzer::fftSize));
        return i < c.size() ? c[i] : 0.0f;
    }

    float correctionAt (const MixMindProcessor& p, double hz)
    {
        const auto& c = p.getCorrectionDb();
        const auto i = (size_t) std::lround (hz / (kFs / AudioAnalyzer::fftSize));
        return i < c.size() ? c[i] : 0.0f;
    }
}

class ProcessorTests : public juce::UnitTest
{
public:
    ProcessorTests() : juce::UnitTest ("MixMindProcessor (reference, state, design loop)", "Processor") {}

    void runTest() override
    {
        beginTest ("Latency is constant; host bypass is a latency-matched pass-through");
        {
            Harness h;
            expectEquals (h.p.getLatencySamples(), ShaperProcessor::kLatency);
            h.setParam ("shapeEnable", 1.0f);
            expectEquals (h.p.getLatencySamples(), ShaperProcessor::kLatency);

            std::vector<float> l (4096, 0.0f);
            l[10] = 1.0f;
            const auto out = h.play (l, l, true);
            expectWithinAbsoluteError (out[10 + ShaperProcessor::kLatency], 1.0f, 1.0e-6f);
        }

        const auto whiteL = whiteNoise (kFs, 6.0, -20.0, 11);
        const auto whiteR = whiteNoise (kFs, 6.0, -20.0, 12);

        beginTest ("Reference loads on the background thread and installs on the message thread");
        {
            TempWav wav ("proc-ref");
            expect (writeWav (wav.file, whiteL, whiteR, kFs));

            Harness h;
            h.p.loadReference (wav.file);
            expect (h.p.getReferenceStatus() == MixMindProcessor::RefStatus::loading);
            expect (pumpUntil ([&] { return h.p.getReferenceStatus() == MixMindProcessor::RefStatus::ready; }, 20000));

            const auto ref = h.p.getReference();
            expect (ref != nullptr);
            if (ref != nullptr) expectEquals (ref->name, juce::String ("proc-ref"));
            expectEquals ((int) h.p.getReferenceBins().size(), AudioAnalyzer::numBins);
        }

        beginTest ("Session state round-trips reference (even if the file is gone), trace and parameters");
        {
            TempWav wav ("proc-state");
            expect (writeWav (wav.file, whiteL, whiteR, kFs));

            juce::MemoryBlock state;
            std::vector<float> originalBins;
            {
                Harness h;
                h.p.loadReference (wav.file);
                expect (pumpUntil ([&] { return h.p.getReferenceStatus() == MixMindProcessor::RefStatus::ready; }, 20000));
                originalBins = h.p.getReferenceBins();

                h.p.setTrace ({ { 5000.0f, -50.0f }, { 100.0f, -40.0f } });   // unsorted on purpose
                h.setParam ("shapeAmount", 0.4f);
                h.setParam ("shapeMode", 1.0f);
                h.setParam ("shapeEnable", 1.0f);
                h.p.getStateInformation (state);
            }

            wav.file.deleteFile();   // the cached analysis must be enough

            Harness h;
            h.p.setStateInformation (state.getData(), (int) state.getSize());

            expect (h.p.getReferenceStatus() == MixMindProcessor::RefStatus::ready);
            const auto ref = h.p.getReference();
            expect (ref != nullptr && ref->name == "proc-state");
            const auto& bins = h.p.getReferenceBins();
            expectEquals (bins.size(), originalBins.size());
            float diff = 0.0f;
            for (size_t i = 0; i < bins.size() && i < originalBins.size(); ++i)
                diff = juce::jmax (diff, std::abs (bins[i] - originalBins[i]));
            expectEquals (diff, 0.0f);

            const auto trace = h.p.getTrace();
            expectEquals ((int) trace.size(), 2);
            if (trace.size() == 2)
            {
                expectEquals (trace[0].hz, 100.0f);
                expectEquals (trace[1].db, -50.0f);
            }

            expectWithinAbsoluteError (h.p.parameters.getRawParameterValue ("shapeAmount")->load(), 0.4f, 1.0e-6f);
            expectEquals (h.p.parameters.getRawParameterValue ("shapeMode")->load(), 1.0f);
            expectEquals (h.p.parameters.getRawParameterValue ("shapeEnable")->load(), 1.0f);
        }

        beginTest ("A tampered session cannot install an implausible reference");
        {
            // Session state comes from project files other people can write.
            TempWav wav ("proc-tamper");
            expect (writeWav (wav.file, whiteL, whiteR, kFs));
            juce::MemoryBlock good;
            {
                Harness h;
                h.p.loadReference (wav.file);
                expect (pumpUntil ([&] { return h.p.getReferenceStatus() == MixMindProcessor::RefStatus::ready; }, 20000));
                h.p.getStateInformation (good);
            }
            wav.file.deleteFile();   // no fallback: only the cached analysis is left

            const auto tampered = [&] (const std::function<void (juce::XmlElement&)>& edit)
            {
                auto xml = juce::AudioProcessor::getXmlFromBinary (good.getData(), (int) good.getSize());
                juce::MemoryBlock out;
                if (xml == nullptr) return out;
                if (auto* ref = xml->getChildByName ("Reference")) edit (*ref);
                juce::AudioProcessor::copyXmlToBinary (*xml, out);
                return out;
            };
            const auto floats = [] (std::vector<float> v)
            {
                juce::MemoryBlock b (v.data(), v.size() * sizeof (float));
                return b.toBase64Encoding();
            };

            const std::vector<std::pair<const char*, std::function<void (juce::XmlElement&)>>> cases {
                // Subnormal: passes "> 0", but rate / fftSize underflows to 0.
                { "subnormal rate", [] (juce::XmlElement& r) { r.setAttribute ("sampleRate", "0.00000000000001e-308"); } },
                { "negative rate",  [] (juce::XmlElement& r) { r.setAttribute ("sampleRate", "-48000"); } },
                { "fftSize that does not match the rate",
                  [&] (juce::XmlElement& r) { r.setAttribute ("fftSize", 64);
                                              r.setAttribute ("spectrum", floats (std::vector<float> (32, 1.0f)));
                                              r.removeAttribute ("sideSpectrum"); } },
                { "non-finite spectrum",
                  [&] (juce::XmlElement& r) { std::vector<float> v (1024, 1.0f); v[3] = std::numeric_limits<float>::quiet_NaN();
                                              r.setAttribute ("spectrum", floats (v)); } },
                { "negative magnitudes",
                  [&] (juce::XmlElement& r) { std::vector<float> v (1024, 1.0f); v[7] = -1.0f;
                                              r.setAttribute ("spectrum", floats (v)); } },
            };
            for (const auto& c : cases)
            {
                const auto state = tampered (c.second);
                expect (state.getSize() > 0);
                Harness h;
                h.p.setStateInformation (state.getData(), (int) state.getSize());
                expect (h.p.getReference() == nullptr, c.first);
                pumpUntil ([] { return false; }, 150);   // design-loop ticks run the reference mapping
                const auto& bins = h.p.getReferenceBins();
                expect (std::all_of (bins.begin(), bins.end(), [] (float v) { return std::isfinite (v); }), c.first);
            }

            // The untouched state still restores.
            Harness ok;
            ok.p.setStateInformation (good.getData(), (int) good.getSize());
            expect (ok.p.getReference() != nullptr);
        }

        beginTest ("Sessions saved before v2 (bare parameter tree) still restore");
        {
            Harness a;
            a.setParam ("shapeAmount", 0.2f);
            juce::MemoryBlock legacy;
            if (auto xml = a.p.parameters.copyState().createXml())
                juce::AudioProcessor::copyXmlToBinary (*xml, legacy);

            Harness b;
            b.p.setStateInformation (legacy.getData(), (int) legacy.getSize());
            expectWithinAbsoluteError (b.p.parameters.getRawParameterValue ("shapeAmount")->load(), 0.2f, 1.0e-6f);
            expect (b.p.getReference() == nullptr);
        }

        beginTest ("A failed load keeps the previous reference and reports why");
        {
            TempWav wav ("proc-keep");
            expect (writeWav (wav.file, whiteL, whiteR, kFs));

            Harness h;
            h.p.loadReference (wav.file);
            expect (pumpUntil ([&] { return h.p.getReferenceStatus() == MixMindProcessor::RefStatus::ready; }, 20000));

            h.p.loadReference (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("nope-mixmind.wav"));
            expect (pumpUntil ([&] { return h.p.getReferenceStatus() == MixMindProcessor::RefStatus::failed; }, 20000));
            expect (h.p.getReference() != nullptr && h.p.getReference()->name == "proc-keep");
            expect (h.p.getReferenceMessage().isNotEmpty());

            h.p.clearReference();
            expect (h.p.getReference() == nullptr);
            expect (h.p.getReferenceStatus() == MixMindProcessor::RefStatus::none);
        }

        beginTest ("AUTO: the correction tilts a pink mix toward a white reference, and is applied");
        {
            TempWav wav ("proc-auto");
            expect (writeWav (wav.file, whiteL, whiteR, kFs));

            Harness h;
            h.p.loadReference (wav.file);
            expect (pumpUntil ([&] { return h.p.getReferenceStatus() == MixMindProcessor::RefStatus::ready; }, 20000));

            const auto pinkL = pinkNoise (kFs, 6.0, 0.02, 21);
            const auto pinkR = pinkNoise (kFs, 6.0, 0.02, 22);
            h.play (pinkL, pinkR);   // builds the live long-term spectrum

            expect (pumpUntil ([&] { return ! h.p.getCorrectionDb().empty(); }, 5000));
            // Pink falls 3 dB/oct relative to white: 250 Hz → 8 kHz is 5 octaves.
            expectGreaterThan (correctionAt (h.p, 8000.0) - correctionAt (h.p, 250.0), 8.0f);

            h.setParam ("shapeEnable", 1.0f);
            pumpUntil ([] { return false; }, 150);   // let the design loop publish
            const auto out = h.play (pinkL, pinkR);

            // Compare against the input delayed by the (constant) latency.
            const size_t start = 48000, n = pinkL.size() - start;
            double diff = 0.0;
            for (size_t i = start; i < pinkL.size(); ++i)
                diff += std::abs (out[i] - pinkL[i - (size_t) ShaperProcessor::kLatency]);
            expectGreaterThan (diff / (double) n, 1.0e-4);
            const double inDb  = rmsDb (pinkL.data() + start, n);
            const double outDb = rmsDb (out.data() + start, n);
            expectWithinAbsoluteError (outDb, inDb, 4.0);   // tone moves, level roughly holds
        }

        beginTest ("MANUAL: the correction follows the trace; clearing it removes the correction");
        {
            Harness h;
            h.setParam ("shapeMode", 1.0f);
            h.play (whiteL, whiteR);
            h.p.setTrace ({ { 100.0f, -70.0f }, { 10000.0f, -50.0f } });

            expect (pumpUntil ([&] { return ! h.p.getCorrectionDb().empty(); }, 5000));
            expectGreaterThan (correctionAt (h.p, 5000.0) - correctionAt (h.p, 300.0), 8.0f);

            h.p.setTrace ({});
            expect (pumpUntil ([&] { return h.p.getCorrectionDb().empty(); }, 5000));
        }

        beginTest ("Mid/side matching: the side follows the reference's width, the mid its tone");
        {
            // Reference: independent L/R noise (side as loud as mid). Mix: the
            // same noise spectrum but narrow (side 20 dB below mid).
            TempWav wav ("proc-wide");
            expect (writeWav (wav.file, whiteL, whiteR, kFs));

            const auto a = whiteNoise (kFs, 6.0, -20.0, 41), b = whiteNoise (kFs, 6.0, -40.0, 42);
            std::vector<float> mixL (a.size()), mixR (a.size());
            for (size_t i = 0; i < a.size(); ++i) { mixL[i] = a[i] + b[i]; mixR[i] = a[i] - b[i]; }

            Harness h;
            h.setParam ("shapeAmount", 1.0f);
            h.p.loadReference (wav.file);
            expect (pumpUntil ([&] { return h.p.getReferenceStatus() == MixMindProcessor::RefStatus::ready; }, 20000));
            expect (h.p.getReference() != nullptr && h.p.getReference()->hasSide());
            h.play (mixL, mixR);

            // Linked: one curve, no side curve.
            expect (pumpUntil ([&] { return ! h.p.getCorrectionDb().empty(); }, 5000));
            expect (h.p.getCorrectionSideDb().empty());

            h.setParam ("matchStereo", 1.0f);
            expect (pumpUntil ([&] { return ! h.p.getCorrectionSideDb().empty(); }, 5000));

            // Mid is white in both → ~flat. The mix's side sits 20 dB under its
            // mid; the reference's side is level with its mid (sum and
            // difference of independent noise) → the side comes up ~20 dB.
            for (double f : { 200.0, 1000.0, 5000.0 })
            {
                expectWithinAbsoluteError (correctionAt (h.p, f), 0.0f, 1.5f, "mid @ " + juce::String (f));
                expectWithinAbsoluteError (correctionSideAt (h.p, f), 20.0f, 2.5f, "side @ " + juce::String (f));
            }

            // Applied: the output gets wider (side up relative to mid).
            h.setParam ("shapeEnable", 1.0f);
            pumpUntil ([] { return false; }, 150);
            std::vector<float> outL, outR;
            {
                juce::AudioBuffer<float> buf (2, 512);
                juce::MidiBuffer midi;
                for (size_t i = 0; i + 512 <= mixL.size(); i += 512)
                {
                    buf.copyFrom (0, 0, mixL.data() + i, 512);
                    buf.copyFrom (1, 0, mixR.data() + i, 512);
                    h.p.processBlock (buf, midi);
                    outL.insert (outL.end(), buf.getReadPointer (0), buf.getReadPointer (0) + 512);
                    outR.insert (outR.end(), buf.getReadPointer (1), buf.getReadPointer (1) + 512);
                }
            }
            double mid = 0, side = 0;
            for (size_t i = outL.size() / 2; i < outL.size(); ++i)
            {
                const double m = 0.5 * (outL[i] + outR[i]), sd = 0.5 * (outL[i] - outR[i]);
                mid += m * m; side += sd * sd;
            }
            expectWithinAbsoluteError (10.0 * std::log10 (side / mid), 0.0, 3.0);   // was -20 dB
        }

        beginTest ("Band parameters drive the EQ; the output analyzer shows the result");
        {
            Harness h;
            ParametricEq::Band b;
            b.on = true; b.type = ParametricEq::Type::bell; b.freq = 1000.0f; b.gainDb = 12.0f; b.q = 1.0f;
            setBand (h.p, 2, b);

            const auto x = sine (kFs, 1000.0, 0.05, 2.0);
            const auto out = h.play (x, x);
            const size_t start = 48000, n = x.size() - start;
            expectWithinAbsoluteError (rmsDb (out.data() + start, n) - rmsDb (x.data() + start - ShaperProcessor::kLatency, n),
                                       12.0, 0.1);
            expectGreaterThan (h.p.outputAnalyzer.getShortTermLufs() - h.p.audioAnalyzer.getShortTermLufs(), 10.0f);

            // Host bypass skips the bands (a pure latency delay).
            const auto bypassed = h.play (x, x, true);
            expectWithinAbsoluteError (rmsDb (bypassed.data() + start, n), rmsDb (x.data() + start, n), 0.05);
        }

        beginTest ("Non-finite input never reaches the output or the match spectrum");
        {
            Harness h;
            ParametricEq::Band b;
            b.on = true; b.type = ParametricEq::Type::bell; b.freq = 200.0f; b.gainDb = 6.0f; b.q = 2.0f;
            setBand (h.p, 0, b);
            h.setParam ("shapeEnable", 1.0f);

            auto x = whiteNoise (kFs, 1.0, -20.0, 77);
            x[1000] = std::numeric_limits<float>::quiet_NaN();
            x[20000] = std::numeric_limits<float>::infinity();
            const auto out = h.play (x, x);

            bool finite = true;
            for (float v : out) finite = finite && std::isfinite (v);
            expect (finite);
            const float* lt = h.p.audioAnalyzer.getLongTermBins();
            for (int i = 0; i < AudioAnalyzer::numBins; ++i) finite = finite && std::isfinite (lt[i]);
            expect (finite);
            expect (std::isfinite (h.p.outputAnalyzer.getShortTermLufs()));
        }

        beginTest ("Editor opens, syncs with the processor and closes cleanly");
        {
            // A darker (pink-ish, -3 dB/oct) reference against a white-noise mix.
            const auto refL = pinkNoise (kFs, 6.0, 0.05, 31);
            const auto refR = pinkNoise (kFs, 6.0, 0.05, 32);
            TempWav wav ("Reference Master");
            expect (writeWav (wav.file, refL, refR, kFs));

            Harness h;
            h.p.loadReference (wav.file);
            h.play (whiteL, whiteR);
            expect (pumpUntil ([&] { return h.p.getReferenceStatus() == MixMindProcessor::RefStatus::ready; }, 20000));
            h.setParam ("shapeEnable", 1.0f);

            std::unique_ptr<juce::AudioProcessorEditor> editor (h.p.createEditor());
            expect (editor != nullptr);
            if (auto* mm = dynamic_cast<MixMindEditor*> (editor.get()))
                expect (mm->rendersWithOpenGL(), "the editor should render through OpenGL");
            else
                expect (false, "createEditor() should return a MixMindEditor");
            pumpUntil ([] { return false; }, 700);   // let the display smoothing settle

            // Optional visual check: MIXMIND_SNAPSHOT_DIR=/some/dir writes PNGs.
            const auto dir = juce::SystemStats::getEnvironmentVariable ("MIXMIND_SNAPSHOT_DIR", {});
            const auto snap = [&] (const juce::String& shot)
            {
                juce::Image img (juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);
                {
                    juce::Graphics g (img);
                    editor->paintEntireComponent (g, true);
                }
                if (dir.isEmpty()) return;
                const auto file = juce::File (dir).getChildFile (shot + ".png");
                file.deleteFile();
                juce::FileOutputStream out (file);
                juce::PNGImageFormat().writeImageToStream (img, out);
            };

            snap ("mixmind-auto");

            h.setParam ("shapeMode", 1.0f);
            h.p.setTrace ({ { 60.0f, -48.0f }, { 400.0f, -52.0f }, { 3000.0f, -58.0f }, { 12000.0f, -64.0f } });
            pumpUntil ([] { return false; }, 400);
            snap ("mixmind-manual-trace");

            // EQ nodes + mid/side match.
            h.p.setTrace ({});
            h.setParam ("shapeMode", 0.0f);
            h.setParam ("matchStereo", 1.0f);
            ParametricEq::Band lc; lc.on = true; lc.type = ParametricEq::Type::lowCut; lc.freq = 40.0f; lc.q = 0.7071f; lc.slope = 24;
            ParametricEq::Band bell; bell.on = true; bell.freq = 320.0f; bell.gainDb = -4.5f; bell.q = 1.4f;
            ParametricEq::Band air; air.on = true; air.type = ParametricEq::Type::highShelf; air.freq = 9000.0f; air.gainDb = 3.0f; air.q = 0.7071f;
            air.placement = ParametricEq::Placement::side;
            setBand (h.p, 0, lc); setBand (h.p, 1, bell); setBand (h.p, 2, air);
            h.play (whiteL, whiteR);
            pumpUntil ([] { return false; }, 700);
            snap ("mixmind-eq-ms");

            // Opt-in paint timing (software renderer), with the EQ, match
            // curves, reference and live spectrum all on screen.
            if (dir.isNotEmpty())
            {
                for (auto size : { juce::Point<int> (1080, 680), juce::Point<int> (1800, 1100) })
                {
                    editor->setSize (size.x, size.y);
                    juce::Image img (juce::Image::ARGB, size.x, size.y, true);
                    const auto t0 = juce::Time::getMillisecondCounterHiRes();
                    constexpr int frames = 60;
                    for (int f = 0; f < frames; ++f)
                    {
                        juce::Graphics g (img);
                        editor->paintEntireComponent (g, true);
                    }
                    const double whole = (juce::Time::getMillisecondCounterHiRes() - t0) / frames;

                    // The analyzer canvas on its own (the rest is the header).
                    double canvasMs = 0.0;
                    for (auto* child : editor->getChildren())
                        if (auto* canvas = dynamic_cast<TelemetryCanvas*> (child))
                        {
                            juce::Image ci (juce::Image::ARGB, canvas->getWidth(), canvas->getHeight(), true);
                            const auto c0 = juce::Time::getMillisecondCounterHiRes();
                            for (int f = 0; f < frames; ++f)
                            {
                                juce::Graphics g (ci);
                                canvas->paintEntireComponent (g, true);
                            }
                            canvasMs = (juce::Time::getMillisecondCounterHiRes() - c0) / frames;
                        }
                    logMessage ("paint " + juce::String (size.x) + "x" + juce::String (size.y) + ": "
                                + juce::String (whole, 2) + " ms/frame (canvas " + juce::String (canvasMs, 2) + ")");
                }
                editor->setSize (1080, 680);
            }

            editor->setSize (960, 420);
            snap ("mixmind-min-size");
            editor->setSize (1800, 1100);
            snap ("mixmind-max-size");

            editor.reset();
            expect (h.p.getReference() != nullptr);   // closing the UI keeps the reference
        }
    }
};

static ProcessorTests processorTests;

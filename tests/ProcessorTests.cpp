#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "TestSignals.h"

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

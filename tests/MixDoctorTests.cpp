#include "PluginProcessor.h"
#include "MixDoctor.h"
#include "TestSignals.h"

using namespace TestSignals;

namespace
{
    using Stats = MeasurementHistory::Stats;
    using MixDoctor::Severity;
    using MixDoctor::Confidence;

    // A stats window with signal and flat bands at `bandDb`.
    Stats baseStats (double gatedSeconds = 25.0, float bandDb = -30.0f)
    {
        Stats s;
        s.frames = (int) (gatedSeconds * 10.0);
        s.seconds = s.gatedSeconds = gatedSeconds;
        s.integratedLufs = -14.0f;
        s.truePeakDb = -1.5f;
        s.rmsDb = -13.0f;
        s.crestDb = 11.5f;
        s.correlation = 0.9f;
        s.width = 0.3f;
        for (int b = 0; b < MeasurementFrame::kBands; ++b)
        {
            s.bandHasData[(size_t) b] = true;
            s.bandDb[(size_t) b] = bandDb;
            s.monoLossDb[(size_t) b] = -0.2f;
        }
        return s;
    }

    MixDoctor::ReferenceProfile baseReference (float bandDb = -30.0f)
    {
        MixDoctor::ReferenceProfile r;
        r.name = "Ref";
        for (int b = 0; b < MeasurementFrame::kBands; ++b)
        {
            r.bandHasData[(size_t) b] = true;
            r.bandDb[(size_t) b] = bandDb;
        }
        r.lufs = -14.0f;
        r.truePeakDb = -1.0f;
        r.crestDb = 11.0f;
        return r;
    }

    const MixDoctor::Finding* find (const MixDoctor::Report& r, const juce::String& id)
    {
        for (const auto& f : r.findings)
            if (f.id == id) return &f;
        return nullptr;
    }
    // A pointer into a temporary report would dangle: use findCopy for those.
    const MixDoctor::Finding* find (const MixDoctor::Report&&, const juce::String&) = delete;

    // The finding, copied out of a report that is not kept (id empty if absent).
    MixDoctor::Finding findCopy (const MixDoctor::Report& r, const juce::String& id)
    {
        const auto* f = find (r, id);
        return f != nullptr ? *f : MixDoctor::Finding {};
    }

    // Runs l/r through an analyzer and returns the stats of everything heard.
    Stats measure (const std::vector<float>& l, const std::vector<float>& r, double fs)
    {
        AudioAnalyzer a;
        a.prepare (fs, 512);
        MeasurementHistory h;
        juce::AudioBuffer<float> buf (2, 512);
        for (size_t i = 0; i + 512 <= l.size(); i += 512)
        {
            buf.copyFrom (0, 0, l.data() + i, 512);
            buf.copyFrom (1, 0, r.data() + i, 512);
            a.process (buf);
            if ((i / 512) % 64 == 0) h.drain (a);
        }
        h.drain (a);
        return h.statsSince (0);
    }

    // The reference as Mix Doctor sees it: analysed from a WAV, mapped onto the live grid.
    MixDoctor::ReferenceProfile profileOf (const std::vector<float>& l, const std::vector<float>& r, double fs,
                                           std::vector<float>* gridOut = nullptr)
    {
        TempWav wav ("mixdoctor-ref");
        writeWav (wav.file, l, r, fs);
        ReferenceAnalyzer analyzer;
        ReferenceAnalyzer::Result res;
        juce::String err;
        analyzer.analyse (wav.file, res, err);
        std::vector<float> grid ((size_t) AudioAnalyzer::numBins);
        ReferenceAnalyzer::mapToGrid (res, fs, grid.data(), AudioAnalyzer::numBins);
        if (gridOut != nullptr) *gridOut = grid;
        return MixDoctor::profileFromReference (res, grid.data(), fs);
    }

    ParametricEq::Band shelfBand (float gainDb)
    {
        ParametricEq::Band b;
        b.on = true; b.type = ParametricEq::Type::lowShelf; b.freq = 120.0f; b.gainDb = gainDb; b.q = 0.7071f;
        return b;
    }

    // What the tone rule should read for the low end after a low shelf: the
    // shelf's own response applied to the reference's spectrum, then the
    // rule's region means and level matching (22-177 Hz, 177-707 Hz,
    // 0.7-2.8 kHz, 2.8-5.7 kHz, 5.7-22 kHz).
    float predictedBassDeviation (const std::vector<float>& refGrid, double fs, float gainDb)
    {
        std::vector<float> shaped (refGrid.size());
        for (size_t k = 0; k < refGrid.size(); ++k)
            shaped[k] = refGrid[k] * (float) juce::Decibels::decibelsToGain (
                            ParametricEq::responseDb (shelfBand (gainDb), (double) k * fs / AudioAnalyzer::fftSize, fs));
        float ref[MeasurementFrame::kBands], mix[MeasurementFrame::kBands];
        AudioAnalyzer::bandMagnitudes (refGrid.data(), fs, ref);
        AudioAnalyzer::bandMagnitudes (shaped.data(), fs, mix);
        const int regions[5][2] = { { 0, 2 }, { 3, 4 }, { 5, 6 }, { 7, 7 }, { 8, 9 } };
        float dev[5], mean = 0.0f;
        for (int r = 0; r < 5; ++r)
        {
            double m = 0.0, f = 0.0;
            for (int b = regions[r][0]; b <= regions[r][1]; ++b) { m += mix[b]; f += ref[b]; }
            dev[r] = (float) (20.0 * std::log10 (m / f));
            mean += dev[r] / 5.0f;
        }
        return dev[0] - mean;
    }

    // x through one EQ band (the EQ runs stereo: the right side is discarded).
    std::vector<float> filtered (std::vector<float> x, double fs, const ParametricEq::Band& band)
    {
        ParametricEq eq;
        eq.prepare (fs, 512);
        eq.setBand (0, band);
        std::vector<float> right = x;
        for (size_t i = 0; i < x.size(); i += 512)
        {
            const int n = (int) juce::jmin ((size_t) 512, x.size() - i);
            eq.process (x.data() + i, right.data() + i, n);
        }
        return x;
    }

    std::vector<float> lowShelf (std::vector<float> x, double fs, float gainDb)
    {
        return filtered (std::move (x), fs, shelfBand (gainDb));
    }
}

class MixDoctorTests : public juce::UnitTest
{
public:
    MixDoctorTests() : juce::UnitTest ("Mix Doctor (diagnostic rules)", "Diagnostics") {}

    void runTest() override
    {
        constexpr double kFs = 48000.0;

        beginTest ("Nothing is diagnosed from too little signal");
        {
            auto s = baseStats (2.0);
            const auto r = MixDoctor::diagnose (s, nullptr);
            expect (! r.ready && r.findings.empty() && r.notReadyReason.isNotEmpty());
            expect (r.toMarkdown().contains (r.notReadyReason));
            expect (r.notReadyReason.contains ("1 more second "), r.notReadyReason);
            expect (MixDoctor::diagnose (baseStats (0.4), nullptr).notReadyReason.contains ("3 more seconds"));
            expect (MixDoctor::diagnose (baseStats (3.0), nullptr).ready);
        }

        beginTest ("True peak: against the published -1 dBTP ceiling");
        {
            auto s = baseStats();
            s.truePeakDb = 0.4f;
            auto f = findCopy (MixDoctor::diagnose (s, nullptr), "true_peak");
            expect (f.id.isNotEmpty() && f.severity == Severity::high && f.confidence == Confidence::high);
            s.truePeakDb = -0.5f;
            f = findCopy (MixDoctor::diagnose (s, nullptr), "true_peak");
            expect (f.id.isNotEmpty() && f.severity == Severity::medium && f.confidence == Confidence::high);
            s.truePeakDb = -1.2f;
            f = findCopy (MixDoctor::diagnose (s, nullptr), "true_peak");
            expect (f.id.isNotEmpty() && f.severity == Severity::healthy);
            expect (! f.evidence.empty() && juce::exactlyEqual (f.evidence[0].value, -1.2f));
        }

        beginTest ("Mono fold-down of the low end, and a mix that is out of phase");
        {
            const auto lowEnd = [] (float lossDb)
            {
                auto s = baseStats();
                for (int b = 0; b <= 2; ++b) s.monoLossDb[(size_t) b] = lossDb;
                return MixDoctor::diagnose (s, nullptr);
            };
            const auto grade = [&] (float lossDb)
            {
                const auto f = findCopy (lowEnd (lossDb), "mono_low_end");
                expect (f.id.isNotEmpty());
                return f.severity;
            };
            expect (grade (-0.5f) == Severity::healthy);
            expect (grade (-2.0f) == Severity::low);
            expect (grade (-4.0f) == Severity::medium);
            expect (grade (-8.0f) == Severity::high);
            expect (findCopy (lowEnd (-0.5f), "phase").id.isEmpty());

            auto s = baseStats();
            s.correlation = -0.3f;
            const auto f = findCopy (MixDoctor::diagnose (s, nullptr), "phase");
            expect (f.id.isNotEmpty() && f.severity == Severity::high && ! f.potentialCauses.isEmpty());
            // Uncorrelated (very wide) material reads around 0: wide, not out of phase.
            s.correlation = -0.05f;
            expect (findCopy (MixDoctor::diagnose (s, nullptr), "phase").id.isEmpty());
        }

        beginTest ("Tone against the reference is level-matched and graded");
        {
            const auto ref = baseReference();

            auto louder = baseStats (25.0, -20.0f);   // everything 10 dB up: not a tonal problem
            auto r = MixDoctor::diagnose (louder, &ref);
            for (const auto& f : r.findings)
                expect (! f.id.startsWith ("tone_") || f.id == "tone_balanced", f.id);
            expect (find (r, "tone_balanced") != nullptr && find (r, "tone_balanced")->evidence.size() == 5);

            auto bassy = baseStats();
            for (int b = 0; b <= 2; ++b) bassy.bandDb[(size_t) b] += 6.0f;   // +6 dB in 22-177 Hz
            r = MixDoctor::diagnose (bassy, &ref);
            const auto* f = find (r, "tone_bass");
            expect (f != nullptr);
            if (f != nullptr)
            {
                // Level-matched: the region mean (+1.2) comes off, leaving +4.8 -> Medium.
                expectWithinAbsoluteError (f->evidence[0].value, 4.8f, 1.0e-4f);
                expect (f->severity == Severity::medium && f->title.contains ("above"));
                expect (f->action.contains ("5 dB") && f->action.contains ("low shelf"), f->action);
                expect (f->potentialCauses.size() >= 2);   // inferred: causes, not a named culprit (ADR-007/008)
            }
            expect (find (r, "tone_mid") == nullptr);

            auto dark = baseStats();
            for (int b = 8; b <= 9; ++b) dark.bandDb[(size_t) b] -= 9.0f;
            const auto air = findCopy (MixDoctor::diagnose (dark, &ref), "tone_air");
            expect (air.id.isNotEmpty() && air.severity == Severity::high && air.title.contains ("below"));

            // Grades: < 2 healthy, 2-3.5 low, 3.5-6 medium, >= 6 high (after level matching).
            const auto grade = [&] (float lift)
            {
                auto s = baseStats();
                s.bandDb[7] += lift * 5.0f / 4.0f;   // one region (upper mids): its deviation after matching = lift
                const auto g = findCopy (MixDoctor::diagnose (s, &ref), "tone_high_mid");
                return g.id.isEmpty() ? Severity::healthy : g.severity;
            };
            expect (grade (1.9f) == Severity::healthy);
            expect (grade (2.5f) == Severity::low);
            expect (grade (4.0f) == Severity::medium);
            expect (grade (6.5f) == Severity::high);
        }

        beginTest ("Density and loudness against the reference");
        {
            const auto ref = baseReference();   // crest 11 dB, -14 LUFS
            const auto density = [&] (float crest)
            {
                auto s = baseStats();
                s.crestDb = crest;
                const auto f = findCopy (MixDoctor::diagnose (s, &ref), "density");
                expect (f.id.isNotEmpty());
                return f;
            };
            expect (density (11.5f).severity == Severity::healthy);
            expect (density (7.5f).severity == Severity::medium);
            expect (density (4.0f).severity == Severity::high);
            expect (density (18.0f).severity == Severity::low);
            expect (density (4.0f).confidence == Confidence::medium);   // never High: different stretches

            auto s = baseStats();
            s.integratedLufs = -20.5f;
            auto f = findCopy (MixDoctor::diagnose (s, &ref), "loudness");
            expect (f.id.isNotEmpty() && f.severity == Severity::low && f.title.contains ("quieter"));
            s.integratedLufs = -15.0f;
            f = findCopy (MixDoctor::diagnose (s, &ref), "loudness");
            expect (f.id.isNotEmpty() && f.severity == Severity::healthy);
            expect (findCopy (MixDoctor::diagnose (baseStats(), nullptr), "loudness").id.isEmpty());   // no reference, no comparison
        }

        beginTest ("Confidence follows what was heard; lost frames lower it");
        {
            const auto ref = baseReference();
            const auto conf = [&] (double seconds, int missing)
            {
                auto s = baseStats (seconds);
                for (int b = 0; b <= 2; ++b) s.bandDb[(size_t) b] += 8.0f;
                s.missingFrames = missing;
                const auto f = findCopy (MixDoctor::diagnose (s, &ref), "tone_bass");
                expect (f.id.isNotEmpty());
                return f.confidence;
            };
            expect (conf (5.0, 0) == Confidence::low);
            expect (conf (10.0, 0) == Confidence::medium);
            expect (conf (25.0, 0) == Confidence::high);
            expect (conf (25.0, 100) == Confidence::medium);   // 100 of 350 frames lost
        }

        beginTest ("Findings are ranked, carry evidence, and the report is deterministic");
        {
            const auto ref = baseReference();
            auto s = baseStats (12.0);
            s.truePeakDb = 0.3f;
            for (int b = 0; b <= 2; ++b) { s.bandDb[(size_t) b] += 7.0f; s.monoLossDb[(size_t) b] = -2.0f; }
            const auto r = MixDoctor::diagnose (s, &ref);
            expect (r.ready && r.referenceName == "Ref");
            for (size_t i = 1; i < r.findings.size(); ++i)
            {
                const auto& a = r.findings[i - 1];
                const auto& b = r.findings[i];
                expect ((int) a.severity > (int) b.severity
                        || (a.severity == b.severity && (int) a.confidence >= (int) b.confidence));
            }
            for (const auto& f : r.findings)
            {
                expect (f.title.isNotEmpty() && f.observation.isNotEmpty(), f.id);
                if (f.severity != Severity::healthy)
                    expect (! f.evidence.empty() && f.impact.isNotEmpty() && f.action.isNotEmpty(), f.id);
            }
            const auto md = r.toMarkdown();
            expect (md == MixDoctor::diagnose (s, &ref).toMarkdown());
            expect (md.contains ("## Critical") && md.contains ("## Moderate") && md.contains ("## Healthy"));
            expect (md.contains ("Severity: High") && md.contains ("Confidence:"));
            expect (md.indexOf ("## Critical") < md.indexOf ("## Moderate") && md.indexOf ("## Moderate") < md.indexOf ("## Healthy"));
        }

        beginTest ("End to end: a bass-heavy mix against its own reference");
        {
            // Peaks stay below full scale: the reference WAV is 24-bit and would clip them.
            const auto l = pinkNoise (kFs, 20.0, 0.1, 101), r = pinkNoise (kFs, 20.0, 0.1, 102);
            std::vector<float> refGrid;
            const auto profile = profileOf (l, r, kFs, &refGrid);

            // A known change reads as predicted: within 0.5 dB of the shelf's
            // own response, level-matched as the rule does.
            for (float gain : { 4.0f, 8.0f })
            {
                const auto rep = MixDoctor::diagnose (measure (lowShelf (l, kFs, gain), lowShelf (r, kFs, gain), kFs), &profile);
                const auto* bass = find (rep, "tone_bass");
                const float predicted = predictedBassDeviation (refGrid, kFs, gain);
                expect (bass != nullptr, "+" + juce::String (gain) + " dB shelf flagged");
                if (bass != nullptr)
                {
                    logMessage ("+" + juce::String (gain, 0) + " dB low shelf: low end reads " + juce::String (bass->evidence[0].value, 2)
                                + " dB, predicted " + juce::String (predicted, 2) + " dB (" + MixDoctor::toString (bass->severity) + ")");
                    expectWithinAbsoluteError (bass->evidence[0].value, predicted, 0.5f);
                }
            }

            // The same material with +8 dB of low shelf at 120 Hz.
            const auto bl = lowShelf (l, kFs, 8.0f), br = lowShelf (r, kFs, 8.0f);
            auto report = MixDoctor::diagnose (measure (bl, br, kFs), &profile);
            const auto* f = find (report, "tone_bass");
            expect (f != nullptr, "the low end is flagged");
            if (f != nullptr)
            {
                logMessage ("bass vs reference: " + juce::String (f->evidence[0].value, 2) + " dB, " + MixDoctor::toString (f->severity));
                expect (f->title.contains ("above") && f->evidence[0].value > 3.0f && f->evidence[0].value < 9.0f);
            }
            for (const auto& g : report.findings)
                if (g.id.startsWith ("tone_") && g.id != "tone_bass" && g.id != "tone_balanced")
                    expect (g.severity == Severity::low || g.severity == Severity::healthy, g.id + " " + g.title);

            // The unaltered material, 6 dB quieter: no tonal finding, only a loudness note.
            report = MixDoctor::diagnose (measure (scaled (l, 0.5f), scaled (r, 0.5f), kFs), &profile);
            for (const auto& g : report.findings)
                expect (! g.id.startsWith ("tone_") || g.id == "tone_balanced", g.id + " " + g.title);
            const auto* loud = find (report, "loudness");
            expect (loud != nullptr && loud->severity == Severity::low);
            if (loud != nullptr) expectWithinAbsoluteError (loud->evidence[1].value, -6.0f, 0.3f);

            // Wide, uncorrelated low end loses about 3 dB in mono (right at the
            // Low / Medium boundary: the grades themselves are tested above).
            report = MixDoctor::diagnose (measure (l, r, kFs), nullptr);
            const auto* mono = find (report, "mono_low_end");
            expect (mono != nullptr && (mono->severity == Severity::medium || mono->severity == Severity::low));
            if (mono != nullptr) expectWithinAbsoluteError (mono->evidence[0].value, -3.0f, 0.5f);
            report = MixDoctor::diagnose (measure (l, l, kFs), nullptr);
            const auto centred = findCopy (report, "mono_low_end");
            expect (centred.id.isNotEmpty() && centred.severity == Severity::healthy);

            // Anti-phase low end: l's low end, polarity flipped on the right,
            // under r's top end in both channels.
            {
                ParametricEq::Band lp;
                lp.on = true; lp.type = ParametricEq::Type::highCut; lp.freq = 150.0f; lp.q = 0.7071f; lp.slope = 48;
                auto hp = lp;
                hp.type = ParametricEq::Type::lowCut;
                const auto low = filtered (l, kFs, lp), top = filtered (r, kFs, hp);
                std::vector<float> al (l.size()), ar (l.size());
                for (size_t i = 0; i < l.size(); ++i)
                {
                    al[i] =  low[i] + top[i];
                    ar[i] = -low[i] + top[i];
                }
                report = MixDoctor::diagnose (measure (al, ar, kFs), nullptr);
                const auto flipped = findCopy (report, "mono_low_end");
                expect (flipped.severity == Severity::high, flipped.title);
                expect (flipped.evidence.size() == 1 && flipped.evidence[0].value < -20.0f, flipped.title);
            }

            // A clipped mix against its unclipped self: the crest factor falls
            // and the density finding says so.
            {
                const auto clip = [] (std::vector<float> x, float ceiling)
                {
                    for (auto& v : x) v = juce::jlimit (-ceiling, ceiling, v);
                    return x;
                };
                report = MixDoctor::diagnose (measure (clip (l, 0.2f), clip (r, 0.2f), kFs), &profile);
                const auto dense = findCopy (report, "density");
                logMessage ("clipped at 0.2: " + dense.title);
                expect (dense.id.isNotEmpty() && (dense.severity == Severity::medium || dense.severity == Severity::high)
                        && dense.title.contains ("denser"), dense.title);
                // The unclipped mix reads the reference's own crest factor.
                const auto same = findCopy (MixDoctor::diagnose (measure (l, r, kFs), &profile), "density");
                expect (same.severity == Severity::healthy && std::abs (same.evidence[0].value) < 0.5f, same.title);
            }
        }

        beginTest ("End to end through the processor: a run observes the output from its start");
        {
            const auto l = pinkNoise (kFs, 12.0, 0.1, 111), r = pinkNoise (kFs, 12.0, 0.1, 112);
            TempWav wav ("mixdoctor-proc-ref");
            expect (writeWav (wav.file, l, r, kFs));

            MixMindProcessor p;
            p.setRateAndBufferSizeDetails (kFs, 512);
            p.prepareToPlay (kFs, 512);
            p.loadReference (wav.file);
            const auto end = juce::Time::getMillisecondCounter() + 20000;
            while (p.getReferenceStatus() != MixMindProcessor::RefStatus::ready && juce::Time::getMillisecondCounter() < end)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
            expect (p.getReferenceStatus() == MixMindProcessor::RefStatus::ready);

            const auto play = [&] (const std::vector<float>& a, const std::vector<float>& b)
            {
                juce::AudioBuffer<float> buf (2, 512);
                juce::MidiBuffer midi;
                for (size_t i = 0; i + 512 <= a.size(); i += 512)
                {
                    buf.copyFrom (0, 0, a.data() + i, 512);
                    buf.copyFrom (1, 0, b.data() + i, 512);
                    p.processBlock (buf, midi);
                    if ((i / 512) % 64 == 0) juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
                }
            };

            play (scaled (l, 0.1f), scaled (r, 0.1f));   // before the run: ignored by it
            p.beginMixDoctorRun();
            expect (p.isMixDoctorRunning());
            play (lowShelf (l, kFs, 8.0f), lowShelf (r, kFs, 8.0f));
            const auto report = p.getMixDoctorReport();
            expect (report.ready && report.referenceName == "mixdoctor-proc-ref");
            expectWithinAbsoluteError (report.observedSeconds, 12.0, 0.2);
            const auto* f = find (report, "tone_bass");
            expect (f != nullptr && f->title.contains ("above"));
            expect (report.toMarkdown().contains ("Low end"));
        }
    }

    static std::vector<float> scaled (std::vector<float> x, float g) { for (auto& v : x) v *= g; return x; }
};

static MixDoctorTests mixDoctorTests;

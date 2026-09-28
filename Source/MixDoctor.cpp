#include "MixDoctor.h"
#include <algorithm>
#include <cmath>

namespace MixDoctor
{
namespace
{
    // ── Rule thresholds (explicit, tested; ADR-007) ──────────────────────────
    constexpr float kTruePeakCeiling = -1.0f;   // dBTP, EBU R 128
    constexpr float kToneLow = 2.0f, kToneMedium = 3.5f, kToneHigh = 6.0f;   // |dB| vs reference, level-matched
    constexpr float kDenserMedium = -3.0f, kDenserHigh = -6.0f, kLooser = 6.0f;   // crest − reference crest, dB
    constexpr float kMonoLow = -1.0f, kMonoMedium = -3.0f, kMonoHigh = -6.0f;     // low-end fold-down loss, dB
    constexpr float kLoudnessNote = 3.0f;       // |LU| vs reference
    // Out of phase below this correlation: balanced channels then lose more
    // than 4 dB in mono, beyond the 3 dB of merely uncorrelated (very wide)
    // material, which reads around 0 and is not a phase problem.
    constexpr float kPhaseCorrelation = -0.2f;

    // Tonal regions, as octave bands of MeasurementFrame.
    struct Region
    {
        const char* id; const char* name; int first, last; const char* range;
        const char* hzHint;
        const char* impactMore; const char* impactLess;
        const char* causesMore; const char* causesLess;
        const char* fixMore; const char* fixLess;
    };
    const Region kRegions[] = {
        { "bass", "Low end", 0, 2, "22-177 Hz", "a low shelf around 100 Hz",
          "Too much low end masks the kick's attack, muddies the mix and eats headroom.",
          "The mix will sound thin and small next to the reference, most of all on big speakers.",
          "a loud bass or sub|a boomy kick|low-end build-up from several parts",
          "a quiet bass or kick|high-pass filters set too high|low end cut on the mix bus",
          "Try pulling the low end down by about {n} dB with {how}, then run Mix Doctor again.",
          "Try bringing the low end up by about {n} dB with {how}, then run Mix Doctor again." },
        { "low_mid", "Low mids", 3, 4, "177-707 Hz", "a wide bell around 300 Hz",
          "Excess low mids sound muddy or boxy and blur the definition of everything else.",
          "Too little low-mid body can make the mix sound hollow.",
          "guitars, keys or pads stacking up|boomy vocals or room tone|several parts sharing 200-500 Hz",
          "heavy low-mid cuts|thin sources",
          "Try a cut of about {n} dB with {how}, then run Mix Doctor again.",
          "Try a boost of about {n} dB with {how}, or ease off low-mid cuts." },
        { "mid", "Mids", 5, 6, "0.7-2.8 kHz", "a wide bell around 1.5 kHz",
          "Too much mid-range sounds honky or harsh and gets tiring at volume.",
          "Scooped mids push vocals and leads back; the mix loses presence.",
          "guitars, synths or vocals competing in the same range|boosts around 1-2 kHz",
          "mid scoops on several parts|a quiet lead or vocal",
          "Try a cut of about {n} dB with {how}, then run Mix Doctor again.",
          "Try a boost of about {n} dB with {how}, or ease off mid cuts." },
        { "high_mid", "Upper mids", 7, 7, "2.8-5.7 kHz", "a bell around 4 kHz",
          "Excess upper mids sound harsh and edgy, most of all on small speakers.",
          "Too little presence makes vocals and leads less clear and the mix dull.",
          "bright synths or guitars|vocal presence boosts|cymbals",
          "dark sources|presence cuts",
          "Try a cut of about {n} dB with {how} (or a de-esser if it comes and goes).",
          "Try a boost of about {n} dB with {how}." },
        { "air", "Highs", 8, 9, "5.7-22 kHz", "a high shelf above 8 kHz",
          "Too much top end sounds brittle or hissy and tiring.",
          "A dull top end loses sparkle and space next to the reference.",
          "bright cymbals or hi-hats|air boosts|saturation or distortion",
          "dark sources|low-pass filters",
          "Try pulling the top end down by about {n} dB with {how}.",
          "Try lifting the top end by about {n} dB with {how}." },
    };

    juce::String db (float v, int decimals = 1)
    {
        return (v > 0.0f ? "+" : "") + juce::String (v, decimals);
    }

    Confidence lower (Confidence c)
    {
        return c == Confidence::high ? Confidence::medium : Confidence::low;
    }

    Confidence byDuration (const MeasurementHistory::Stats& s, double mediumAt, double highAt)
    {
        auto c = s.gatedSeconds >= highAt ? Confidence::high
               : s.gatedSeconds >= mediumAt ? Confidence::medium : Confidence::low;
        if (s.frames > 0 && s.missingFrames * 10 > s.frames + s.missingFrames)   // > 10 % of the window lost
            c = lower (c);
        return c;
    }

    Confidence atMost (Confidence c, Confidence cap) { return (int) c < (int) cap ? c : cap; }

    // Mean band magnitude over a region's bands present on both sides, in dB.
    bool regionLevels (const Region& r, const MeasurementHistory::Stats& mix, const ReferenceProfile& ref,
                       float& mixDb, float& refDb)
    {
        double m = 0.0, f = 0.0; int n = 0;
        for (int b = r.first; b <= r.last; ++b)
            if (mix.bandHasData[(size_t) b] && ref.bandHasData[(size_t) b])
            {
                m += std::pow (10.0, mix.bandDb[(size_t) b] / 20.0);
                f += std::pow (10.0, ref.bandDb[(size_t) b] / 20.0);
                ++n;
            }
        if (n == 0 || m <= 0.0 || f <= 0.0) return false;
        mixDb = (float) (20.0 * std::log10 (m / n));
        refDb = (float) (20.0 * std::log10 (f / n));
        return true;
    }

    void truePeakRule (const MeasurementHistory::Stats& s, std::vector<Finding>& out)
    {
        if (s.truePeakDb <= LoudnessMeter::kSilenceDb) return;
        Finding f;
        f.id = "true_peak";
        f.evidence.push_back ({ "true_peak_max", "", s.truePeakDb, "dBTP" });
        if (s.truePeakDb > kTruePeakCeiling)
        {
            f.severity   = s.truePeakDb > 0.0f ? Severity::high : Severity::medium;
            f.confidence = Confidence::high;   // a measured maximum: it happened
            f.title      = "True peak " + db (s.truePeakDb) + " dBTP, above the -1 dBTP ceiling";
            f.observation = "The highest true (inter-sample) peak reached " + db (s.truePeakDb) + " dBTP.";
            f.impact     = "Peaks this close to full scale can clip in converters and when streaming services encode the track.";
            f.action     = "Set the limiter's output ceiling to -1 dBTP (true-peak mode if it has one).";
        }
        else
        {
            f.severity   = Severity::healthy;
            f.confidence = byDuration (s, kMediumSeconds, kHighSeconds);   // more listening, fewer missed peaks
            f.title      = "True peak " + db (s.truePeakDb) + " dBTP, below the -1 dBTP ceiling";
            f.observation = "The highest true peak heard was " + db (s.truePeakDb) + " dBTP.";
        }
        out.push_back (f);
    }

    void toneRules (const MeasurementHistory::Stats& s, const ReferenceProfile& ref, std::vector<Finding>& out)
    {
        // Level-matched: the mean difference across regions is removed, so a
        // louder or quieter mix does not read as a tonal problem.
        struct Diff { const Region* r; float mixDb, refDb, dev; };
        std::vector<Diff> diffs;
        for (const auto& r : kRegions)
        {
            float m = 0.0f, f = 0.0f;
            if (regionLevels (r, s, ref, m, f)) diffs.push_back ({ &r, m, f, m - f });
        }
        if (diffs.size() < 2) return;
        float mean = 0.0f;
        for (const auto& d : diffs) mean += d.dev;
        mean /= (float) diffs.size();

        const auto confidence = byDuration (s, kMediumSeconds, kHighSeconds);
        Finding healthy;
        healthy.id = "tone_balanced";
        healthy.severity = Severity::healthy;
        healthy.confidence = confidence;

        for (auto& d : diffs)
        {
            d.dev -= mean;
            const float a = std::abs (d.dev);
            if (a < kToneLow)
            {
                healthy.evidence.push_back ({ "band_level_vs_reference", d.r->range, d.dev, "dB" });
                continue;
            }
            const bool more = d.dev > 0.0f;
            const int amount = juce::jmax (1, juce::roundToInt (a));
            Finding f;
            f.id = juce::String ("tone_") + d.r->id;
            f.severity = a >= kToneHigh ? Severity::high : a >= kToneMedium ? Severity::medium : Severity::low;
            f.confidence = confidence;
            f.title = juce::String (d.r->name) + " " + juce::String (a, 1) + " dB " + (more ? "above" : "below")
                    + " the reference";
            f.observation = juce::String (d.r->range) + " sits " + db (d.dev) + " dB relative to the reference, level-matched.";
            f.impact = more ? d.r->impactMore : d.r->impactLess;
            f.potentialCauses = juce::StringArray::fromTokens (more ? d.r->causesMore : d.r->causesLess, "|", "");
            f.action = juce::String (more ? d.r->fixMore : d.r->fixLess)
                           .replace ("{n}", juce::String (amount)).replace ("{how}", d.r->hzHint);
            f.evidence.push_back ({ "band_level_vs_reference", d.r->range, d.dev, "dB" });
            out.push_back (f);
        }
        if (! healthy.evidence.empty())
        {
            juce::StringArray names;
            for (const auto& d : diffs)
                if (std::abs (d.dev) < kToneLow) names.add (d.r->name);
            healthy.title = "Tonal balance close to the reference: " + names.joinIntoString (", ").toLowerCase();
            healthy.observation = "Within " + juce::String (kToneLow, 0) + " dB of the reference, level-matched.";
            out.push_back (healthy);
        }
    }

    void densityRule (const MeasurementHistory::Stats& s, const ReferenceProfile& ref, std::vector<Finding>& out)
    {
        if (s.crestDb <= 0.0f || ref.crestDb <= 0.0f) return;
        const float d = s.crestDb - ref.crestDb;
        Finding f;
        f.id = "density";
        // The reference's crest is over the whole file, the mix's over what was
        // heard: comparable, but not the same stretch — never more than Medium.
        f.confidence = atMost (byDuration (s, kMediumSeconds, kHighSeconds), Confidence::medium);
        f.evidence.push_back ({ "crest_vs_reference", "", d, "dB" });
        f.evidence.push_back ({ "crest", "", s.crestDb, "dB" });
        if (d <= kDenserMedium)
        {
            f.severity = d <= kDenserHigh ? Severity::high : Severity::medium;
            f.title = "Mix is " + juce::String (-d, 1) + " dB denser than the reference";
            f.observation = "Crest factor (peak to RMS) is " + juce::String (s.crestDb, 1) + " dB, against "
                          + juce::String (ref.crestDb, 1) + " dB in the reference.";
            f.impact = "Less peak-to-average range than the reference usually means heavy compression or limiting: punch and transients suffer.";
            f.potentialCauses = { "heavy bus compression or limiting", "over-compressed drums", "saturation or clipping" };
            f.action = "Ease off the heaviest compressor or limiter by a few dB, then compare again at matched loudness.";
        }
        else if (d >= kLooser)
        {
            f.severity = Severity::low;
            f.title = "Mix is " + juce::String (d, 1) + " dB more dynamic than the reference";
            f.observation = "Crest factor is " + juce::String (s.crestDb, 1) + " dB, against "
                          + juce::String (ref.crestDb, 1) + " dB in the reference.";
            f.impact = "Not a problem in itself, but reaching the reference's loudness will take more limiting.";
            f.action = "Nothing to fix yet; keep it in mind when you master or bus-compress.";
        }
        else
        {
            f.severity = Severity::healthy;
            f.title = "Density close to the reference (crest factor " + db (d) + " dB)";
            f.observation = "Crest factor " + juce::String (s.crestDb, 1) + " dB, reference "
                          + juce::String (ref.crestDb, 1) + " dB.";
        }
        out.push_back (f);
    }

    void monoRules (const MeasurementHistory::Stats& s, std::vector<Finding>& out)
    {
        const auto confidence = byDuration (s, kMinSeconds, 10.0);   // stabilises quickly

        // The whole mix: out of phase is its own problem.
        if (s.correlation < kPhaseCorrelation)
        {
            Finding f;
            f.id = "phase";
            f.severity = Severity::high;
            f.confidence = confidence;
            f.title = "Mix is out of phase (correlation " + juce::String (s.correlation, 2) + ")";
            f.observation = "Left and right are negatively correlated overall (" + juce::String (s.correlation, 2) + ").";
            f.impact = "Large parts of the mix cancel when played in mono: phones, club systems, many speakers.";
            f.potentialCauses = { "one channel's polarity flipped", "extreme stereo widening", "a phase-inverted stereo sample" };
            f.action = "Check the stereo image in mono; look for a flipped polarity or a widener on the mix bus.";
            f.evidence.push_back ({ "correlation", "", s.correlation, "" });
            out.push_back (f);
        }

        // The low end, where mono playback matters most.
        double loss = 0.0; int n = 0;
        for (int b = kRegions[0].first; b <= kRegions[0].last; ++b)
            if (s.bandHasData[(size_t) b]) { loss += s.monoLossDb[(size_t) b]; ++n; }
        if (n == 0) return;
        const float l = (float) (loss / n);
        Finding f;
        f.id = "mono_low_end";
        f.confidence = confidence;
        f.evidence.push_back ({ "mono_fold_down_loss", kRegions[0].range, l, "dB" });
        if (l <= kMonoLow)
        {
            f.severity = l <= kMonoHigh ? Severity::high : l <= kMonoMedium ? Severity::medium : Severity::low;
            f.title = "Low end loses " + juce::String (-l, 1) + " dB in mono";
            f.observation = juce::String ("Folding ") + kRegions[0].range + " to mono loses " + juce::String (-l, 1) + " dB.";
            f.impact = "Club systems, phones and many speakers play the low end in mono: stereo or out-of-phase bass partly cancels there.";
            f.potentialCauses = { "stereo-widened bass or sub", "a stereo bass sample", "left/right phase differences in the low end" };
            f.action = "Keep the low end centred below about 120 Hz: a low cut on the Side channel (MixMind's EQ, placement Side) does it.";
        }
        else
        {
            f.severity = Severity::healthy;
            f.title = "Low end holds up in mono (" + db (l) + " dB)";
            f.observation = juce::String ("Folding ") + kRegions[0].range + " to mono changes it by " + db (l) + " dB.";
        }
        out.push_back (f);
    }

    void loudnessRule (const MeasurementHistory::Stats& s, const ReferenceProfile& ref, std::vector<Finding>& out)
    {
        if (s.integratedLufs <= LoudnessMeter::kSilenceDb || ref.lufs <= LoudnessMeter::kSilenceDb) return;
        const float d = s.integratedLufs - ref.lufs;
        Finding f;
        f.id = "loudness";
        f.confidence = Confidence::medium;   // what was heard vs the whole reference file
        f.evidence.push_back ({ "integrated_loudness", "", s.integratedLufs, "LUFS" });
        f.evidence.push_back ({ "loudness_vs_reference", "", d, "LU" });
        if (std::abs (d) >= kLoudnessNote)
        {
            f.severity = Severity::low;
            f.title = "Mix is " + juce::String (std::abs (d), 1) + " LU " + (d < 0.0f ? "quieter" : "louder") + " than the reference";
            f.observation = "Integrated loudness " + juce::String (s.integratedLufs, 1) + " LUFS, reference "
                          + juce::String (ref.lufs, 1) + " LUFS.";
            f.impact = "Louder almost always sounds better, so judge tone at matched loudness (Mix Doctor's comparisons already are).";
            f.action = "Match playback levels before comparing by ear; final loudness is set at mastering.";
        }
        else
        {
            f.severity = Severity::healthy;
            f.title = "Loudness within " + juce::String (std::abs (d), 1) + " LU of the reference";
            f.observation = "Integrated loudness " + juce::String (s.integratedLufs, 1) + " LUFS.";
        }
        out.push_back (f);
    }
}

juce::String toString (Severity s)
{
    switch (s)
    {
        case Severity::high:    return "High";
        case Severity::medium:  return "Medium";
        case Severity::low:     return "Low";
        case Severity::healthy: break;
    }
    return "Healthy";
}

juce::String toString (Confidence c)
{
    switch (c)
    {
        case Confidence::high:   return "High";
        case Confidence::medium: return "Medium";
        case Confidence::low:    break;
    }
    return "Low";
}

ReferenceProfile profileFromReference (const ReferenceAnalyzer::Result& ref, const float* liveGridBins, double liveSampleRate)
{
    ReferenceProfile p;
    p.name       = ref.name;
    p.lufs       = ref.lufs;
    p.truePeakDb = ref.truePeakDb;
    p.crestDb    = ref.getCrestFactor();
    if (liveGridBins == nullptr || liveSampleRate <= 0.0 || ! ref.isValid()) return p;

    float bands[MeasurementFrame::kBands];
    AudioAnalyzer::bandMagnitudes (liveGridBins, liveSampleRate, bands);
    for (int b = 0; b < MeasurementFrame::kBands; ++b)
    {
        // A band reaching past the reference file's Nyquist has no data up
        // there (mapToGrid leaves those bins at 0). Content above 20 kHz is
        // negligible, so a band only needs coverage up to there.
        const double top = juce::jmin (MeasurementFrame::bandCentreHz (b) * std::sqrt (2.0), 20000.0);
        const bool covered = ref.sampleRate * 0.5 >= top && AudioAnalyzer::bandBinCount (b, liveSampleRate) > 0;
        p.bandHasData[(size_t) b] = covered && bands[b] > 0.0f;
        p.bandDb[(size_t) b] = p.bandHasData[(size_t) b] ? 20.0f * std::log10 (bands[b]) : LoudnessMeter::kSilenceDb;
    }
    return p;
}

Report diagnose (const MeasurementHistory::Stats& mix, const ReferenceProfile* reference)
{
    Report r;
    r.observedSeconds = mix.gatedSeconds;
    if (reference != nullptr) r.referenceName = reference->name;
    if (mix.gatedSeconds < kMinSeconds)
    {
        const int more = juce::jmax (1, (int) std::ceil (kMinSeconds - mix.gatedSeconds));
        r.notReadyReason = "Play at least " + juce::String (more) + (more == 1 ? " more second" : " more seconds")
                         + " of the mix.";
        return r;
    }
    r.ready = true;

    truePeakRule (mix, r.findings);
    monoRules (mix, r.findings);
    if (reference != nullptr)
    {
        toneRules (mix, *reference, r.findings);
        densityRule (mix, *reference, r.findings);
        loudnessRule (mix, *reference, r.findings);
    }

    std::stable_sort (r.findings.begin(), r.findings.end(), [] (const Finding& a, const Finding& b)
    {
        if (a.severity != b.severity)     return (int) a.severity > (int) b.severity;
        if (a.confidence != b.confidence) return (int) a.confidence > (int) b.confidence;
        return a.id < b.id;
    });
    return r;
}

juce::String Report::toMarkdown() const
{
    juce::String md ("# Mix Doctor Report\n\n");
    if (! ready)
        return md + notReadyReason + "\n";

    md << "Heard " << juce::String (observedSeconds, 1) << " s of signal"
       << (referenceName.isNotEmpty() ? ", compared with \"" + referenceName + "\"" : juce::String (", no reference loaded"))
       << ".\n";

    const auto section = [&] (const char* heading, auto pick)
    {
        bool any = false;
        for (const auto& f : findings)
        {
            if (! pick (f.severity)) continue;
            if (! any) { md << "\n## " << heading << "\n"; any = true; }
            if (f.severity == Severity::healthy)
            {
                md << "- " << f.title << " (confidence: " << toString (f.confidence) << ")\n";
                continue;
            }
            md << "\n### " << f.title << "\n\n"
               << "Severity: " << toString (f.severity) << " | Confidence: " << toString (f.confidence) << "\n\n"
               << "**Observed:** " << f.observation << "\n\n"
               << "**Why it matters:** " << f.impact << "\n\n";
            if (! f.potentialCauses.isEmpty())
                md << "**Possible causes:** " << f.potentialCauses.joinIntoString ("; ") << "\n\n";
            md << "**Try first:** " << f.action << "\n";
        }
    };
    section ("Critical", [] (Severity s) { return s == Severity::high; });
    section ("Moderate", [] (Severity s) { return s == Severity::medium || s == Severity::low; });
    section ("Healthy",  [] (Severity s) { return s == Severity::healthy; });
    return md;
}
}

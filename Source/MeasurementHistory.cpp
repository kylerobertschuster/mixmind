#include "MeasurementHistory.h"
#include <algorithm>
#include <cmath>

MeasurementHistory::MeasurementHistory (double secondsKept)
    : ring ((size_t) juce::jmax (2, (int) std::ceil (secondsKept * 10.0) + 1))
{
}

void MeasurementHistory::clear()
{
    head = count = 0;
    haveEpoch = false;
    ++generationCount;
}

void MeasurementHistory::drain (AudioAnalyzer& analyzer)
{
    MeasurementFrame f;
    while (analyzer.popFrame (f))
        add (f);
}

void MeasurementHistory::add (const MeasurementFrame& f)
{
    if (! haveEpoch || f.epoch != epoch || ! juce::exactlyEqual (f.sampleRate, sampleRate))
    {
        clear();
        haveEpoch  = true;
        epoch      = f.epoch;
        sampleRate = f.sampleRate;
    }
    if (count > 0 && f.index <= at (count - 1).index) return;   // never goes backwards within an epoch

    const int cap = (int) ring.size();
    if (count < cap)
    {
        ring[(size_t) ((head + count) % cap)] = f;
        ++count;
    }
    else
    {
        ring[(size_t) head] = f;
        head = (head + 1) % cap;
    }
}

const MeasurementFrame& MeasurementHistory::at (int i) const
{
    return ring[(size_t) ((head + i) % (int) ring.size())];
}

juce::uint32 MeasurementHistory::latestIndex() const
{
    return count > 0 ? at (count - 1).index : 0;
}

double MeasurementHistory::secondsAvailable() const
{
    double samples = 0.0;
    for (int i = 0; i < count; ++i) samples += at (i).samples;
    return sampleRate > 0.0 ? samples / sampleRate : 0.0;
}

MeasurementHistory::Stats MeasurementHistory::statsSince (juce::uint32 firstIndex) const
{
    int from = 0;
    while (from < count && at (from).index < firstIndex) ++from;
    return compute (from, count);
}

MeasurementHistory::Stats MeasurementHistory::statsForLast (double seconds) const
{
    const double wanted = seconds * sampleRate;
    double samples = 0.0;
    int from = count;
    while (from > 0 && samples + at (from - 1).samples <= wanted + 0.5)
        samples += at (--from).samples;
    return compute (from, count);
}

MeasurementHistory::Stats MeasurementHistory::compute (int from, int to) const
{
    Stats s;
    if (to <= from || sampleRate <= 0.0) return s;

    s.frames = to - from;
    s.missingFrames = (int) (at (to - 1).index - at (from).index) + 1 - s.frames;

    const auto meanToLufs = [] (double meanSquare)
    {
        return meanSquare > 0.0 ? LoudnessMeter::toLufs (meanSquare, 1.0) : kSilenceDb;
    };

    // ── Per frame: true peak everywhere; the rest over frames above the gate.
    float peak = 0.0f;
    double samples = 0.0, gatedSamples = 0.0, energy = 0.0, ll = 0.0, rr = 0.0, lr = 0.0;
    std::array<double, kBands> mag {}, midPow {}, sidePow {}, weight {};
    for (int i = from; i < to; ++i)
    {
        const auto& f = at (i);
        samples += f.samples;
        peak = juce::jmax (peak, f.truePeak);
        if (f.samples <= 0 || f.energy / f.samples <= kFrameGateMeanSquare) continue;

        gatedSamples += f.samples;
        energy += f.energy;
        ll += f.ll; rr += f.rr; lr += f.lr;
        if (f.fftFrames > 0)
            for (size_t b = 0; b < (size_t) kBands; ++b)
            {
                const double w = f.fftFrames;
                mag[b]     += w * f.bandMag[b];
                midPow[b]  += w * f.bandMidPow[b];
                sidePow[b] += w * f.bandSidePow[b];
                weight[b]  += w;
            }
    }
    s.seconds      = samples / sampleRate;
    s.gatedSeconds = gatedSamples / sampleRate;
    s.truePeakDb   = peak > 0.0f ? juce::Decibels::gainToDecibels (peak, kSilenceDb) : kSilenceDb;
    s.rmsDb        = energy > 0.0 && gatedSamples > 0.0 ? (float) (10.0 * std::log10 (energy / gatedSamples)) : kSilenceDb;
    s.crestDb      = s.truePeakDb > kSilenceDb && s.rmsDb > kSilenceDb ? juce::jmax (0.0f, s.truePeakDb - s.rmsDb) : 0.0f;

    const double denom = std::sqrt (ll * rr);
    s.correlation = denom > 1e-12 ? (float) juce::jlimit (-1.0, 1.0, lr / denom) : 1.0f;
    const double mm = (ll + 2.0 * lr + rr) * 0.25, ss = (ll - 2.0 * lr + rr) * 0.25;
    s.width = mm > 1e-12 ? (float) std::sqrt (juce::jmax (0.0, ss) / mm) : (ss > 1e-12 ? 1.0f : 0.0f);

    for (size_t b = 0; b < (size_t) kBands; ++b)
    {
        s.bandHasData[b] = weight[b] > 0.0 && AudioAnalyzer::bandBinCount ((int) b, sampleRate) > 0;
        s.bandDb[b]      = kSilenceDb;
        s.monoLossDb[b]  = 0.0f;
        if (! s.bandHasData[b]) continue;
        const double m = mag[b] / weight[b];
        s.bandDb[b] = m > 0.0 ? juce::jmax (kSilenceDb, (float) (20.0 * std::log10 (m))) : kSilenceDb;
        const double total = midPow[b] + sidePow[b];
        if (total > 0.0)
            s.monoLossDb[b] = midPow[b] > 0.0 ? juce::jmax (kMonoFloorDb, (float) (10.0 * std::log10 (midPow[b] / total)))
                                              : kMonoFloorDb;
    }

    // ── Blocks of consecutive frames (never across a missing one).
    const auto blocks = [&] (int length, std::vector<float>& lufs, std::vector<double>& means)
    {
        lufs.clear(); means.clear();
        for (int end = from + length; end <= to; ++end)
        {
            if (at (end - 1).index - at (end - length).index != (juce::uint32) (length - 1)) continue;
            double e = 0.0, n = 0.0;
            for (int i = end - length; i < end; ++i) { e += at (i).kEnergy; n += at (i).samples; }
            const double mean = n > 0.0 ? e / n : 0.0;
            means.push_back (mean);
            lufs.push_back (meanToLufs (mean));
        }
    };
    std::vector<float> lufs;
    std::vector<double> means;

    // BS.1770-4 integrated: 400 ms blocks, 75 % overlap.
    blocks (4, lufs, means);
    {
        double sum = 0.0; int n = 0;
        for (size_t i = 0; i < lufs.size(); ++i)
            if (lufs[i] > kGateLufs) { sum += means[i]; ++n; }
        if (n > 0)
        {
            const float relative = meanToLufs (sum / n) - 10.0f;
            double gated = 0.0; int m = 0;
            for (size_t i = 0; i < lufs.size(); ++i)
                if (lufs[i] > kGateLufs && lufs[i] > relative) { gated += means[i]; ++m; }
            s.integratedLufs = m > 0 ? meanToLufs (gated / m) : kSilenceDb;
        }
    }

    // EBU Tech 3342 loudness range: 3 s blocks every 100 ms.
    blocks (30, lufs, means);
    {
        double sum = 0.0; int n = 0;
        for (size_t i = 0; i < lufs.size(); ++i)
            if (lufs[i] > kGateLufs) { sum += means[i]; ++n; }
        if (n > 0)
        {
            const float relative = meanToLufs (sum / n) - 20.0f;
            std::vector<float> kept;
            for (float v : lufs)
                if (v > kGateLufs && v > relative) kept.push_back (v);
            if (! kept.empty())
            {
                std::sort (kept.begin(), kept.end());
                const auto pct = [&kept] (double p) { return kept[(size_t) std::lround ((double) (kept.size() - 1) * p)]; };
                s.loudnessRangeLu = pct (0.95) - pct (0.10);
            }
        }
    }
    return s;
}

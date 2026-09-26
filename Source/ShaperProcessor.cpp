#include "ShaperProcessor.h"
#include <cmath>
#include <complex>

namespace
{
    // Dot products with four partial sums each, so the compiler can keep
    // several multiply-adds in flight (and vectorise).
    inline float dot (const float* h, const float* x, int n) noexcept
    {
        float s0 = 0, s1 = 0, s2 = 0, s3 = 0;
        for (int i = 0; i < n; i += 4)
        {
            s0 += h[i] * x[i];         s1 += h[i + 1] * x[i + 1];
            s2 += h[i + 2] * x[i + 2]; s3 += h[i + 3] * x[i + 3];
        }
        return (s0 + s1) + (s2 + s3);
    }

    inline void dot2 (const float* h, const float* a, const float* b, int n, float& outA, float& outB) noexcept
    {
        float a0 = 0, a1 = 0, a2 = 0, a3 = 0;
        float b0 = 0, b1 = 0, b2 = 0, b3 = 0;
        for (int i = 0; i < n; i += 4)
        {
            a0 += h[i]     * a[i];     b0 += h[i]     * b[i];
            a1 += h[i + 1] * a[i + 1]; b1 += h[i + 1] * b[i + 1];
            a2 += h[i + 2] * a[i + 2]; b2 += h[i + 2] * b[i + 2];
            a3 += h[i + 3] * a[i + 3]; b3 += h[i + 3] * b[i + 3];
        }
        outA = (a0 + a1) + (a2 + a3);
        outB = (b0 + b1) + (b2 + b3);
    }

    // acc += h · x over `bins` interleaved complex values.
    inline void complexMac (float* acc, const float* h, const float* x, int bins) noexcept
    {
        for (int b = 0; b < 2 * bins; b += 2)
        {
            acc[b]     += h[b] * x[b]     - h[b + 1] * x[b + 1];
            acc[b + 1] += h[b] * x[b + 1] + h[b + 1] * x[b];
        }
    }

    // acc += h · 0.5·(x ± y): the mid / side spectrum from the L / R spectra.
    inline void complexMacMidSide (float* acc, const float* h, const float* x, const float* y, float sign, int bins) noexcept
    {
        for (int b = 0; b < 2 * bins; b += 2)
        {
            const float re = 0.5f * (x[b] + sign * y[b]);
            const float im = 0.5f * (x[b + 1] + sign * y[b + 1]);
            acc[b]     += h[b] * re - h[b + 1] * im;
            acc[b + 1] += h[b] * im + h[b + 1] * re;
        }
    }

    static_assert (ShaperProcessor::kTapCount % ShaperProcessor::kPartition == 0, "whole partitions only");
    static_assert (ShaperProcessor::kPartition % 4 == 0, "dot products unroll by 4");
    static_assert ((1 << ShaperProcessor::kFftOrder) == 2 * ShaperProcessor::kPartition, "FFT spans two partitions");
}

// ─────────────────────────────────────────────────────────────────────────────
//  Setup + hand-over
// ─────────────────────────────────────────────────────────────────────────────

ShaperProcessor::ShaperProcessor()
{
    // Slots are written by the message thread (possibly before prepare), so
    // they are sized once here and never reallocated.
    for (auto& s : slots)
        for (int c = 0; c < 2; ++c)
        {
            s.head[(size_t) c].assign ((size_t) kPartition, 0.0f);
            s.parts[(size_t) c].assign ((size_t) (2 * kTailParts * kBinsPerPartition), 0.0f);
        }
}

void ShaperProcessor::prepare (double, int)
{
    for (auto& l : headLines)  l.assign ((size_t) (2 * kPartition), 0.0f);
    for (auto& d : delayLines) d.assign ((size_t) kDelaySize, 0.0f);
    for (auto& b : blocks)     b.assign ((size_t) (2 * kPartition), 0.0f);
    for (auto& f : fdl)        f.assign ((size_t) (2 * kTailParts * kBinsPerPartition), 0.0f);
    tailActive.assign   ((size_t) (2 * kPartition), 0.0f);
    tailPrevious.assign ((size_t) (2 * kPartition), 0.0f);
    fftBuffer.assign    ((size_t) (4 * kPartition), 0.0f);
    accum.assign        ((size_t) (2 * kBinsPerPartition), 0.0f);
    pos = delayIdx = fdlNewest = 0;

    activeSlot.store (kIdentity);
    previousSlot.store (kNone);
    appliedSerial = -1;               // pick up whatever is pending on the first block
    fadePos = kFadeSamples;
}

void ShaperProcessor::reset()
{
    for (auto* v : { &headLines[0], &headLines[1], &headLines[2], &headLines[3], &delayLines[0], &delayLines[1],
                     &blocks[0], &blocks[1], &fdl[0], &fdl[1], &tailActive, &tailPrevious })
        std::fill (v->begin(), v->end(), 0.0f);
    pos = delayIdx = fdlNewest = 0;
    if (activeSlot.load() >= 0) computeTail (activeSlot.load(), tailActive.data());
}

void ShaperProcessor::buildSlotChannel (Slot& s, const float* taps, int channel, int count)
{
    std::vector<float> h ((size_t) kTapCount, 0.0f);
    if (taps != nullptr)
        std::copy (taps, taps + juce::jlimit (0, kTapCount, count), h.begin());
    else
        h[(size_t) kLatency] = 1.0f;   // identity for this channel

    auto& head = s.head[(size_t) channel];
    head.resize ((size_t) kPartition);
    for (int j = 0; j < kPartition; ++j)
        head[(size_t) j] = h[(size_t) (kPartition - 1 - j)];

    auto& parts = s.parts[(size_t) channel];
    parts.resize ((size_t) (2 * kTailParts * kBinsPerPartition));

    juce::dsp::FFT f (kFftOrder);
    std::vector<float> buf ((size_t) (4 * kPartition));
    for (int k = 1; k < kPartitions; ++k)
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        std::copy (h.begin() + k * kPartition, h.begin() + (k + 1) * kPartition, buf.begin());
        f.performRealOnlyForwardTransform (buf.data(), true);
        std::copy (buf.begin(), buf.begin() + 2 * kBinsPerPartition,
                   parts.begin() + 2 * (k - 1) * kBinsPerPartition);
    }
}

int ShaperProcessor::freeSlot() const
{
    for (int i = 0; i < kSlots; ++i)
        if (i != pendingSlot.load() && i != designedSlot.load() && i != activeSlot.load() && i != previousSlot.load())
            return i;
    jassertfalse;   // at most four slots are ever in use
    return 0;
}

void ShaperProcessor::publish (const Slot* s)
{
    const juce::SpinLock::ScopedLockType sl (lock);

    if (s == nullptr)
    {
        pendingSlot.store (kIdentity);
    }
    else
    {
        const int f = freeSlot();
        auto& dst = slots[(size_t) f];
        dst.midSide = s->midSide;
        for (size_t c = 0; c < (s->midSide ? 2u : 1u); ++c)
        {
            std::copy (s->head[c].begin(), s->head[c].end(), dst.head[c].begin());
            std::copy (s->parts[c].begin(), s->parts[c].end(), dst.parts[c].begin());
        }
        pendingSlot.store (f);
    }
    pendingSerial.fetch_add (1);
}

void ShaperProcessor::setFilter (const float* taps, int count)
{
    if (taps == nullptr || count <= 0) { publish (nullptr); return; }

    Slot s;
    buildSlotChannel (s, taps, 0, count);
    publish (&s);
}

void ShaperProcessor::setMidSideFilters (const float* midTaps, const float* sideTaps, int count)
{
    if (midTaps == nullptr && sideTaps == nullptr) { publish (nullptr); return; }

    Slot s;
    s.midSide = true;
    buildSlotChannel (s, midTaps, 0, count);
    buildSlotChannel (s, sideTaps, 1, count);
    publish (&s);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Audio thread
// ─────────────────────────────────────────────────────────────────────────────

void ShaperProcessor::beginTransition (int target)
{
    const int from = activeSlot.load();
    previousSlot.store (from);
    std::swap (tailActive, tailPrevious);   // the old filter keeps its tail for this partition
    activeSlot.store (target);
    if (target >= 0) computeTail (target, tailActive.data());

    fadePos = (from < 0 && target < 0) ? kFadeSamples : 0;
    if (fadePos >= kFadeSamples) previousSlot.store (kNone);
}

void ShaperProcessor::computeTail (int slot, float* out)
{
    constexpr int P = kPartition;
    const Slot& s = slots[(size_t) slot];

    // Filter-domain channel c: L / R when linked, mid / side otherwise.
    for (int c = 0; c < 2; ++c)
    {
        const float* H = s.parts[s.midSide ? (size_t) c : 0].data();
        std::fill (accum.begin(), accum.end(), 0.0f);

        for (int k = 1; k < kPartitions; ++k)
        {
            const int idx = (fdlNewest - (k - 1) + kTailParts) % kTailParts;   // window j − k
            const float* Hk = H + 2 * (k - 1) * kBinsPerPartition;
            const float* XL = fdl[0].data() + 2 * idx * kBinsPerPartition;
            const float* XR = fdl[1].data() + 2 * idx * kBinsPerPartition;

            if (! s.midSide) complexMac (accum.data(), Hk, c == 0 ? XL : XR, kBinsPerPartition);
            else             complexMacMidSide (accum.data(), Hk, XL, XR, c == 0 ? 1.0f : -1.0f, kBinsPerPartition);
        }

        std::copy (accum.begin(), accum.end(), fftBuffer.begin());
        std::fill (fftBuffer.begin() + 2 * kBinsPerPartition, fftBuffer.end(), 0.0f);
        fft.performRealOnlyInverseTransform (fftBuffer.data());       // includes the 1/N
        std::copy (fftBuffer.begin() + P, fftBuffer.begin() + 2 * P, out + c * P);   // overlap-save: last half
    }

    if (s.midSide)
        for (int u = 0; u < P; ++u)
        {
            const float m = out[u], sd = out[P + u];
            out[u]     = m + sd;
            out[P + u] = m - sd;
        }
}

void ShaperProcessor::blockComplete()
{
    constexpr int P = kPartition;

    // Spectrum of [previous partition, the one just completed] → FDL.
    fdlNewest = (fdlNewest + 1) % kTailParts;
    for (size_t c = 0; c < 2; ++c)
    {
        std::copy (blocks[c].begin(), blocks[c].end(), fftBuffer.begin());
        std::fill (fftBuffer.begin() + 2 * P, fftBuffer.end(), 0.0f);
        fft.performRealOnlyForwardTransform (fftBuffer.data(), true);
        std::copy (fftBuffer.begin(), fftBuffer.begin() + 2 * kBinsPerPartition,
                   fdl[c].begin() + 2 * fdlNewest * kBinsPerPartition);
        std::copy (blocks[c].begin() + P, blocks[c].end(), blocks[c].begin());
    }

    // Tails for the next partition.
    if (activeSlot.load() >= 0) computeTail (activeSlot.load(), tailActive.data());
    if (fadePos < kFadeSamples && previousSlot.load() >= 0) computeTail (previousSlot.load(), tailPrevious.data());
}

void ShaperProcessor::render (int slot, const float* tail, float delayedL, float delayedR,
                              float& yL, float& yR) const noexcept
{
    if (slot < 0) { yL = delayedL; yR = delayedR; return; }   // identity = pure kLatency delay

    constexpr int P = kPartition;
    const Slot& s = slots[(size_t) slot];
    const int w = pos + 1;   // newest kPartition samples are at [pos+1, pos+P]

    if (! s.midSide)
    {
        dot2 (s.head[0].data(), headLines[0].data() + w, headLines[1].data() + w, P, yL, yR);
    }
    else
    {
        const float m  = dot (s.head[0].data(), headLines[2].data() + w, P);
        const float sd = dot (s.head[1].data(), headLines[3].data() + w, P);
        yL = m + sd;
        yR = m - sd;
    }
    yL += tail[pos];
    yR += tail[P + pos];
}

void ShaperProcessor::process (float* L, float* R, int n)
{
    if (tailActive.empty() || n <= 0) return;

    // Only start a new transition once the previous crossfade has finished.
    if (fadePos >= kFadeSamples)
    {
        if (pendingSerial.load() != appliedSerial)
        {
            const juce::SpinLock::ScopedTryLockType tl (lock);
            if (tl.isLocked())
            {
                const int p = pendingSlot.load();
                if (p != kNone) { designedSlot.store (p); pendingSlot.store (kNone); }
                appliedSerial = pendingSerial.load();
            }
        }

        const int target = enabled.load() ? designedSlot.load() : kIdentity;
        if (target != activeSlot.load())
            beginTransition (target);
    }

    constexpr int P = kPartition;
    const bool mono = (R == nullptr);
    const int active = activeSlot.load();

    for (int i = 0; i < n; ++i)
    {
        const float l = L[i];
        const float r = mono ? l : R[i];

        headLines[0][(size_t) pos] = headLines[0][(size_t) (pos + P)] = l;
        headLines[1][(size_t) pos] = headLines[1][(size_t) (pos + P)] = r;
        headLines[2][(size_t) pos] = headLines[2][(size_t) (pos + P)] = 0.5f * (l + r);
        headLines[3][(size_t) pos] = headLines[3][(size_t) (pos + P)] = 0.5f * (l - r);
        blocks[0][(size_t) (P + pos)] = l;
        blocks[1][(size_t) (P + pos)] = r;
        delayLines[0][(size_t) delayIdx] = l;
        delayLines[1][(size_t) delayIdx] = r;

        const size_t d = (size_t) ((delayIdx - kLatency) & (kDelaySize - 1));
        const float dl = delayLines[0][d], dr = delayLines[1][d];

        float yL, yR;
        render (active, tailActive.data(), dl, dr, yL, yR);

        if (fadePos < kFadeSamples)
        {
            float pL, pR;
            render (previousSlot.load(), tailPrevious.data(), dl, dr, pL, pR);

            const float g = ((float) fadePos + 0.5f) / (float) kFadeSamples;
            yL = pL + (yL - pL) * g;
            yR = pR + (yR - pR) * g;
            if (++fadePos == kFadeSamples) previousSlot.store (kNone);
        }

        L[i] = yL;
        if (! mono) R[i] = yR;

        delayIdx = (delayIdx + 1) & (kDelaySize - 1);
        if (++pos == P) { pos = 0; blockComplete(); }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  FIR design (frequency-sampled, linear-phase, 1/3-octave smoothed)
// ─────────────────────────────────────────────────────────────────────────────

bool ShaperProcessor::buildCorrection (const float* targetBins, const float* liveBins, int numBins,
                                       double sampleRate, float amount, std::vector<float>& outCurveDb,
                                       const float* forcedMeanDb, float* outMeanDb)
{
    const int n = numBins;
    outCurveDb.assign ((size_t) juce::jmax (0, n), 0.0f);
    if (n <= 1 || targetBins == nullptr || liveBins == nullptr || sampleRate <= 0.0) return false;

    const double binHz = sampleRate / (2.0 * n);

    // 1. dB difference (target − live) where both sides have something to
    //    compare; the other bins are filled from their neighbours below.
    std::vector<float> diff ((size_t) n, 0.0f);
    std::vector<char>  valid ((size_t) n, 0);
    double wSum = 0.0, dSum = 0.0;
    bool any = false;
    for (int i = 1; i < n; ++i)
    {
        const float t = targetBins[i], l = liveBins[i];
        if (t <= 0.0f || l < kLiveFloor) continue;

        const float d = 20.0f * std::log10 (t / l);
        diff[(size_t) i]  = d;
        valid[(size_t) i] = 1;
        any = true;

        // Octave-weighted (1/f) mean over the band where both are reliable.
        const double f = (double) i * binHz;
        if (f >= 40.0 && f <= 16000.0) { wSum += 1.0 / f; dSum += d / f; }
    }
    if (! any || (forcedMeanDb == nullptr && wSum <= 0.0)) return false;

    // 2. Level-neutral: remove the mean (or the given offset), then clamp.
    const float mean = forcedMeanDb != nullptr ? *forcedMeanDb : (float) (dSum / wSum);
    if (outMeanDb != nullptr) *outMeanDb = mean;
    for (int i = 0; i < n; ++i)
        if (valid[(size_t) i])
            diff[(size_t) i] = juce::jlimit (-kMaxCorrectionDb, kMaxCorrectionDb, diff[(size_t) i] - mean);

    // Bins without data (outside a trace, above a reference's Nyquist, where
    // the mix is silent) carry the correction of their valid neighbours —
    // held at the edges, interpolated across gaps — so the curve never steps
    // back to 0 dB at the edge of the data. Sub-20 Hz is never boosted/cut.
    {
        int prev = -1;
        for (int i = 0; i < n; ++i)
        {
            if (! valid[(size_t) i]) continue;
            for (int k = prev + 1; k < i; ++k)
                diff[(size_t) k] = prev < 0 ? diff[(size_t) i]
                                            : diff[(size_t) prev] + (diff[(size_t) i] - diff[(size_t) prev])
                                                                    * (float) (k - prev) / (float) (i - prev);
            prev = i;
        }
        for (int k = prev + 1; k < n; ++k)
            diff[(size_t) k] = diff[(size_t) prev];

        for (int i = 0; i < n && (double) i * binHz < 20.0; ++i)
            diff[(size_t) i] = 0.0f;
    }

    // 3. 1/3-octave smoothing (prefix sums over f·2^(±1/6)) — the
    //    anti-pre-ringing guard: narrow features become long, ringing taps.
    const float amt = juce::jlimit (0.0f, 1.0f, amount);
    {
        const float halfWin = std::pow (2.0f, 1.0f / 6.0f);
        std::vector<double> prefix ((size_t) n + 1, 0.0);
        for (int i = 0; i < n; ++i) prefix[(size_t) i + 1] = prefix[(size_t) i] + diff[(size_t) i];

        for (int i = 0; i < n; ++i)
        {
            const int lo = juce::jlimit (0, n - 1, (int) std::floor ((float) i / halfWin));
            const int hi = juce::jlimit (0, n - 1, (int) std::ceil  ((float) i * halfWin));
            outCurveDb[(size_t) i] = amt * (float) ((prefix[(size_t) hi + 1] - prefix[(size_t) lo]) / (hi - lo + 1));
        }
    }
    return true;
}

void ShaperProcessor::designFromCurve (const std::vector<float>& curveDb, std::vector<float>& outTaps)
{
    outTaps.assign ((size_t) kTapCount, 0.0f);
    outTaps[(size_t) kLatency] = 1.0f;

    const int n = (int) curveDb.size();
    if (n < 2) return;

    // Magnitude on the design grid (0 … Nyquist inclusive), interpolated from
    // the analyzer grid (the design grid is finer: the curve is smooth).
    constexpr int N = kDesignSize;
    std::vector<float> mag ((size_t) N / 2 + 1);
    for (int k = 0; k <= N / 2; ++k)
    {
        const double p  = (double) k * n / (N / 2);
        const int    i0 = juce::jmin ((int) p, n - 1);
        const int    i1 = juce::jmin (i0 + 1, n - 1);
        const float  t  = (float) (p - i0);
        const float  db = curveDb[(size_t) i0] + (curveDb[(size_t) i1] - curveDb[(size_t) i0]) * t;
        mag[(size_t) k] = std::pow (10.0f, db / 20.0f);
    }

    // Linear phase centred exactly on kLatency, inverse FFT. JUCE's inverse
    // perform() already applies 1/N.
    std::vector<juce::dsp::Complex<float>> spec ((size_t) N), imp ((size_t) N);
    for (int k = 0; k <= N / 2; ++k)
    {
        const double ph = -2.0 * juce::MathConstants<double>::pi * (double) k * kLatency / (double) N;
        spec[(size_t) k] = { (float) (mag[(size_t) k] * std::cos (ph)), (float) (mag[(size_t) k] * std::sin (ph)) };
    }
    for (int k = N / 2 + 1; k < N; ++k)
        spec[(size_t) k] = std::conj (spec[(size_t) (N - k)]);

    juce::dsp::FFT fft (kDesignOrder);
    fft.perform (spec.data(), imp.data(), true);

    // Keep the kTapCount taps around the centre under a periodic Hann window
    // (w[kLatency] = 1, w[0] = 0): exactly symmetric about kLatency, so the
    // group delay is exactly kLatency samples.
    for (int m = 0; m < kTapCount; ++m)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * (double) m / (double) kTapCount);
        outTaps[(size_t) m] = (float) (imp[(size_t) m].real() * w);
    }
}

void ShaperProcessor::buildMatchFilter (const float* targetBins, const float* liveBins,
                                        int numBins, double sampleRate, float amount,
                                        std::vector<float>& outTaps,
                                        std::vector<float>* outCurveDb)
{
    std::vector<float> curve;
    const bool ok = buildCorrection (targetBins, liveBins, numBins, sampleRate, amount, curve);
    if (outCurveDb != nullptr) *outCurveDb = curve;

    if (ok && amount > 0.0f) designFromCurve (curve, outTaps);
    else                     designFromCurve ({}, outTaps);   // identity
}

// ─────────────────────────────────────────────────────────────────────────────
//  Manual-mode target: hand-drawn trace (Hz, dB) → linear magnitudes
// ─────────────────────────────────────────────────────────────────────────────

void ShaperProcessor::buildTargetFromCurve (const std::vector<TracePoint>& curve,
                                            int numBins, double sampleRate,
                                            std::vector<float>& outTarget)
{
    const int n = numBins;
    outTarget.assign ((size_t) juce::jmax (0, n), 0.0f);
    if (curve.size() < 2 || sampleRate <= 0.0) return;

    const double binHz = sampleRate / (2.0 * n);
    const double lo = curve.front().hz, hi = curve.back().hz;
    size_t seg = 0;

    for (int i = 1; i < n; ++i)
    {
        const double f = (double) i * binHz;
        if (f < lo || f > hi) continue;   // outside the trace: no target

        while (seg + 2 < curve.size() && f > (double) curve[seg + 1].hz) ++seg;
        const auto& a = curve[seg];
        const auto& b = curve[seg + 1];

        const double la = std::log (juce::jmax (1.0, (double) a.hz));
        const double lb = std::log (juce::jmax (1.0, (double) b.hz));
        const double t  = lb > la ? juce::jlimit (0.0, 1.0, (std::log (f) - la) / (lb - la)) : 0.0;
        const double db = a.db + (b.db - a.db) * t;

        // The trace is drawn on the analyzer's dB scale; the shaper works in
        // linear magnitude, so convert explicitly here and nowhere else.
        outTarget[(size_t) i] = (float) std::pow (10.0, db / 20.0);
    }
}

float ShaperProcessor::responseDb (const std::vector<float>& taps, double hz, double sampleRate)
{
    const double w = 2.0 * juce::MathConstants<double>::pi * hz / sampleRate;
    std::complex<double> acc { 0.0, 0.0 };
    for (size_t k = 0; k < taps.size(); ++k)
        acc += (double) taps[k] * std::polar (1.0, -w * (double) k);
    return (float) (20.0 * std::log10 (juce::jmax (1.0e-12, std::abs (acc))));
}

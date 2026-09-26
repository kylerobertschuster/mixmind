#include "ShaperProcessor.h"
#include <cmath>
#include <complex>

namespace
{
    // Two dot products sharing the tap loads. Four partial sums each so the
    // compiler can keep several multiply-adds in flight (and vectorise).
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

    static_assert ((ShaperProcessor::kTapCount & (ShaperProcessor::kTapCount - 1)) == 0, "kTapCount must be a power of two");
    static_assert (ShaperProcessor::kTapCount % 4 == 0, "dot2 unrolls by 4");
}

void ShaperProcessor::prepare (double, int)
{
    lineL.assign ((size_t) (2 * kTapCount), 0.0f);
    lineR.assign ((size_t) (2 * kTapCount), 0.0f);
    writeIdx = 0;

    // Reserve everything the audio thread will ever copy into, so swapping
    // filters never allocates there.
    for (auto* set : { &designed, &active, &previous })
    {
        set->identity = true;
        set->reversed.clear();
        set->reversed.reserve ((size_t) kTapCount);
    }

    appliedSerial  = -1;              // pick up whatever is pending on the first block
    appliedEnabled = false;
    fadePos        = kFadeSamples;
}

void ShaperProcessor::reset()
{
    std::fill (lineL.begin(), lineL.end(), 0.0f);
    std::fill (lineR.begin(), lineR.end(), 0.0f);
    writeIdx = 0;
    fadePos  = kFadeSamples;
}

void ShaperProcessor::setFilter (const float* t, int count)
{
    const juce::SpinLock::ScopedLockType sl (lock);

    count = juce::jlimit (0, kTapCount, count);
    if (t == nullptr || count == 0)
    {
        pendingIdentity = true;
    }
    else
    {
        pendingTaps.assign ((size_t) kTapCount, 0.0f);
        for (int k = 0; k < count; ++k)
            pendingTaps[(size_t) (kTapCount - 1 - k)] = t[k];
        pendingIdentity = false;
    }
    pendingSerial.fetch_add (1);
}

void ShaperProcessor::process (float* L, float* R, int n)
{
    if (lineL.empty() || n <= 0) return;

    // Only start a new transition once the previous crossfade has finished.
    if (fadePos >= kFadeSamples)
    {
        bool designChanged = false;
        if (pendingSerial.load() != appliedSerial)
        {
            const juce::SpinLock::ScopedTryLockType tl (lock);
            if (tl.isLocked())
            {
                designed.identity = pendingIdentity;
                if (! pendingIdentity)
                    designed.reversed.assign (pendingTaps.begin(), pendingTaps.end());
                appliedSerial = pendingSerial.load();
                designChanged = true;
            }
        }

        const bool en = enabled.load();
        if (en != appliedEnabled || (en && designChanged))
        {
            std::swap (previous, active);
            active.identity = ! en || designed.identity;
            if (! active.identity)
                active.reversed.assign (designed.reversed.begin(), designed.reversed.end());

            appliedEnabled = en;
            fadePos = (previous.identity && active.identity) ? kFadeSamples : 0;
        }
    }

    const bool stereo = (R != nullptr);
    float* dl = lineL.data();
    float* dr = lineR.data();
    constexpr int M = kTapCount;

    for (int i = 0; i < n; ++i)
    {
        const int w = writeIdx;
        dl[w] = dl[w + M] = L[i];
        if (stereo) dr[w] = dr[w + M] = R[i];

        // Identity = a pure kLatency delay (same latency as the FIR).
        float yL, yR;
        if (active.identity) { yL = dl[w + M - kLatency]; yR = dr[w + M - kLatency]; }
        else                 dot2 (active.reversed.data(), dl + w + 1, dr + w + 1, M, yL, yR);

        if (fadePos < kFadeSamples)
        {
            float pL, pR;
            if (previous.identity) { pL = dl[w + M - kLatency]; pR = dr[w + M - kLatency]; }
            else                   dot2 (previous.reversed.data(), dl + w + 1, dr + w + 1, M, pL, pR);

            const float g = ((float) fadePos + 0.5f) / (float) kFadeSamples;
            yL = pL + (yL - pL) * g;
            yR = pR + (yR - pR) * g;
            ++fadePos;
        }

        L[i] = yL;
        if (stereo) R[i] = yR;
        writeIdx = (w + 1) & (M - 1);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  FIR design (frequency-sampled, linear-phase, 1/3-octave smoothed)
// ─────────────────────────────────────────────────────────────────────────────

void ShaperProcessor::buildMatchFilter (const float* targetBins, const float* liveBins,
                                        int numBins, double sampleRate, float amount,
                                        std::vector<float>& outTaps,
                                        std::vector<float>* outCurveDb)
{
    // Default: identity (unit impulse at the centre tap).
    outTaps.assign ((size_t) kTapCount, 0.0f);
    outTaps[(size_t) kLatency] = 1.0f;

    const int n = juce::jmin (numBins, kNumBins);
    if (outCurveDb != nullptr) outCurveDb->assign ((size_t) juce::jmax (0, n), 0.0f);
    if (n <= 1 || targetBins == nullptr || liveBins == nullptr || sampleRate <= 0.0) return;

    const double binHz = sampleRate / (double) kDesignSize;

    // 1. dB difference (target − live) where both sides have something to
    //    compare; the other bins are filled from their neighbours below.
    std::vector<float> diff ((size_t) n, 0.0f);
    std::vector<char>  valid ((size_t) n, 0);
    double wSum = 0.0, dSum = 0.0;
    for (int i = 1; i < n; ++i)
    {
        const float t = targetBins[i], l = liveBins[i];
        if (t <= 0.0f || l < kLiveFloor) continue;

        const float d = 20.0f * std::log10 (t / l);
        diff[(size_t) i]  = d;
        valid[(size_t) i] = 1;

        // Octave-weighted (1/f) mean over the band where both are reliable.
        const double f = (double) i * binHz;
        if (f >= 40.0 && f <= 16000.0) { wSum += 1.0 / f; dSum += d / f; }
    }
    if (wSum <= 0.0) return;

    // 2. Level-neutral: remove the mean, then clamp.
    const float mean = (float) (dSum / wSum);
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
    std::vector<float> smoothed ((size_t) n);
    {
        const float halfWin = std::pow (2.0f, 1.0f / 6.0f);
        std::vector<double> prefix ((size_t) n + 1, 0.0);
        for (int i = 0; i < n; ++i) prefix[(size_t) i + 1] = prefix[(size_t) i] + diff[(size_t) i];

        for (int i = 0; i < n; ++i)
        {
            const int lo = juce::jlimit (0, n - 1, (int) std::floor ((float) i / halfWin));
            const int hi = juce::jlimit (0, n - 1, (int) std::ceil  ((float) i * halfWin));
            smoothed[(size_t) i] = (float) ((prefix[(size_t) hi + 1] - prefix[(size_t) lo]) / (hi - lo + 1));
        }
    }

    const float amt = juce::jlimit (0.0f, 1.0f, amount);
    if (outCurveDb != nullptr)
        for (int i = 0; i < n; ++i)
            (*outCurveDb)[(size_t) i] = smoothed[(size_t) i] * amt;
    if (amt <= 0.0f) return;

    // 4. Magnitude on the design grid (0 … Nyquist inclusive).
    constexpr int N = kDesignSize;
    std::vector<float> mag ((size_t) N / 2 + 1);
    for (int k = 0; k <= N / 2; ++k)
        mag[(size_t) k] = std::pow (10.0f, smoothed[(size_t) juce::jmin (k, n - 1)] * amt / 20.0f);

    // 5. Linear phase centred exactly on kLatency, inverse FFT. JUCE's
    //    inverse perform() already applies 1/N.
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

    // 6. Keep the kTapCount taps around the centre under a periodic Hann
    //    window (w[kLatency] = 1, w[0] = 0): exactly symmetric about kLatency,
    //    so the group delay is exactly kLatency samples.
    for (int m = 0; m < kTapCount; ++m)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * (double) m / (double) kTapCount);
        outTaps[(size_t) m] = (float) (imp[(size_t) m].real() * w);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Manual-mode target: hand-drawn trace (Hz, dB) → linear magnitudes
// ─────────────────────────────────────────────────────────────────────────────

void ShaperProcessor::buildTargetFromCurve (const std::vector<TracePoint>& curve,
                                            int numBins, double sampleRate,
                                            std::vector<float>& outTarget)
{
    const int n = juce::jmin (numBins, kNumBins);
    outTarget.assign ((size_t) juce::jmax (0, n), 0.0f);
    if (curve.size() < 2 || sampleRate <= 0.0) return;

    const double binHz = sampleRate / (double) kDesignSize;
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

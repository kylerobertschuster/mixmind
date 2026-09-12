#include "ShaperProcessor.h"
#include <cmath>

void ShaperProcessor::prepare (double sr, int)
{
    sampleRate = sr;
    delayL.assign (kDelayLen, 0.0f);
    delayR.assign (kDelayLen, 0.0f);
    curTaps.assign (kTapCount, 0.0f);
    writeIdx = 0;
}

void ShaperProcessor::reset()
{
    juce::zeromem (delayL.data(), sizeof (float) * delayL.size());
    juce::zeromem (delayR.data(), sizeof (float) * delayR.size());
    writeIdx = 0;
}

void ShaperProcessor::setFilter (const float* t, int count)
{
    const juce::SpinLock::ScopedLockType sl (lock);

    count = juce::jlimit (0, kTapCount, count);
    if (count <= 0 || t == nullptr)
    {
        activeTaps.store (0);
        return;
    }

    const int back = 1 - activeIdx.load();
    taps[back].assign (t, t + count);
    activeTaps.store (count);
    activeIdx.store (back);
}

void ShaperProcessor::process (const float* inL, const float* inR, float* outL, float* outR, int n)
{
    if (!enabled.load())
    {
        if (outL != inL) juce::FloatVectorOperations::copy (outL, inL, n);
        if (outR != inR) juce::FloatVectorOperations::copy (outR, inR, n);
        return;
    }

    int M = 0;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        M = activeTaps.load();
        if (M > 0)
            curTaps.assign (taps[activeIdx.load()].begin(), taps[activeIdx.load()].begin() + M);
    }

    if (M <= 0)
    {
        if (outL != inL) juce::FloatVectorOperations::copy (outL, inL, n);
        if (outR != inR) juce::FloatVectorOperations::copy (outR, inR, n);
        return;
    }

    const float* h = curTaps.data();
    auto& dl = delayL;
    auto& dr = delayR;

    for (int i = 0; i < n; ++i)
    {
        dl[writeIdx] = inL[i];
        dr[writeIdx] = inR[i];

        float aL = 0.0f, aR = 0.0f;
        int j = writeIdx;
        for (int k = 0; k < M; ++k)
        {
            const float c = h[k];
            aL += c * dl[j];
            aR += c * dr[j];
            j = (j - 1) & kDelayMask;
        }

        outL[i] = aL;
        outR[i] = aR;
        writeIdx = (writeIdx + 1) & kDelayMask;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  FIR design (frequency-sampled, linear-phase, 1/3-octave smoothed)
// ─────────────────────────────────────────────────────────────────────────────

void ShaperProcessor::buildMatchFilter (const float* targetBins, const float* liveBins,
                                        int numBins, float amount,
                                        std::vector<float>& outTaps)
{
    outTaps.assign (kTapCount, 0.0f);
    const int n = juce::jmin (numBins, kNumBins);
    if (n <= 0 || targetBins == nullptr || liveBins == nullptr) return;

    // 1. dB difference (target − live), clamped; don't boost silence.
    std::vector<float> diff ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        const float tDb = targetBins[i] > 1e-5f ? 20.0f * std::log10 (targetBins[i]) : -100.0f;
        const float lDb = liveBins[i]     > 1e-5f ? 20.0f * std::log10 (liveBins[i])     : -100.0f;

        float d = juce::jlimit (-24.0f, 24.0f, tDb - lDb);
        if (lDb < -80.0f) d = juce::jmin (d, 0.0f);   // nothing to boost below the noise floor
        diff[(size_t) i] = d;
    }

    // 2. 1/3-octave smoothing (prefix sums over a multiplicative frequency window).
    std::vector<float> smoothed ((size_t) n);
    {
        const float halfWin = std::pow (2.0f, 1.0f / 6.0f);   // f · 2^(±1/6)
        std::vector<double> prefix ((size_t) n + 1, 0.0);
        for (int i = 0; i < n; ++i) prefix[(size_t) i + 1] = prefix[(size_t) i] + diff[(size_t) i];

        for (int i = 0; i < n; ++i)
        {
            const int lo = juce::jlimit (0, n - 1, (int) std::floor ((float) i / halfWin));
            const int hi = juce::jlimit (0, n - 1, (int) std::ceil  ((float) i * halfWin));
            const int cnt = hi - lo + 1;
            smoothed[(size_t) i] = (cnt > 0) ? (float)((prefix[(size_t) hi + 1] - prefix[(size_t) lo]) / cnt)
                                             : diff[(size_t) i];
        }
    }

    // 3. Apply amount → linear magnitude response over the design grid.
    const int N = kDesignSize;
    std::vector<float> mag ((size_t) N / 2 + 1, 0.0f);
    const float amt = juce::jlimit (0.0f, 1.0f, amount);
    for (int i = 0; i < n; ++i)
        mag[(size_t) i] = std::pow (10.0f, smoothed[(size_t) i] * amt / 20.0f);

    // 4. Frequency-sampled linear-phase FIR (inverse FFT).
    juce::dsp::FFT fft (kDesignOrder);
    std::vector<juce::dsp::Complex<float>> spec ((size_t) N), imp ((size_t) N);
    const float phaseStep = -juce::MathConstants<float>::pi * (float)(N - 1) / (float) N;
    for (int k = 0; k <= N / 2; ++k)
    {
        const float ph = phaseStep * (float) k;
        spec[(size_t) k] = { mag[(size_t) k] * std::cos (ph), mag[(size_t) k] * std::sin (ph) };
    }
    for (int k = N / 2 + 1; k < N; ++k)
        spec[(size_t) k] = std::conj (spec[(size_t)(N - k)]);

    fft.perform (spec.data(), imp.data(), true);   // inverse (already 1/N-scaled)

    // 5. Hann-window the full impulse, truncate to the central kTapCount taps.
    std::vector<float> h ((size_t) N);
    for (int i = 0; i < N; ++i)
    {
        const float w = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float)(N - 1));
        h[(size_t) i] = imp[(size_t) i].real() * w;
    }

    const int start = (N - kTapCount) / 2;
    float sum = 0.0f;
    for (int i = 0; i < kTapCount; ++i)
    {
        outTaps[(size_t) i] = h[(size_t)(start + i)];
        sum += outTaps[(size_t) i];
    }

    // 6. DC-normalise — preserve the broadband level; only reshape the spectrum.
    if (std::abs (sum) > 1e-9f)
        for (auto& v : outTaps) v /= sum;
    else
        outTaps.assign (kTapCount, 0.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Manual-mode target: hand-drawn trace → linear-magnitude spectrum
// ─────────────────────────────────────────────────────────────────────────────

void ShaperProcessor::buildTargetFromCurve (const juce::Array<std::pair<float, float>>& curve,
                                            int numBins, double sampleRate,
                                            std::vector<float>& outTarget)
{
    const int n = juce::jmin (numBins, kNumBins);
    outTarget.assign ((size_t) n, 0.0f);
    if (curve.size() < 2) return;

    const float nyquist = (float) sampleRate * 0.5f;

    // The analysis grid starts at DC, but the trace is only drawn from its first
    // point upward — 20 Hz by default, and higher when a focus band is selected
    // (TelemetryCanvas raises axisMin to the band start). Outside the drawn
    // range, hold the nearest endpoint flat, so a bin below the trace inherits
    // the LOWEST point's value rather than the highest. Bin 0 matters most:
    // buildMatchFilter DC-normalises by dividing the taps by their sum, and
    // sum(taps) == H(0) == mag[0], so a wrong DC value mis-scales every tap.
    const float firstLf = std::log10 (juce::jmax (1.0f, curve.getReference (0).first));
    const float lastLf  = std::log10 (juce::jmax (1.0f, curve.getReference (curve.size() - 1).first));

    // The trace lives in the same space as the spectrum plot: y is LINEAR
    // magnitude (0..1), identical to the reference/live bins. Interpolate the
    // trace points (freqHz, magnitude01) onto the analysis bins directly.
    for (int i = 0; i < n; ++i)
    {
        const float f  = (float) i * nyquist / (float) n;
        const float lf = f > 1.0f ? std::log10 (f) : 0.0f;

        float mag;

        if (lf <= firstLf)
            mag = curve.getReference (0).second;
        else if (lf >= lastLf)
            mag = curve.getReference (curve.size() - 1).second;
        else
        {
            // The guards above mean a containing segment exists; the walk is
            // kept as a safety net for an unsorted curve.
            int k = 0;
            while (k < curve.size() - 1)
            {
                const float lo = std::log10 (juce::jmax (1.0f, curve.getReference (k).first));
                const float hi = std::log10 (juce::jmax (1.0f, curve.getReference (k + 1).first));
                if (lf >= lo && lf <= hi) break;
                ++k;
            }

            const auto& a = curve.getReference (k);
            const auto& b = curve.getReference (juce::jmin (k + 1, curve.size() - 1));
            const float lo = std::log10 (juce::jmax (1.0f, a.first));
            const float hi = std::log10 (juce::jmax (1.0f, b.first));
            const float t  = (hi > lo) ? juce::jlimit (0.0f, 1.0f, (lf - lo) / (hi - lo)) : 0.0f;
            mag = a.second + (b.second - a.second) * t;
        }

        outTarget[(size_t) i] = juce::jlimit (0.0f, 1.0f, mag);
    }
}

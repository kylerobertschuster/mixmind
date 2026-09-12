#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
//  Shared helpers for the MixMind tests.
//  Everything here is deterministic — fixed-seed noise, integer-cycle sines —
//  so a failure is always reproducible.
// ─────────────────────────────────────────────────────────────────────────────
namespace TestSignals
{
    constexpr double kSampleRate = 48000.0;

    // Stereo sine pair at an exact number of cycles per block-ish length, so no
    // windowing discontinuity confuses the spectrum checks.
    inline juce::AudioBuffer<float> stereoSines (int numSamples, double lHz, double rHz,
                                                 float lAmp = 0.5f, float rAmp = 0.5f)
    {
        juce::AudioBuffer<float> b (2, numSamples);

        for (int i = 0; i < numSamples; ++i)
        {
            const double t = (double) i / kSampleRate;
            b.setSample (0, i, lAmp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * lHz * t));
            b.setSample (1, i, rAmp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * rHz * t));
        }

        return b;
    }

    // Amplitude for a given RMS level in dBFS.
    inline float amplitudeForRmsDb (float dB) { return std::pow (10.0f, dB / 20.0f) * (float) juce::MathConstants<double>::sqrt2; }

    // Deterministic pseudo-noise (xorshift) — for "garbage in, finite out" checks.
    inline juce::AudioBuffer<float> noise (int numSamples, float amp, uint32_t seed = 0x1234567u)
    {
        juce::AudioBuffer<float> b (2, numSamples);
        uint32_t s = seed;

        for (int i = 0; i < numSamples; ++i)
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                s ^= s << 13; s ^= s >> 17; s ^= s << 5;
                b.setSample (ch, i, amp * ((float) (s & 0xFFFFFF) / 8388608.0f - 1.0f));
            }
        }

        return b;
    }
}

// ── DSP assertions ────────────────────────────────────────────────────────
namespace TestDsp
{
    struct Peak { float value { 0.0f }; int index { 0 }; };

    inline Peak maxAbs (const float* x, int n)
    {
        Peak p;
        for (int i = 0; i < n; ++i)
            if (std::abs (x[i]) > std::abs (p.value)) { p.value = x[i]; p.index = i; }

        return p;
    }

    // Largest absolute difference between x and y (same length).
    inline float maxAbsDiff (const float* x, const float* y, int n)
    {
        float m = 0.0f;
        for (int i = 0; i < n; ++i)
            m = juce::jmax (m, std::abs (x[i] - y[i]));

        return m;
    }

    // Lag (in samples) at which b best aligns with a, searched over ±maxLag.
    // Positive lag means b is delayed by that many samples relative to a.
    inline int bestLag (const float* a, const float* b, int n, int maxLag)
    {
        int best = 0;
        double bestScore = -1.0e30;

        for (int lag = -maxLag; lag <= maxLag; ++lag)
        {
            double dot = 0.0;
            for (int i = 0; i < n; ++i)
            {
                const int j = i - lag;
                if (j >= 0 && j < n) dot += (double) a[i] * (double) b[j];
            }

            if (dot > bestScore) { bestScore = dot; best = lag; }
        }

        return best;
    }

    // Magnitude response of a real FIR at a frequency, by direct evaluation of
    // H(e^jw) — cheaper and more precise than FFT-interpolating for a few points.
    inline float firMagnitudeAt (const float* h, int m, double freqHz, double sampleRate)
    {
        const double w = 2.0 * juce::MathConstants<double>::pi * freqHz / sampleRate;
        double re = 0.0, im = 0.0;

        for (int k = 0; k < m; ++k)
        {
            re += (double) h[k] * std::cos (-w * k);
            im += (double) h[k] * std::sin (-w * k);
        }

        return (float) std::sqrt (re * re + im * im);
    }

    inline float dbOf (float linearMag) { return 20.0f * std::log10 (juce::jmax (1.0e-9f, linearMag)); }
}

#include "LoudnessMeter.h"
#include <cmath>

namespace
{
    struct Biquad { float b0, b1, b2, a1, a2; };

    // ── K-weighting, designed from the standard ───────────────────────────
    // ITU-R BS.1770-4 gives the filter as an analog prototype plus prewarped
    // bilinear transform. These two functions reproduce the standard's published
    // 48 kHz coefficient table to floating-point precision and generalise it to
    // any sample rate.
    //
    // Do NOT swap this for a generic RBJ shelf (juce::dsp::IIR::Coefficients::
    // makeHighShelf with this f0/Q/gain): that filter's shape is measurably
    // different — about −0.26 dB at 1 kHz and −0.49 dB near 1.7 kHz, so every
    // LUFS reading would sit a fraction of a dB low.
    inline Biquad kWeightingShelf (double sr)
    {
        constexpr double f0 = 1681.974450955533;
        constexpr double Q  = 0.7071752369554196;
        constexpr double G  = 3.999843853973347;

        const double K  = std::tan (juce::MathConstants<double>::pi * f0 / sr);
        const double Vh = std::pow (10.0, G / 20.0);
        const double Vb = std::pow (Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;

        return { (float) ((Vh + Vb * K / Q + K * K) / a0),
                 (float) (2.0 * (K * K - Vh) / a0),
                 (float) ((Vh - Vb * K / Q + K * K) / a0),
                 (float) (2.0 * (K * K - 1.0) / a0),
                 (float) ((1.0 - K / Q + K * K) / a0) };
    }

    inline Biquad kWeightingHighPass (double sr)
    {
        constexpr double f0 = 38.13547087602444;
        constexpr double Q  = 0.5003270373238773;

        const double K  = std::tan (juce::MathConstants<double>::pi * f0 / sr);
        const double a0 = 1.0 + K / Q + K * K;

        // Numerator is [1, -2, 1] unnormalised: the standard leaves the shelf's
        // high-frequency gain at ~+0.04 dB rather than exactly unity.
        return { 1.0f, -2.0f, 1.0f,
                 (float) (2.0 * (K * K - 1.0) / a0),
                 (float) ((1.0 - K / Q + K * K) / a0) };
    }
}

void LoudnessMeter::prepare (double sampleRate, int bs)
{
    sr        = sampleRate;
    blockSize = juce::jmax (1, bs);
    segTarget = juce::jmax (1, (int) std::lround (sr * 0.1));

    // K-weighting (ITU-R BS.1770): high-shelf then high-pass, per channel.
    juce::dsp::ProcessSpec spec { (double) sr, (juce::uint32) blockSize, 1 };
    const auto shelf = kWeightingShelf (sr);
    const auto hp    = kWeightingHighPass (sr);

    for (size_t ch = 0; ch < (size_t) kChannels; ++ch)
    {
        k1[ch].prepare (spec);
        k2[ch].prepare (spec);
        k1[ch].coefficients = new juce::dsp::IIR::Coefficients<float> (
            shelf.b0, shelf.b1, shelf.b2, 1.0f, shelf.a1, shelf.a2);
        k2[ch].coefficients = new juce::dsp::IIR::Coefficients<float> (
            hp.b0, hp.b1, hp.b2, 1.0f, hp.a1, hp.a2);
    }

    // 4× polyphase upsampler (windowed-sinc lowpass, cutoff 0.5/4 = 0.125 cyc/sample).
    const int protoLen = kPhaseTaps * kUp;
    std::vector<float> proto ((size_t) protoLen);
    const float fc = 0.5f / (float) kUp;
    float sum = 0.0f;
    for (int i = 0; i < protoLen; ++i)
    {
        const float t = (float) i - (float)(protoLen - 1) * 0.5f;
        const float w = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float)(protoLen - 1));
        const float sinc = (std::abs (t) < 1e-6f) ? 2.0f * fc
                                                  : (float)(std::sin (2.0 * juce::MathConstants<float>::pi * fc * t)
                                                            / (juce::MathConstants<float>::pi * t));
        proto[(size_t) i] = sinc * w;
        sum += proto[(size_t) i];
    }
    const float scale = (float) kUp / sum;   // unity DC gain through the upsampler
    for (auto& v : proto) v *= scale;
    for (int p = 0; p < kUp; ++p)
        for (int k = 0; k < kPhaseTaps; ++k)
            phases[(size_t) p][(size_t) k] = proto[(size_t)(p + k * kUp)];

    reset();
}

void LoudnessMeter::reset()
{
    for (size_t ch = 0; ch < (size_t) kChannels; ++ch)
    {
        k1[ch].reset();
        k2[ch].reset();

        for (size_t p = 0; p < (size_t) kUp; ++p)
            hist[ch][p].fill (0.0f);
    }

    blockEnergy = 0.0f;
    blockRmsEnergy = { };
    blockCount = 0;
    segEnergy = 0.0f;
    segCount = 0;
    w400.clear(); w3000.clear();
    s400 = s3000 = 0; sum400 = sum3000 = 0.0f;
    gateSegments.clear();
    gateBlocks.clear();
    truePeak = 0.0f;
    integrated.set (-120.0f);
    momentary.set  (-120.0f);
    shortTerm.set  (-120.0f);
    truePeakDb.set (-120.0f);
    rmsDb.set      (-120.0f);
}

float LoudnessMeter::toLufs (float energy, int count)
{
    if (count <= 0 || energy <= 0.0f) return -120.0f;
    return -0.691f + 10.0f * std::log10 (energy / (float) count);
}

void LoudnessMeter::pushWindow (std::deque<BlockEnergy>& w, int& samples, float& sum,
                                float energy, int count, int targetSamples)
{
    w.push_back ({ energy, count });
    sum += energy;
    samples += count;
    while (samples > targetSamples && !w.empty())
    {
        sum -= w.front().energy;
        samples -= w.front().count;
        w.pop_front();
    }
}

// One 100 ms segment completed: slide the 400 ms window and re-run the gate.
void LoudnessMeter::pushGatingSegment()
{
    gateSegments.push_back ({ segEnergy, segCount });
    segEnergy = 0.0f;
    segCount  = 0;

    if ((int) gateSegments.size() > kSegmentsPerBlock)
        gateSegments.pop_front();

    if ((int) gateSegments.size() < kSegmentsPerBlock)
        return;                       // the first 400 ms window isn't complete yet

    float energy = 0.0f;
    int   count  = 0;
    for (const auto& s : gateSegments) { energy += s.energy; count += s.count; }
    gateBlocks.push_back ({ energy, count });

    // Absolute gate (−70 LUFS) over energies, then the relative gate (−10 LU)
    // measured from the mean energy of what survived, then the mean energy of
    // what survived both. Averaging energies, not LUFS values, is what the
    // standard specifies.
    double absSum = 0.0; int absCnt = 0;
    for (const auto& b : gateBlocks)
        if (toLufs (b.energy, b.count) > -70.0f) { absSum += b.energy; absCnt += b.count; }

    if (absCnt <= 0)
        return;

    const float absLufs = toLufs ((float) absSum, absCnt);
    const float relThr  = absLufs - 10.0f;

    double relSum = 0.0; int relCnt = 0;
    for (const auto& b : gateBlocks)
        if (toLufs (b.energy, b.count) > relThr) { relSum += b.energy; relCnt += b.count; }

    integrated.set (relCnt > 0 ? toLufs ((float) relSum, relCnt) : absLufs);
}

void LoudnessMeter::process (const float* L, const float* R, int n)
{
    if (n <= 0 || L == nullptr) return;

    // A null right channel means a genuine mono source: one channel of loudness.
    const int nch = (R != nullptr) ? 2 : 1;
    const float* in[kChannels] { L, (R != nullptr) ? R : L };

    for (int i = 0; i < n; ++i)
    {
        float energy = 0.0f;

        for (int ch = 0; ch < nch; ++ch)
        {
            const float x = in[ch][i];

            // True peak: 4× oversample via polyphase FIR, track max |·| per channel.
            for (int p = 0; p < kUp; ++p)
            {
                auto& h = hist[(size_t) ch][(size_t) p];
                for (int k = kPhaseTaps - 1; k > 0; --k)
                    h[(size_t) k] = h[(size_t)(k - 1)];
                h[0] = x;

                float y = 0.0f;
                for (int k = 0; k < kPhaseTaps; ++k)
                    y += phases[(size_t) p][(size_t) k] * h[(size_t) k];
                truePeak = juce::jmax (truePeak, std::abs (y));
            }

            // K-weighted energy (summed over channels, G = 1.0) + unweighted RMS.
            const float kw = k2[(size_t) ch].processSample (k1[(size_t) ch].processSample (x));
            energy += kw * kw;
            blockRmsEnergy[(size_t) ch] += x * x;
        }

        blockEnergy += energy;
        ++blockCount;

        // Gating grid: exact 100 ms segments regardless of the host's block size.
        segEnergy += energy;
        if (++segCount >= segTarget)
            pushGatingSegment();
    }

    // ── Windows (400 ms momentary, 3 s short-term) ────────────────────────
    const int target400  = (int) std::lround (sr * 0.4);
    const int target3000 = (int) std::lround (sr * 3.0);
    pushWindow (w400,  s400,  sum400,  blockEnergy, blockCount, target400);
    pushWindow (w3000, s3000, sum3000, blockEnergy, blockCount, target3000);

    momentary.set (toLufs (sum400,  s400));
    shortTerm.set (toLufs (sum3000, s3000));

    // RMS of the loudest channel — the same basis as the per-channel true peak,
    // so the crest factor (peak − rms) means something for hard-panned material.
    float rms = 0.0f;
    for (int ch = 0; ch < nch; ++ch)
        rms = juce::jmax (rms, std::sqrt (blockRmsEnergy[(size_t) ch] / (float) blockCount));
    rmsDb.set (juce::Decibels::gainToDecibels (rms));

    // ── True-peak readout (slow release) ──────────────────────────────────
    const float blockPeak = truePeak;
    const float release   = std::exp (-(float) n / ((float) sr * 1.5f));
    truePeak = juce::jmax (blockPeak, truePeak * release);
    truePeakDb.set (juce::Decibels::gainToDecibels (truePeak));

    blockEnergy = 0.0f;
    blockRmsEnergy = { };
    blockCount = 0;
}

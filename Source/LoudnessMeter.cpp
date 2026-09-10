#include "LoudnessMeter.h"
#include <cmath>

void LoudnessMeter::prepare (double sampleRate, int bs)
{
    sr        = sampleRate;
    blockSize = juce::jmax (1, bs);
    gateTarget = (int) std::lround (sr * 0.4);

    // K-weighting (ITU-R BS.1770): high-shelf then high-pass.
    juce::dsp::ProcessSpec spec { (double) sr, (juce::uint32) blockSize, 1 };
    k1.prepare (spec);
    k2.prepare (spec);
    k1.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighShelf (
        sr, 1681.9744509555319, 0.7071752369554196f,
        juce::Decibels::decibelsToGain (3.999843853973347f));
    k2.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (
        sr, 38.13547087602444, 0.5003270373238773f);

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
    k1.reset();
    k2.reset();
    for (int p = 0; p < kUp; ++p)
        hist[p].fill (0.0f);

    blockEnergy = blockRmsEnergy = blockCount = 0;
    gateEnergy = gateCount = 0;
    w400.clear(); w3000.clear();
    s400 = s3000 = 0; sum400 = sum3000 = 0.0f;
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

void LoudnessMeter::process (const float* L, const float* R, int n)
{
    if (n <= 0 || L == nullptr) return;

    const float* r = (R != nullptr) ? R : L;

    for (int i = 0; i < n; ++i)
    {
        const float m = (L[i] + r[i]) * 0.5f;

        // True peak: 4× oversample via polyphase FIR, track max |·|.
        for (int p = 0; p < kUp; ++p)
        {
            for (int k = kPhaseTaps - 1; k > 0; --k)
                hist[p][(size_t) k] = hist[p][(size_t)(k - 1)];
            hist[p][0] = m;

            float y = 0.0f;
            for (int k = 0; k < kPhaseTaps; ++k)
                y += phases[(size_t) p][(size_t) k] * hist[p][(size_t) k];
            truePeak = juce::jmax (truePeak, std::abs (y));
        }

        // K-weighted energy + unweighted RMS energy.
        const float kw = k2.processSample (k1.processSample (m));
        blockEnergy    += kw * kw;
        blockRmsEnergy += m * m;
        ++blockCount;
    }

    // ── Windows (400 ms momentary, 3 s short-term) ────────────────────────
    const int target400  = (int) std::lround (sr * 0.4);
    const int target3000 = (int) std::lround (sr * 3.0);
    pushWindow (w400,  s400,  sum400,  blockEnergy, blockCount, target400);
    pushWindow (w3000, s3000, sum3000, blockEnergy, blockCount, target3000);

    momentary.set (toLufs (sum400,  s400));
    shortTerm.set (toLufs (sum3000, s3000));
    rmsDb.set (juce::Decibels::gainToDecibels (std::sqrt (blockRmsEnergy / (float) blockCount)));

    // ── Integrated gating block (400 ms) ──────────────────────────────────
    gateEnergy += blockEnergy;
    gateCount  += blockCount;
    if (gateCount >= gateTarget)
    {
        gateBlocks.push_back (toLufs (gateEnergy, gateCount));
        gateEnergy = gateCount = 0;

        // Absolute gate (−70 LUFS), then relative gate (−10 LU).
        double absSum = 0.0; int absCnt = 0;
        for (float v : gateBlocks) if (v > -70.0f) { absSum += v; ++absCnt; }
        if (absCnt > 0)
        {
            const float absMean = (float)(absSum / absCnt);
            const float relThr  = absMean - 10.0f;
            double relSum = 0.0; int relCnt = 0;
            for (float v : gateBlocks) if (v > relThr) { relSum += v; ++relCnt; }
            integrated.set (relCnt > 0 ? (float)(relSum / relCnt) : absMean);
        }
    }

    // ── True-peak readout (slow release) ──────────────────────────────────
    const float blockPeak = truePeak;
    const float release   = std::exp (-(float) n / ((float) sr * 1.5f));
    truePeak = juce::jmax (blockPeak, truePeak * release);
    truePeakDb.set (juce::Decibels::gainToDecibels (truePeak));

    blockEnergy = blockRmsEnergy = blockCount = 0;
}

#include "LoudnessMeter.h"
#include <cmath>
#include <algorithm>

float LoudnessMeter::toLufs (double energy, double count)
{
    if (count <= 0.0 || energy <= 0.0) return kSilenceDb;
    return juce::jmax (kSilenceDb, (float) (-0.691 + 10.0 * std::log10 (energy / count)));
}

// K-weighting for an arbitrary sample rate. These are the bilinear-transform
// forms of the BS.1770 pre-filter (shelf) and RLB high-pass; at 48 kHz they
// reproduce the coefficients tabulated in the standard. RBJ cookbook filters
// with the same f0/Q are close but not exact (the high-pass differs by
// ~0.04 dB of broadband gain), so the analogue prototypes are used directly.
void LoudnessMeter::designKWeighting (double fs, Biquad& s, Biquad& h)
{
    {
        const double f0 = 1681.974450955533;
        const double G  = 3.999843853973347;
        const double Q  = 0.7071752369554196;
        const double K  = std::tan (juce::MathConstants<double>::pi * f0 / fs);
        const double Vh = std::pow (10.0, G / 20.0);
        const double Vb = std::pow (Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;

        s.b0 = (Vh + Vb * K / Q + K * K) / a0;
        s.b1 = 2.0 * (K * K - Vh) / a0;
        s.b2 = (Vh - Vb * K / Q + K * K) / a0;
        s.a1 = 2.0 * (K * K - 1.0) / a0;
        s.a2 = (1.0 - K / Q + K * K) / a0;
    }
    {
        const double f0 = 38.13547087602444;
        const double Q  = 0.5003270373238773;
        const double K  = std::tan (juce::MathConstants<double>::pi * f0 / fs);
        const double a0 = 1.0 + K / Q + K * K;

        h.b0 = 1.0;
        h.b1 = -2.0;
        h.b2 = 1.0;
        h.a1 = 2.0 * (K * K - 1.0) / a0;
        h.a2 = (1.0 - K / Q + K * K) / a0;
    }
}

void LoudnessMeter::prepare (double sampleRate, int)
{
    sr        = sampleRate > 0.0 ? sampleRate : 48000.0;
    subLength = juce::jmax (1, (int) std::lround (sr * 0.1));

    for (int c = 0; c < kMaxChannels; ++c)
        designKWeighting (sr, shelf[c], highPass[c]);

    histEnergy.assign ((size_t) kHistBins, 0.0);
    histCount.assign  ((size_t) kHistBins, 0);

    // 4× polyphase interpolator: windowed-sinc low-pass, cutoff 0.125 cycles
    // per (oversampled) sample = the original Nyquist.
    const int protoLen = kPhaseTaps * kUp;
    std::vector<double> proto ((size_t) protoLen);
    const double fc = 0.5 / (double) kUp;
    double sum = 0.0;
    for (int i = 0; i < protoLen; ++i)
    {
        const double t = (double) i - (double) (protoLen - 1) * 0.5;
        const double w = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * (double) i / (double) (protoLen - 1));
        const double sinc = std::sin (2.0 * juce::MathConstants<double>::pi * fc * t) / (juce::MathConstants<double>::pi * t);
        proto[(size_t) i] = sinc * w;
        sum += proto[(size_t) i];
    }
    const double scale = (double) kUp / sum;   // unity DC gain per phase
    for (size_t p = 0; p < (size_t) kUp; ++p)
        for (size_t k = 0; k < (size_t) kPhaseTaps; ++k)
            phases[p][k] = (float) (proto[p + k * (size_t) kUp] * scale);

    reset();
}

void LoudnessMeter::reset()
{
    for (int c = 0; c < kMaxChannels; ++c)
    {
        shelf[c].clear();
        highPass[c].clear();
        tpHist[(size_t) c].fill (0.0f);
    }

    subEnergy = subRms = 0.0;
    subPeak = 0.0f;
    subCount = 0;

    ringEnergy.fill (0.0);
    ringRms.fill (0.0);
    ringPeak.fill (0.0f);
    subHead = subFilled = 0;

    std::fill (histEnergy.begin(), histEnergy.end(), 0.0);
    std::fill (histCount.begin(),  histCount.end(),  0);
    gatedEnergy = 0.0;
    gatedBlocks = 0;
    truePeakMax = 0.0f;

    integrated.set    (kSilenceDb);
    momentary.set     (kSilenceDb);
    shortTerm.set     (kSilenceDb);
    truePeakMaxDb.set (kSilenceDb);
    recentPeakDb.set  (kSilenceDb);
    rmsDb.set         (kSilenceDb);
}

void LoudnessMeter::process (const float* L, const float* R, int n)
{
    if (n <= 0 || L == nullptr || histCount.empty()) return;
    if (resetRequested.exchange (false)) reset();

    const int numCh = (R != nullptr) ? 2 : 1;
    const float* const ch[kMaxChannels] = { L, R };

    for (int i = 0; i < n; ++i)
    {
        double e = 0.0, u = 0.0;
        float pk = 0.0f;

        for (int c = 0; c < numCh; ++c)
        {
            const float x = ch[c][i];

            const double y = highPass[c].process (shelf[c].process ((double) x));
            e += y * y;
            u += (double) x * (double) x;

            // True peak: shift the history, evaluate the 4 interpolated phases.
            auto& h = tpHist[(size_t) c];
            for (size_t k = kPhaseTaps - 1; k > 0; --k)
                h[k] = h[k - 1];
            h[0] = x;

            float m = std::abs (x);
            for (const auto& ph : phases)
            {
                float acc = 0.0f;
                for (size_t k = 0; k < (size_t) kPhaseTaps; ++k)
                    acc += ph[k] * h[k];
                m = juce::jmax (m, std::abs (acc));
            }
            pk = juce::jmax (pk, m);
        }

        subEnergy += e;
        subRms    += u / (double) numCh;
        subPeak    = juce::jmax (subPeak, pk);
        truePeakMax = juce::jmax (truePeakMax, pk);

        if (++subCount >= subLength)
            closeSubBlock();
    }

    truePeakMaxDb.set (truePeakMax > 0.0f ? juce::Decibels::gainToDecibels (truePeakMax, kSilenceDb) : kSilenceDb);
}

void LoudnessMeter::closeSubBlock()
{
    lastStep = { subEnergy, subRms, subPeak, subCount };
    ++stepCount;

    ringEnergy[(size_t) subHead] = subEnergy;
    ringRms   [(size_t) subHead] = subRms;
    ringPeak  [(size_t) subHead] = subPeak;
    subHead   = (subHead + 1) % kSubPerShort;
    subFilled = juce::jmin (subFilled + 1, kSubPerShort);

    subEnergy = subRms = 0.0;
    subPeak = 0.0f;
    subCount = 0;

    // Sum the newest `count` sub-blocks of a ring.
    const auto recent = [this] (const auto& ring, int count)
    {
        double s = 0.0;
        for (int k = 1; k <= count; ++k)
            s += ring[(size_t) ((subHead - k + kSubPerShort) % kSubPerShort)];
        return s;
    };

    const double len = (double) subLength;

    if (subFilled >= kSubPerGate)
    {
        // Momentary loudness == the loudness of the newest gating block.
        const double blockEnergy = recent (ringEnergy, kSubPerGate);
        const float  blockLufs   = toLufs (blockEnergy, kSubPerGate * len);
        momentary.set (blockLufs);

        if (blockLufs > kHistMinLufs)
        {
            const int idx = juce::jlimit (0, kHistBins - 1, (int) ((blockLufs - kHistMinLufs) / kHistStep));
            const double meanSquare = blockEnergy / (kSubPerGate * len);
            histEnergy[(size_t) idx] += meanSquare;
            histCount [(size_t) idx] += 1;
            gatedEnergy += meanSquare;
            ++gatedBlocks;
            updateIntegrated();
        }

        // Short-term: 3 s once available (until then, what has been measured).
        const int stCount = subFilled;
        shortTerm.set (toLufs (recent (ringEnergy, stCount), stCount * len));
    }

    const double rmsEnergy = recent (ringRms, subFilled);
    rmsDb.set (rmsEnergy > 0.0 ? juce::jmax (kSilenceDb, (float) (10.0 * std::log10 (rmsEnergy / (subFilled * len))))
                               : kSilenceDb);

    float pk = 0.0f;
    for (int k = 0; k < subFilled; ++k)
        pk = juce::jmax (pk, ringPeak[(size_t) k]);
    recentPeakDb.set (pk > 0.0f ? juce::Decibels::gainToDecibels (pk, kSilenceDb) : kSilenceDb);
}

void LoudnessMeter::updateIntegrated()
{
    if (gatedBlocks == 0) return;

    // Relative threshold from the blocks that passed the absolute gate.
    const float absGated = toLufs (gatedEnergy, (double) gatedBlocks);
    const float relThr   = absGated - 10.0f;
    if (relThr <= kHistMinLufs)
    {
        integrated.set (absGated);
        return;
    }

    const int start = juce::jlimit (0, kHistBins, (int) std::ceil ((relThr - kHistMinLufs) / kHistStep));
    double e = 0.0;
    long   n = 0;
    for (size_t i = (size_t) start; i < (size_t) kHistBins; ++i)
    {
        e += histEnergy[i];
        n += histCount[i];
    }
    integrated.set (n > 0 ? toLufs (e, (double) n) : absGated);
}

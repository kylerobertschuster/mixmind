#include "ParametricEq.h"
#include <cmath>
#include <complex>

namespace
{
    constexpr double kPi = juce::MathConstants<double>::pi;

    // Second-order analog prototypes (RBJ cookbook forms, s normalised to the
    // band frequency). A = 10^(gain/40), so peak / shelf gain = A².
    enum class Proto { lowPass, highPass, notch, peak, lowShelf, highShelf };

    // |H(ju)|², u = ω / ω0.
    double analogMag2 (Proto p, double A, double Q, double u)
    {
        const double u2 = u * u, d = 1.0 - u2;
        switch (p)
        {
            case Proto::lowPass:   return 1.0 / (d * d + u2 / (Q * Q));
            case Proto::highPass:  return u2 * u2 / (d * d + u2 / (Q * Q));
            case Proto::notch:     return d * d / (d * d + u2 / (Q * Q));
            case Proto::peak:      return (d * d + u2 * A * A / (Q * Q)) / (d * d + u2 / (A * A * Q * Q));
            case Proto::lowShelf:
            {
                const double n = A - u2, m = 1.0 - A * u2, x = u2 * A / (Q * Q);
                return A * A * (n * n + x) / (m * m + x);
            }
            case Proto::highShelf:
            {
                const double n = 1.0 - A * u2, m = A - u2, x = u2 * A / (Q * Q);
                return A * A * (n * n + x) / (m * m + x);
            }
        }
        return 1.0;
    }

    // Natural frequency (× ω0) and damping of the prototype's poles.
    void poles (Proto p, double A, double Q, double& wn, double& zeta)
    {
        zeta = 1.0 / (2.0 * Q);
        wn   = 1.0;
        if (p == Proto::peak)      zeta = 1.0 / (2.0 * A * Q);
        if (p == Proto::lowShelf)  wn = 1.0 / std::sqrt (A);
        if (p == Proto::highShelf) wn = std::sqrt (A);
    }

    ParametricEq::Biquad invert (const ParametricEq::Biquad& h)
    {
        return { 1.0 / h.b0, h.a1 / h.b0, h.a2 / h.b0, h.b1 / h.b0, h.b2 / h.b0 };
    }

    // w0 in radians per sample (< π).
    ParametricEq::Biquad matched (Proto p, double A, double Q, double w0)
    {
        // A boosting high shelf / cutting low shelf has its poles above ω0
        // (possibly past Nyquist, where impulse invariance aliases). Its
        // inverse is the mirror shelf with poles below ω0: design that, invert.
        // A cutting bell is the exact inverse of the boosting one; its shape
        // lives in the zeros, which the three-point fit captures poorly, so it
        // is designed as the boost (shape in the poles) and inverted too.
        if ((p == Proto::highShelf && A > 1.0) || (p == Proto::lowShelf && A < 1.0) || (p == Proto::peak && A < 1.0))
            return invert (matched (p, 1.0 / A, Q, w0));

        double wn, zeta;
        poles (p, A, Q, wn, zeta);
        const double wp = wn * w0;

        // Poles: impulse-invariant image of the analog poles.
        ParametricEq::Biquad h;
        const double r = std::exp (-zeta * wp);
        if (zeta < 1.0) h.a1 = -2.0 * r * std::cos (wp * std::sqrt (1.0 - zeta * zeta));
        else            h.a1 = -2.0 * r * std::cosh (wp * std::sqrt (zeta * zeta - 1.0));
        h.a2 = r * r;

        // Zeros: fit |H|² to the analog magnitude at DC, ω0 and Nyquist using
        // |P(e^jω)|² = P0·φ0 + P1·φ1 + P2·φ2 with φ0 = cos²(ω/2),
        // φ1 = sin²(ω/2), φ2 = 4·φ0·φ1.
        const double A0 = (1.0 + h.a1 + h.a2) * (1.0 + h.a1 + h.a2);
        const double A1 = (1.0 - h.a1 + h.a2) * (1.0 - h.a1 + h.a2);
        const double A2 = -4.0 * h.a2;

        const double s  = std::sin (0.5 * w0);
        const double phi1 = s * s, phi0 = 1.0 - phi1, phi2 = 4.0 * phi0 * phi1;

        const double B0 = analogMag2 (p, A, Q, 0.0) * A0;
        const double B1 = analogMag2 (p, A, Q, kPi / w0) * A1;
        const double Bm = analogMag2 (p, A, Q, 1.0) * (A0 * phi0 + A1 * phi1 + A2 * phi2);
        const double B2 = (Bm - B0 * phi0 - B1 * phi1) / phi2;

        // Pinning the Nyquist gain to the analog value is what cramps wide
        // bands centred near Nyquist (the analog response goes on past it).
        // Keep DC and ω0 exact and choose the Nyquist term B1 by least squares
        // on the relative |H|² error from ω0/8 to 0.45·fs instead; errors
        // below −40 dB are weighted as if at −40 dB so a deep stopband does
        // not dominate. B2 follows from the ω0 constraint: B2 = c0 + c1·B1.
        double B1fit = B1, B2fit = B2;
        {
            const double c0 = (Bm - B0 * phi0) / phi2, c1 = -phi1 / phi2;
            const double lo = w0 / 8.0, hi = 0.9 * kPi;
            double num = 0.0, den = 0.0;
            constexpr int K = 48;
            const double step = std::pow (hi / lo, 1.0 / (K - 1));
            double w = lo;
            for (int k = 0; k < K && hi > lo; ++k, w *= step)
            {
                const double sw = std::sin (0.5 * w);
                const double f1 = sw * sw, f0 = 1.0 - f1, f2 = 4.0 * f0 * f1;
                const double Ad = A0 * f0 + A1 * f1 + A2 * f2;
                const double y  = analogMag2 (p, A, Q, w / w0) * Ad;
                const double wt = 1.0 / juce::jmax (y, 1.0e-4 * Ad);
                const double base  = (B0 * f0 + c0 * f2 - y) * wt;
                const double slope = (f1 + c1 * f2) * wt;
                num += base * slope;
                den += slope * slope;
            }
            if (den > 0.0 && -num / den >= 0.0)
            {
                B1fit = -num / den;
                B2fit = c0 + c1 * B1fit;
            }
        }

        const auto factor = [&h] (double b0sq, double b1sq, double b2)
        {
            const double sB0 = std::sqrt (juce::jmax (0.0, b0sq));
            const double sB1 = std::sqrt (juce::jmax (0.0, b1sq));
            const double W   = 0.5 * (sB0 + sB1);
            if (W * W + b2 < 0.0) return false;
            h.b0 = 0.5 * (W + std::sqrt (W * W + b2));
            h.b1 = 0.5 * (sB0 - sB1);
            h.b2 = h.b0 > 1.0e-30 ? -b2 / (4.0 * h.b0) : 0.0;
            return true;
        };
        if (! factor (B0, B1fit, B2fit))
            factor (B0, B1, juce::jmax (B2, -0.25 * (std::sqrt (B0) + std::sqrt (B1)) * (std::sqrt (B0) + std::sqrt (B1))));
        return h;
    }

    // Section prototypes + Qs for a band. Returns the section count.
    int sectionsFor (const ParametricEq::Band& b, Proto* protos, double* qs, double& A)
    {
        using T = ParametricEq::Type;
        A = std::pow (10.0, juce::jlimit (-ParametricEq::kMaxGainDb, ParametricEq::kMaxGainDb, b.gainDb) / 40.0);
        const double q = juce::jlimit ((double) ParametricEq::kMinQ, (double) ParametricEq::kMaxQ, (double) b.q);

        switch (b.type)
        {
            case T::bell:      protos[0] = Proto::peak;      qs[0] = q; return 1;
            case T::lowShelf:  protos[0] = Proto::lowShelf;  qs[0] = q; return 1;
            case T::highShelf: protos[0] = Proto::highShelf; qs[0] = q; return 1;
            case T::notch:     protos[0] = Proto::notch;     qs[0] = q; return 1;
            case T::lowCut:
            case T::highCut:
            {
                // Butterworth cascade; Q sets the resonance of the sharpest
                // section (Q = 0.707 → plain Butterworth).
                static const double bw24[] = { 0.5411961, 1.3065630 };
                static const double bw48[] = { 0.5097956, 0.6013449, 0.8999762, 2.5629154 };
                const Proto p = b.type == T::lowCut ? Proto::highPass : Proto::lowPass;
                const double res = q / std::sqrt (0.5);
                int n = 1;
                if (b.slope >= 48)      { n = 4; for (int i = 0; i < 4; ++i) qs[i] = bw48[i]; }
                else if (b.slope >= 24) { n = 2; for (int i = 0; i < 2; ++i) qs[i] = bw24[i]; }
                else                    { qs[0] = std::sqrt (0.5); }
                qs[n - 1] *= res;
                for (int i = 0; i < n; ++i) protos[i] = p;
                A = 1.0;
                return n;
            }
        }
        return 0;
    }

    double bandW0 (float hz, double sampleRate)
    {
        return 2.0 * kPi * juce::jlimit (1.0, 0.49 * sampleRate, (double) hz) / sampleRate;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Design + analysis
// ─────────────────────────────────────────────────────────────────────────────

int ParametricEq::design (const Band& b, double sampleRate, Biquad* out)
{
    Proto protos[kMaxSections];
    double qs[kMaxSections], A = 1.0;
    const int n = sectionsFor (b, protos, qs, A);
    const double w0 = bandW0 (b.freq, sampleRate);
    for (int i = 0; i < n; ++i)
        out[i] = matched (protos[i], A, qs[i], w0);
    return n;
}

double ParametricEq::responseDb (const Band& b, double hz, double sampleRate)
{
    Biquad s[kMaxSections];
    const int n = design (b, sampleRate, s);
    return magnitudeDb (s, n, hz, sampleRate);
}

double ParametricEq::magnitudeDb (const Biquad* s, int n, double hz, double sampleRate)
{
    const std::complex<double> z1 = std::polar (1.0, -2.0 * kPi * hz / sampleRate), z2 = z1 * z1;
    double mag = 1.0;
    for (int i = 0; i < n; ++i)
        mag *= std::abs ((s[i].b0 + s[i].b1 * z1 + s[i].b2 * z2) / (1.0 + s[i].a1 * z1 + s[i].a2 * z2));
    return 20.0 * std::log10 (juce::jmax (1.0e-12, mag));
}

double ParametricEq::analogResponseDb (const Band& b, double hz)
{
    Proto protos[kMaxSections];
    double qs[kMaxSections], A = 1.0;
    const int n = sectionsFor (b, protos, qs, A);
    double mag2 = 1.0;
    for (int i = 0; i < n; ++i)
        mag2 *= analogMag2 (protos[i], A, qs[i], hz / juce::jmax (1.0f, b.freq));
    return 10.0 * std::log10 (juce::jmax (1.0e-24, mag2));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Processing
// ─────────────────────────────────────────────────────────────────────────────

void ParametricEq::prepare (double sr, int)
{
    sampleRate = sr > 0.0 ? sr : 48000.0;
    glide   = 1.0 - std::exp (-(double) kSubBlock / (0.02 * sampleRate));   // ~20 ms
    mixStep = (float) (1.0 / (0.02 * sampleRate));                           // 20 ms fades
    reset();
}

ParametricEq::Svf ParametricEq::toSvf (const Biquad& h)
{
    // Denominator of the trapezoidal SVF: 1 + a1 + a2 = 4g²/d, 1 − a1 + a2 = 4/d
    // with d = 1 + gk + g². Numerator: lp, bp, hp span (1 + z⁻¹)², (1 − z⁻²), (1 − z⁻¹)².
    const double sumP = 1.0 + h.a1 + h.a2, sumN = 1.0 - h.a1 + h.a2;
    Svf s;
    s.g  = std::sqrt (juce::jmax (1.0e-30, sumP / sumN));
    s.k  = (4.0 / sumN - 1.0 - s.g * s.g) / s.g;
    s.mL = (h.b0 + h.b1 + h.b2) / sumP;              // DC gain
    s.mH = (h.b0 - h.b1 + h.b2) / sumN;              // Nyquist gain
    s.mB = 2.0 * (h.b0 - h.b2) / (s.g * sumN);
    return s;
}

void ParametricEq::clearState (Voice& v)
{
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < kMaxSections; ++i)
            v.ic1[c][i] = v.ic2[c][i] = 0.0;
    v.fresh = true;
}

void ParametricEq::reset()
{
    for (auto& v : voices)
    {
        v.current = v.target;
        v.mix = v.mixTarget = v.target.on ? 1.0f : 0.0f;
        v.logFreq = std::log (juce::jmax (1.0f, v.target.freq));
        v.logQ    = std::log (juce::jmax (kMinQ, v.target.q));
        v.needsDesign = true;
        clearState (v);
    }
}

void ParametricEq::setBand (int index, const Band& target)
{
    if (juce::isPositiveAndBelow (index, kNumBands))
        voices[(size_t) index].target = target;
}

bool ParametricEq::isActive() const
{
    for (const auto& v : voices)
        if (v.target.on || v.mix > 0.0f) return true;
    return false;
}

void ParametricEq::updateVoice (Voice& v)
{
    auto& c = v.current;
    const auto& t = v.target;

    for (int i = 0; i < v.numSections; ++i)
        v.from[i] = v.to[i];   // the last sub-block ended on its target

    // Discrete changes wait until the band has faded out.
    const bool discrete = c.type != t.type || c.slope != t.slope || c.placement != t.placement;
    if (discrete && v.mix <= 0.0f)
    {
        c.type = t.type; c.slope = t.slope; c.placement = t.placement;
        clearState (v);
        v.needsDesign = true;
    }
    const bool settled = c.type == t.type && c.slope == t.slope && c.placement == t.placement;
    v.mixTarget = (t.on && settled) ? 1.0f : 0.0f;

    // A silent band just follows its target.
    if (v.mix <= 0.0f && v.mixTarget <= 0.0f)
    {
        if (! juce::exactlyEqual (c.freq, t.freq) || ! juce::exactlyEqual (c.gainDb, t.gainDb) || ! juce::exactlyEqual (c.q, t.q))
            v.needsDesign = true;
        c.freq = t.freq; c.gainDb = t.gainDb; c.q = t.q;
        v.logFreq = std::log (juce::jmax (1.0f, c.freq));
        v.logQ    = std::log (juce::jmax (kMinQ, c.q));
        clearState (v);
        return;
    }

    // Glide frequency / Q (log domain) and gain.
    const auto glideTo = [this] (double& x, double target, double eps)
    {
        if (std::abs (target - x) < eps) { const bool moved = ! juce::exactlyEqual (x, target); x = target; return moved; }
        x += (target - x) * glide;
        return true;
    };
    double gain = c.gainDb;
    bool moved = false;
    moved |= glideTo (v.logFreq, std::log (juce::jmax (1.0f, t.freq)), 1.0e-4);
    moved |= glideTo (v.logQ,    std::log (juce::jmax (kMinQ, t.q)),    1.0e-4);
    moved |= glideTo (gain,      t.gainDb,                               1.0e-3);
    if (moved)
    {
        c.freq = (float) std::exp (v.logFreq);
        c.q    = (float) std::exp (v.logQ);
        c.gainDb = (float) gain;
        v.needsDesign = true;
    }

    if (v.needsDesign)
    {
        Biquad bq[kMaxSections];
        v.numSections = design (c, sampleRate, bq);
        for (int i = 0; i < v.numSections; ++i)
        {
            v.to[i] = toSvf (bq[i]);
            if (v.fresh) v.from[i] = v.to[i];   // nothing to interpolate from
        }
        v.fresh = false;
        v.needsDesign = false;
    }
}

void ParametricEq::process (float* L, float* R, int n)
{
    const bool mono = (R == nullptr);

    for (int start = 0; start < n; start += kSubBlock)
    {
        const int len = juce::jmin (kSubBlock, n - start);

        bool any = false;
        for (auto& v : voices)
        {
            updateVoice (v);
            any |= (v.mix > 0.0f || v.mixTarget > 0.0f);
        }
        if (! any) continue;   // untouched: bit-exact bypass

        for (int j = 0; j < len; ++j)
        {
            const int i = start + j;
            const double t = (double) (j + 1) / (double) len;

            double m = mono ? L[i] : 0.5 * ((double) L[i] + R[i]);
            double s = mono ? 0.0  : 0.5 * ((double) L[i] - R[i]);

            for (auto& v : voices)
            {
                if (v.mix <= 0.0f && v.mixTarget <= 0.0f) continue;

                if (v.mix < v.mixTarget)      v.mix = juce::jmin (v.mixTarget, v.mix + mixStep);
                else if (v.mix > v.mixTarget) v.mix = juce::jmax (v.mixTarget, v.mix - mixStep);

                const auto place = v.current.placement;
                const bool doMid  = place != Placement::side;
                const bool doSide = place != Placement::mid && ! mono;

                double ym = m, ys = s;
                for (int k = 0; k < v.numSections; ++k)
                {
                    const auto& a = v.from[k];
                    const auto& b = v.to[k];
                    const double g  = a.g  + (b.g  - a.g)  * t;
                    const double kk = a.k  + (b.k  - a.k)  * t;
                    const double mH = a.mH + (b.mH - a.mH) * t;
                    const double mB = a.mB + (b.mB - a.mB) * t;
                    const double mL = a.mL + (b.mL - a.mL) * t;

                    const double a1 = 1.0 / (1.0 + g * (g + kk)), a2 = g * a1, a3 = g * a2;
                    const auto tick = [&] (double x, double& ic1, double& ic2)
                    {
                        const double v3 = x - ic2;
                        const double v1 = a1 * ic1 + a2 * v3;
                        const double v2 = ic2 + a2 * ic1 + a3 * v3;
                        ic1 = 2.0 * v1 - ic1;
                        ic2 = 2.0 * v2 - ic2;
                        return mH * (x - kk * v1 - v2) + mB * v1 + mL * v2;
                    };
                    if (doMid)  ym = tick (ym, v.ic1[0][k], v.ic2[0][k]);
                    if (doSide) ys = tick (ys, v.ic1[1][k], v.ic2[1][k]);
                }

                if (doMid)  m += (double) v.mix * (ym - m);
                if (doSide) s += (double) v.mix * (ys - s);
            }

            if (mono) L[i] = (float) m;
            else      { L[i] = (float) (m + s); R[i] = (float) (m - s); }
        }
    }
}

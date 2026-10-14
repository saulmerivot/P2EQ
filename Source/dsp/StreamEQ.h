#pragma once

#include "BandFilter.h"
#include <algorithm>
#include <array>

struct BandTarget
{
    BandSpec spec;
    int panMode = 0;      // 0 = L/R pan, 1 = M/S balance
    double pan = 0.0;     // -1..1
};

// Routes a (dl, dr) pair: L/R pan or M/S balance (-1 = mid only, +1 = side only).
inline void routePair (double& dl, double& dr, int mode, double v) noexcept
{
    if (v == 0.0) return;
    if (mode == 0)
    {
        dl *= v > 0 ? 1.0 - v : 1.0;
        dr *= v < 0 ? 1.0 + v : 1.0;
    }
    else
    {
        const double m = 0.5 * (dl + dr), s = 0.5 * (dl - dr);
        const double mg = v > 0 ? 1.0 - v : 1.0, sg = v < 0 ? 1.0 + v : 1.0;
        dl = m * mg + s * sg;
        dr = m * mg - s * sg;
    }
}

// 8-band EQ for one stream (Transient or Tonal). Band 0 = HPF, 7 = LPF, 1..6 parametric.
// Bands 1..6 are applied as routed deltas (effect = f(x) - x) so pan/M-S act on the band's effect.
class StreamEQ
{
public:
    static constexpr int numBands = 8;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        for (auto& b : sec) for (auto& s : b) s.clear();
        first = true;
    }

    void setTargets (const std::array<BandTarget, numBands>& t) { target = t; }

    // Call once per sub-block: glides gain/freq/q, rebuilds coefficients when needed.
    void step()
    {
        const double rate = first ? 1.0 : 0.25;
        for (int b = 0; b < numBands; ++b)
        {
            const auto& tg = target[(size_t) b];
            auto& cu = cur[(size_t) b];
            const bool structural = first || cu.spec.type != tg.spec.type || cu.spec.slope != tg.spec.slope
                                    || cu.spec.active != tg.spec.active;
            if (structural)
            {
                cu.spec = tg.spec;
                for (auto& s : sec[(size_t) b]) s.clear();
                count[(size_t) b] = BandFilter::build (cu.spec, sr, sec[(size_t) b]);
            }
            else
            {
                const double lf = std::log (cu.spec.freq), tf = std::log (tg.spec.freq);
                const double nf = std::exp (lf + (tf - lf) * rate);
                const double ng = cu.spec.gain + (tg.spec.gain - cu.spec.gain) * rate;
                const double nq = cu.spec.q + (tg.spec.q - cu.spec.q) * rate;
                if (std::abs (nf - cu.spec.freq) > 1e-4 * nf || std::abs (ng - cu.spec.gain) > 1e-4
                    || std::abs (nq - cu.spec.q) > 1e-4)
                {
                    cu.spec.freq = nf; cu.spec.gain = ng; cu.spec.q = nq;
                    count[(size_t) b] = BandFilter::build (cu.spec, sr, sec[(size_t) b]);
                }
            }
            cu.panMode = tg.panMode;
            cu.pan += (tg.pan - cu.pan) * rate;
        }
        first = false;
    }

    inline void process (float& l, float& r, bool stereo) noexcept
    {
        double xl = l, xr = stereo ? r : 0.0;

        for (int b : { 0, 7 })
        {
            const int n = count[(size_t) b];
            for (int i = 0; i < n; ++i)
            {
                xl = sec[(size_t) b][(size_t) i].process (0, xl);
                if (stereo) xr = sec[(size_t) b][(size_t) i].process (1, xr);
            }
        }

        for (int b = 1; b <= 6; ++b)
        {
            const int n = count[(size_t) b];
            if (n == 0) continue;
            double yl = xl, yr = xr;
            for (int i = 0; i < n; ++i)
            {
                yl = sec[(size_t) b][(size_t) i].process (0, yl);
                if (stereo) yr = sec[(size_t) b][(size_t) i].process (1, yr);
            }
            double dl = yl - xl, dr = yr - xr;
            if (stereo) routePair (dl, dr, cur[(size_t) b].panMode, cur[(size_t) b].pan);
            xl += dl;
            xr += dr;
        }

        l = (float) xl;
        if (stereo) r = (float) xr;
    }

private:
    double sr = 44100.0;
    bool first = true;
    std::array<BandTarget, numBands> target {}, cur {};
    std::array<BandFilter::Sections, numBands> sec {};
    std::array<int, numBands> count {};
};

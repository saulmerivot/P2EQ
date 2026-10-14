#pragma once

#include <array>
#include <cmath>
#include <complex>

enum class BandType { LowShelf = 0, Peak, Notch, HighShelf, TiltShelf, BandPass, HighPass, LowPass };

struct BandSpec
{
    bool active = false;
    BandType type = BandType::Peak;
    int slope = 12;          // dB/oct
    double freq = 1000.0;
    double gain = 0.0;       // dB
    double q = 1.0;

    bool operator== (const BandSpec& o) const
    {
        return active == o.active && type == o.type && slope == o.slope
               && freq == o.freq && gain == o.gain && q == o.q;
    }
};

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1[2] { 0, 0 }, z2[2] { 0, 0 };

    inline double process (int ch, double x) noexcept
    {
        const double y = b0 * x + z1[ch];
        z1[ch] = b1 * x - a1 * y + z2[ch];
        z2[ch] = b2 * x - a2 * y;
        return y;
    }

    void setCoeffs (double nb0, double nb1, double nb2, double a0, double na1, double na2) noexcept
    {
        const double inv = 1.0 / a0;
        b0 = nb0 * inv; b1 = nb1 * inv; b2 = nb2 * inv; a1 = na1 * inv; a2 = na2 * inv;
    }

    void clear() noexcept { z1[0] = z1[1] = z2[0] = z2[1] = 0; }

    double magnitudeDb (double w) const noexcept
    {
        using C = std::complex<double>;
        const C e1 = std::polar (1.0, -w), e2 = std::polar (1.0, -2.0 * w);
        const C num = b0 + b1 * e1 + b2 * e2;
        const C den = 1.0 + a1 * e1 + a2 * e2;
        return 20.0 * std::log10 (std::max (1.0e-9, std::abs (num / den)));
    }
};

namespace BandFilter
{
    static constexpr int maxSections = 16;
    using Sections = std::array<Biquad, maxSections>;
    constexpr double pi = 3.14159265358979323846;

    inline void rbj (Biquad& s, BandType t, double sr, double f, double gainDb, double q)
    {
        f = std::min (f, sr * 0.49);
        const double w0 = 2.0 * pi * f / sr, c = std::cos (w0), sn = std::sin (w0);
        const double alpha = sn / (2.0 * std::max (0.05, q));
        const double A = std::pow (10.0, gainDb / 40.0), sA = std::sqrt (A);

        switch (t)
        {
            case BandType::Peak:
                s.setCoeffs (1 + alpha * A, -2 * c, 1 - alpha * A, 1 + alpha / A, -2 * c, 1 - alpha / A); break;
            case BandType::Notch:
                s.setCoeffs (1, -2 * c, 1, 1 + alpha, -2 * c, 1 - alpha); break;
            case BandType::BandPass:
                s.setCoeffs (alpha, 0, -alpha, 1 + alpha, -2 * c, 1 - alpha); break;
            case BandType::LowShelf:
                s.setCoeffs (A * ((A + 1) - (A - 1) * c + 2 * sA * alpha),
                             2 * A * ((A - 1) - (A + 1) * c),
                             A * ((A + 1) - (A - 1) * c - 2 * sA * alpha),
                             (A + 1) + (A - 1) * c + 2 * sA * alpha,
                             -2 * ((A - 1) + (A + 1) * c),
                             (A + 1) + (A - 1) * c - 2 * sA * alpha); break;
            case BandType::HighShelf:
                s.setCoeffs (A * ((A + 1) + (A - 1) * c + 2 * sA * alpha),
                             -2 * A * ((A - 1) + (A + 1) * c),
                             A * ((A + 1) + (A - 1) * c - 2 * sA * alpha),
                             (A + 1) - (A - 1) * c + 2 * sA * alpha,
                             2 * ((A - 1) - (A + 1) * c),
                             (A + 1) - (A - 1) * c - 2 * sA * alpha); break;
            default: break;
        }
    }

    inline void pass2 (Biquad& s, bool high, double sr, double f, double q)
    {
        f = std::min (f, sr * 0.49);
        const double w0 = 2.0 * pi * f / sr, c = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
        if (high) s.setCoeffs ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + alpha, -2 * c, 1 - alpha);
        else      s.setCoeffs ((1 - c) / 2,   1 - c,  (1 - c) / 2, 1 + alpha, -2 * c, 1 - alpha);
    }

    inline void pass1 (Biquad& s, bool high, double sr, double f)
    {
        f = std::min (f, sr * 0.49);
        const double K = std::tan (pi * f / sr);
        const double n = 1.0 / (1.0 + K), a1 = (K - 1.0) * n;
        if (high) s.setCoeffs (n, -n, 0, 1, a1, 0);
        else      s.setCoeffs (K * n, K * n, 0, 1, a1, 0);
    }

    // Fills `out` with `n` cascaded sections realising the band. Returns n (0 = inactive/none).
    inline int build (const BandSpec& sp, double sr, Sections& out)
    {
        if (! sp.active) return 0;

        if (sp.type == BandType::HighPass || sp.type == BandType::LowPass)
        {
            const bool high = sp.type == BandType::HighPass;
            const int order = std::max (1, sp.slope / 6);
            if (order == 1) { pass1 (out[0], high, sr, sp.freq); return 1; }
            const int n = std::min (order / 2, maxSections);
            for (int k = 1; k <= n; ++k)
            {
                const double q = 1.0 / (2.0 * std::cos (pi * (2.0 * k - 1.0) / (2.0 * order)));
                pass2 (out[(size_t) (k - 1)], high, sr, sp.freq, q);
            }
            return n;
        }

        const int nSec = std::max (1, std::min (8, sp.slope / 12));
        int n = 0;
        switch (sp.type)
        {
            case BandType::Peak:
            case BandType::LowShelf:
            case BandType::HighShelf:
                for (int i = 0; i < nSec; ++i) rbj (out[(size_t) n++], sp.type, sr, sp.freq, sp.gain / nSec, sp.q);
                break;
            case BandType::TiltShelf:
                for (int i = 0; i < nSec; ++i)
                {
                    rbj (out[(size_t) n++], BandType::LowShelf,  sr, sp.freq, -0.5 * sp.gain / nSec, sp.q);
                    rbj (out[(size_t) n++], BandType::HighShelf, sr, sp.freq,  0.5 * sp.gain / nSec, sp.q);
                }
                break;
            case BandType::Notch:
            case BandType::BandPass:
                for (int i = 0; i < nSec; ++i) rbj (out[(size_t) n++], sp.type, sr, sp.freq, 0.0, sp.q);
                break;
            default: break;
        }
        return n;
    }

    inline double responseDb (const Sections& s, int n, double sr, double f)
    {
        const double w = 2.0 * pi * f / sr;
        double db = 0;
        for (int i = 0; i < n; ++i) db += s[(size_t) i].magnitudeDb (w);
        return db;
    }
}

// Offline tool + self-test for the transient/tonal split.
//   SplitTool                         -> self-test (reconstruction + transient isolation)
//   SplitTool in.wav [sep] [decay] [smoothMs] -> writes in_transient.wav / in_tonal.wav
#include <juce_audio_formats/juce_audio_formats.h>
#include "../Source/dsp/TransientSplit.h"
#include "../Source/dsp/StreamEQ.h"
#include "../Source/dsp/Saturator.h"
#include <cstdio>
#include <memory>

static bool gLinked = true;

// stereo split helper (whole buffers, latency removed)
static void splitStereo (const float* inL, const float* inR, int n, float* trL, float* tnL, float* trR, float* tnR,
                         double sr, float sep, float decay, float smooth, bool linked)
{
    auto sp = std::make_unique<TransientSplit>();
    sp->prepare (sr);
    sp->setLinked (linked);
    sp->setParams (sep, decay, smooth);
    const int lat = TransientSplit::latency;
    for (int i = 0; i < n + lat; ++i)
    {
        float a, b, c, d;
        sp->process (i < n ? inL[i] : 0.0f, i < n ? inR[i] : 0.0f, a, b, c, d);
        if (i >= lat) { trL[i - lat] = a; tnL[i - lat] = b; trR[i - lat] = c; tnR[i - lat] = d; }
    }
}

static double gXover = -1.0;
static float gSepC = -1.0f;
static float gSharpB = -1.0f, gSharpR = -1.0f;
static int gPasses = -1;
static int gSource = 0;

static void splitChannel (const float* in, int n, float* trOut, float* tnOut,
                          double sr, float sep, float decay, float smooth, bool forceTonal = false)
{
    auto sp = std::make_unique<TransientSplit>();
    if (gXover > 0) sp->crossoverHz = gXover;
    if (gSepC >= 0) sp->sepCentre = gSepC;
    if (gSharpB >= 0) sp->sharpBase = gSharpB;
    if (gSharpR >= 0) sp->sharpRange = gSharpR;
    if (gPasses >= 0) sp->smoothPassesLong = sp->smoothPassesShort = gPasses;
    sp->prepare (sr);
    sp->setSource (gSource);
    sp->setParams (sep, decay, smooth);
    sp->setDebugForceTonal (forceTonal);

    const int lat = TransientSplit::latency;
    for (int i = 0; i < n + lat; ++i)
    {
        float tr, tn;
        sp->process (i < n ? in[i] : 0.0f, tr, tn);
        if (i >= lat) { trOut[i - lat] = tr; tnOut[i - lat] = tn; }
    }
}

static int selfTest()
{
    const double sr = 44100.0;
    const int n = (int) (sr * 4.0);
    std::vector<float> x ((size_t) n), tr ((size_t) n), tn ((size_t) n);
    juce::Random rng (1234);

    for (int i = 0; i < n; ++i)
        x[(size_t) i] = 0.3f * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * i / sr)
                      + 0.1f * std::sin (2.0 * juce::MathConstants<double>::pi * 661.0 * i / sr);

    const int clickEvery = (int) (sr * 0.5), clickLen = (int) (sr * 0.02);
    for (int c = clickEvery; c < n; c += clickEvery)
        for (int j = 0; j < clickLen && c + j < n; ++j)
            x[(size_t) (c + j)] += (rng.nextFloat() * 2.0f - 1.0f) * std::exp (-(float) j / (0.004f * (float) sr)) * 0.8f;

    int failures = 0;

    // 1. WOLA identity: with mask forced to tonal, Tonal must equal the input.
    splitChannel (x.data(), n, tr.data(), tn.data(), sr, 0.5f, 0, 0, true);
    double maxErr = 0;
    for (int i = TransientSplit::N; i < n; ++i) maxErr = std::max (maxErr, (double) std::abs (tn[(size_t) i] - x[(size_t) i]));
    std::printf ("[1] WOLA identity   max |tonal - input| = %.3e  %s\n", maxErr, maxErr < 1e-4 ? "OK" : "FAIL");
    failures += maxErr < 1e-4 ? 0 : 1;

    // 2. Reconstruction: transient + tonal == input (holds by construction).
    splitChannel (x.data(), n, tr.data(), tn.data(), sr, 0.5f, 0, 0);
    maxErr = 0;
    for (int i = 0; i < n; ++i) maxErr = std::max (maxErr, (double) std::abs (tr[(size_t) i] + tn[(size_t) i] - x[(size_t) i]));
    std::printf ("[2] Reconstruction  max |T+N - input|   = %.3e  %s\n", maxErr, maxErr < 1e-5 ? "OK" : "FAIL");
    failures += maxErr < 1e-5 ? 0 : 1;

    // 3. Isolation: transient energy should concentrate around clicks, tonal energy should not.
    double eTrIn = 0, eTrOut = 0, eTnIn = 0, eTnOut = 0;
    const int pad = (int) (sr * 0.03);
    for (int i = clickEvery / 2; i < n - clickEvery / 2; ++i)
    {
        const int r = i % clickEvery;
        const bool nearClick = r < clickLen + pad;
        (nearClick ? eTrIn : eTrOut) += (double) tr[(size_t) i] * tr[(size_t) i];
        (nearClick ? eTnIn : eTnOut) += (double) tn[(size_t) i] * tn[(size_t) i];
    }
    const double trShare = eTrIn / (eTrIn + eTrOut);
    const double tnClickShare = eTnIn / (eTnIn + eTnOut);
    std::printf ("[3] Isolation       transient energy near clicks = %.1f%%  (tonal near clicks = %.1f%%)  %s\n",
                 100.0 * trShare, 100.0 * tnClickShare, trShare > 0.6 ? "OK" : "WEAK");
    failures += trShare > 0.6 ? 0 : 1;

    // 4. Time precision: transient energy leaking BEFORE each click (pre-echo) vs right after it,
    //    and how compact the transient is (energy in the first 10 ms vs first 60 ms after onset).
    double ePre = 0, ePost = 0, e10 = 0, e60 = 0;
    const int ms = (int) (sr / 1000.0);
    for (int c = clickEvery; c + 60 * ms < n; c += clickEvery)
    {
        for (int j = -25 * ms; j < -2 * ms; ++j) ePre  += (double) tr[(size_t) (c + j)] * tr[(size_t) (c + j)];
        for (int j = 0; j < 25 * ms; ++j)       ePost += (double) tr[(size_t) (c + j)] * tr[(size_t) (c + j)];
        for (int j = 0; j < 60 * ms; ++j)
        {
            const double v = (double) tr[(size_t) (c + j)] * tr[(size_t) (c + j)];
            e60 += v;
            if (j < 10 * ms) e10 += v;
        }
    }
    std::printf ("[4] Time precision  pre-echo = %.2f%% of onset energy | compactness (10ms/60ms) = %.1f%%\n",
                 100.0 * ePre / std::max (1e-12, ePost), 100.0 * e10 / std::max (1e-12, e60));

    // 5. Ground-truth mix: kick (pitch-drop) + hats = true transient ; sustained bass + pad = true tonal.
    {
        const double pi2 = 2.0 * juce::MathConstants<double>::pi;
        std::vector<float> tTrue ((size_t) n, 0.0f), nTrue ((size_t) n, 0.0f), mix ((size_t) n);
        juce::Random r2 (99);
        float hpPrev = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const double t = i / sr;
            nTrue[(size_t) i] = (float) (0.30 * std::sin (pi2 * 55.0 * t)
                                         + 0.08 * (std::sin (pi2 * 220.0 * t) + std::sin (pi2 * 277.2 * t) + std::sin (pi2 * 329.6 * t)));
            const double tk = std::fmod (t, 0.5);                               // kick every 500 ms
            const double phase = pi2 * (50.0 * tk + (100.0 * 0.03) * (1.0 - std::exp (-tk / 0.03)));
            float v = (float) (0.8 * std::exp (-tk / 0.08) * std::sin (phase));
            const double th = std::fmod (t + 0.25, 0.25);                       // hat every 250 ms (offbeat)
            const float noise = r2.nextFloat() * 2.0f - 1.0f;
            const float hp = noise - hpPrev; hpPrev = noise;                     // crude high-pass
            v += (float) (0.25 * std::exp (-th / 0.015)) * hp;
            tTrue[(size_t) i] = v;
            mix[(size_t) i] = v + nTrue[(size_t) i];
        }
        auto sdr = [] (const std::vector<float>& est, const std::vector<float>& ref, int from)
        {
            double s = 0, e = 0;
            for (size_t i = (size_t) from; i < ref.size(); ++i) { s += (double) ref[i] * ref[i]; e += (double) (est[i] - ref[i]) * (est[i] - ref[i]); }
            return 10.0 * std::log10 (s / std::max (1e-12, e));
        };
        splitChannel (mix.data(), n, tr.data(), tn.data(), sr, 0.5f, 0, 0);
        const int from = (int) sr;   // skip warm-up
        std::printf ("[5] Ground truth    SDR transient = %.2f dB | SDR tonal = %.2f dB  (higher = closer to the true split)\n",
                     sdr (tr, tTrue, from), sdr (tn, nTrue, from));

        // 6. Separation semantics (manual): higher Separation => LESS material classified as Transient.
        {
            double et = 0, ea = 0;
            for (int i = from; i < n; ++i) { et += (double) tTrue[(size_t) i] * tTrue[(size_t) i]; ea += (double) mix[(size_t) i] * mix[(size_t) i]; }
            std::printf ("    true transient share = %.1f%%\n", 100.0 * et / ea);
        }
        std::printf ("[6] Separation ->   transient energy share:");
        double prev = 1e9; bool mono = true;
        for (float s : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            splitChannel (mix.data(), n, tr.data(), tn.data(), sr, s, 0, 0);
            double et = 0, ea = 0;
            for (int i = from; i < n; ++i) { et += (double) tr[(size_t) i] * tr[(size_t) i]; ea += (double) mix[(size_t) i] * mix[(size_t) i]; }
            const double share = 100.0 * et / ea;
            std::printf ("  %3.0f%%:%5.1f%% (SDR %.1f/%.1f)", s * 100, share, sdr (tr, tTrue, from), sdr (tn, nTrue, from));
            mono = mono && share <= prev + 0.05;
            prev = share;
        }
        std::printf ("   %s\n", mono ? "OK (monotonic)" : "NOT monotonic");
        failures += mono ? 0 : 1;

        // 7. Decay semantics (manual): more Decay => longer Transient regions => more transient energy.
        std::printf ("[7] Decay ->        transient energy share:");
        prev = -1.0; mono = true;
        for (float d : { 0.0f, 0.25f, 0.5f, 1.0f })
        {
            splitChannel (mix.data(), n, tr.data(), tn.data(), sr, 0.5f, d, 0);
            double et = 0, ea = 0;
            for (int i = from; i < n; ++i) { et += (double) tr[(size_t) i] * tr[(size_t) i]; ea += (double) mix[(size_t) i] * mix[(size_t) i]; }
            const double share = 100.0 * et / ea;
            std::printf ("  %3.0f%%:%5.1f%%", d * 100, share);
            mono = mono && share >= prev - 0.05;
            prev = share;
        }
        std::printf ("   %s\n", mono ? "OK (monotonic)" : "NOT monotonic");
        failures += mono ? 0 : 1;

        if (std::getenv ("SPLIT_DIAG") != nullptr)
            for (double xo : { 800.0, 1500.0, 3000.0, 20000.0 })
                for (float sc : { 0.5f, 0.75f, 1.0f })
                {
                    std::printf ("    diag xover %5.0f sepCentre %.2f:", xo, sc);
                    for (float s : { 0.25f, 0.5f, 0.75f })
                    {
                        gXover = xo; gSepC = sc;
                        splitChannel (mix.data(), n, tr.data(), tn.data(), sr, s, 0, 0);
                        std::printf ("  sep%3.0f%% %5.2f/%5.2f", s * 100, sdr (tr, tTrue, from), sdr (tn, nTrue, from));
                    }
                    std::printf ("\n");
                }
        gXover = -1.0; gSepC = -1.0f; gSource = 0;

        // 8/9. Smoothness: "false transient" on purely sustained material, and how clean a -12 dB
        //      Transient-only cut is compared with the ideal cut (tonal + 0.25 * transient).
        std::vector<float> stat ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            const double t = i / sr, vib = 1.0 + 0.005 * std::sin (pi2 * 4.0 * t);
            double v = nTrue[(size_t) i];
            for (int h = 1; h <= 6; ++h) v += 0.05 / h * std::sin (pi2 * 110.0 * h * t * vib);
            stat[(size_t) i] = (float) v;
        }
        auto evaluate = [&] (float sep, float dec, float smo, double& sT, double& sN, double& cut, double& leak)
        {
            splitChannel (mix.data(), n, tr.data(), tn.data(), sr, sep, dec, smo);
            sT = sdr (tr, tTrue, from); sN = sdr (tn, nTrue, from);
            std::vector<float> out ((size_t) n), ideal ((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                out[(size_t) i]   = tn[(size_t) i] + 0.25f * tr[(size_t) i];
                ideal[(size_t) i] = nTrue[(size_t) i] + 0.25f * tTrue[(size_t) i];
            }
            cut = sdr (out, ideal, from);
            splitChannel (stat.data(), n, tr.data(), tn.data(), sr, sep, dec, smo);
            double et = 0, ea = 0;
            for (int i = from; i < n; ++i) { et += (double) tr[(size_t) i] * tr[(size_t) i]; ea += (double) stat[(size_t) i] * stat[(size_t) i]; }
            leak = 100.0 * et / ea;
        };
        {
            double a, b2, cut, leak;
            evaluate (0.5f, 0.3f, 10.0f, a, b2, cut, leak);
            std::printf ("[8] Sustained only  false transient = %.2f%% of energy (lower = smoother)\n", leak);
            std::printf ("[9] -12 dB T cut    SDR vs ideal cut = %.2f dB (higher = cleaner)   [split SDR %.2f/%.2f]\n", cut, a, b2);
        }

        // 10. "Cracked": +12 dB Transient boost vs the ideal boost. Error kurtosis ~3 = smooth noise-like,
        //     >> 3 = impulsive crackle. Stereo: different hats on L and R, linked vs per-channel masks.
        {
            auto kurt = [] (const std::vector<double>& e)
            {
                double m2 = 0, m4 = 0;
                for (double v : e) { m2 += v * v; m4 += v * v * v * v; }
                m2 /= (double) e.size(); m4 /= (double) e.size();
                return m4 / std::max (1e-30, m2 * m2);
            };
            auto boostEval = [&] (const std::vector<float>& t, const std::vector<float>& tonal,
                                  const std::vector<float>& tT, const std::vector<float>& nT, double& sdrB, double& k)
            {
                std::vector<float> out ((size_t) n), ideal ((size_t) n);
                std::vector<double> e;
                for (int i = 0; i < n; ++i)
                {
                    out[(size_t) i] = tonal[(size_t) i] + 4.0f * t[(size_t) i];
                    ideal[(size_t) i] = nT[(size_t) i] + 4.0f * tT[(size_t) i];
                    if (i >= from) e.push_back ((double) out[(size_t) i] - ideal[(size_t) i]);
                }
                sdrB = sdr (out, ideal, from);
                k = kurt (e);
            };

            splitChannel (mix.data(), n, tr.data(), tn.data(), sr, 0.5f, 0.3f, 10.0f);
            double sb, k; boostEval (tr, tn, tTrue, nTrue, sb, k);
            std::printf ("[10] +12 dB T boost (mono)    SDR vs ideal %.2f dB | error kurtosis %.1f\n", sb, k);

            // stereo material: L = hats (offbeat) + pad, R = shaker 16ths + pad (different noise), kick centred
            std::vector<float> tL ((size_t) n), tR ((size_t) n), nL ((size_t) n), nR ((size_t) n), mL ((size_t) n), mR ((size_t) n);
            juce::Random r3 (5);
            float hpL = 0, hpR = 0;
            for (int i = 0; i < n; ++i)
            {
                const double t = i / sr;
                const double tk = std::fmod (t, 0.5);
                const float kick = (float) (0.7 * std::exp (-tk / 0.08) * std::sin (pi2 * (50.0 * tk + 3.0 * (1.0 - std::exp (-tk / 0.03)))));
                const float a = r3.nextFloat() * 2 - 1, b = r3.nextFloat() * 2 - 1;
                const float hl = a - hpL; hpL = a;  const float hr = b - hpR; hpR = b;
                const float hatL = (float) (0.30 * std::exp (-std::fmod (t + 0.25, 0.25) / 0.015)) * hl;
                const float shkR = (float) (0.18 * std::exp (-std::fmod (t, 0.125) / 0.010)) * hr;
                const float padL = (float) (0.30 * std::sin (pi2 * 55.0 * t) + 0.08 * std::sin (pi2 * 220.0 * t) + 0.08 * std::sin (pi2 * 329.6 * t));
                const float padR = (float) (0.30 * std::sin (pi2 * 55.0 * t) + 0.08 * std::sin (pi2 * 277.2 * t) + 0.08 * std::sin (pi2 * 440.0 * t));
                tL[(size_t) i] = kick + hatL; tR[(size_t) i] = kick + shkR;
                nL[(size_t) i] = padL;        nR[(size_t) i] = padR;
                mL[(size_t) i] = tL[(size_t) i] + nL[(size_t) i];
                mR[(size_t) i] = tR[(size_t) i] + nR[(size_t) i];
            }
            std::vector<float> aL ((size_t) n), bL ((size_t) n), aR ((size_t) n), bR ((size_t) n);
            for (bool lk : { false, true })
            {
                splitStereo (mL.data(), mR.data(), n, aL.data(), bL.data(), aR.data(), bR.data(), sr, 0.5f, 0.3f, 10.0f, lk);
                double s1, k1, s2, k2;
                boostEval (aL, bL, tL, nL, s1, k1);
                boostEval (aR, bR, tR, nR, s2, k2);
                // stereo image: side signal of the boosted result vs ideal side
                std::vector<float> so ((size_t) n), si ((size_t) n);
                for (int i = 0; i < n; ++i)
                {
                    so[(size_t) i] = 0.5f * ((bL[(size_t) i] + 4 * aL[(size_t) i]) - (bR[(size_t) i] + 4 * aR[(size_t) i]));
                    si[(size_t) i] = 0.5f * ((nL[(size_t) i] + 4 * tL[(size_t) i]) - (nR[(size_t) i] + 4 * tR[(size_t) i]));
                }
                std::printf ("[11] +12 dB T boost (stereo, %s)  SDR L/R %.2f / %.2f dB | kurtosis %.1f / %.1f | side SDR %.2f dB\n",
                             lk ? "linked  " : "separate", s1, s2, k1, k2, sdr (so, si, from));
            }
            if (std::getenv ("CRACK_DIAG") != nullptr)
                for (float p : { 1.0f, 1.5f, 2.0f, 3.0f, 4.5f })
                    for (float smo : { 10.0f, 20.0f, 35.0f })
                    {
                        gSharpB = p; gSharpR = 0.0f;
                        splitChannel (mix.data(), n, tr.data(), tn.data(), sr, 0.5f, 0.3f, smo);
                        double sb2, k2; boostEval (tr, tn, tTrue, nTrue, sb2, k2);
                        std::printf ("    crack p=%.1f smooth %4.1f: boost SDR %5.2f  kurt %4.1f | split %5.2f/%5.2f\n",
                                     p, smo, sb2, k2, sdr (tr, tTrue, from), sdr (tn, nTrue, from));
                    }
            gSharpB = gSharpR = -1.0f;
        }

        // 12. Stationary noise is TONAL (stable in time). Ground truth v2 = [5] + a steady hiss bed in the tonal part.
        {
            std::vector<float> bed ((size_t) n), mix2 ((size_t) n), nTrue2 ((size_t) n);
            juce::Random r4 (11);
            float lp = 0, prevx = 0;
            for (int i = 0; i < n; ++i)
            {
                const float w = r4.nextFloat() * 2.0f - 1.0f;
                const float hp = w - prevx; prevx = w;           // tilt up (hiss)
                lp = lp + 0.5f * (hp - lp);                     // tame the very top
                bed[(size_t) i] = 0.06f * lp;
                mix2[(size_t) i] = mix[(size_t) i] + bed[(size_t) i];
                nTrue2[(size_t) i] = nTrue[(size_t) i] + bed[(size_t) i];
            }
            auto eval = [&] (float sep, double& sT, double& sN, double& leak)
            {
                splitChannel (mix2.data(), n, tr.data(), tn.data(), sr, sep, 0.3f, 10.0f);
                sT = sdr (tr, tTrue, from); sN = sdr (tn, nTrue2, from);
                splitChannel (bed.data(), n, tr.data(), tn.data(), sr, sep, 0.3f, 10.0f);
                double et = 0, ea = 0;
                for (int i = from; i < n; ++i) { et += (double) tr[(size_t) i] * tr[(size_t) i]; ea += (double) bed[(size_t) i] * bed[(size_t) i]; }
                leak = 100.0 * et / ea;
            };
            double a, b, l; eval (0.5f, a, b, l);
            std::printf ("[12] Noise bed      split SDR %.2f / %.2f dB | steady hiss sent to Transient = %.1f%%\n", a, b, l);

            if (std::getenv ("NOISE_DIAG") != nullptr)
                for (float sc : { 0.45f, 0.55f, 0.65f, 0.75f, 0.85f })
                    for (float sb : { 1.5f, 3.0f })
                    {
                        gSepC = sc; gSharpB = sb; gSharpR = 3.0f;
                        double a2, b2, l2; eval (0.5f, a2, b2, l2);
                        double a1, b1x; { splitChannel (mix.data(), n, tr.data(), tn.data(), sr, 0.5f, 0.3f, 10.0f); a1 = sdr (tr, tTrue, from); b1x = sdr (tn, nTrue, from); }
                        std::printf ("    noise sepC %.2f p=%.1f+3*sep: v2 SDR %5.2f/%5.2f leak %5.1f%% | v1 SDR %5.2f/%5.2f\n",
                                     sc, sb, a2, b2, l2, a1, b1x);
                    }
            gSepC = -1.0f; gSharpB = gSharpR = -1.0f;
        }

        if (std::getenv ("SMOOTH_DIAG") != nullptr)
        {
            for (float dec : { 0.0f, 0.15f, 0.3f })
                for (float smo : { 0.0f, 5.0f, 10.0f, 20.0f })
                {
                    double a, b2, cut, leak;
                    evaluate (0.5f, dec, smo, a, b2, cut, leak);
                    std::printf ("    decay %3.0f%% smooth %4.1f ms : split %5.2f/%5.2f  cut %5.2f  leak %5.2f%%\n",
                                 dec * 100, smo, a, b2, cut, leak);
                }
        }
    }

    std::printf ("latency: %d samples (%.1f ms @ 44.1 kHz)\n", TransientSplit::latency, 1000.0 * TransientSplit::latency / sr);
    return failures;
}


static int eqTest()
{
    const double sr = 48000.0;
    int fail = 0;
    auto check = [&] (const char* name, double got, double want, double tol)
    {
        const bool ok = std::abs (got - want) <= tol;
        std::printf ("[EQ] %-34s got %8.2f  want %8.2f  %s\n", name, got, want, ok ? "OK" : "FAIL");
        fail += ok ? 0 : 1;
    };
    BandFilter::Sections s;
    auto resp = [&] (BandSpec sp, double f) { const int n = BandFilter::build (sp, sr, s); return BandFilter::responseDb (s, n, sr, f); };

    BandSpec sp; sp.active = true; sp.freq = 1000; sp.q = 1.0;
    sp.type = BandType::Peak; sp.gain = 6;           check ("peak +6 dB @ f0", resp (sp, 1000), 6.0, 0.05);
    sp.type = BandType::LowShelf; sp.gain = -9;      check ("low shelf -9 dB @ 50 Hz", resp (sp, 50), -9.0, 0.5);
    sp.type = BandType::HighShelf; sp.gain = 9;      check ("high shelf +9 dB @ 15 kHz", resp (sp, 15000), 9.0, 0.5);
    sp.type = BandType::TiltShelf; sp.gain = 12;     check ("tilt +12: low end -6", resp (sp, 40), -6.0, 0.7);
                                                     check ("tilt +12: high end +6", resp (sp, 16000), 6.0, 0.7);
    sp.type = BandType::Notch;                       check ("notch deep @ f0", resp (sp, 1000), -120.0, 70.0);
    sp.type = BandType::HighPass; sp.slope = 96;     check ("HPF 96 dB/oct @ f0/2", resp (sp, 500), -96.0, 12.0);
    sp.type = BandType::HighPass; sp.slope = 12;     check ("HPF 12 dB/oct @ f0 (-3 dB)", resp (sp, 1000), -3.0, 0.2);
    sp.type = BandType::LowPass;  sp.slope = 6;      check ("LPF 6 dB/oct @ f0 (-3 dB)", resp (sp, 1000), -3.0, 0.2);
    sp.type = BandType::Peak; sp.gain = 12; sp.slope = 24;  check ("peak +12, 24 dB/oct @ f0", resp (sp, 1000), 12.0, 0.1);

    // identity: flat EQ must not change the signal
    StreamEQ eq; eq.prepare (sr);
    std::array<BandTarget, 8> t {};
    for (auto& b : t) b.spec.active = false;
    for (int i = 1; i <= 6; ++i) { t[(size_t) i].spec.active = true; t[(size_t) i].spec.freq = 200.0 * i; }
    eq.setTargets (t); eq.step();
    double maxErr = 0;
    for (int i = 0; i < 4800; ++i)
    {
        const float x = std::sin (0.05f * (float) i), y = std::cos (0.031f * (float) i);
        float l = x, r = y; eq.process (l, r, true);
        maxErr = std::max (maxErr, (double) std::max (std::abs (l - x), std::abs (r - y)));
    }
    check ("flat EQ identity (max err)", maxErr, 0.0, 1e-6);

    // pan / M-S routing
    double dl = 1, dr = 1; routePair (dl, dr, 0, 1.0);   check ("pan hard right: L", dl, 0.0, 1e-9);
    dl = 1; dr = 1; routePair (dl, dr, 1, 1.0);          check ("M/S side only (mono in): L", dl, 0.0, 1e-9);
    dl = 1; dr = -1; routePair (dl, dr, 1, -1.0);        check ("M/S mid only (side in): L", dl, 0.0, 1e-9);
    return fail;
}

static int satTest()
{
    const double sr = 48000.0;
    const int n = 48000, blk = 512;
    int fail = 0;
    auto run = [&] (Saturator::Settings st, bool active, bool shape, const std::vector<float>& in)
    {
        Saturator s; s.prepare (sr, blk); s.setSettings (st);
        juce::AudioBuffer<float> b (2, n);
        for (int c = 0; c < 2; ++c) b.copyFrom (c, 0, in.data(), n);
        for (int pos = 0; pos < n; pos += blk)
        {
            juce::AudioBuffer<float> view (b.getArrayOfWritePointers(), 2, pos, std::min (blk, n - pos));
            s.process (view, 2, view.getNumSamples(), active, shape);
        }
        std::vector<float> out ((size_t) n);
        for (int i = 0; i < n; ++i) out[(size_t) i] = b.getSample (0, i);
        return std::make_pair (out, s.latency());
    };
    auto tone = [&] (double f, double amp) { std::vector<float> v ((size_t) n);
        for (int i = 0; i < n; ++i) v[(size_t) i] = (float) (amp * std::sin (2.0 * juce::MathConstants<double>::pi * f * i / sr)); return v; };
    auto goertzel = [&] (const std::vector<float>& x, double f, int from)
    {
        double s1 = 0, s2 = 0; const double w = 2.0 * juce::MathConstants<double>::pi * f / sr, cw = 2.0 * std::cos (w);
        for (size_t i = (size_t) from; i < x.size(); ++i) { const double s0 = x[i] + cw * s1 - s2; s2 = s1; s1 = s0; }
        const double p = s1 * s1 + s2 * s2 - cw * s1 * s2;
        return 20.0 * std::log10 (std::sqrt (std::max (1e-30, p)) / ((double) (x.size() - (size_t) from) * 0.5));
    };
    auto check = [&] (const char* name, bool ok, const juce::String& info)
    {
        std::printf ("[SAT] %-40s %s  %s\n", name, info.toRawUTF8(), ok ? "OK" : "FAIL");
        fail += ok ? 0 : 1;
    };

    const auto sine = tone (1000.0, 0.25);
    Saturator::Settings flat;

    // off = exact delay
    { auto [o, lat] = run (flat, false, false, sine); double e = 0;
      for (int i = lat; i < n; ++i) e = std::max (e, (double) std::abs (o[(size_t) i] - sine[(size_t) (i - lat)]));
      check ("off: exact delay of latency", e < 1e-6, "latency " + juce::String (lat) + " smp, err " + juce::String (e, 8)); }

    // on but not shaping = transparent (oversampling filters only)
    { auto [o, lat] = run (flat, true, false, sine); double e = 0;
      for (int i = lat + 2000; i < n; ++i) e = std::max (e, (double) std::abs (o[(size_t) i] - sine[(size_t) (i - lat)]));
      check ("on, not shaping: transparent", e < 2e-3, "max err " + juce::String (e, 6)); }

    // harmonic signature per style at Drive 6 (18 dB)
    const char* names[5] = { "A", "E", "N", "T", "P" };
    double h2[5], h3[5];
    for (int st = 0; st < 5; ++st)
    {
        Saturator::Settings s; s.style = st; s.driveDb = 18.0f; s.outDb = -18.0f * 0.55f;
        auto [o, lat] = run (s, true, true, sine);
        const double f1 = goertzel (o, 1000.0, lat + 4000);
        h2[st] = goertzel (o, 2000.0, lat + 4000) - f1;
        h3[st] = goertzel (o, 3000.0, lat + 4000) - f1;
        std::printf ("[SAT] style %s: H2 %6.1f dBc  H3 %6.1f dBc\n", names[st], h2[st], h3[st]);
    }
    // expectations from measurements of the reference saturation:
    //   style 5 has the strongest 3rd harmonic; every style adds clear even AND odd harmonics.
    check ("style 5 (K) has the strongest H3", h3[4] >= *std::max_element (h3, h3 + 4) - 0.5, "");
    bool allRich = true;
    for (int st = 0; st < 5; ++st) allRich = allRich && h2[st] > -35.0 && h3[st] > -30.0;
    check ("every style adds even and odd harmonics", allRich, "");

    // aliasing: 7 kHz into P at heavy drive, look for the folded 5th harmonic (35k -> 13k at 48k)
    { Saturator::Settings s; s.style = 4; s.driveDb = 30.0f;
      auto [o, lat] = run (s, true, true, tone (7000.0, 0.25));
      const double f1 = goertzel (o, 7000.0, lat + 4000), alias = goertzel (o, 13000.0, lat + 4000) - f1;
      check ("aliasing (folded 5th of 7 kHz) below -50 dBc", alias < -50.0, juce::String (alias, 1) + " dBc"); }

    // mix 0 = dry
    { Saturator::Settings s; s.style = 4; s.driveDb = 30.0f; s.mix = 0.0f;
      auto [o, lat] = run (s, true, true, sine); double e = 0;
      for (int i = lat + 4000; i < n; ++i) e = std::max (e, (double) std::abs (o[(size_t) i] - sine[(size_t) (i - lat)]));
      check ("mix 0%: dry", e < 2e-3, "max err " + juce::String (e, 6)); }
    return fail;
}

static int renderFile (const juce::File& in, float sep, float decay, float smooth)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (in));
    if (reader == nullptr) { std::printf ("cannot read %s\n", in.getFullPathName().toRawUTF8()); return 2; }

    const int n = (int) reader->lengthInSamples;
    const int ch = (int) std::min (2u, reader->numChannels);
    juce::AudioBuffer<float> src (ch, n), tr (ch, n), tn (ch, n);
    reader->read (&src, 0, n, 0, true, ch > 1);

    for (int c = 0; c < ch; ++c)
        splitChannel (src.getReadPointer (c), n, tr.getWritePointer (c), tn.getWritePointer (c),
                      reader->sampleRate, sep, decay, smooth);

    auto write = [&] (const juce::AudioBuffer<float>& b, const char* suffix)
    {
        auto out = in.getSiblingFile (in.getFileNameWithoutExtension() + suffix + ".wav");
        out.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> os (out.createOutputStream());
        if (os == nullptr) return;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), reader->sampleRate, (unsigned) ch, 24, {}, 0));
        if (w != nullptr) { os.release(); w->writeFromAudioSampleBuffer (b, 0, n); }
        std::printf ("wrote %s\n", out.getFullPathName().toRawUTF8());
    };
    write (tr, "_transient");
    write (tn, "_tonal");
    return 0;
}

// fit <in.wav> <refT.wav> <refN.wav> : sweep internal tuning against a reference plugin's solo renders
static int fitToReference (const juce::File& in, const juce::File& rT, const juce::File& rN)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    auto readAll = [&] (const juce::File& f, juce::AudioBuffer<float>& b, double& sr)
    {
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
        if (r == nullptr) return false;
        b.setSize (2, (int) r->lengthInSamples);
        r->read (&b, 0, (int) r->lengthInSamples, 0, true, true);
        sr = r->sampleRate;
        return true;
    };
    juce::AudioBuffer<float> x, refT, refN; double sr = 44100;
    if (! readAll (in, x, sr) || ! readAll (rT, refT, sr) || ! readAll (rN, refN, sr)) { std::printf ("read error\n"); return 2; }
    const int n = std::min ({ x.getNumSamples(), refT.getNumSamples(), refN.getNumSamples() });
    std::vector<float> aL ((size_t) n), bL ((size_t) n), aR ((size_t) n), bR ((size_t) n);
    const int from = (int) sr / 2;
    auto sdr2 = [&] (const std::vector<float>& eL, const std::vector<float>& eR, const juce::AudioBuffer<float>& ref)
    {
        double s = 0, e = 0;
        for (int i = from; i < n; ++i)
        {
            const double l = ref.getSample (0, i), r = ref.getSample (1, i);
            s += l * l + r * r;
            e += (eL[(size_t) i] - l) * (eL[(size_t) i] - l) + (eR[(size_t) i] - r) * (eR[(size_t) i] - r);
        }
        return 10.0 * std::log10 (s / std::max (1e-20, e));
    };
    const int source = std::getenv ("FIT_SOURCE") ? std::atoi (std::getenv ("FIT_SOURCE")) : 1;
    struct R { float c, pb, pr, d; double t, nn; };
    std::vector<R> res;
    for (float c : { 0.35f, 0.6f, 0.75f, 0.85f, 0.95f, 1.1f })
        for (float pb : { 0.6f, 0.8f, 1.0f, 1.5f })
            for (float pr : { 0.0f, 1.0f, 3.0f })
                for (float d : { 0.3f })
                {
                    auto sp = std::make_unique<TransientSplit>();
                    sp->sepCentre = c; sp->sharpBase = pb; sp->sharpRange = pr;
                    sp->prepare (sr); sp->setSource (source); sp->setParams (0.5f, d, 10.0f);
                    const int lat = TransientSplit::latency;
                    for (int i = 0; i < n + lat; ++i)
                    {
                        float a, b, cc, dd;
                        sp->process (i < n ? x.getSample (0, i) : 0.0f, i < n ? x.getSample (1, i) : 0.0f, a, b, cc, dd);
                        if (i >= lat) { aL[(size_t) (i - lat)] = a; bL[(size_t) (i - lat)] = b; aR[(size_t) (i - lat)] = cc; bR[(size_t) (i - lat)] = dd; }
                    }
                    res.push_back ({ c, pb, pr, d, sdr2 (aL, aR, refT), sdr2 (bL, bR, refN) });
                    std::printf ("fit sepC %.2f p=%.1f+%.1f*sep : SDR vs ref  T %6.2f  N %6.2f\n", c, pb, pr, res.back().t, res.back().nn);
                }
    auto best = *std::max_element (res.begin(), res.end(), [] (const R& a, const R& b) { return a.t + a.nn < b.t + b.nn; });
    std::printf ("BEST sepC %.2f p=%.1f+%.1f*sep : T %.2f N %.2f\n", best.c, best.pb, best.pr, best.t, best.nn);
    return 0;
}

// sat <in.wav> <out.wav> style driveDb toneDb lowCutHz highCutHz steep thump mix outDb
static int satRender (int argc, char** argv)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    const juce::File in (argv[2]), out (argv[3]);
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (in));
    if (r == nullptr) return 2;
    const int n = (int) r->lengthInSamples; const double sr = r->sampleRate;
    juce::AudioBuffer<float> b (2, n);
    r->read (&b, 0, n, 0, true, true);
    Saturator::Settings st;
    st.style = std::atoi (argv[4]); st.driveDb = (float) std::atof (argv[5]); st.toneDb = (float) std::atof (argv[6]);
    st.lowCutHz = (float) std::atof (argv[7]); st.highCutHz = (float) std::atof (argv[8]);
    st.steep = std::atoi (argv[9]) != 0; st.thump = std::atoi (argv[10]) != 0;
    st.mix = (float) std::atof (argv[11]); st.outDb = (float) std::atof (argv[12]);
    Saturator s; s.prepare (sr, 512); s.setSettings (st);
    // settle smoothing on silence
    juce::AudioBuffer<float> z (2, 512);
    for (int i = 0; i < (int) (sr / 512); ++i) { z.clear(); s.process (z, 2, 512, true, true); }
    const int lat = s.latency();
    juce::AudioBuffer<float> o (2, n); o.clear();
    juce::AudioBuffer<float> blk (2, 512);
    for (int pos = 0; pos < n + lat; pos += 512)
    {
        blk.clear();
        for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i) if (pos + i < n) blk.setSample (c, i, b.getSample (c, pos + i));
        s.process (blk, 2, 512, true, true);
        for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i) { const int k = pos + i - lat; if (k >= 0 && k < n) o.setSample (c, k, blk.getSample (c, i)); }
    }
    out.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> os (out.createOutputStream());
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), sr, 2, 24, {}, 0));
    if (w == nullptr) return 5;
    os.release();
    w->writeFromAudioSampleBuffer (o, 0, n);
    return 0;
}

int main (int argc, char** argv)
{
    if (auto* e = std::getenv ("SEPC")) gSepC = (float) std::atof (e);   // tuning override
    if (argc >= 13 && juce::String (argv[1]) == "sat")
        return satRender (argc, argv);
    if (argc >= 5 && juce::String (argv[1]) == "fit")
        return fitToReference (juce::File (argv[2]), juce::File (argv[3]), juce::File (argv[4]));
    if (argc < 2)
        return selfTest() + eqTest() + satTest();

    const float sep    = argc > 2 ? (float) std::atof (argv[2]) * 0.01f : 0.5f;
    const float decay  = argc > 3 ? (float) std::atof (argv[3]) * 0.01f : 0.0f;
    const float smooth = argc > 4 ? (float) std::atof (argv[4]) : 0.0f;
    return renderFile (juce::File (argv[1]), sep, decay, smooth);
}

#pragma once

#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <vector>

// ----------------------------------------------------------------------------------------------
// HpssCore: one STFT resolution of a median-filter harmonic/percussive split (Fitzgerald 2010),
// stereo. Returns the TONAL estimate per channel, delayed by `latency` samples.
//   Tonal evidence      H = median of |X| along TIME (stable over time)
//   Transient evidence  P = median of |X| along FREQUENCY (broadband at one instant)
//   transient mask  m = (P/b)^p / ((P/b)^p + H^p)     b = threshold, p = sharpness
//   then per-bin release (Decay) and symmetric smoothing (Smoothing), both in milliseconds.
// linked = true: ONE mask from the combined L/R magnitude, applied to both channels (stable stereo
// image, no L/R disagreement). linked = false: each channel decides on its own.
// ----------------------------------------------------------------------------------------------
template <int Order, int Lframes>
class HpssCore
{
public:
    static constexpr int N = 1 << Order;
    static constexpr int H = N / 4;
    static constexpr int bins = N / 2 + 1;
    static constexpr int L = Lframes;
    static constexpr int K = 2 * L + 1;
    static constexpr int maxF = 24;
    static constexpr int stages = 8;              // each frame's work is spread over 8 slices of one hop
    static constexpr int latency = N + L * H + H;  // + one hop: the slack the spreading needs

    static constexpr int nextPow2 (int v) { int p = 1; while (p < v) p <<= 1; return p; }
    static constexpr int ringSize = nextPow2 (latency + N + 1);
    static constexpr int ringMask = ringSize - 1;

    bool linked = true;
    int freqSmoothPasses = 0;   // [1 2 1]/4 passes across frequency (measured: worse, kept for diagnostics)
    bool debugForceTonal = false;

    HpssCore()
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            spec[ch].resize ((size_t) K);
            mag[ch].resize ((size_t) K);
            for (int k = 0; k < K; ++k) { spec[ch][(size_t) k].assign (bins, {}); mag[ch][(size_t) k].assign (bins, 0.0f); }
            inRing[ch].assign (ringSize, 0.0f);
            olaRing[ch].assign (ringSize, 0.0f);
            decayState[ch].assign (bins, 0.0f);
            smoothState[ch].assign (bins, 0.0f);
            rawMask[ch].assign (bins, 0.0f);
        }
        fftIn.assign (N, {});
        fftOut.assign (N, {});
        window.resize (N);
        for (int j = 0; j < N; ++j)
            window[(size_t) j] = std::sqrt (0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) j / (float) N));
    }

    void prepare (double sampleRate) { sr = sampleRate; reset(); }

    void reset()
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            std::fill (inRing[ch].begin(), inRing[ch].end(), 0.0f);
            std::fill (olaRing[ch].begin(), olaRing[ch].end(), 0.0f);
            for (auto& s : spec[ch]) std::fill (s.begin(), s.end(), std::complex<float> {});
            for (auto& m : mag[ch])  std::fill (m.begin(), m.end(), 0.0f);
            std::fill (decayState[ch].begin(), decayState[ch].end(), 0.0f);
            std::fill (smoothState[ch].begin(), smoothState[ch].end(), 0.0f);
        }
        count = 0;
        head = 0;
        stage = -1;
    }

    void setParams (float sharpness, float threshold, float decayMs, float smoothMs, float kernelMs, float kernelHz)
    {
        power = sharpness;
        invThresh = 1.0f / std::max (1.0e-3f, threshold);
        const double hopSec = (double) H / sr;
        releaseCoef = decayMs  > 0.01f ? (float) std::exp (-hopSec / (decayMs  * 0.001)) : 0.0f;
        smoothCoef  = smoothMs > 0.01f ? (float) std::exp (-hopSec / (smoothMs * 0.001)) : 0.0f;
        tHalf = juce::jlimit (1, L, (int) std::lround (kernelMs * 0.001 * sr / H));
        fHalf = juce::jlimit (1, maxF, (int) std::lround (kernelHz / (sr / N)));
    }

    inline void process (float xL, float xR, float& tonalL, float& tonalR)
    {
        const auto w = (size_t) (count & ringMask);
        inRing[0][w] = xL;
        inRing[1][w] = xR;
        ++count;
        if (count % H == 0)
        {
            frameCount = count;
            head = (head + 1) % K;
            stage = 0;
        }
        if (stage >= 0 && (count - frameCount) % (H / stages) == 0)
        {
            runStage (stage);
            if (++stage == stages) stage = -1;
        }

        const long long s = count - 1 - latency;
        if (s < 0) { tonalL = tonalR = 0.0f; return; }
        const auto idx = (size_t) (s & ringMask);
        tonalL = olaRing[0][idx]; olaRing[0][idx] = 0.0f;
        tonalR = olaRing[1][idx]; olaRing[1][idx] = 0.0f;
    }

private:
    static float median (float* v, int n)
    {
        std::nth_element (v, v + n / 2, v + n);
        return v[n / 2];
    }

    // magnitude seen by the mask: one channel, or the RMS of both when linked
    inline float magAt (int which, int k, int b) const
    {
        if (which >= 0) return mag[which][(size_t) k][(size_t) b];
        const float l = mag[0][(size_t) k][(size_t) b], r = mag[1][(size_t) k][(size_t) b];
        return std::sqrt (0.5f * (l * l + r * r));
    }

    void computeMask (int which, int c, std::vector<float>& out, int b0, int b1)
    {
        std::array<float, K> tv;
        std::array<float, 2 * maxF + 1> fv;
        const int tn = 2 * tHalf + 1;
        const float eps = 1.0e-12f;

        for (int b = b0; b < b1; ++b)
        {
            for (int k = 0; k < tn; ++k)
                tv[(size_t) k] = magAt (which, (c + k - tHalf + K) % K, b);
            const float hMed = median (tv.data(), tn);

            const int lo = std::max (0, b - fHalf), hi = std::min (bins - 1, b + fHalf);
            int n = 0;
            for (int q = lo; q <= hi; ++q) fv[(size_t) n++] = magAt (which, c, q);
            const float pMed = median (fv.data(), n);

            const float pp = std::pow (pMed * invThresh, power), hp = std::pow (hMed, power);
            out[(size_t) b] = debugForceTonal ? 0.0f : pp / (pp + hp + eps);
        }

        for (int pass = 0; b1 == bins && pass < freqSmoothPasses; ++pass)
        {
            float prev = out[0];
            for (int b = 0; b < bins; ++b)
            {
                const float cur = out[(size_t) b];
                const float next = out[(size_t) std::min (bins - 1, b + 1)];
                out[(size_t) b] = 0.25f * prev + 0.5f * cur + 0.25f * next;
                prev = cur;
            }
        }
    }

    void applyDynamics (std::vector<float>& m, std::vector<float>& dState, std::vector<float>& sState, int b0, int b1)
    {
        for (int b = b0; b < b1; ++b)
        {
            const float tm = m[(size_t) b];
            float& d = dState[(size_t) b];
            d = tm > d ? tm : d * releaseCoef + tm * (1.0f - releaseCoef);     // Decay: release only
            float& sm = sState[(size_t) b];
            sm = sm * smoothCoef + d * (1.0f - smoothCoef);                     // Smoothing: both ways
            m[(size_t) b] = sm;
        }
    }

    // Stage plan (H/8 samples apart):  0 FFT L | 1 FFT R | 2..5 mask, a quarter of the bins each |
    //                                   6 iFFT + overlap-add L | 7 iFFT + overlap-add R
    void runStage (int st)
    {
        using Cx = std::complex<float>;
        const int c = (head - L + K) % K;   // centre frame (L frames of look-ahead)

        if (st <= 1)
        {
            const int ch = st;
            for (int j = 0; j < N; ++j)
                fftIn[(size_t) j] = Cx (inRing[ch][(size_t) ((frameCount - N + j) & ringMask)] * window[(size_t) j], 0.0f);
            fft.perform (fftIn.data(), fftOut.data(), false);
            for (int b = 0; b < bins; ++b)
            {
                spec[ch][(size_t) head][(size_t) b] = fftOut[(size_t) b];
                mag [ch][(size_t) head][(size_t) b] = std::abs (fftOut[(size_t) b]);
            }
            return;
        }

        if (st <= 5)
        {
            const int slice = st - 2;
            const int b0 = bins * slice / 4, b1 = bins * (slice + 1) / 4;
            if (linked)
            {
                computeMask (-1, c, rawMask[0], b0, b1);
                applyDynamics (rawMask[0], decayState[0], smoothState[0], b0, b1);
            }
            else
                for (int ch = 0; ch < 2; ++ch)
                {
                    computeMask (ch, c, rawMask[ch], b0, b1);
                    applyDynamics (rawMask[ch], decayState[ch], smoothState[ch], b0, b1);
                }
            return;
        }

        const int ch = st - 6;
        const auto& m = rawMask[linked ? 0 : ch];
        for (int b = 0; b < bins; ++b)
            fftIn[(size_t) b] = spec[ch][(size_t) c][(size_t) b] * (1.0f - m[(size_t) b]);
        for (int b = bins; b < N; ++b)
            fftIn[(size_t) b] = std::conj (fftIn[(size_t) (N - b)]);

        fft.perform (fftIn.data(), fftOut.data(), true);   // JUCE scales the inverse by 1/N

        const long long start = frameCount - (long long) L * H - N;
        for (int j = 0; j < N; ++j)
            olaRing[ch][(size_t) ((start + j) & ringMask)] += fftOut[(size_t) j].real() * window[(size_t) j] * 0.5f;
    }

    double sr = 44100.0;
    float power = 2.5f, invThresh = 1.0f, releaseCoef = 0.0f, smoothCoef = 0.0f;
    int tHalf = L, fHalf = 8;

    juce::dsp::FFT fft { Order };
    std::array<std::vector<float>, 2> inRing, olaRing, decayState, smoothState, rawMask;
    std::array<std::vector<std::vector<std::complex<float>>>, 2> spec;
    std::array<std::vector<std::vector<float>>, 2> mag;
    std::vector<float> window;
    std::vector<std::complex<float>> fftIn, fftOut;
    long long count = 0, frameCount = 0;
    int head = 0, stage = -1;
};

// ----------------------------------------------------------------------------------------------
// TransientSplit (stereo): two resolutions merged by a LINEAR-PHASE complementary crossover on the
// TONAL estimate (an IIR crossover rotates phase right where tonal content sits and smears the split).
//   long  STFT (2048, ~46 ms) : fine frequency detail -> kick vs bass, chords, low mids
//   short STFT  (512, ~12 ms) : fine time detail      -> tight hats / snaps, no smearing
//   tonal = delay(tonalShort) + FIR_LP(tonalLong - tonalShort)
//   Transient = delayed input - tonal                       (perfect reconstruction)
// ----------------------------------------------------------------------------------------------
class TransientSplit
{
public:
    using LongCore  = HpssCore<11, 4>;    // N 2048, hop 512, look-ahead 4 frames
    using ShortCore = HpssCore<9, 16>;    // N 512,  hop 128, look-ahead 16 frames
    static constexpr int firLen = 255;
    static constexpr int firDelay = (firLen - 1) / 2;
    static constexpr int coreLatency = std::max (LongCore::latency, ShortCore::latency);
    static constexpr int latency = coreLatency + firDelay;
    static constexpr int N = LongCore::N;

    TransientSplit()
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            dryDelay[ch].assign (latency + 1, 0.0f);
            shortDelay[ch].assign (latency - ShortCore::latency + 1, 0.0f);
            longDelay[ch].assign (latency - LongCore::latency - firDelay + 1, 0.0f);
            dHist[ch].assign (firLen, 0.0f);
        }
        taps.assign (firLen, 0.0f);
    }

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        longCore.prepare (sr);
        shortCore.prepare (sr);
        for (int ch = 0; ch < 2; ++ch)
        {
            std::fill (dryDelay[ch].begin(), dryDelay[ch].end(), 0.0f);
            std::fill (shortDelay[ch].begin(), shortDelay[ch].end(), 0.0f);
            std::fill (longDelay[ch].begin(), longDelay[ch].end(), 0.0f);
            std::fill (dHist[ch].begin(), dHist[ch].end(), 0.0f);
        }
        dryPos = shortPos = longPos = dPos = 0;
        designFir();
        applyParams();
    }

    void setCrossover (double hz) { crossoverHz = hz; designFir(); }
    void setSource (int s) { if (s != source) { source = juce::jlimit (0, 6, s); applyParams(); } }
    void setLinked (bool b) { longCore.linked = shortCore.linked = b; }

    // sep, decay: 0..1 ; smoothMs: 0..50
    void setParams (float sepIn, float decayIn, float smoothMsIn)
    {
        if (sepIn == sep && decayIn == decay && smoothMsIn == smoothMs) return;
        sep = sepIn; decay = decayIn; smoothMs = smoothMsIn;
        applyParams();
    }

    void process (float xL, float xR, float& trL, float& tnL, float& trR, float& tnR)
    {
        float tl[2], ts[2];
        longCore.process (xL, xR, tl[0], tl[1]);
        shortCore.process (xL, xR, ts[0], ts[1]);
        const float x[2] = { xL, xR };
        float tr[2], tn[2];

        const int sSize = (int) shortDelay[0].size(), lSize = (int) longDelay[0].size(), dSize = (int) dryDelay[0].size();
        for (int ch = 0; ch < 2; ++ch)
        {
            // long core: aligned to coreLatency ; short core: aligned to coreLatency + firDelay
            float tlA = tl[ch];
            if (lSize > 1) { longDelay[ch][(size_t) longPos] = tl[ch]; tlA = longDelay[ch][(size_t) ((longPos + 1) % lSize)]; }
            shortDelay[ch][(size_t) shortPos] = ts[ch];
            const float tsOut  = shortDelay[ch][(size_t) ((shortPos + 1) % sSize)];
            const float tsCore = shortDelay[ch][(size_t) ((shortPos + 1 + firDelay) % sSize)];   // short tonal at coreLatency

            dHist[ch][(size_t) dPos] = tlA - tsCore;
            double acc = 0.0;
            int idx = dPos;
            for (int k = 0; k < firLen; ++k)
            {
                acc += (double) taps[(size_t) k] * dHist[ch][(size_t) idx];
                idx = idx == 0 ? firLen - 1 : idx - 1;
            }

            const float tonal = tsOut + (float) acc;
            dryDelay[ch][(size_t) dryPos] = x[ch];
            const float dry = dryDelay[ch][(size_t) ((dryPos + 1) % dSize)];
            tn[ch] = tonal;
            tr[ch] = dry - tonal;
        }
        if (lSize > 1) longPos = (longPos + 1) % lSize;
        shortPos = (shortPos + 1) % sSize;
        dryPos = (dryPos + 1) % dSize;
        dPos = (dPos + 1) % firLen;

        trL = tr[0]; tnL = tn[0]; trR = tr[1]; tnR = tn[1];
    }

    // mono convenience (same signal on both channels)
    void process (float x, float& transientOut, float& tonalOut)
    {
        float trR, tnR;
        process (x, x, transientOut, tonalOut, trR, tnR);
    }

    void setDebugForceTonal (bool b) { longCore.debugForceTonal = shortCore.debugForceTonal = b; }

private:
    void designFir()
    {
        // windowed-sinc low-pass (Blackman), unity DC gain
        const double fc = crossoverHz / sr;
        double sum = 0.0;
        for (int k = 0; k < firLen; ++k)
        {
            const double m = k - firDelay;
            const double sinc = m == 0 ? 2.0 * fc : std::sin (2.0 * juce::MathConstants<double>::pi * fc * m) / (juce::MathConstants<double>::pi * m);
            const double w = 0.42 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * k / (firLen - 1))
                                  + 0.08 * std::cos (4.0 * juce::MathConstants<double>::pi * k / (firLen - 1));
            taps[(size_t) k] = (float) (sinc * w);
            sum += sinc * w;
        }
        for (auto& t : taps) t = (float) (t / sum);
    }

    void applyParams()
    {
        // Source: time / frequency extent of the medians (ms, Hz), resolution independent.
        static constexpr float table[7][2] = {
            { 46, 350 },   // General
            { 46, 520 },   // Full Mix
            { 30, 260 },   // Drums / Perc
            { 60, 130 },   // Bass
            { 50, 430 },   // Piano / Synth
            { 40, 350 },   // Guitar
            { 35, 430 },   // Vocal
        };
        const float kMs = table[source][0], kHz = table[source][1];

        // Separation: raises the threshold (less is called Transient) and sharpens the boundary.
        const float sharp  = sharpBase + sharpRange * sep;
        const float thresh = std::pow (2.0f, (sep - sepCentre) * 6.0f);   // ~0.03 .. 0.23 (50%) .. 1.9
        // Decay: release of Transient regions, 0 .. 400 ms (quadratic for finer low end).
        const float decMs  = 400.0f * decay * decay;

        longCore.freqSmoothPasses  = smoothPassesLong;
        shortCore.freqSmoothPasses = smoothPassesShort;
        longCore.setParams  (sharp, thresh, decMs, smoothMs, kMs, kHz);
        shortCore.setParams (sharp, thresh, decMs, smoothMs, kMs, kHz);
    }

public:
    double crossoverHz = 1500.0;
    float sharpBase = 1.0f, sharpRange = 0.0f;   // soft mask (fitted on test material)
    int smoothPassesLong = 0, smoothPassesShort = 0;
    float sepCentre = 0.75f;       // fitted on test material

private:
    double sr = 44100.0;
    int source = 0;
    float sep = 0.5f, decay = 0.0f, smoothMs = 0.0f;
    LongCore longCore;
    ShortCore shortCore;
    std::array<std::vector<float>, 2> dryDelay, shortDelay, longDelay, dHist;
    std::vector<float> taps;
    int dryPos = 0, shortPos = 0, longPos = 0, dPos = 0;
};

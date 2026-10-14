#pragma once

#include <juce_dsp/juce_dsp.h>
#include "BandFilter.h"
#include <memory>
#include <vector>

// Analog-style saturation stage with five selectable curve styles.
// Chain, all at 4x oversampling (linear-phase FIR, integer latency):
//   [dry] -> Low Cut (+Thump bump) -> Tone tilt -> style pre-EQ -> Drive -> style waveshaper -> DC block
//   -> style post-EQ -> High Cut (6 or 30 dB/oct) -> Output (+Auto) -> Mix with dry
// When the module is off, audio takes a plain delay of the same latency so the plugin latency never changes.
class Saturator
{
public:
    enum Style { A = 0, E, N, T, P };

    struct Settings
    {
        int style = A;
        float driveDb = 0.0f;       // Drive + Punish, in dB
        float toneDb = 0.0f;        // + = bright, - = dark (total tilt)
        float lowCutHz = 10.0f;     // <= 10 Hz = off
        bool thump = false;
        float highCutHz = 20000.0f; // >= 19.9 kHz = off
        bool steep = false;
        float outDb = 0.0f;         // already includes Auto compensation
        float mix = 1.0f;
    };

    void prepare (double sampleRate, int maxBlock)
    {
        baseSr = sampleRate;
        maxBlk = juce::jmax (16, maxBlock);
        os = std::make_unique<juce::dsp::Oversampling<float>> (2, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple,
                                                                true, true);
        os->initProcessing ((size_t) maxBlk);
        osSr = baseSr * 4.0;
        lat = (int) std::lround (os->getLatencyInSamples());
        for (auto& d : delay) d.assign ((size_t) lat + 1, 0.0f);
        delayPos = 0;
        driveG.reset (osSr, 0.03);
        outG.reset (osSr, 0.03);
        mixV.reset (osSr, 0.03);
        driveG.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (cur.driveDb));
        outG.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (cur.outDb));
        mixV.setCurrentAndTargetValue (cur.mix);
        rebuild();
        clearState();
        wasActive = false;
    }

    int latency() const { return lat; }

    void setSettings (const Settings& s)
    {
        const bool filtersChanged = s.style != cur.style || s.toneDb != cur.toneDb || s.lowCutHz != cur.lowCutHz
                                    || s.thump != cur.thump || s.highCutHz != cur.highCutHz || s.steep != cur.steep;
        cur = s;
        driveG.setTargetValue (juce::Decibels::decibelsToGain (s.driveDb));
        outG.setTargetValue (juce::Decibels::decibelsToGain (s.outDb));
        mixV.setTargetValue (s.mix);
        if (filtersChanged) rebuild();
    }

    // active = module on (oversampled path); shape = actually saturate this stream
    void process (juce::AudioBuffer<float>& buf, int numCh, int n, bool active, bool shape)
    {
        if (! active)
        {
            for (int c = 0; c < numCh; ++c)
            {
                float* d = buf.getWritePointer (c);
                auto& line = delay[(size_t) c];
                int p = delayPos;
                for (int i = 0; i < n; ++i)
                {
                    line[(size_t) p] = d[i];
                    p = (p + 1) % (int) line.size();
                    d[i] = line[(size_t) p];
                }
            }
            delayPos = (delayPos + n) % (int) delay[0].size();
            wasActive = false;
            return;
        }

        if (! wasActive) { os->reset(); clearState(); wasActive = true; }

        for (int start = 0; start < n; start += maxBlk)
        {
            const int len = juce::jmin (maxBlk, n - start);
            juce::dsp::AudioBlock<float> block (buf.getArrayOfWritePointers(), (size_t) numCh, (size_t) start, (size_t) len);
            auto up = os->processSamplesUp (block);
            if (shape)
            {
                const int un = (int) up.getNumSamples();
                for (int i = 0; i < un; ++i)
                {
                    const float dg = driveG.getNextValue(), og = outG.getNextValue(), mx = mixV.getNextValue();
                    for (int c = 0; c < numCh; ++c)
                    {
                        float* s = up.getChannelPointer ((size_t) c);
                        s[i] = processSample (c, s[i], dg, og, mx);
                    }
                }
            }
            os->processSamplesDown (block);
        }
    }

private:
    // Per-style static curve, fitted to measurements of the reference (harmonics H2..H5 at -18 dBFS and the
    // compression curve -40..0 dBFS; Tools/fit_shapers.py). s(u) = u / (1 + |u|^h)^(1/h), biased by b.
    struct Curve { float gainDb, bias, hard; };
    static const Curve& curve (int style)
    {
        static const Curve table[5] = {
            { 0.741f, 0.2026f, 1.968f },   // style 1 (S)
            { 4.139f, 0.2310f, 8.322f },   // style 2 (T)
            { 2.538f, 0.1283f, 7.410f },   // style 3 (P)
            { 2.994f, 0.0843f, 3.306f },   // style 4 (N)
            { 5.510f, 0.1439f, 4.505f }    // style 5 (K)
        };
        return table[juce::jlimit (0, 4, style)];
    }

    static float softClip (float u, float h) { return u / std::pow (1.0f + std::pow (std::abs (u), h), 1.0f / h); }

    float shaper (int style, float x) const
    {
        const auto& c = curve (style);
        const float y = softClip (x * curveGain + c.bias, c.hard) - curveOffset;
        return y / curveSlope / curveGain;
    }

    float processSample (int c, float in, float drive, float out, float mix)
    {
        double x = in;
        if (lowCutOn) x = lowCut.process (c, x);
        if (cur.thump) x = thump.process (c, x);
        x = tiltLo.process (c, x);
        x = tiltHi.process (c, x);
        for (int k = 0; k < nPre; ++k) x = pre[(size_t) k].process (c, x);

        double y = shaper (cur.style, (float) (x * drive));

        // DC blocker (asymmetric styles create DC)
        const double d = y - dcX[c] + dcR * dcY[c];
        dcX[c] = y; dcY[c] = d; y = d;

        for (int k = 0; k < nPost; ++k) y = post[(size_t) k].process (c, y);
        for (int k = 0; k < nHc; ++k) y = hc[(size_t) k].process (c, y);

        y *= out;
        return (float) (in + mix * (y - in));
    }

    void rebuild()
    {
        const double sr = osSr;
        lowCutOn = cur.lowCutHz > 10.5f;
        BandFilter::pass2 (lowCut, true, sr, juce::jmax (10.0f, cur.lowCutHz), 0.62);
        BandFilter::rbj (thump, BandType::Peak, sr, juce::jmax (20.0f, cur.lowCutHz * 1.55f), 3.5, 1.0);
        const double bright = std::max (0.0, (double) cur.toneDb), dark = std::max (0.0, -(double) cur.toneDb);
        if (bright > 0.0)
        {
            BandFilter::rbj (tiltLo, BandType::LowShelf,  sr, 420.0, -1.55 * bright, 0.55);
            BandFilter::rbj (tiltHi, BandType::HighShelf, sr, 2600.0, 0.38 * bright, 0.6);
        }
        else
        {
            BandFilter::rbj (tiltLo, BandType::LowShelf,  sr, 160.0,  0.37 * dark, 0.6);
            BandFilter::rbj (tiltHi, BandType::HighShelf, sr, 1300.0, -1.55 * dark, 0.55);
        }

        nPre = nPost = 0;   // measured response of the reference is flat at drive 0 for every style

        const auto& cv = curve (cur.style);
        curveGain = std::pow (10.0f, cv.gainDb / 20.0f);
        curveOffset = softClip (cv.bias, cv.hard);
        const float e = 1.0e-4f;
        curveSlope = (softClip (cv.bias + e, cv.hard) - softClip (cv.bias - e, cv.hard)) / (2.0f * e);

        // High Cut: 6 dB/oct, or 30 dB/oct (5th-order Butterworth) with Steep
        nHc = 0;
        if (cur.highCutHz < 19900.0f)
        {
            BandFilter::pass1 (hc[(size_t) nHc++], false, sr, cur.highCutHz);
            if (cur.steep)
            {
                BandFilter::pass2 (hc[(size_t) nHc++], false, sr, cur.highCutHz * 1.1, 0.55);
                BandFilter::pass2 (hc[(size_t) nHc++], false, sr, cur.highCutHz * 1.1, 0.9);
            }
        }
        dcR = std::exp (-2.0 * juce::MathConstants<double>::pi * 10.0 / sr);
    }

    void clearState()
    {
        for (auto* b : { &lowCut, &thump, &tiltLo, &tiltHi }) b->clear();
        for (auto& b : pre) b.clear();
        for (auto& b : post) b.clear();
        for (auto& b : hc) b.clear();
        dcX[0] = dcX[1] = dcY[0] = dcY[1] = 0.0;
    }

    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    double baseSr = 44100.0, osSr = 176400.0;
    int maxBlk = 512, lat = 0;
    Settings cur;
    juce::SmoothedValue<float> driveG, outG, mixV;

    Biquad lowCut, thump, tiltLo, tiltHi;
    std::array<Biquad, 2> pre, post;
    std::array<Biquad, 3> hc;
    int nPre = 0, nPost = 0, nHc = 0;
    float curveGain = 1.0f, curveOffset = 0.0f, curveSlope = 1.0f;
    bool lowCutOn = false;
    double dcR = 0.9996, dcX[2] {}, dcY[2] {};

    std::array<std::vector<float>, 2> delay;
    int delayPos = 0;
    bool wasActive = false;
};

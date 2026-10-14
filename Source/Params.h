#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/BandFilter.h"
#include "dsp/StreamEQ.h"

namespace Params
{
    inline const juce::StringArray slopeNames { "6", "12", "24", "36", "48", "72", "96" };
    inline constexpr int slopeValues[7] = { 6, 12, 24, 36, 48, 72, 96 };
    inline const juce::StringArray typeNames { "Low Shelf", "Peak", "Notch", "High Shelf", "Tilt Shelf", "Bandpass" };
    inline const juce::StringArray sourceNames { "General", "Full Mix", "Drums / Perc", "Bass", "Piano / Synth", "Guitar", "Vocal" };

    inline juce::String id (int band, const char* name) { return "b" + juce::String (band) + "_" + name; }

    inline std::unique_ptr<juce::AudioParameterFloat> mkFloat (const juce::String& id, const juce::String& name,
                                                               juce::NormalisableRange<float> r, float def, int decimals = 1)
    {
        auto attrs = juce::AudioParameterFloatAttributes()
                         .withStringFromValueFunction ([decimals] (float v, int) { return juce::String (v, decimals); })
                         .withValueFromStringFunction ([] (const juce::String& t) { return t.getFloatValue(); });
        return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, r, def, attrs);
    }

    inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        using namespace juce;
        using R = NormalisableRange<float>;
        AudioProcessorValueTreeState::ParameterLayout l;
        int v = 1;
        auto pid = [&] (const String& s) { return ParameterID { s, v }; };

        // Split
        l.add (std::make_unique<AudioParameterChoice> (pid ("split_source"), "Source", sourceNames, 0));
        l.add (std::make_unique<AudioParameterBool>   (pid ("split_lock"), "Source Lock", false));
        l.add (mkFloat ("split_sep", "Transient Separation", R (0, 100, 0.1f), 50.0f));
        l.add (mkFloat ("split_decay", "Transient Decay", R (0, 100, 0.1f), 30.0f));
        l.add (mkFloat ("split_smooth", "Smoothing", R (0, 50, 0.1f), 10.0f));
        l.add (std::make_unique<AudioParameterChoice> (pid ("split_solo"), "Split Solo", StringArray { "Off", "Transient", "Tonal" }, 0));

        // Bands
        R freqRange (20.0f, 20000.0f, 0.0f, 0.3f);
        R gainRange (-24.0f, 24.0f, 0.1f);
        R qRange (0.1f, 18.0f, 0.0f, 0.4f);
        static constexpr float defFreq[8] = { 20, 100, 250, 600, 1500, 4000, 10000, 20000 };
        static constexpr int defType[6] = { 0, 1, 1, 1, 1, 3 };

        for (int b = 0; b < 8; ++b)
        {
            const String n = "Band " + String (b + 1) + " ";
            const bool para = b >= 1 && b <= 6;
            l.add (std::make_unique<AudioParameterBool>   (pid (id (b, "active")), n + "Active", para));
            l.add (std::make_unique<AudioParameterBool>   (pid (id (b, "solo")), n + "Solo", false));
            l.add (std::make_unique<AudioParameterChoice> (pid (id (b, "slope")), n + "Slope", slopeNames, 1));
            if (para)
            {
                l.add (std::make_unique<AudioParameterChoice> (pid (id (b, "type")), n + "Type", typeNames, defType[b - 1]));
                l.add (mkFloat (id (b, "freq"), n + "Freq", freqRange, defFreq[b]));
                l.add (mkFloat (id (b, "gainT"), n + "Gain Transient", gainRange, 0.0f));
                l.add (mkFloat (id (b, "gainN"), n + "Gain Tonal", gainRange, 0.0f));
                l.add (mkFloat (id (b, "qT"), n + "Q Transient", qRange, 1.0f, 2));
                l.add (mkFloat (id (b, "qN"), n + "Q Tonal", qRange, 1.0f, 2));
                l.add (std::make_unique<AudioParameterChoice> (pid (id (b, "notch")), n + "Notch Target", StringArray { "Transient", "Both", "Tonal" }, 1));
                l.add (std::make_unique<AudioParameterChoice> (pid (id (b, "panMode")), n + "Pan Mode", StringArray { "L/R", "M/S" }, 0));
                l.add (mkFloat (id (b, "panT"), n + "Pan Transient", R (-100, 100, 0.1f), 0.0f));
                l.add (mkFloat (id (b, "panN"), n + "Pan Tonal", R (-100, 100, 0.1f), 0.0f));
            }
            else
            {
                l.add (mkFloat (id (b, "freqT"), n + "Freq Transient", freqRange, defFreq[b]));
                l.add (mkFloat (id (b, "freqN"), n + "Freq Tonal", freqRange, defFreq[b]));
            }
        }

        // Master
        R mg (-30.0f, 30.0f, 0.1f), pn (-100, 100, 0.1f), sc (0, 200, 0.1f);
        l.add (mkFloat ("out_gainT", "Out Gain Transient", mg, 0.0f));
        l.add (mkFloat ("out_gainN", "Out Gain Tonal", mg, 0.0f));
        l.add (std::make_unique<AudioParameterChoice> (pid ("out_mode"), "Out Pan Mode", StringArray { "L/R", "M/S" }, 0));
        l.add (mkFloat ("out_panT", "Out Pan Transient", pn, 0.0f));
        l.add (mkFloat ("out_panN", "Out Pan Tonal", pn, 0.0f));
        l.add (mkFloat ("scaleT", "Scale Transient", sc, 100.0f));
        l.add (mkFloat ("scaleN", "Scale Tonal", sc, 100.0f));
        l.add (std::make_unique<AudioParameterBool>   (pid ("phase_inv"), "Phase Invert", false));
        l.add (std::make_unique<AudioParameterBool>   (pid ("bypass"), "Bypass", false));
        l.add (std::make_unique<AudioParameterBool>   (pid ("eq_bypass"), "EQ Bypass", false));

        // Saturation (SAT)
        l.add (std::make_unique<AudioParameterBool>   (pid ("sat_on"), "Sat On", false));
        l.add (std::make_unique<AudioParameterChoice> (pid ("sat_target"), "Sat Target", StringArray { "Both", "Transient", "Tonal" }, 0));
        l.add (std::make_unique<AudioParameterChoice> (pid ("sat_style"), "Sat Style", StringArray { "S", "T", "P", "N", "K" }, 0));
        l.add (mkFloat ("sat_drive", "Sat Drive", R (0, 10, 0.01f), 3.0f, 1));
        l.add (std::make_unique<AudioParameterBool>   (pid ("sat_punish"), "Sat Dstrct", false));
        l.add (mkFloat ("sat_tone", "Sat Tone", R (-100, 100, 0.1f), 0.0f, 0));
        auto hzOff = [] (float offBelow, float offAbove)
        {
            return AudioParameterFloatAttributes()
                .withStringFromValueFunction ([offBelow, offAbove] (float v, int)
                {
                    if (v <= offBelow || v >= offAbove) return String ("OFF");
                    return v >= 1000.0f ? String (v / 1000.0f, 1) + "k" : String ((int) std::round (v));
                })
                .withValueFromStringFunction ([offBelow, offAbove] (const String& t)
                {
                    if (t.trim().equalsIgnoreCase ("off")) return offBelow > 0 ? offBelow : offAbove;
                    const auto lc = t.trim().toLowerCase();
                    return lc.endsWithChar ('k') ? lc.dropLastCharacters (1).getFloatValue() * 1000.0f : lc.getFloatValue();
                });
        };
        l.add (std::make_unique<AudioParameterFloat> (pid ("sat_lowcut"), "Sat Low Cut", R (10.0f, 2000.0f, 0.0f, 0.3f), 10.0f, hzOff (10.5f, 1.0e9f)));
        l.add (std::make_unique<AudioParameterBool>   (pid ("sat_thump"), "Sat Thump", false));
        l.add (std::make_unique<AudioParameterFloat> (pid ("sat_highcut"), "Sat High Cut", R (1000.0f, 20000.0f, 0.0f, 0.4f), 20000.0f, hzOff (-1.0f, 19900.0f)));
        l.add (std::make_unique<AudioParameterBool>   (pid ("sat_steep"), "Sat Steep", false));
        l.add (mkFloat ("sat_mix", "Sat Mix", R (0, 100, 0.1f), 100.0f, 0));
        l.add (mkFloat ("sat_output", "Sat Output", R (-24, 12, 0.1f), 0.0f, 1));
        l.add (std::make_unique<AudioParameterBool>   (pid ("sat_auto"), "Sat Auto", true));
        return l;
    }

    inline float raw (juce::AudioProcessorValueTreeState& s, const juce::String& id)
    {
        return s.getRawParameterValue (id)->load();
    }

    // stream: 0 = Transient, 1 = Tonal
    inline BandTarget readBand (juce::AudioProcessorValueTreeState& s, int b, int stream, double scale = 1.0)
    {
        BandTarget t;
        auto& sp = t.spec;
        sp.active = raw (s, id (b, "active")) > 0.5f;
        sp.slope = slopeValues[juce::jlimit (0, 6, (int) raw (s, id (b, "slope")))];

        if (b == 0 || b == 7)
        {
            sp.type = b == 0 ? BandType::HighPass : BandType::LowPass;
            sp.freq = raw (s, id (b, stream == 0 ? "freqT" : "freqN"));
            return t;
        }

        sp.type = (BandType) juce::jlimit (0, 5, (int) raw (s, id (b, "type")));
        sp.freq = raw (s, id (b, "freq"));
        sp.gain = raw (s, id (b, stream == 0 ? "gainT" : "gainN")) * scale;
        sp.q    = raw (s, id (b, stream == 0 ? "qT" : "qN"));

        if (sp.type == BandType::Notch)
        {
            const int target = (int) raw (s, id (b, "notch"));   // 0 T, 1 both, 2 tonal
            if ((stream == 0 && target == 2) || (stream == 1 && target == 0))
                sp.active = false;
        }
        t.panMode = (int) raw (s, id (b, "panMode"));
        t.pan = raw (s, id (b, stream == 0 ? "panT" : "panN")) * 0.01;
        return t;
    }
}

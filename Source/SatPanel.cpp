#include "SatPanel.h"
#include "PluginEditor.h"
#include "Skin.h"

using Attach = juce::AudioProcessorValueTreeState;

SatPanel::SatPanel (PirateEQ2Processor& p) : proc (p)
{
    setLookAndFeel (&laf);
    auto& s = proc.apvts;

    auto toggle = [this, &s] (juce::TextButton& b, const char* id)
    {
        b.setClickingTogglesState (true);
        b.setColour (juce::TextButton::buttonOnColourId, Colours::accent);
        buttonAtts.push_back (std::make_unique<Attach::ButtonAttachment> (s, id, b));
        addAndMakeVisible (b);
    };
    toggle (power, "sat_on");
    toggle (punish, "sat_punish");
    toggle (thump, "sat_thump");
    toggle (steep, "sat_steep");
    toggle (autoBtn, "sat_auto");

    const char* letters[5] = { "S", "T", "P", "N", "K" };
    const char* tips[5] = { "S: smooth tape preamp", "T: console channel (beefy lows, airy top)",
                            "P: germanium preamp (weighty lows, focused mids)", "N: triode (even harmonics, warm)",
                            "K: pentode (odd harmonics, aggressive)" };
    for (int i = 0; i < 5; ++i)
    {
        auto& b = styles[(size_t) i];
        b.setButtonText (letters[i]);
        b.setTooltip (tips[i]);
        b.setColour (juce::TextButton::buttonOnColourId, Colours::accent);
        b.onClick = [this, i]
        {
            if (auto* prm = proc.apvts.getParameter ("sat_style"))
            {
                prm->beginChangeGesture();
                prm->setValueNotifyingHost (prm->convertTo0to1 ((float) i));
                prm->endChangeGesture();
            }
        };
        addAndMakeVisible (b);
    }

    target.addItemList ({ "Both", "Trans", "Tonal" }, 1);
    targetAtt = std::make_unique<Attach::ComboBoxAttachment> (s, "sat_target", target);
    target.setTooltip ("Which split stream gets saturated");
    addAndMakeVisible (target);

    struct K { const char* id; const char* name; const char* suffix; };
    const K defs[6] = { { "sat_drive", "DRIVE", "" }, { "sat_tone", "TONE", "" }, { "sat_lowcut", "LOW CUT", "" },
                        { "sat_highcut", "HIGH CUT", "" }, { "sat_mix", "MIX", " %" }, { "sat_output", "OUTPUT", " dB" } };
    for (auto& d : defs)
    {
        auto k = std::make_unique<Knob>();
        const bool isDrive = juce::String (d.id) == "sat_drive";
        k->attach (s, d.id, d.name, isDrive ? Colours::accent : Colours::tonal, d.suffix);
        k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 50, 14);
        addAndMakeVisible (*k);
        knobs.push_back (std::move (k));
    }

    startTimerHz (10);
    timerCallback();
}

SatPanel::~SatPanel()
{
    setLookAndFeel (nullptr);
}

void SatPanel::timerCallback()
{
    const int st = (int) Params::raw (proc.apvts, "sat_style");
    for (int i = 0; i < 5; ++i)
        styles[(size_t) i].setToggleState (i == st, juce::dontSendNotification);

    // dim the controls while the module is off
    const float a = power.getToggleState() ? 1.0f : 0.45f;
    for (auto& k : knobs) k->setAlpha (a);
    for (auto& b : styles) b.setAlpha (a);
    for (auto* b : { &punish, &thump, &steep, &autoBtn }) b->setAlpha (a);
    target.setAlpha (a);
}

void SatPanel::paint (juce::Graphics& g)
{
    Skin::nine (g, Skin::Id::PanelFrame, getLocalBounds().toFloat(), 72, 14.0f);
}

void SatPanel::resized()
{
    auto r = getLocalBounds().reduced (12, 10);

    auto row1 = r.removeFromTop (24);
    power.setBounds (row1.removeFromLeft (44));
    row1.removeFromLeft (6);
    for (auto& b : styles) { b.setBounds (row1.removeFromLeft (22)); row1.removeFromLeft (1); }
    row1.removeFromLeft (5);
    const int w = (int) ((float) row1.getWidth() / 4.45f);
    punish.setBounds (row1.removeFromLeft ((int) ((float) w * 1.45f)).reduced (1, 0));
    thump.setBounds (row1.removeFromLeft (w).reduced (1, 0));
    steep.setBounds (row1.removeFromLeft (w).reduced (1, 0));
    autoBtn.setBounds (row1.reduced (1, 0));

    r.removeFromTop (3);
    auto tgt = r.removeFromRight (58);
    target.setBounds (tgt.withSizeKeepingCentre (58, 22));
    const int kw = r.getWidth() / (int) knobs.size();
    for (auto& k : knobs) k->setBounds (r.removeFromLeft (kw));
}

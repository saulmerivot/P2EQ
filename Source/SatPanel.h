#pragma once

#include "PluginProcessor.h"
#include "RustTheme.h"

class Knob;

// SATURATION module controls, shown in the header (NORMAL view).
class SatPanel : public juce::Component, private juce::Timer
{
public:
    explicit SatPanel (PirateEQ2Processor&);
    ~SatPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    PirateEQ2Processor& proc;
    RustLookAndFeel laf;
    juce::TextButton power { "SAT" }, punish { "DSTRCT" }, thump { "THUMP" }, steep { "STEEP" }, autoBtn { "AUTO" };
    std::array<juce::TextButton, 5> styles;
    juce::ComboBox target;
    std::vector<std::unique_ptr<Knob>> knobs;   // drive, tone, low, high, mix, out
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAtts;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> targetAtt;
};

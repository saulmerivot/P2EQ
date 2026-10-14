#pragma once

#include "PluginProcessor.h"

class PlotComponent;
class SatPanel;

// Bakelite knobs, chrome fader caps, cream-on-brown faceplate.
class VintageLookAndFeel : public juce::LookAndFeel_V4
{
public:
    VintageLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
};

// Moving-coil VU meter: ~300 ms integration, 0 VU = -18 dBFS RMS.
class VuMeter : public juce::Component, private juce::Timer
{
public:
    VuMeter (PirateEQ2Processor&, int stream, juce::Colour needle, juce::String caption);
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    PirateEQ2Processor& proc;
    int stream;
    juce::Colour needleColour;
    juce::String caption;
    float level = 0.0f;   // smoothed linear RMS
    float peakLamp = 0.0f;
};

// Knob that drives several parameters at once, preserving their ratio (e.g. Q or HP freq of both streams).
class LinkedKnob : public juce::Slider, private juce::Timer
{
public:
    LinkedKnob (PirateEQ2Processor&, juce::StringArray ids, juce::String suffix);

private:
    void timerCallback() override;
    PirateEQ2Processor& proc;
    juce::StringArray ids;
    juce::Array<float> start;
    bool dragging = false;
};

class OldSchoolView : public juce::Component
{
public:
    OldSchoolView (PirateEQ2Processor&, std::unique_ptr<PlotComponent> miniPlot);
    ~OldSchoolView() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Strip;
    PirateEQ2Processor& proc;
    VintageLookAndFeel laf;
    std::unique_ptr<PlotComponent> plot;
    std::unique_ptr<SatPanel> satPanel;
    VuMeter vuT, vuN;
    std::array<std::unique_ptr<Strip>, 8> strips;

    // split + output sections
    juce::Slider sep, decay, smooth, outT, outN;
    juce::ComboBox source;
    juce::ToggleButton bypass { "BYPASS" }, eqBypass { "EQ OUT" };
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sliderAtts;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> sourceAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAtt, eqBypassAtt;

    juce::Rectangle<int> leftCol, face, stripArea, splitArea, outArea, vuArea;
};

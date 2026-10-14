#pragma once

#include "PluginProcessor.h"

// Numeric readout bound to one or more parameters: drag vertically, wheel, double-click to type.
class ParamLabel : public juce::Label
{
public:
    enum Kind { Freq, Gain, Q, Pan, Percent, Ms };

    ParamLabel (PirateEQ2Processor&, juce::StringArray paramIds, Kind, juce::Colour);

    void refresh();
    float plainValue() const;
    void setPlain (float v);
    std::function<void()> onJoin;   // right-click

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    static juce::String format (Kind, float);

private:
    float parse (const juce::String&) const;
    void setNormalised (float n);

    PirateEQ2Processor& proc;
    juce::StringArray ids;
    Kind kind;
    float startNorm = 0.0f;
    bool gesture = false;
};

// A value shown once (white) while Transient == Tonal, and as two values (pink/gold) once they differ.
struct PairRow
{
    std::unique_ptr<ParamLabel> both, t, n;
    ParamLabel::Kind kind = ParamLabel::Gain;
    float tol = 0.05f;

    void make (PirateEQ2Processor&, juce::Component& parent, const juce::String& idT, const juce::String& idN,
               ParamLabel::Kind, float tolerance);
    void refresh (bool rowVisible);
    void setBounds (juce::Rectangle<int>);
};

class BandStrip : public juce::Component, private juce::Timer
{
public:
    explicit BandStrip (PirateEQ2Processor&);
    ~BandStrip() override;

    void setSelected (int band);
    std::function<void (int)> onSelect;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    struct Column;
    void timerCallback() override;
    void updateVisibility();

    PirateEQ2Processor& proc;
    std::array<std::unique_ptr<Column>, 8> cols;

    juce::TextButton eqBtn { "EQ" }, panBtn { "PAN" };
    juce::ComboBox source, solo;
    juce::ToggleButton lock;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> sourceA, soloA;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> lockA;
    std::unique_ptr<ParamLabel> sep, decay, smooth;

    juce::Rectangle<int> splitBar, grid;
    int colWidth = 80, leftW = 46;
    int selected = 1;
    bool panView = false;
    int lastSig = -1;
};

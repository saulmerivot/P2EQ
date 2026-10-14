#pragma once

#include "PluginProcessor.h"
#include "BandStrip.h"
#include "OldSchool.h"
#include "RustTheme.h"
#include "SatPanel.h"

namespace Colours
{
    inline const juce::Colour bg      { 0xff120b08 };
    inline const juce::Colour panel   { 0xff1e1612 };
    inline const juce::Colour line    { 0x2eff9a4a };   // grid: faint orange
    inline const juce::Colour text    { 0xffe9dcc4 };   // bone
    inline const juce::Colour dim     { 0xffa8977c };
    inline const juce::Colour trans   { 0xffe8366f };   // Transient (magenta)
    inline const juce::Colour tonal   { 0xfff5a524 };   // Tonal (amber)
    inline const juce::Colour accent  { 0xffff7a1a };   // rust orange
}

// Labelled rotary knob bound to a parameter.
class Knob : public juce::Component
{
public:
    Knob() { addAndMakeVisible (slider); addAndMakeVisible (label); }

    void attach (juce::AudioProcessorValueTreeState& s, const juce::String& id,
                 const juce::String& name, juce::Colour c, const juce::String& suffix = {})
    {
        att.reset();
        label.setText (name, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setFont (juce::FontOptions (11.0f));
        label.setColour (juce::Label::textColourId, Colours::dim);
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 62, 15);
        slider.setColour (juce::Slider::rotarySliderFillColourId, c);
        slider.setColour (juce::Slider::thumbColourId, c);
        slider.setTextValueSuffix (suffix);
        att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (s, id, slider);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        label.setBounds (r.removeFromTop (14));
        slider.setBounds (r);
    }

    juce::Slider slider;
    juce::Label label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
};

class Combo : public juce::Component
{
public:
    Combo() { addAndMakeVisible (box); addAndMakeVisible (label); }

    void attach (juce::AudioProcessorValueTreeState& s, const juce::String& id,
                 const juce::String& name, const juce::StringArray& items)
    {
        att.reset();
        box.clear (juce::dontSendNotification);
        box.addItemList (items, 1);
        label.setText (name, juce::dontSendNotification);
        label.setFont (juce::FontOptions (11.0f));
        label.setColour (juce::Label::textColourId, Colours::dim);
        att = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (s, id, box);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        label.setBounds (r.removeFromTop (14));
        box.setBounds (r.reduced (0, 1));
    }

    juce::ComboBox box;
    juce::Label label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> att;
};

class Toggle : public juce::ToggleButton
{
public:
    void attach (juce::AudioProcessorValueTreeState& s, const juce::String& id, const juce::String& name)
    {
        att.reset();
        setButtonText (name);
        att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (s, id, *this);
    }
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> att;
};

class PlotComponent : public juce::Component, private juce::Timer
{
public:
    PlotComponent (PirateEQ2Processor&, std::function<void (int)> onSelect);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    int selectedBand = 1;
    bool compact = false;     // mini graph in OLD IS KOOL mode: no hint line

private:
    // stream: -1 both, 0 Transient, 1 Tonal. tab = small pull-tab attached to a "both" handle.
    struct Handle { int band; int stream; juce::Point<float> pos; bool tab = false; };

    void timerCallback() override;
    juce::Rectangle<float> plotArea() const;
    float freqToX (double f) const;
    double xToFreq (float x) const;
    float dbToY (double db) const;
    double yToDb (float y) const;
    std::vector<Handle> handles() const;
    int hitTest (juce::Point<float>) const;
    void setParam (const juce::String& id, float plain);
    float getParam (const juce::String& id) const { return Params::raw (proc.apvts, id); }
    bool isGainBand (int b) const;
    void applyDrag (const juce::MouseEvent&);

    PirateEQ2Processor& proc;
    std::function<void (int)> onSelect;
    juce::dsp::FFT fft { 11 };
    juce::dsp::WindowingFunction<float> window { 2048, juce::dsp::WindowingFunction<float>::hann };
    std::array<float, 1024> spec[2] {};
    std::vector<Handle> cached;
    int dragIndex = -1;
    Handle dragHandle {};
};

class MasterPanel : public juce::Component, private juce::Timer
{
public:
    explicit MasterPanel (PirateEQ2Processor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override { repaint(); }
    PirateEQ2Processor& proc;
    Knob gT, gN, pT, pN, sT, sN;
    Combo mode;
    Toggle phase, bypass, eqBypass;
    juce::Rectangle<int> meterArea;
    float peakHold[2] {}, rmsShow[2] {};
};

class PresetBar : public juce::Component, private juce::Timer
{
public:
    explicit PresetBar (PirateEQ2Processor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void showPresetMenu();
    void savePreset();
    void loadFile (const juce::File&);
    void switchSlot (int slot);
    void resetAll();
    juce::File presetDir() const;

    PirateEQ2Processor& proc;
    juce::TextButton presets { "Presets" }, undo { "Undo" }, redo { "Redo" }, a { "A" }, b { "B" },
                     copy { "Copy" }, reset { "Reset" }, loop { "Loop" },
                     normalBtn { "NORMAL" }, oldBtn { "OLD IS KOOL" };
public:
    std::function<void (int)> onMode;
    void setMode (int m);
private:
    juce::ValueTree slots[2];
    int activeSlot = 0;
    std::unique_ptr<juce::FileChooser> chooser;
};

class PirateEQ2Editor : public juce::AudioProcessorEditor
{
public:
    explicit PirateEQ2Editor (PirateEQ2Processor&);
    ~PirateEQ2Editor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void setMode (int m);

    PirateEQ2Processor& proc;
    RustLookAndFeel laf;
    PresetBar presetBar;
    BandStrip strip;
    MasterPanel masterPanel;
    PlotComponent plot;
    SatPanel satPanel;
    OldSchoolView oldView;
    juce::HyperlinkButton credit { "By St. Punk!", juce::URL ("https://stpunk.art") };
    int mode = 0;    // 0 = NORMAL, 1 = OLD IS KOOL
    static constexpr int headerH = 74;
};

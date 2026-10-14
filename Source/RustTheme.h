#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Post-apocalyptic rusted-metal theme for the NORMAL view. Everything is procedural (no image files).
namespace Rust
{
    inline const juce::Colour orange   { 0xffff7a1a };
    inline const juce::Colour amber    { 0xfff5a524 };
    inline const juce::Colour ember    { 0xffd8401f };
    inline const juce::Colour steel    { 0xff2a231e };
    inline const juce::Colour ink      { 0xff120b08 };
    inline const juce::Colour bone     { 0xffe9dcc4 };
    inline const juce::Colour boneDim  { 0xffa8977c };

    juce::FontOptions stencil (float size);                 // condensed heavy display face
    const juce::Image& texture();                           // 1024x1024 tiling rust texture (cached)

    void plate (juce::Graphics&, juce::Rectangle<float>, float corner = 6.0f, float darken = 0.0f,
                bool rivets = true, int seed = 0);
    void recess (juce::Graphics&, juce::Rectangle<float>, float corner = 4.0f);   // dark sunken window
    void rivet (juce::Graphics&, juce::Point<float>, float radius = 4.0f);
    void spikes (juce::Graphics&, juce::Rectangle<float> band, int seed = 0);       // spikes pointing up
    void title (juce::Graphics&, juce::Rectangle<float>, const juce::String&, float size);
}

class RustLookAndFeel : public juce::LookAndFeel_V4
{
public:
    RustLookAndFeel();

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;

    void drawComboBox (juce::Graphics&, int w, int h, bool down, int, int, int, int, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
};

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Image-based skin (art in assets/skin, embedded as BinaryData "SkinData").
namespace Skin
{
    enum class Id
    {
        Background, Header, PanelFrame, GraphFrame, Recess,
        ButtonOff, ButtonHover, ButtonPressed, ButtonOn,
        Column, ColumnSelected, Combo, ComboArrow,
        BadgeOff, BadgeOn, CheckOff, CheckOn, Knob, MeterOff, MeterOn,
        OldFaceplate, OldFaderSlot, OldFaderCap, OldVuFace, OldLampOff, OldLampOn, OldSection, OldDivider
    };

    // "Metal Mania" (OFL) embedded display face for the P2EQ wordmarks
    juce::Font metal (float height);

    const juce::Image& image (Id);

    // 9-slice: srcBorder in source pixels, destBorder in logical pixels. Result cached per size/scale.
    void nine (juce::Graphics&, Id, juce::Rectangle<float> dest, int srcBorder, float destBorder);

    // Whole image scaled into dest (cached per size/scale).
    void fit (juce::Graphics&, Id, juce::Rectangle<float> dest);

    // badge_on tinted with a colour (cached per colour).
    const juce::Image& tintedBadge (juce::Colour);

    // Header art scaled to a width (cached).
    void header (juce::Graphics&, float width, float y = 0.0f);
    float headerHeightFor (float width);

    // Meter: off image, then the lit image clipped to `level` (0..1) from the bottom.
    void meter (juce::Graphics&, juce::Rectangle<float> dest, float level, float peak);

    // Knob image rotated by `angle` (radians) around the centre of dest.
    void knob (juce::Graphics&, juce::Rectangle<float> dest, float angle);
}

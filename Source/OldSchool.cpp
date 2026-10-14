#include "OldSchool.h"
#include "PluginEditor.h"
#include "Skin.h"
#include "SatPanel.h"

using Attach = juce::AudioProcessorValueTreeState;

namespace Vintage
{
    const juce::Colour face     { 0xff2e2822 };   // dark brown steel
    const juce::Colour faceHi   { 0xff3a332b };
    const juce::Colour cream    { 0xffefe4cc };
    const juce::Colour creamDim { 0xffb9ad93 };
    const juce::Colour brass    { 0xffc9a25a };
    const juce::Colour wood     { 0xff5b3a22 };
    const juce::Colour woodDark { 0xff3d2616 };
    const juce::Colour lampOn   { 0xffffb347 };

    juce::FontOptions serif (float size, bool bold = true)
    {
        return juce::FontOptions ("Georgia", size, bold ? juce::Font::bold : juce::Font::plain);
    }
}

// ============================================================ LookAndFeel
VintageLookAndFeel::VintageLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, Vintage::cream);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0xff1c1814));
    setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff1c1814));
    setColour (juce::ComboBox::textColourId, Vintage::cream);
    setColour (juce::ComboBox::outlineColourId, Vintage::brass.withAlpha (0.5f));
    setColour (juce::ComboBox::arrowColourId, Vintage::brass);
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff1c1814));
    setColour (juce::PopupMenu::textColourId, Vintage::cream);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Vintage::brass.darker (0.4f));
    setColour (juce::ToggleButton::textColourId, Vintage::cream);
    setColour (juce::ToggleButton::tickColourId, Vintage::lampOn);
    setColour (juce::ToggleButton::tickDisabledColourId, Vintage::creamDim);
    setColour (juce::Label::textColourId, Vintage::cream);
}

juce::Font VintageLookAndFeel::getLabelFont (juce::Label&)
{
    return juce::Font (Vintage::serif (11.0f, false));
}

void VintageLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool, bool)
{
    const auto bounds = b.getLocalBounds().toFloat();
    const float sz = juce::jmin (24.0f, bounds.getHeight());
    const bool hasText = b.getButtonText().isNotEmpty();
    const auto lamp = hasText ? juce::Rectangle<float> (0.0f, (bounds.getHeight() - sz) * 0.5f, sz, sz)
                              : juce::Rectangle<float> (sz, sz).withCentre (bounds.getCentre());
    g.setOpacity (1.0f);
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (Skin::image (b.getToggleState() ? Skin::Id::OldLampOn : Skin::Id::OldLampOff), lamp,
                 juce::RectanglePlacement::stretchToFit);
    if (hasText)
    {
        g.setColour (Vintage::cream);
        g.setFont (Vintage::serif (12.0f));
        g.drawText (b.getButtonText(), bounds.withTrimmedLeft (sz + 6.0f), juce::Justification::centredLeft);
    }
}

juce::Font VintageLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return juce::Font (Vintage::serif (10.0f));
}

void VintageLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&)
{
    const auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (juce::Colour (0xff1c1814));
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (Vintage::brass.withAlpha (0.5f));
    g.drawRoundedRectangle (r, 3.0f, 1.0f);
    juce::Path tri;   // small corner marker instead of a full arrow
    tri.addTriangle ((float) w - 8, (float) h - 3, (float) w - 3, (float) h - 3, (float) w - 3, (float) h - 8);
    g.setColour (Vintage::brass);
    g.fillPath (tri);
}

void VintageLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (2, 1, box.getWidth() - 4, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
    label.setJustificationType (juce::Justification::centred);
}

void VintageLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                           float startAngle, float endAngle, juce::Slider& s)
{
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat();
    const float r = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f - 6.0f;
    const auto c = bounds.getCentre();

    // engraved scale
    g.setColour (Vintage::creamDim);
    for (int i = 0; i <= 10; ++i)
    {
        const float a = startAngle + (endAngle - startAngle) * (float) i / 10.0f;
        const float r1 = r + 2.0f, r2 = r + (i % 5 == 0 ? 6.0f : 4.0f);
        g.drawLine (c.x + r1 * std::sin (a), c.y - r1 * std::cos (a), c.x + r2 * std::sin (a), c.y - r2 * std::cos (a),
                    i % 5 == 0 ? 1.4f : 1.0f);
    }

    // rusted gear knob from the skin (its own pointer marks the value)
    juce::ignoreUnused (s);
    const float a = startAngle + pos * (endAngle - startAngle);
    Skin::knob (g, juce::Rectangle<float> (c.x - r, c.y - r, 2 * r, 2 * r), a);
}

void VintageLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                           juce::Slider::SliderStyle style, juce::Slider& s)
{
    if (style != juce::Slider::LinearVertical)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0, 0, style, s);
        return;
    }

    const float cx = (float) x + (float) w * 0.5f;
    const float slotW = juce::jmin (14.0f, (float) w - 8.0f);
    Skin::nine (g, Skin::Id::OldFaderSlot, { cx - slotW * 0.5f, (float) y - 2.0f, slotW, (float) h + 4.0f }, 60, 6.0f);

    const auto& cap = Skin::image (Skin::Id::OldFaderCap);
    const float capW = juce::jmin (44.0f, (float) w);
    const float capH = capW * (float) cap.getHeight() / (float) cap.getWidth();
    const auto capR = juce::Rectangle<float> (cx - capW * 0.5f, pos - capH * 0.5f, capW, capH);
    g.setOpacity (1.0f);
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (cap, capR, juce::RectanglePlacement::stretchToFit);
    g.setColour (s.findColour (juce::Slider::thumbColourId).withAlpha (0.9f));
    g.fillRect (capR.getX() + capW * 0.12f, capR.getCentreY() - 1.0f, capW * 0.76f, 2.0f);
}

// ============================================================ VuMeter
VuMeter::VuMeter (PirateEQ2Processor& p, int s, juce::Colour c, juce::String cap)
    : proc (p), stream (s), needleColour (c), caption (std::move (cap))
{
    startTimerHz (30);
}

void VuMeter::timerCallback()
{
    const float target = proc.rms[stream].load();
    const float alpha = 1.0f - std::exp (-1.0f / (30.0f * 0.3f));   // ~300 ms VU integration
    level += (target - level) * alpha;
    peakLamp = proc.peak[stream].load() > 0.89f ? 1.0f : peakLamp * 0.9f;   // ~ -1 dBFS
    repaint();
}

void VuMeter::paint (juce::Graphics& g)
{
    const auto& face = Skin::image (Skin::Id::OldVuFace);
    auto b = getLocalBounds().toFloat();
    const float aspect = (float) face.getWidth() / (float) face.getHeight();
    auto r = b.getAspectRatio() > aspect ? b.withSizeKeepingCentre (b.getHeight() * aspect, b.getHeight())
                                         : b.withSizeKeepingCentre (b.getWidth(), b.getWidth() / aspect);
    Skin::fit (g, Skin::Id::OldVuFace, r);

    const float k = r.getHeight() / (float) face.getHeight();
    const juce::Point<float> pivot (r.getX() + 561.0f * k, r.getY() + 933.0f * k);
    const float radius = 693.0f * k;
    const auto deg = juce::MathConstants<float>::pi / 180.0f;

    auto angleFor = [&] (float vu)
    {
        const float v = std::pow (10.0f, vu / 20.0f);
        if (vu <= 0.0f)
            return (-39.7f + 62.5f * juce::jlimit (0.0f, 1.0f, (v - 0.1f) / 0.9f)) * deg;
        return (22.8f + 17.1f * juce::jlimit (0.0f, 1.0f, (v - 1.0f) / (1.4125f - 1.0f))) * deg;
    };
    auto pt = [&] (float a, float rr) { return juce::Point<float> (pivot.x + rr * std::sin (a), pivot.y - rr * std::cos (a)); };

    // caption on the face
    g.setColour (juce::Colour (0xff2a2520));
    g.setFont (Vintage::serif (r.getHeight() * 0.13f));
    g.drawText ("VU", r.withTrimmedTop (r.getHeight() * 0.47f).withHeight (r.getHeight() * 0.16f), juce::Justification::centred);
    g.setFont (Vintage::serif (r.getHeight() * 0.075f, false));
    g.drawText (caption, r.withTrimmedTop (r.getHeight() * 0.62f).withHeight (r.getHeight() * 0.1f), juce::Justification::centred);

    // peak lamp in the glass corner
    if (peakLamp > 0.02f)
    {
        g.setOpacity (peakLamp);
        g.drawImage (Skin::image (Skin::Id::OldLampOn),
                     juce::Rectangle<float> (r.getRight() - r.getWidth() * 0.16f, r.getY() + r.getHeight() * 0.13f,
                                             r.getHeight() * 0.13f, r.getHeight() * 0.13f),
                     juce::RectanglePlacement::stretchToFit);
        g.setOpacity (1.0f);
    }

    // needle, clipped to the glass
    const float vuNow = juce::Decibels::gainToDecibels (level, -60.0f) + 18.0f;
    const float a = angleFor (vuNow);
    juce::Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (juce::Rectangle<float> (r.getX() + 70.0f * k, r.getY() + 75.0f * k, 985.0f * k, 625.0f * k).toNearestInt());
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.drawLine ({ pt (a, radius * 0.45f).translated (1.5f, 2.0f), pt (a, radius * 1.06f).translated (1.5f, 2.0f) }, 1.8f);
    g.setColour (needleColour.darker (0.5f));
    g.drawLine ({ pt (a, radius * 0.45f), pt (a, radius * 1.06f) }, 1.8f);
}

// ============================================================ LinkedKnob
LinkedKnob::LinkedKnob (PirateEQ2Processor& p, juce::StringArray paramIds, juce::String suffix)
    : proc (p), ids (std::move (paramIds))
{
    setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    setTextBoxStyle (juce::Slider::TextBoxBelow, false, 58, 15);
    setRange (0.0, 1.0);

    textFromValueFunction = [this, suffix] (double v)
    {
        if (auto* prm = proc.apvts.getParameter (ids[0]))
            return prm->getText ((float) v, 8) + suffix;
        return juce::String();
    };

    onDragStart = [this]
    {
        dragging = true;
        start.clearQuick();
        proc.undoManager.beginNewTransaction();
        for (auto& id : ids)
            if (auto* prm = proc.apvts.getParameter (id))
            {
                prm->beginChangeGesture();
                start.add (Params::raw (proc.apvts, id));
            }
    };
    onDragEnd = [this]
    {
        for (auto& id : ids)
            if (auto* prm = proc.apvts.getParameter (id)) prm->endChangeGesture();
        dragging = false;
    };
    onValueChange = [this]
    {
        if (! dragging || start.isEmpty()) return;
        auto* first = proc.apvts.getParameter (ids[0]);
        if (first == nullptr) return;
        const float newFirst = first->convertFrom0to1 ((float) getValue());
        const float ratio = newFirst / juce::jmax (1.0e-6f, start[0]);
        for (int i = 0; i < ids.size() && i < start.size(); ++i)
            if (auto* prm = proc.apvts.getParameter (ids[i]))
                prm->setValueNotifyingHost (prm->convertTo0to1 (i == 0 ? newFirst : start[i] * ratio));
    };

    startTimerHz (15);
    if (auto* prm = proc.apvts.getParameter (ids[0]))
        setValue (prm->getValue(), juce::dontSendNotification);
    updateText();
}

void LinkedKnob::timerCallback()
{
    if (dragging) return;
    if (auto* prm = proc.apvts.getParameter (ids[0]))
        if (std::abs (getValue() - prm->getValue()) > 1.0e-5)
            setValue (prm->getValue(), juce::dontSendNotification);
}

// ============================================================ OldSchoolView
struct OldSchoolView::Strip
{
    int band = 0;
    juce::ToggleButton lamp;
    juce::ComboBox type;
    std::unique_ptr<LinkedKnob> freq, q;
    juce::Slider gainT, gainN;
    std::unique_ptr<Attach::ButtonAttachment> lampA;
    std::unique_ptr<Attach::ComboBoxAttachment> typeA;
    std::unique_ptr<Attach::SliderAttachment> gainTA, gainNA;
    juce::Rectangle<int> bounds;
};

OldSchoolView::OldSchoolView (PirateEQ2Processor& p, std::unique_ptr<PlotComponent> miniPlot)
    : proc (p), plot (std::move (miniPlot)),
      vuT (p, 0, Colours::trans, "TRANSIENT"), vuN (p, 1, Colours::tonal, "TONAL")
{
    setLookAndFeel (&laf);
    plot->compact = true;
    addAndMakeVisible (*plot);
    satPanel = std::make_unique<SatPanel> (p);
    addAndMakeVisible (*satPanel);
    addAndMakeVisible (vuT);
    addAndMakeVisible (vuN);
    auto& s = proc.apvts;

    auto setupFader = [this] (juce::Slider& f, juce::Colour c)
    {
        f.setSliderStyle (juce::Slider::LinearVertical);
        f.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 44, 15);
        f.setColour (juce::Slider::thumbColourId, c);
        addAndMakeVisible (f);
    };

    for (int b = 0; b < 8; ++b)
    {
        auto st = std::make_unique<Strip>();
        st->band = b;
        const auto id = [b] (const char* n) { return Params::id (b, n); };
        const bool edge = b == 0 || b == 7;

        st->lampA = std::make_unique<Attach::ButtonAttachment> (s, id ("active"), st->lamp);
        addAndMakeVisible (st->lamp);

        if (edge)
        {
            st->freq = std::make_unique<LinkedKnob> (p, juce::StringArray { id ("freqT"), id ("freqN") }, " Hz");
        }
        else
        {
            st->freq = std::make_unique<LinkedKnob> (p, juce::StringArray { id ("freq") }, " Hz");
            st->q    = std::make_unique<LinkedKnob> (p, juce::StringArray { id ("qT"), id ("qN") }, "");
            st->q->setColour (juce::Slider::thumbColourId, Vintage::cream);
            addAndMakeVisible (*st->q);

            st->type.addItemList ({ "LOW", "PEAK", "NOTCH", "HIGH", "TILT", "BAND" }, 1);
            st->typeA = std::make_unique<Attach::ComboBoxAttachment> (s, id ("type"), st->type);
            addAndMakeVisible (st->type);

            setupFader (st->gainT, Colours::trans);
            setupFader (st->gainN, Colours::tonal);
            st->gainTA = std::make_unique<Attach::SliderAttachment> (s, id ("gainT"), st->gainT);
            st->gainNA = std::make_unique<Attach::SliderAttachment> (s, id ("gainN"), st->gainN);
        }
        st->freq->setColour (juce::Slider::thumbColourId, Vintage::brass);
        addAndMakeVisible (*st->freq);
        strips[(size_t) b] = std::move (st);
    }

    // split section knobs
    for (auto* k : { &sep, &decay, &smooth })
    {
        k->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 58, 15);
        k->setColour (juce::Slider::thumbColourId, Vintage::lampOn);
        addAndMakeVisible (k);
    }
    sliderAtts.push_back (std::make_unique<Attach::SliderAttachment> (s, "split_sep", sep));
    sliderAtts.push_back (std::make_unique<Attach::SliderAttachment> (s, "split_decay", decay));
    sliderAtts.push_back (std::make_unique<Attach::SliderAttachment> (s, "split_smooth", smooth));
    sep.setTextValueSuffix (" %");
    decay.setTextValueSuffix (" %");
    smooth.setTextValueSuffix (" ms");

    source.addItemList (Params::sourceNames, 1);
    sourceAtt = std::make_unique<Attach::ComboBoxAttachment> (s, "split_source", source);
    addAndMakeVisible (source);

    // output faders
    setupFader (outT, Colours::trans);
    setupFader (outN, Colours::tonal);
    sliderAtts.push_back (std::make_unique<Attach::SliderAttachment> (s, "out_gainT", outT));
    sliderAtts.push_back (std::make_unique<Attach::SliderAttachment> (s, "out_gainN", outN));

    bypassAtt = std::make_unique<Attach::ButtonAttachment> (s, "bypass", bypass);
    eqBypassAtt = std::make_unique<Attach::ButtonAttachment> (s, "eq_bypass", eqBypass);
    addAndMakeVisible (bypass);
    addAndMakeVisible (eqBypass);
}

OldSchoolView::~OldSchoolView()
{
    setLookAndFeel (nullptr);
}

void OldSchoolView::resized()
{
    auto r = getLocalBounds();
    leftCol = r.removeFromLeft (330);
    r.removeFromLeft (8);
    face = r;

    // left column: mini graph, SAT module, two VU meters
    auto lc = leftCol;
    plot->setBounds (lc.removeFromTop ((int) (lc.getHeight() * 0.42f)));
    lc.removeFromTop (8);
    satPanel->setBounds (lc.removeFromTop (128));
    lc.removeFromTop (8);
    vuArea = lc;
    auto vuCol = lc.reduced (18, 16);
    const int vuH = (vuCol.getHeight() - 8) / 2;
    const int vuW = juce::jmin (vuCol.getWidth(), (int) ((float) vuH * 1.43f));
    vuT.setBounds (vuCol.removeFromTop (vuH).withSizeKeepingCentre (vuW, vuH));
    vuCol.removeFromTop (8);
    vuN.setBounds (vuCol.withSizeKeepingCentre (vuW, vuH));

    // faceplate: wood cheeks, strips, split + output sections
    auto f = face.reduced (30, 22);
    f.removeFromTop (40);                   // engraved header
    outArea = f.removeFromRight (110);
    f.removeFromRight (6);
    splitArea = f.removeFromRight (120);
    f.removeFromRight (6);
    stripArea = f;

    const int edgeW = 62;
    const int bandW = (stripArea.getWidth() - 2 * edgeW) / 6;
    int x = stripArea.getX();
    for (auto& st : strips)
    {
        const bool edge = st->band == 0 || st->band == 7;
        const int w = edge ? edgeW : bandW;
        st->bounds = { x, stripArea.getY(), w, stripArea.getHeight() };
        x += w;

        auto c = st->bounds.reduced (4, 0);
        c.removeFromTop (22);                                   // engraved band name
        st->lamp.setBounds (c.removeFromTop (22).withSizeKeepingCentre (22, 22));
        c.removeFromTop (4);
        if (! edge) st->type.setBounds (c.removeFromTop (22));
        else        c.removeFromTop (22);
        c.removeFromTop (6);
        st->freq->setBounds (c.removeFromTop (78));
        if (st->q) st->q->setBounds (c.removeFromTop (70));
        else       c.removeFromTop (70);
        c.removeFromTop (8);
        if (! edge)
        {
            auto fr = c.removeFromTop (c.getHeight() - 6);
            st->gainT.setBounds (fr.removeFromLeft (fr.getWidth() / 2));
            st->gainN.setBounds (fr);
        }
    }

    auto sp = splitArea.reduced (4, 0);
    sp.removeFromTop (22);
    source.setBounds (sp.removeFromTop (24));
    sp.removeFromTop (8);
    for (auto* k : { &sep, &decay, &smooth })
    {
        sp.removeFromTop (14);
        k->setBounds (sp.removeFromTop (86));
    }

    auto op = outArea.reduced (4, 0);
    op.removeFromTop (36);
    eqBypass.setBounds (op.removeFromBottom (24));
    bypass.setBounds (op.removeFromBottom (24));
    op.removeFromBottom (8);
    outT.setBounds (op.removeFromLeft (op.getWidth() / 2));
    outN.setBounds (op);
}

void OldSchoolView::paint (juce::Graphics& g)
{
    Skin::nine (g, Skin::Id::OldSection, vuArea.toFloat(), 170, 18.0f);
    Skin::nine (g, Skin::Id::OldFaceplate, face.toFloat(), 110, 26.0f);
    const auto plate = face.toFloat().reduced (26.0f, 0.0f);

    // header: P2EQ wordmark in Metal Mania
    g.setColour (Vintage::lampOn);
    g.setFont (Skin::metal (30.0f));
    g.drawText ("P2EQ", plate.withHeight (46).reduced (10, 0).translated (0, 6), juce::Justification::centredLeft);
    g.setColour (Vintage::brass);
    g.setFont (Vintage::serif (11.0f, false));
    g.drawText ("OLD IS KOOL  -  TRANSIENT / TONAL EQUALIZER", plate.withHeight (46).reduced (10, 0).translated (0, 6),
                juce::Justification::centredRight);

    // strip separators + engraved labels
    g.setFont (Vintage::serif (12.0f));
    for (auto& st : strips)
    {
        const auto b = st->bounds;
        g.setColour (Vintage::cream);
        const juce::String name = st->band == 0 ? "HP" : st->band == 7 ? "LP" : juce::String (st->band);
        g.drawText (name, b.withHeight (20), juce::Justification::centred);
        if (st->band < 7)
            Skin::nine (g, Skin::Id::OldDivider, juce::Rectangle<float> ((float) b.getRight() - 4.0f, (float) b.getY(),
                                                                       8.0f, (float) b.getHeight()), 29, 4.0f);

        g.setColour (Vintage::creamDim);
        g.setFont (Vintage::serif (9.0f, false));
        g.drawText ("FREQ", b.getX(), st->freq->getY() - 2, b.getWidth(), 10, juce::Justification::centred);
        if (st->q)
        {
            g.drawText ("Q", b.getX(), st->q->getY() - 2, b.getWidth(), 10, juce::Justification::centred);
            g.setColour (Colours::trans);
            g.drawText ("T", st->gainT.getBounds().withHeight (10).translated (0, -12), juce::Justification::centred);
            g.setColour (Colours::tonal);
            g.drawText ("N", st->gainN.getBounds().withHeight (10).translated (0, -12), juce::Justification::centred);
        }
        g.setFont (Vintage::serif (12.0f));
    }

    auto section = [&] (juce::Rectangle<int> r, const juce::String& t)
    {
        Skin::nine (g, Skin::Id::OldSection, r.toFloat(), 170, 14.0f);
        g.setColour (Vintage::cream);
        g.setFont (Vintage::serif (12.0f));
        g.drawText (t, r.withHeight (20), juce::Justification::centred);
    };
    section (splitArea, "SPLIT");
    section (outArea, "OUTPUT");

    g.setColour (Vintage::creamDim);
    g.setFont (Vintage::serif (9.0f, false));
    for (auto [k, t] : { std::pair<juce::Slider*, const char*> { &sep, "SEPARATION" }, { &decay, "DECAY" }, { &smooth, "SMOOTH" } })
        g.drawText (t, k->getBounds().withHeight (12).translated (0, -13), juce::Justification::centred);
    g.setColour (Colours::trans);
    g.drawText ("T", outT.getBounds().withHeight (10).translated (0, -12), juce::Justification::centred);
    g.setColour (Colours::tonal);
    g.drawText ("N", outN.getBounds().withHeight (10).translated (0, -12), juce::Justification::centred);
}

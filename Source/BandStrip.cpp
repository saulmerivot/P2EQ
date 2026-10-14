#include "BandStrip.h"
#include "PluginEditor.h"
#include "RustTheme.h"
#include "Skin.h"

using Attach = juce::AudioProcessorValueTreeState;

// ============================================================ ParamLabel
ParamLabel::ParamLabel (PirateEQ2Processor& p, juce::StringArray paramIds, Kind k, juce::Colour c)
    : proc (p), ids (std::move (paramIds)), kind (k)
{
    setJustificationType (juce::Justification::centred);
    setFont (Rust::stencil (16.0f));
    setColour (juce::Label::textColourId, c);
    setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::outlineWhenEditingColourId, Colours::accent);
    setEditable (false, true, false);
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);

    onTextChange = [this] { setPlain (parse (getText())); };
}

float ParamLabel::plainValue() const
{
    return Params::raw (proc.apvts, ids[0]);
}

void ParamLabel::setPlain (float v)
{
    for (auto& id : ids)
        if (auto* p = proc.apvts.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (v));
            p->endChangeGesture();
        }
}

void ParamLabel::setNormalised (float n)
{
    for (auto& id : ids)
        if (auto* p = proc.apvts.getParameter (id))
            p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, n));
}

juce::String ParamLabel::format (Kind k, float v)
{
    switch (k)
    {
        case Freq:    return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + "k" : (v >= 100.0f ? juce::String ((int) std::round (v)) : juce::String (v, 1));
        case Gain:    return juce::String (v, 1);
        case Q:       return juce::String (v, 2);
        case Pan:     return juce::String ((int) std::round (v));
        case Percent: return juce::String (v, 0) + "%";
        case Ms:      return juce::String (v, 1) + " ms";
    }
    return {};
}

float ParamLabel::parse (const juce::String& s) const
{
    auto t = s.trim().toLowerCase();
    float mul = 1.0f;
    if (t.endsWithChar ('k')) { mul = 1000.0f; t = t.dropLastCharacters (1); }
    return t.retainCharacters ("-0123456789.").getFloatValue() * mul;
}

void ParamLabel::refresh()
{
    if (isBeingEdited() || ids.isEmpty()) return;
    const auto txt = format (kind, plainValue());
    if (getText() != txt) setText (txt, juce::dontSendNotification);
}

void ParamLabel::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isRightButtonDown())
    {
        if (onJoin) onJoin();
        return;
    }
    if (auto* p = proc.apvts.getParameter (ids[0])) startNorm = p->getValue();
    proc.undoManager.beginNewTransaction();
    for (auto& id : ids)
        if (auto* p = proc.apvts.getParameter (id)) p->beginChangeGesture();
    gesture = true;
}

void ParamLabel::mouseDrag (const juce::MouseEvent& e)
{
    if (! gesture) return;
    const float px = e.mods.isShiftDown() ? 1200.0f : 260.0f;
    setNormalised (startNorm - (float) e.getDistanceFromDragStartY() / px);
}

void ParamLabel::mouseUp (const juce::MouseEvent&)
{
    if (! gesture) return;
    for (auto& id : ids)
        if (auto* p = proc.apvts.getParameter (id)) p->endChangeGesture();
    gesture = false;
}

void ParamLabel::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (auto* p = proc.apvts.getParameter (ids[0]))
    {
        proc.undoManager.beginNewTransaction();
        setNormalised (p->getValue() + w.deltaY * (e.mods.isShiftDown() ? 0.01f : 0.04f));
    }
}

// ============================================================ PairRow
void PairRow::make (PirateEQ2Processor& p, juce::Component& parent, const juce::String& idT, const juce::String& idN,
                    ParamLabel::Kind k, float tolerance)
{
    kind = k;
    tol = tolerance;
    const bool same = idT == idN;

    both = std::make_unique<ParamLabel> (p, same ? juce::StringArray { idT } : juce::StringArray { idT, idN }, k, Colours::text);
    t    = std::make_unique<ParamLabel> (p, juce::StringArray { idT }, k, Colours::trans);
    n    = std::make_unique<ParamLabel> (p, juce::StringArray { idN }, k, Colours::tonal);

    t->onJoin = [this] { n->setPlain (t->plainValue()); };
    n->onJoin = [this] { t->setPlain (n->plainValue()); };

    parent.addChildComponent (both.get());
    parent.addChildComponent (t.get());
    parent.addChildComponent (n.get());
}

void PairRow::refresh (bool rowVisible)
{
    if (! both) return;
    const float vT = t->plainValue(), vN = n->plainValue();
    const bool equal = kind == ParamLabel::Freq ? std::abs (std::log (std::max (1.0f, vT) / std::max (1.0f, vN))) < 0.005f
                                                : std::abs (vT - vN) < tol;
    both->setVisible (rowVisible && equal);
    t->setVisible (rowVisible && ! equal);
    n->setVisible (rowVisible && ! equal);
    if (rowVisible) { both->refresh(); t->refresh(); n->refresh(); }
}

void PairRow::setBounds (juce::Rectangle<int> r)
{
    if (! both) return;
    both->setBounds (r);
    auto h = r;
    t->setBounds (h.removeFromLeft (r.getWidth() / 2));
    n->setBounds (h);
}

// ============================================================ BandStrip
namespace
{
    const juce::Colour bandColours[8] = {
        juce::Colour (0xffff7a59), juce::Colour (0xffff4f9a), juce::Colour (0xffffc83d), juce::Colour (0xff9be15d),
        juce::Colour (0xff3dd6f5), juce::Colour (0xff8b7bff), juce::Colour (0xffd16bff), juce::Colour (0xffb0b6c3)
    };
    const juce::StringArray shortTypes { "L.Shelf", "Peak", "Notch", "H.Shelf", "Tilt", "BPass" };
}

struct BandStrip::Column
{
    int band = 0;
    juce::TextButton badge, solo { "S" };
    juce::ComboBox type, slope, notch, mode;
    std::unique_ptr<Attach::ButtonAttachment> activeA, soloA;
    std::unique_ptr<Attach::ComboBoxAttachment> typeA, slopeA, notchA, modeA;
    PairRow freq, gain, q, pan;
};

BandStrip::BandStrip (PirateEQ2Processor& p) : proc (p)
{
    auto& s = proc.apvts;

    for (int b = 0; b < 8; ++b)
    {
        auto c = std::make_unique<Column>();
        c->band = b;
        const auto id = [b] (const char* n) { return Params::id (b, n); };
        const bool edge = b == 0 || b == 7;
        const auto col = bandColours[b];

        c->badge.setButtonText (juce::String (b + 1));
        c->badge.getProperties().set ("skin", "badge");
        c->badge.setClickingTogglesState (true);
        c->badge.setColour (juce::TextButton::buttonColourId, Colours::bg);
        c->badge.setColour (juce::TextButton::buttonOnColourId, col);
        c->badge.setColour (juce::TextButton::textColourOffId, col);
        c->badge.setColour (juce::TextButton::textColourOnId, Colours::bg);
        c->activeA = std::make_unique<Attach::ButtonAttachment> (s, id ("active"), c->badge);

        c->solo.setClickingTogglesState (true);
        c->solo.setColour (juce::TextButton::buttonColourId, Colours::bg);
        c->solo.setColour (juce::TextButton::buttonOnColourId, Colours::accent);
        c->solo.setColour (juce::TextButton::textColourOffId, Colours::dim);
        c->solo.setColour (juce::TextButton::textColourOnId, Colours::bg);
        c->soloA = std::make_unique<Attach::ButtonAttachment> (s, id ("solo"), c->solo);

        c->slope.addItemList (Params::slopeNames, 1);
        c->slopeA = std::make_unique<Attach::ComboBoxAttachment> (s, id ("slope"), c->slope);

        if (edge)
        {
            c->freq.make (p, *this, id ("freqT"), id ("freqN"), ParamLabel::Freq, 0.0f);
        }
        else
        {
            c->type.addItemList (shortTypes, 1);
            c->typeA = std::make_unique<Attach::ComboBoxAttachment> (s, id ("type"), c->type);
            c->notch.addItemList ({ "Transient", "Both", "Tonal" }, 1);
            c->notchA = std::make_unique<Attach::ComboBoxAttachment> (s, id ("notch"), c->notch);
            c->mode.addItemList ({ "L/R", "M/S" }, 1);
            c->modeA = std::make_unique<Attach::ComboBoxAttachment> (s, id ("panMode"), c->mode);

            c->freq.make (p, *this, id ("freq"), id ("freq"), ParamLabel::Freq, 0.0f);
            c->gain.make (p, *this, id ("gainT"), id ("gainN"), ParamLabel::Gain, 0.05f);
            c->q.make (p, *this, id ("qT"), id ("qN"), ParamLabel::Q, 0.02f);
            c->pan.make (p, *this, id ("panT"), id ("panN"), ParamLabel::Pan, 0.5f);
        }

        for (juce::Component* w : { (juce::Component*) &c->badge, (juce::Component*) &c->solo,
                                    (juce::Component*) &c->type, (juce::Component*) &c->slope,
                                    (juce::Component*) &c->notch, (juce::Component*) &c->mode })
            addChildComponent (w);
        c->badge.setVisible (true);
        c->solo.setVisible (true);
        c->slope.setVisible (true);
        cols[(size_t) b] = std::move (c);
    }

    // EQ / PAN view switch
    for (auto* b : { &eqBtn, &panBtn })
    {
        b->setRadioGroupId (4242);
        b->setClickingTogglesState (true);
        b->setColour (juce::TextButton::buttonColourId, Colours::bg);
        b->setColour (juce::TextButton::buttonOnColourId, Colours::accent.darker (0.3f));
        addAndMakeVisible (b);
    }
    eqBtn.setToggleState (true, juce::dontSendNotification);
    eqBtn.onClick  = [this] { panView = false; updateVisibility(); };
    panBtn.onClick = [this] { panView = true;  updateVisibility(); };

    // Split bar
    source.addItemList (Params::sourceNames, 1);
    sourceA = std::make_unique<Attach::ComboBoxAttachment> (s, "split_source", source);
    solo.addItemList ({ "Solo off", "Solo Transient", "Solo Tonal" }, 1);
    soloA = std::make_unique<Attach::ComboBoxAttachment> (s, "split_solo", solo);
    lock.setButtonText ("Lock");
    lockA = std::make_unique<Attach::ButtonAttachment> (s, "split_lock", lock);
    sep    = std::make_unique<ParamLabel> (p, juce::StringArray { "split_sep" }, ParamLabel::Percent, Colours::trans);
    decay  = std::make_unique<ParamLabel> (p, juce::StringArray { "split_decay" }, ParamLabel::Percent, Colours::trans);
    smooth = std::make_unique<ParamLabel> (p, juce::StringArray { "split_smooth" }, ParamLabel::Ms, Colours::trans);
    for (juce::Component* w : { (juce::Component*) &source, (juce::Component*) &solo, (juce::Component*) &lock,
                                (juce::Component*) sep.get(), (juce::Component*) decay.get(), (juce::Component*) smooth.get() })
        addAndMakeVisible (w);

    updateVisibility();
    startTimerHz (15);
}

BandStrip::~BandStrip() = default;

void BandStrip::setSelected (int band)
{
    selected = band;
    repaint();
}

void BandStrip::mouseDown (const juce::MouseEvent& e)
{
    const int i = (e.x - grid.getX() - leftW) / std::max (1, colWidth);
    if (e.y < grid.getBottom() && i >= 0 && i < 8)
    {
        selected = i;
        if (onSelect) onSelect (i);
        repaint();
    }
}

void BandStrip::timerCallback()
{
    // detect band-type changes (they alter which rows are shown)
    int sig = panView ? 1 : 0;
    for (int b = 1; b <= 6; ++b)
        sig = sig * 7 + (int) Params::raw (proc.apvts, Params::id (b, "type"));
    if (sig != lastSig) { lastSig = sig; updateVisibility(); }

    for (auto& c : cols)
    {
        const bool edge = c->band == 0 || c->band == 7;
        c->freq.refresh (! panView);
        if (! edge)
        {
            const int t = (int) Params::raw (proc.apvts, Params::id (c->band, "type"));
            const bool gainOk = t != (int) BandType::Notch && t != (int) BandType::BandPass;
            c->gain.refresh (! panView && gainOk);
            c->q.refresh (! panView);
            c->pan.refresh (panView);
        }
    }
    sep->refresh();
    decay->refresh();
    smooth->refresh();
}

void BandStrip::updateVisibility()
{
    for (auto& c : cols)
    {
        const bool edge = c->band == 0 || c->band == 7;
        c->type.setVisible (! edge && ! panView);
        c->slope.setVisible (! panView);
        c->mode.setVisible (! edge && panView);
        if (! edge)
        {
            const int t = (int) Params::raw (proc.apvts, Params::id (c->band, "type"));
            c->notch.setVisible (! panView && t == (int) BandType::Notch);
        }
    }
    resized();
    repaint();
}

void BandStrip::resized()
{
    auto r = getLocalBounds().reduced (12, 10);
    splitBar = r.removeFromBottom (42);
    r.removeFromBottom (4);
    grid = r;

    colWidth = (grid.getWidth() - leftW) / 8;
    const int oy = grid.getY();

    for (auto& c : cols)
    {
        const int x = grid.getX() + leftW + c->band * colWidth + 3;
        const int w = colWidth - 6;

        c->badge.setBounds (x + 2, oy + 6, 30, 24);
        c->solo.setBounds (x + w - 28, oy + 6, 26, 24);

        const juce::Rectangle<int> typeRow (x + 2, oy + 34, w - 4, 22), slopeRow (x + 2, oy + 58, w - 4, 22),
                                   freqRow (x, oy + 84, w, 26), gainRow (x, oy + 112, w, 26), qRow (x, oy + 140, w, 26);
        c->type.setBounds (typeRow);
        c->slope.setBounds (slopeRow);
        c->mode.setBounds (typeRow);
        c->freq.setBounds (freqRow);
        c->pan.setBounds (freqRow);
        c->gain.setBounds (gainRow);
        c->notch.setBounds (gainRow.reduced (0, 3));
        c->q.setBounds (qRow);
    }

    // split bar: [EQ][PAN]  SPLIT  [Lock] <source>  Separation: x  Decay: x  Smooth: x  <solo>
    auto b = splitBar.reduced (4, 6);
    eqBtn.setBounds (b.removeFromLeft (44));
    b.removeFromLeft (4);
    panBtn.setBounds (b.removeFromLeft (44));
    b.removeFromLeft (50);                      // "SPLIT"
    lock.setBounds (b.removeFromLeft (52));
    source.setBounds (b.removeFromLeft (100));
    b.removeFromLeft (14);
    b.removeFromLeft (74);                      // "Separation:"
    sep->setBounds (b.removeFromLeft (46));
    b.removeFromLeft (8);
    b.removeFromLeft (46);                      // "Decay:"
    decay->setBounds (b.removeFromLeft (46));
    b.removeFromLeft (8);
    b.removeFromLeft (50);                      // "Smooth:"
    smooth->setBounds (b.removeFromLeft (62));
    b.removeFromLeft (10);
    solo.setBounds (b.removeFromLeft (110));
}

void BandStrip::paint (juce::Graphics& g)
{
    Skin::nine (g, Skin::Id::PanelFrame, getLocalBounds().toFloat(), 72, 18.0f);

    // row captions
    g.setFont (Rust::stencil (13.0f));
    g.setColour (Colours::dim);
    const int oy = grid.getY(), ox = grid.getX();
    auto cap = [&] (const juce::String& t, int y, int h = 26)
    {
        g.drawText (t, ox + 2, oy + y, leftW - 4, h, juce::Justification::centredLeft);
    };
    if (! panView) { cap ("TYPE", 34, 22); cap ("SLOPE", 58, 22); cap ("FREQ", 84); cap ("GAIN", 112); cap ("Q", 140); }
    else           { cap ("MODE", 34, 22); cap ("PAN", 84); }

    for (auto& c : cols)
    {
        const int x = ox + leftW + c->band * colWidth;
        const auto colR = juce::Rectangle<int> (x + 1, oy, colWidth - 2, grid.getHeight()).toFloat();
        Skin::nine (g, c->band == selected ? Skin::Id::ColumnSelected : Skin::Id::Column, colR, 48, 9.0f);

        const bool edge = c->band == 0 || c->band == 7;
        g.setColour (Colours::dim);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        if (edge && ! panView)
            g.drawText (c->band == 0 ? "HIGH-PASS" : "LOW-PASS", x + 3, oy + 34, colWidth - 6, 22, juce::Justification::centred);
        if (edge && panView)
            g.drawText ("-", x + 3, oy + 84, colWidth - 6, 26, juce::Justification::centred);
    }

    // split bar
    Skin::nine (g, Skin::Id::Recess, splitBar.toFloat(), 125, 11.0f);

    auto b = splitBar.reduced (4, 6);
    b.removeFromLeft (44 + 4 + 44);
    g.setColour (Colours::text);
    g.setFont (Rust::stencil (16.0f));
    g.drawText ("SPLIT", b.removeFromLeft (50).translated (6, 0), juce::Justification::centredLeft);
    b.removeFromLeft (52 + 100 + 14);
    g.setColour (Colours::accent);
    g.setFont (Rust::stencil (14.0f));
    g.drawText ("SEPARATION:", b.removeFromLeft (74), juce::Justification::centredLeft);
    b.removeFromLeft (46 + 8);
    g.drawText ("DECAY:", b.removeFromLeft (46), juce::Justification::centredLeft);
    b.removeFromLeft (46 + 8);
    g.drawText ("SMOOTH:", b.removeFromLeft (50), juce::Justification::centredLeft);
}

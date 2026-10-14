#include "PluginEditor.h"
#include "Skin.h"

using juce::Colour;
using juce::Graphics;

static constexpr double plotRangeDb = 12.0;

// ============================================================ PlotComponent
PlotComponent::PlotComponent (PirateEQ2Processor& p, std::function<void (int)> sel)
    : proc (p), onSelect (std::move (sel))
{
    for (auto& s : spec) s.fill (-100.0f);
    startTimerHz (30);
}

juce::Rectangle<float> PlotComponent::plotArea() const
{
    return getLocalBounds().toFloat().reduced (26.0f, 22.0f).withTrimmedLeft (10.0f).withTrimmedBottom (8.0f);
}

float PlotComponent::freqToX (double f) const
{
    const auto a = plotArea();
    return a.getX() + a.getWidth() * (float) (std::log (f / 20.0) / std::log (1000.0));
}

double PlotComponent::xToFreq (float x) const
{
    const auto a = plotArea();
    return 20.0 * std::pow (1000.0, (double) ((x - a.getX()) / a.getWidth()));
}

float PlotComponent::dbToY (double db) const
{
    const auto a = plotArea();
    // display range is +-12 dB (parameters still allow +-24; points beyond are pinned to the edge)
    return juce::jlimit (a.getY(), a.getBottom(), a.getCentreY() - (float) (db / plotRangeDb) * a.getHeight() * 0.5f);
}

double PlotComponent::yToDb (float y) const
{
    const auto a = plotArea();
    return (double) ((a.getCentreY() - y) / (a.getHeight() * 0.5f)) * plotRangeDb;
}

bool PlotComponent::isGainBand (int b) const
{
    if (b < 1 || b > 6) return false;
    const int t = (int) getParam (Params::id (b, "type"));
    return t != (int) BandType::Notch && t != (int) BandType::BandPass;
}

std::vector<PlotComponent::Handle> PlotComponent::handles() const
{
    std::vector<Handle> h;
    for (int b = 0; b < 8; ++b)
    {
        if (getParam (Params::id (b, "active")) < 0.5f) continue;

        if (b == 0 || b == 7)
        {
            const double fT = getParam (Params::id (b, "freqT")), fN = getParam (Params::id (b, "freqN"));
            const float y = dbToY (0);
            if (std::abs (std::log (fT / fN)) < 0.005)
            {
                const float x = freqToX (fT);
                h.push_back ({ b, -1, { x, y } });
                h.push_back ({ b, 0, { x - 16.0f, y }, true });   // Transient tab (left)
                h.push_back ({ b, 1, { x + 16.0f, y }, true });   // Tonal tab (right)
            }
            else { h.push_back ({ b, 0, { freqToX (fT), y } }); h.push_back ({ b, 1, { freqToX (fN), y } }); }
            continue;
        }

        const double f = getParam (Params::id (b, "freq"));
        const bool gain = isGainBand (b);
        const double gT = gain ? getParam (Params::id (b, "gainT")) : 0.0;
        const double gN = gain ? getParam (Params::id (b, "gainN")) : 0.0;
        const float x = freqToX (f);

        if ((int) getParam (Params::id (b, "type")) == (int) BandType::Notch)
        {
            const int target = (int) getParam (Params::id (b, "notch"));
            h.push_back ({ b, target == 1 ? -1 : (target == 0 ? 0 : 1), { x, dbToY (0) } });
        }
        else if (std::abs (gT - gN) < 0.05)
        {
            const float y = dbToY (gT);
            h.push_back ({ b, -1, { x, y } });
            if (gain)
            {
                h.push_back ({ b, 0, { x, y - 17.0f }, true });   // Transient tab (up)
                h.push_back ({ b, 1, { x, y + 17.0f }, true });   // Tonal tab (down)
            }
        }
        else
        {
            h.push_back ({ b, 0, { x, dbToY (gT) } });
            h.push_back ({ b, 1, { x, dbToY (gN) } });
        }
    }
    return h;
}

int PlotComponent::hitTest (juce::Point<float> p) const
{
    const auto hs = handles();
    int best = -1;

    // pull tabs win over the handle they hang from
    float bestD = 8.5f;
    for (size_t i = 0; i < hs.size(); ++i)
    {
        if (! hs[i].tab) continue;
        const float d = hs[i].pos.getDistanceFrom (p);
        if (d < bestD) { bestD = d; best = (int) i; }
    }
    if (best >= 0) return best;

    bestD = 12.0f;
    for (size_t i = 0; i < hs.size(); ++i)
    {
        if (hs[i].tab) continue;
        const float d = hs[i].pos.getDistanceFrom (p);
        if (d < bestD) { bestD = d; best = (int) i; }
    }
    return best;
}

void PlotComponent::setParam (const juce::String& id, float plain)
{
    if (auto* p = proc.apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (plain));
        p->endChangeGesture();
    }
}

void PlotComponent::applyDrag (const juce::MouseEvent& e)
{
    const double f = juce::jlimit (20.0, 20000.0, xToFreq (e.position.x));
    const double g = juce::jlimit (-24.0, 24.0, yToDb (e.position.y));
    const int b = dragHandle.band, st = dragHandle.stream;

    if (b == 0 || b == 7)
    {
        if (st != 1) setParam (Params::id (b, "freqT"), (float) f);
        if (st != 0) setParam (Params::id (b, "freqN"), (float) f);
        return;
    }

    setParam (Params::id (b, "freq"), (float) f);
    if (isGainBand (b))
    {
        if (st != 1) setParam (Params::id (b, "gainT"), (float) g);
        if (st != 0) setParam (Params::id (b, "gainN"), (float) g);
    }
}

void PlotComponent::mouseDown (const juce::MouseEvent& e)
{
    dragIndex = -1;
    const auto hs = handles();
    const int idx = hitTest (e.position);
    if (idx < 0) return;

    dragHandle = hs[(size_t) idx];
    selectedBand = dragHandle.band;
    onSelect (selectedBand);

    if (e.mods.isRightButtonDown())
    {
        setParam (Params::id (dragHandle.band, "active"), 0.0f);
        return;
    }

    if (dragHandle.stream == -1)
    {
        if (e.mods.isCtrlDown())     dragHandle.stream = 0;   // Ctrl: only Transient
        else if (e.mods.isAltDown()) dragHandle.stream = 1;   // Alt: only Tonal
    }
    dragIndex = idx;
    proc.undoManager.beginNewTransaction();
}

void PlotComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (dragIndex >= 0) applyDrag (e);
}

void PlotComponent::mouseUp (const juce::MouseEvent&) { dragIndex = -1; }

void PlotComponent::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto hs = handles();
    const int idx = hitTest (e.position);
    proc.undoManager.beginNewTransaction();

    if (idx >= 0)
    {
        const auto h = hs[(size_t) idx];
        if (h.stream == -1) return;
        const int other = 1 - h.stream;
        if (h.band == 0 || h.band == 7)
            setParam (Params::id (h.band, other == 0 ? "freqT" : "freqN"),
                      getParam (Params::id (h.band, h.stream == 0 ? "freqT" : "freqN")));
        else if (isGainBand (h.band))
            setParam (Params::id (h.band, other == 0 ? "gainT" : "gainN"),
                      getParam (Params::id (h.band, h.stream == 0 ? "gainT" : "gainN")));
        return;
    }

    for (int b = 1; b <= 6; ++b)
    {
        if (getParam (Params::id (b, "active")) > 0.5f) continue;
        const float f = (float) juce::jlimit (20.0, 20000.0, xToFreq (e.position.x));
        const float g = (float) juce::jlimit (-24.0, 24.0, yToDb (e.position.y));
        setParam (Params::id (b, "freq"), f);
        setParam (Params::id (b, "gainT"), g);
        setParam (Params::id (b, "gainN"), g);
        setParam (Params::id (b, "active"), 1.0f);
        selectedBand = b;
        onSelect (b);
        break;
    }
}

void PlotComponent::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const auto hs = handles();
    const int idx = hitTest (e.position);
    const int b = idx >= 0 ? hs[(size_t) idx].band : selectedBand;
    if (b < 1 || b > 6) return;

    const float factor = std::exp (w.deltaY * 0.6f);
    for (auto* id : { "qT", "qN" })
        setParam (Params::id (b, id), juce::jlimit (0.1f, 18.0f, getParam (Params::id (b, id)) * factor));
}

void PlotComponent::timerCallback()
{
    const int w = proc.anWrite.load (std::memory_order_acquire);
    for (int s = 0; s < 2; ++s)
    {
        std::array<float, 4096> data {};
        for (int i = 0; i < 2048; ++i)
            data[(size_t) i] = proc.anBuf[s][(size_t) ((w - 2048 + i) & (PirateEQ2Processor::anSize - 1))];
        window.multiplyWithWindowingTable (data.data(), 2048);
        fft.performFrequencyOnlyForwardTransform (data.data());
        for (int k = 0; k < 1024; ++k)
        {
            const float db = 20.0f * std::log10 (data[(size_t) k] / 512.0f + 1.0e-6f);
            spec[s][(size_t) k] = std::max (db, spec[s][(size_t) k] - 2.0f);
        }
    }
    repaint();
}

namespace
{
    // kind: 0 diamond, 1 triangle up, 2 triangle down, 3 triangle left, 4 triangle right
    juce::Path shapePath (int kind, juce::Point<float> c, float r)
    {
        juce::Path p;
        switch (kind)
        {
            case 0:  p.addQuadrilateral (c.x, c.y - r, c.x + r, c.y, c.x, c.y + r, c.x - r, c.y); break;
            case 1:  p.addTriangle (c.x, c.y - r, c.x + r * 0.95f, c.y + r * 0.8f, c.x - r * 0.95f, c.y + r * 0.8f); break;
            case 2:  p.addTriangle (c.x, c.y + r, c.x + r * 0.95f, c.y - r * 0.8f, c.x - r * 0.95f, c.y - r * 0.8f); break;
            case 3:  p.addTriangle (c.x - r, c.y, c.x + r * 0.8f, c.y - r * 0.95f, c.x + r * 0.8f, c.y + r * 0.95f); break;
            default: p.addTriangle (c.x + r, c.y, c.x - r * 0.8f, c.y - r * 0.95f, c.x - r * 0.8f, c.y + r * 0.95f); break;
        }
        return p;
    }
}

void PlotComponent::paint (Graphics& g)
{
    const auto a = plotArea();
    Skin::nine (g, Skin::Id::GraphFrame, getLocalBounds().toFloat(), 175, 30.0f);
    g.setColour (juce::Colour (0xc0100806));
    g.fillRect (a);

    // grid
    g.setFont (Rust::stencil (13.0f));
    for (int db = -12; db <= 12; db += 3)
    {
        const float y = dbToY (db);
        g.setColour (db == 0 ? Colours::accent.withAlpha (0.45f) : Colours::line);
        g.drawHorizontalLine ((int) y, a.getX(), a.getRight());
        g.setColour (Colours::dim);
        g.drawText (juce::String (db), (int) a.getX() - 34, (int) y - 7, 28, 14, juce::Justification::centredRight);
    }
    for (double f : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
    {
        const float x = freqToX (f);
        g.setColour (Colours::line);
        g.drawVerticalLine ((int) x, a.getY(), a.getBottom());
        g.setColour (Colours::dim);
        g.drawText (f >= 1000 ? juce::String (f / 1000.0, f >= 10000 ? 0 : (f == 1000 || f == 2000 || f == 5000 ? 0 : 1)) + "k"
                              : juce::String ((int) f),
                    (int) x - 16, (int) a.getBottom() + 4, 32, 14, juce::Justification::centred);
    }

    // spectrum
    const double sr = proc.currentSampleRate.load();
    for (int s = 0; s < 2; ++s)
    {
        juce::Path p;
        bool started = false;
        for (int k = 1; k < 1024; ++k)
        {
            const double f = k * sr / 2048.0;
            if (f < 20.0 || f > 20000.0) continue;
            const float y = juce::jlimit (a.getY(), a.getBottom(),
                                          a.getBottom() - (spec[s][(size_t) k] + 100.0f) / 100.0f * a.getHeight());
            if (! started) { p.startNewSubPath (freqToX (f), a.getBottom()); started = true; }
            p.lineTo (freqToX (f), y);
        }
        if (! started) continue;
        p.lineTo (a.getRight(), a.getBottom());
        p.closeSubPath();
        const auto c = s == 0 ? Colours::trans : Colours::tonal;
        g.setColour (c.withAlpha (0.20f));
        g.fillPath (p);
        g.setColour (c.withAlpha (0.85f));
        g.strokePath (p, juce::PathStrokeType (1.5f));
    }

    // legend: what the two coloured spectra are
    {
        const int solo = (int) getParam ("split_solo");
        auto item = [&] (float x, Colour c, const juce::String& t)
        {
            g.setColour (c);
            g.fillRoundedRectangle (x, a.getY() + 8, 10.0f, 10.0f, 2.0f);
            g.setColour (Colours::text);
            g.setFont (Rust::stencil (16.0f));
            g.drawText (t.toUpperCase(), (int) x + 15, (int) a.getY() + 4, 90, 18, juce::Justification::left);
        };
        item (a.getX() + 10, Colours::trans, "Transient");
        item (a.getX() + 100, Colours::tonal, "Tonal");
        if (solo != 0)
        {
            g.setColour (Colours::accent);
            g.drawText (juce::String ("SOLO: ") + (solo == 1 ? "Transient" : "Tonal"),
                        (int) a.getX() + 190, (int) a.getY() + 5, 160, 16, juce::Justification::left);
        }
    }

    // EQ curves
    const bool eqOff = getParam ("eq_bypass") > 0.5f;
    std::vector<float> curve[2];
    const int step = 2;
    const int nPts = (int) (a.getWidth() / step) + 1;
    for (int st = 0; st < 2; ++st)
    {
        curve[st].assign ((size_t) nPts, 0.0f);
        if (eqOff) continue;
        const double scale = getParam (st == 0 ? "scaleT" : "scaleN") * 0.01;
        std::array<BandFilter::Sections, 8> secs;
        std::array<int, 8> cnt {};
        for (int b = 0; b < 8; ++b)
            cnt[(size_t) b] = BandFilter::build (Params::readBand (proc.apvts, b, st, scale).spec, sr, secs[(size_t) b]);
        for (int i = 0; i < nPts; ++i)
        {
            const double f = xToFreq (a.getX() + (float) (i * step));
            double db = 0;
            for (int b = 0; b < 8; ++b)
                db += BandFilter::responseDb (secs[(size_t) b], cnt[(size_t) b], sr, f);
            curve[st][(size_t) i] = (float) db;
        }
    }

    auto strokeCurve = [&] (const std::vector<float>& c, Colour col, float thick)
    {
        juce::Path p;
        for (int i = 0; i < nPts; ++i)
        {
            const float x = a.getX() + (float) (i * step);
            const float y = juce::jlimit (a.getY(), a.getBottom(), dbToY (c[(size_t) i]));
            i == 0 ? p.startNewSubPath (x, y) : p.lineTo (x, y);
        }
        g.setColour (col);
        g.strokePath (p, juce::PathStrokeType (thick));
    };

    float maxDiff = 0;
    for (int i = 0; i < nPts; ++i) maxDiff = std::max (maxDiff, std::abs (curve[0][(size_t) i] - curve[1][(size_t) i]));
    if (maxDiff < 0.05f)
        strokeCurve (curve[0], Colours::text, 2.0f);
    else
    {
        // shade the region between the Transient and Tonal curves
        juce::Path between;
        for (int i = 0; i < nPts; ++i)
        {
            const float x = a.getX() + (float) (i * step);
            const float y = juce::jlimit (a.getY(), a.getBottom(), dbToY (curve[0][(size_t) i]));
            i == 0 ? between.startNewSubPath (x, y) : between.lineTo (x, y);
        }
        for (int i = nPts - 1; i >= 0; --i)
            between.lineTo (a.getX() + (float) (i * step),
                            juce::jlimit (a.getY(), a.getBottom(), dbToY (curve[1][(size_t) i])));
        between.closeSubPath();
        g.setColour (juce::Colour (0xffff8a5c).withAlpha (0.20f));
        g.fillPath (between);

        strokeCurve (curve[1], Colours::tonal, 2.0f);
        strokeCurve (curve[0], Colours::trans, 2.0f);
    }

    // handles: diamond = Transient+Tonal together; it splits into triangle-up (Transient) and triangle-down (Tonal)
    cached = handles();
    for (const auto& h : cached)
    {
        const bool horiz = h.band == 0 || h.band == 7;
        if (h.tab)
        {
            const int kind = horiz ? (h.stream == 0 ? 3 : 4) : (h.stream == 0 ? 1 : 2);
            const auto tc = h.stream == 0 ? Colours::trans : Colours::tonal;
            const auto path = shapePath (kind, h.pos, 6.5f);
            g.setColour (tc);
            g.fillPath (path);
            g.setColour (Colours::bg);
            g.strokePath (path, juce::PathStrokeType (1.3f));
            continue;
        }

        const bool sel = h.band == selectedBand;
        const auto c = h.stream == 0 ? Colours::trans : (h.stream == 1 ? Colours::tonal : Colours::text);
        const int kind = h.stream == -1 ? 0 : (h.stream == 0 ? 1 : 2);
        const float r = sel ? 11.5f : 10.0f;
        const auto path = shapePath (kind, h.pos, r);
        g.setColour (Colours::bg);
        g.fillPath (path);
        g.setColour (c);
        g.strokePath (path, juce::PathStrokeType (sel ? 2.6f : 1.9f));

        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        const float ty = kind == 1 ? h.pos.y + 2.0f : (kind == 2 ? h.pos.y - 2.0f : h.pos.y);
        g.drawText (juce::String (h.band + 1), (int) (h.pos.x - 8), (int) (ty - 7), 16, 14, juce::Justification::centred);
    }

    if (compact) return;
    g.setColour (Colours::dim.withAlpha (0.8f));
    g.setFont (juce::FontOptions (10.0f));
    g.drawText ("double-click: add band  |  top tab: Transient only  |  bottom tab: Tonal only  |  double-click handle: join  |  wheel: Q  |  right-click: remove",
                plotArea().toNearestInt().reduced (8, 6).removeFromTop (14), juce::Justification::centredRight);
}

// ============================================================ MasterPanel
MasterPanel::MasterPanel (PirateEQ2Processor& p) : proc (p)
{
    auto& s = p.apvts;
    gT.attach (s, "out_gainT", "Gain T", Colours::trans, " dB");
    gN.attach (s, "out_gainN", "Gain N", Colours::tonal, " dB");
    pT.attach (s, "out_panT", "Pan T", Colours::trans);
    pN.attach (s, "out_panN", "Pan N", Colours::tonal);
    sT.attach (s, "scaleT", "Scale T", Colours::trans, " %");
    sN.attach (s, "scaleN", "Scale N", Colours::tonal, " %");
    mode.attach (s, "out_mode", "Pan mode", { "L/R", "M/S" });
    phase.attach (s, "phase_inv", "Phase");
    bypass.attach (s, "bypass", "Bypass");
    eqBypass.attach (s, "eq_bypass", "EQ Byp");
    for (juce::Component* c : { (juce::Component*) &gT, (juce::Component*) &gN, (juce::Component*) &pT,
                                (juce::Component*) &pN, (juce::Component*) &sT, (juce::Component*) &sN,
                                (juce::Component*) &mode, (juce::Component*) &phase, (juce::Component*) &bypass,
                                (juce::Component*) &eqBypass })
        addAndMakeVisible (c);
    startTimerHz (30);
}

void MasterPanel::paint (Graphics& g)
{
    Skin::nine (g, Skin::Id::PanelFrame, getLocalBounds().toFloat(), 72, 20.0f);
    g.setColour (Colours::text);
    g.setFont (Rust::stencil (18.0f));
    g.drawText ("MASTER", 24, 10, 140, 22, juce::Justification::left);

    for (int s = 0; s < 2; ++s)
    {
        peakHold[s] = std::max (proc.peak[s].load(), peakHold[s] * 0.93f);
        rmsShow[s] += (proc.rms[s].load() - rmsShow[s]) * 0.4f;
        const auto col = s == 0 ? Colours::trans : Colours::tonal;
        auto bar = meterArea.toFloat().withWidth (meterArea.getWidth() * 0.5f - 3.0f)
                       .translated (s * (meterArea.getWidth() * 0.5f + 3.0f), 0);
        auto level = [] (float v)
        {
            const float db = juce::Decibels::gainToDecibels (v, -60.0f);
            return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        };
        juce::ignoreUnused (col);
        Skin::meter (g, bar, level (rmsShow[s]), level (peakHold[s]));
    }
}

void MasterPanel::resized()
{
    auto r = getLocalBounds().reduced (16, 14);
    r.removeFromTop (22);
    meterArea = r.removeFromRight (50);
    r.removeFromRight (6);

    auto row1 = r.removeFromTop (r.getHeight() / 2);
    const int w = row1.getWidth() / 3;
    gT.setBounds (row1.removeFromLeft (w));
    pT.setBounds (row1.removeFromLeft (w));
    sT.setBounds (row1);

    auto row2 = r.removeFromTop (r.getHeight() - 26);
    gN.setBounds (row2.removeFromLeft (w));
    pN.setBounds (row2.removeFromLeft (w));
    sN.setBounds (row2);

    auto row3 = r;
    mode.setVisible (false);   // keep the pan-mode combo off the compact layout; toggled via host automation
    phase.setBounds (row3.removeFromLeft (w));
    bypass.setBounds (row3.removeFromLeft (w));
    eqBypass.setBounds (row3);
}

// ============================================================ PresetBar
PresetBar::PresetBar (PirateEQ2Processor& p) : proc (p)
{
    for (auto* b : { &presets, &undo, &redo, &a, &b, &copy, &reset })
        addAndMakeVisible (b);

    presets.onClick = [this] { showPresetMenu(); };
    undo.onClick  = [this] { proc.undoManager.undo(); };
    redo.onClick  = [this] { proc.undoManager.redo(); };
    a.onClick     = [this] { switchSlot (0); };
    b.onClick     = [this] { switchSlot (1); };
    copy.onClick  = [this] { slots[1 - activeSlot] = proc.apvts.copyState(); };
    reset.onClick = [this] { resetAll(); };

    for (auto* m : { &normalBtn, &oldBtn })
    {
        m->setRadioGroupId (7001);
        m->setClickingTogglesState (true);
        m->setColour (juce::TextButton::buttonColourId, Colours::bg);
        m->setColour (juce::TextButton::buttonOnColourId, Colours::accent);
        addAndMakeVisible (m);
    }
    normalBtn.onClick = [this] { if (onMode) onMode (0); };
    oldBtn.onClick    = [this] { if (onMode) onMode (1); };

    // Standalone only: built-in test loop player
    if (proc.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
    {
        addAndMakeVisible (loop);
        loop.onClick = [this]
        {
            juce::PopupMenu m;
            m.addItem (1, "Load loop file...");
            m.addItem (2, "Stop loop", proc.isTestLoopOn());
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (loop), [this] (int r)
            {
                if (r == 2) proc.stopTestLoop();
                else if (r == 1)
                {
                    chooser = std::make_unique<juce::FileChooser> ("Load loop", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3");
                    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                          [this] (const juce::FileChooser& fc)
                                          {
                                              if (fc.getResult().existsAsFile()) proc.loadTestLoop (fc.getResult());
                                          });
                }
            });
        };

        // test loop: first line of Documents/P2EQ/test_loop.txt, else the built-in default
        juce::File def;   // optional: first line of <Documents>/P2EQ/test_loop.txt
        const auto cfg = presetDir().getParentDirectory().getChildFile ("test_loop.txt");
        if (cfg.existsAsFile())
        {
            const juce::File custom (cfg.loadFileAsString().upToFirstOccurrenceOf ("\n", false, false).trim());
            if (custom.existsAsFile()) def = custom;
        }
        if (! proc.isTestLoopOn() && def.existsAsFile())
            proc.loadTestLoop (def);
    }
    startTimerHz (10);
}

juce::File PresetBar::presetDir() const
{
    auto d = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                 .getChildFile ("P2EQ").getChildFile ("Presets");
    d.createDirectory();
    return d;
}

void PresetBar::timerCallback()
{
    undo.setEnabled (proc.undoManager.canUndo());
    redo.setEnabled (proc.undoManager.canRedo());
    a.setColour (juce::TextButton::buttonOnColourId, Colours::accent);
    b.setColour (juce::TextButton::buttonOnColourId, Colours::accent);
    a.setToggleState (activeSlot == 0, juce::dontSendNotification);
    b.setToggleState (activeSlot == 1, juce::dontSendNotification);
    a.repaint();
    b.repaint();
}

void PresetBar::paint (Graphics& g)
{
    Skin::nine (g, Skin::Id::PanelFrame, getLocalBounds().toFloat(), 72, 14.0f);
    auto np = juce::Rectangle<float> (14.0f, 5.0f, 78.0f, (float) getHeight() - 10.0f);   // small name plate
    Skin::nine (g, Skin::Id::Recess, np, 125, 9.0f);
    g.setColour (Colours::tonal);
    g.setFont (Skin::metal (22.0f));
    g.drawText ("P2EQ", np.toNearestInt().translated (0, 1), juce::Justification::centred);
    g.setColour (Colours::dim);
    g.setFont (Rust::stencil (14.0f));
    g.drawText ("LATENCY " + juce::String (proc.getLatencySamples()) + " SMP", getWidth() - 175, 0, 155, getHeight(),
                juce::Justification::right);
}

void PresetBar::setMode (int m)
{
    normalBtn.setToggleState (m == 0, juce::dontSendNotification);
    oldBtn.setToggleState (m == 1, juce::dontSendNotification);
}

void PresetBar::resized()
{
    auto r = getLocalBounds().reduced (6);
    {
        auto right = r;
        right.removeFromRight (150);   // latency readout
        oldBtn.setBounds (right.removeFromRight (110).reduced (3, 2));
        normalBtn.setBounds (right.removeFromRight (84).reduced (3, 2));
    }
    r.removeFromLeft (90);
    for (auto* btn : { &presets, &undo, &redo, &a, &b, &copy, &reset, &loop })
        btn->setBounds (r.removeFromLeft (btn == &presets ? 90 : 62).reduced (3, 2));
}

void PresetBar::showPresetMenu()
{
    juce::PopupMenu m;
    m.addItem (1, "Save as...");
    m.addItem (2, "Load file...");
    m.addSeparator();
    const auto files = presetDir().findChildFiles (juce::File::findFiles, true, "*.p2eq");
    for (int i = 0; i < files.size(); ++i)
        m.addItem (100 + i, files[i].getFileNameWithoutExtension());

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presets),
                     [this, files] (int r)
                     {
                         if (r == 1) savePreset();
                         else if (r == 2)
                         {
                             chooser = std::make_unique<juce::FileChooser> ("Load preset", presetDir(), "*.p2eq");
                             chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                                   [this] (const juce::FileChooser& fc)
                                                   {
                                                       if (fc.getResult().existsAsFile()) loadFile (fc.getResult());
                                                   });
                         }
                         else if (r >= 100 && r - 100 < files.size())
                             loadFile (files[r - 100]);
                     });
}

void PresetBar::savePreset()
{
    chooser = std::make_unique<juce::FileChooser> ("Save preset", presetDir(), "*.p2eq");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (f == juce::File()) return;
                              if (auto xml = proc.apvts.copyState().createXml())
                                  xml->writeTo (f.withFileExtension ("p2eq"));
                          });
}

void PresetBar::loadFile (const juce::File& f)
{
    auto xml = juce::XmlDocument::parse (f);
    if (xml == nullptr || ! xml->hasTagName (proc.apvts.state.getType())) return;

    const bool lock = Params::raw (proc.apvts, "split_lock") > 0.5f;
    const float src = Params::raw (proc.apvts, "split_source");
    proc.undoManager.beginNewTransaction();
    proc.apvts.replaceState (juce::ValueTree::fromXml (*xml));
    if (lock)
        if (auto* p = proc.apvts.getParameter ("split_source"))
            p->setValueNotifyingHost (p->convertTo0to1 (src));
}

void PresetBar::switchSlot (int slot)
{
    if (slot == activeSlot) return;
    slots[activeSlot] = proc.apvts.copyState();
    if (slots[slot].isValid())
        proc.apvts.replaceState (slots[slot]);
    else
        slots[slot] = proc.apvts.copyState();
    activeSlot = slot;
}

void PresetBar::resetAll()
{
    proc.undoManager.beginNewTransaction();
    for (auto* p : proc.getParameters())
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->getDefaultValue());
        p->endChangeGesture();
    }
}

// ============================================================ Editor
PirateEQ2Editor::PirateEQ2Editor (PirateEQ2Processor& p)
    : AudioProcessorEditor (&p), proc (p), presetBar (p), strip (p), masterPanel (p),
      plot (p, [this] (int b) { strip.setSelected (b); }),
      satPanel (p),
      oldView (p, std::make_unique<PlotComponent> (p, [] (int) {}))
{
    setLookAndFeel (&laf);

    credit.setFont (Skin::metal (21.0f), false, juce::Justification::centred);
    credit.setColour (juce::HyperlinkButton::textColourId, juce::Colour (0xffffb43a));
    credit.setTooltip ("stpunk.art");
    credit.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    addAndMakeVisible (credit);

    addAndMakeVisible (presetBar);
    addAndMakeVisible (plot);
    addAndMakeVisible (strip);
    strip.onSelect = [this] (int b) { plot.selectedBand = b; plot.repaint(); };
    addAndMakeVisible (masterPanel);
    addAndMakeVisible (satPanel);
    addChildComponent (oldView);
    presetBar.onMode = [this] (int m) { setMode (m); };

    setResizable (true, true);
    setResizeLimits (980, 740, 1800, 1300);
    setSize (1120, 860);
    setMode ((int) proc.apvts.state.getProperty ("uiMode", 0));
}

void PirateEQ2Editor::setMode (int m)
{
    mode = juce::jlimit (0, 1, m);
    proc.apvts.state.setProperty ("uiMode", mode, nullptr);
    presetBar.setMode (mode);
    for (juce::Component* c : { (juce::Component*) &plot, (juce::Component*) &strip, (juce::Component*) &masterPanel,
                                (juce::Component*) &satPanel })
        c->setVisible (mode == 0);
    oldView.setVisible (mode == 1);
    resized();
}

PirateEQ2Editor::~PirateEQ2Editor()
{
    setLookAndFeel (nullptr);
}

void PirateEQ2Editor::paint (Graphics& g)
{
    const auto all = getLocalBounds().toFloat();
    Skin::fit (g, Skin::Id::Background, all);
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.fillRect (all);
    if (mode == 0)
        Skin::header (g, all.getWidth());
}

void PirateEQ2Editor::resized()
{
    auto r = getLocalBounds().reduced (8);
    credit.setBounds (r.removeFromBottom (28).withSizeKeepingCentre (200, 28));   // footer, centred
    r.removeFromBottom (2);
    if (mode == 0)
    {
        const int headerBottom = juce::roundToInt (Skin::headerHeightFor ((float) getWidth()) * 0.47f);
        r.removeFromTop (headerBottom - 8);
        // SAT panel: header, right of the P2EQ title plate
        const int x = juce::roundToInt ((float) getWidth() * 0.665f);
        satPanel.setBounds (x, 4, getWidth() - 8 - x, headerBottom - 8);
    }
    presetBar.setBounds (r.removeFromTop (46));
    r.removeFromTop (6);
    oldView.setBounds (r);

    auto bottom = r.removeFromBottom (250);
    r.removeFromBottom (6);
    plot.setBounds (r);

    masterPanel.setBounds (bottom.removeFromRight (330));
    bottom.removeFromRight (6);
    strip.setBounds (bottom);
}

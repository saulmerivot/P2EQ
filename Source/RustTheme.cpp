#include "RustTheme.h"
#include "Skin.h"

namespace
{
    inline float hash2 (int x, int y, juce::uint32 seed)
    {
        juce::uint32 h = (juce::uint32) x * 374761393u + (juce::uint32) y * 668265263u + seed * 2246822519u;
        h = (h ^ (h >> 13)) * 1274126177u;
        return (float) ((h ^ (h >> 16)) & 0xffffu) / 65535.0f;
    }

    // periodic value noise (period p cells) so the texture tiles seamlessly
    inline float vnoise (float x, float y, int p, juce::uint32 seed)
    {
        const int xi = (int) std::floor (x), yi = (int) std::floor (y);
        float fx = x - (float) xi, fy = y - (float) yi;
        fx = fx * fx * (3.0f - 2.0f * fx);
        fy = fy * fy * (3.0f - 2.0f * fy);
        auto h = [&] (int a, int b) { return hash2 (((a % p) + p) % p, ((b % p) + p) % p, seed); };
        const float a = h (xi, yi), b = h (xi + 1, yi), c = h (xi, yi + 1), d = h (xi + 1, yi + 1);
        return juce::jmap (fy, juce::jmap (fx, a, b), juce::jmap (fx, c, d));
    }

    inline float fbm (float x, float y, int basePeriod, int octaves, juce::uint32 seed)
    {
        float sum = 0.0f, amp = 0.5f, norm = 0.0f;
        int p = basePeriod;
        for (int o = 0; o < octaves; ++o)
        {
            sum += amp * vnoise (x, y, p, seed + (juce::uint32) o * 101u);
            norm += amp;
            amp *= 0.5f;
            x *= 2.0f; y *= 2.0f; p *= 2;
        }
        return sum / norm;
    }

    juce::Image makeTexture()
    {
        constexpr int S = 1024;
        juce::Image img (juce::Image::RGB, S, S, false);
        {
            juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
            for (int y = 0; y < S; ++y)
                for (int x = 0; x < S; ++x)
                {
                    const float u = (float) x / S, v = (float) y / S;
                    const float big   = fbm (u * 8.0f,  v * 8.0f,  8,  5, 11u);   // rust patches
                    const float fine  = fbm (u * 64.0f, v * 64.0f, 64, 3, 23u);   // grain
                    const float pit   = hash2 (x, y, 7u);

                    float rust = juce::jlimit (0.0f, 1.0f, (big + 0.35f * (fine - 0.5f) - 0.53f) / 0.24f);
                    rust = rust * rust * (3.0f - 2.0f * rust);

                    const float g0 = 27.0f + 20.0f * (fine - 0.5f);
                    float r = juce::jmap (rust, g0 + 5.0f, 98.0f + 48.0f * fine);
                    float g = juce::jmap (rust, g0 + 1.0f, 42.0f + 24.0f * fine);
                    float b = juce::jmap (rust, g0 - 3.0f, 20.0f + 10.0f * fine);

                    if (pit > 0.992f) { r *= 0.45f; g *= 0.45f; b *= 0.45f; }                // pitting
                    else if (pit < 0.004f) { r += 40.0f; g += 28.0f; b += 18.0f; }           // bright flecks

                    bd.setPixelColour (x, y, juce::Colour ((juce::uint8) juce::jlimit (0.0f, 255.0f, r),
                                                           (juce::uint8) juce::jlimit (0.0f, 255.0f, g),
                                                           (juce::uint8) juce::jlimit (0.0f, 255.0f, b)));
                }
        }

        // scratches (wrap-safe enough at this density)
        juce::Graphics g (img);
        juce::Random rng (99);
        for (int i = 0; i < 260; ++i)
        {
            const float x = rng.nextFloat() * S, y = rng.nextFloat() * S;
            const float a = rng.nextFloat() * juce::MathConstants<float>::twoPi, len = 10.0f + rng.nextFloat() * 70.0f;
            g.setColour ((rng.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha (0.05f + 0.06f * rng.nextFloat()));
            g.drawLine (x, y, x + len * std::cos (a), y + len * std::sin (a), 0.8f);
        }
        return img;
    }
}

namespace Rust
{
    juce::FontOptions stencil (float size)
    {
        return juce::FontOptions ("Impact", size, juce::Font::plain);
    }

    const juce::Image& texture()
    {
        static const juce::Image tex = makeTexture();
        return tex;
    }

    void rivet (juce::Graphics& g, juce::Point<float> c, float r)
    {
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillEllipse (c.x - r + 1, c.y - r + 1.5f, 2 * r, 2 * r);
        juce::ColourGradient cg (juce::Colour (0xffb9a690), c.x - r * 0.4f, c.y - r * 0.5f,
                                 juce::Colour (0xff3b2a20), c.x + r * 0.6f, c.y + r * 0.7f, true);
        g.setGradientFill (cg);
        g.fillEllipse (c.x - r, c.y - r, 2 * r, 2 * r);
        g.setColour (juce::Colour (0x66ff9a4a));
        g.drawEllipse (c.x - r, c.y - r, 2 * r, 2 * r, 0.6f);
    }

    void plate (juce::Graphics& g, juce::Rectangle<float> r, float corner, float darken, bool rivets, int seed)
    {
        // drop shadow
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRoundedRectangle (r.translated (0, 3), corner);

        g.setTiledImageFill (texture(), (int) r.getX() + seed * 389, (int) r.getY() + seed * 211, 1.0f);
        g.fillRoundedRectangle (r, corner);
        if (darken > 0.0f)
        {
            g.setColour (juce::Colours::black.withAlpha (darken));
            g.fillRoundedRectangle (r, corner);
        }

        // bevel: warm highlight top-left, dark edge bottom-right
        juce::Path top;
        top.addRoundedRectangle (r.reduced (1.0f), corner);
        g.setColour (juce::Colour (0x38ffb070));
        g.strokePath (top, juce::PathStrokeType (1.2f));
        g.setColour (juce::Colours::black.withAlpha (0.85f));
        g.drawRoundedRectangle (r, corner, 1.6f);

        if (rivets && r.getWidth() > 40 && r.getHeight() > 30)
        {
            const float m = 8.0f;
            rivet (g, { r.getX() + m, r.getY() + m }, 3.2f);
            rivet (g, { r.getRight() - m, r.getY() + m }, 3.2f);
            rivet (g, { r.getX() + m, r.getBottom() - m }, 3.2f);
            rivet (g, { r.getRight() - m, r.getBottom() - m }, 3.2f);
        }
    }

    void recess (juce::Graphics& g, juce::Rectangle<float> r, float corner)
    {
        g.setColour (ink);
        g.fillRoundedRectangle (r, corner);
        g.setTiledImageFill (texture(), (int) r.getX() * 3, (int) r.getY() * 5, 0.10f);
        g.fillRoundedRectangle (r, corner);
        // inner shadow
        for (int i = 0; i < 6; ++i)
        {
            g.setColour (juce::Colours::black.withAlpha (0.16f - 0.025f * (float) i));
            g.drawRoundedRectangle (r.reduced ((float) i), corner, 1.0f);
        }
        g.setColour (juce::Colour (0x30ff9a4a));
        g.drawRoundedRectangle (r.expanded (1.0f), corner, 1.0f);
    }

    void spikes (juce::Graphics& g, juce::Rectangle<float> band, int seed)
    {
        juce::Random rng (seed + 5);
        float x = band.getX() + 6.0f;
        while (x < band.getRight() - 10.0f)
        {
            const float w = 10.0f + rng.nextFloat() * 8.0f;
            const float h = band.getHeight() * (0.45f + 0.55f * rng.nextFloat());
            const float lean = (rng.nextFloat() - 0.5f) * 6.0f;
            juce::Path p;
            p.addTriangle (x, band.getBottom(), x + w, band.getBottom(), x + w * 0.5f + lean, band.getBottom() - h);
            juce::ColourGradient sg (juce::Colour (0xff8f8579), x, band.getBottom() - h,
                                     juce::Colour (0xff2b211b), x + w, band.getBottom(), false);
            sg.addColour (0.55, juce::Colour (0xff6b3a1c));
            g.setGradientFill (sg);
            g.fillPath (p);
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            g.strokePath (p, juce::PathStrokeType (0.8f));
            x += w + 18.0f + rng.nextFloat() * 26.0f;
        }
    }

    void title (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, float size)
    {
        juce::GlyphArrangement ga;
        ga.addFittedText (juce::Font (stencil (size)), text, r.getX(), r.getY(), r.getWidth(), r.getHeight(),
                          juce::Justification::centred, 1);
        juce::Path p;
        ga.createPath (p);

        g.setColour (juce::Colours::black.withAlpha (0.8f));
        g.strokePath (p, juce::PathStrokeType (5.0f), juce::AffineTransform::translation (2, 3));
        g.strokePath (p, juce::PathStrokeType (4.0f));
        const auto b = p.getBounds();
        juce::ColourGradient tg (juce::Colour (0xffffd25a), b.getX(), b.getY(), juce::Colour (0xffd2591a), b.getX(), b.getBottom(), false);
        tg.addColour (0.55, amber);
        g.setGradientFill (tg);
        g.fillPath (p);
        // grime over the letters
        g.saveState();
        g.reduceClipRegion (p);
        g.setTiledImageFill (texture(), 0, 0, 0.35f);
        g.fillRect (b);
        g.restoreState();
    }
}

// ============================================================ LookAndFeel
RustLookAndFeel::RustLookAndFeel()
{
    setColourScheme (getMidnightColourScheme());
    setColour (juce::Label::textColourId, Rust::bone);
    setColour (juce::ComboBox::textColourId, Rust::bone);
    setColour (juce::ComboBox::arrowColourId, Rust::orange);
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff1a120e));
    setColour (juce::PopupMenu::textColourId, Rust::bone);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Rust::ember.darker (0.3f));
    setColour (juce::Slider::textBoxTextColourId, Rust::bone);
    setColour (juce::Slider::textBoxBackgroundColourId, Rust::ink);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colour (0x55ff9a4a));
    setColour (juce::TextButton::textColourOffId, Rust::bone);
    setColour (juce::TextButton::textColourOnId, Rust::ink);
    setColour (juce::ToggleButton::textColourId, Rust::bone);
    setColour (juce::TextEditor::backgroundColourId, Rust::ink);
    setColour (juce::TextEditor::textColourId, Rust::bone);
}

void RustLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    const auto r = b.getLocalBounds().toFloat();
    const bool on = b.getToggleState();

    if (b.getProperties()["skin"].toString() == "badge")
    {
        const auto& img = on ? Skin::tintedBadge (b.findColour (juce::TextButton::buttonOnColourId))
                             : Skin::image (Skin::Id::BadgeOff);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.setOpacity (1.0f);
        g.drawImage (img, r, juce::RectanglePlacement::stretchToFit);
        return;
    }

    const auto id = on ? Skin::Id::ButtonOn : (down ? Skin::Id::ButtonPressed : (over ? Skin::Id::ButtonHover : Skin::Id::ButtonOff));
    Skin::nine (g, id, r, 72, 72.0f * r.getHeight() / 211.0f);
}

juce::Font RustLookAndFeel::getTextButtonFont (juce::TextButton&, int h)
{
    return juce::Font (Rust::stencil (juce::jmin (17.0f, (float) h * 0.62f)));
}

void RustLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont (getTextButtonFont (b, b.getHeight()));
    const bool on = b.getToggleState();
    g.setColour (on ? b.findColour (juce::TextButton::textColourOnId)
                    : (b.isEnabled() ? b.findColour (juce::TextButton::textColourOffId) : Rust::boneDim.withAlpha (0.45f)));
    g.drawFittedText (b.getButtonText().toUpperCase(), b.getLocalBounds().reduced (3, 0), juce::Justification::centred, 1, 0.5f);
}

void RustLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&)
{
    const auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h);
    Skin::nine (g, Skin::Id::Combo, r, 70, 70.0f * (float) h / 289.0f);
    const float as = (float) h * 0.42f;
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.setOpacity (1.0f);
    g.drawImage (Skin::image (Skin::Id::ComboArrow),
                 juce::Rectangle<float> ((float) w - as - (float) h * 0.28f, ((float) h - as * 0.75f) * 0.5f, as, as * 0.75f),
                 juce::RectanglePlacement::stretchToFit);
}

juce::Font RustLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return juce::Font (juce::FontOptions (13.0f));
}

void RustLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool, bool)
{
    const float sz = juce::jmin (20.0f, (float) b.getHeight() - 2.0f);
    const auto box = juce::Rectangle<float> (1.0f, ((float) b.getHeight() - sz) * 0.5f, sz, sz);
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.setOpacity (1.0f);
    g.drawImage (Skin::image (b.getToggleState() ? Skin::Id::CheckOn : Skin::Id::CheckOff), box,
                 juce::RectanglePlacement::stretchToFit);
    g.setColour (b.findColour (juce::ToggleButton::textColourId));
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (b.getButtonText(), b.getLocalBounds().withTrimmedLeft ((int) sz + 6), juce::Justification::centredLeft);
}

void RustLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                        float startAngle, float endAngle, juce::Slider& s)
{
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (2.0f);
    const float r = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const float arcR = r - 2.0f;

    juce::Path track;
    track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour (juce::Colours::black.withAlpha (0.75f));
    g.strokePath (track, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float a = startAngle + pos * (endAngle - startAngle);
    juce::Path val;
    val.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, a, true);
    g.setColour (s.findColour (juce::Slider::rotarySliderFillColourId));
    g.strokePath (val, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float kr = arcR - 4.0f;
    Skin::knob (g, juce::Rectangle<float> (c.x - kr, c.y - kr, 2 * kr, 2 * kr), a);
}

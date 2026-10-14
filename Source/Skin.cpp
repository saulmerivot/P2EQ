#include "Skin.h"
#include "SkinData.h"
#include <map>
#include <tuple>

namespace
{
    const juce::Image& load (const void* data, int size)
    {
        // ImageCache keeps the decoded image alive and returns the same instance on later calls
        static std::map<const void*, juce::Image> cache;
        auto it = cache.find (data);
        if (it == cache.end())
            it = cache.emplace (data, juce::ImageCache::getFromMemory (data, size)).first;
        return it->second;
    }

    float scaleOf (juce::Graphics& g)
    {
        return juce::jlimit (1.0f, 3.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    }

    // cache key: id, w, h (physical px), extra
    using Key = std::tuple<int, int, int, int>;
    std::map<Key, juce::Image>& renderCache()
    {
        static std::map<Key, juce::Image> c;
        if (c.size() > 400) c.clear();   // window resizing creates many sizes; keep memory bounded
        return c;
    }

    void drawNineInto (juce::Graphics& g, const juce::Image& img, juce::Rectangle<float> d, int sb, float db)
    {
        const int W = img.getWidth(), H = img.getHeight();
        sb = juce::jmin (sb, W / 2 - 1, H / 2 - 1);
        db = juce::jmin (db, d.getWidth() * 0.5f, d.getHeight() * 0.5f);
        const float xs[4] = { d.getX(), d.getX() + db, d.getRight() - db, d.getRight() };
        const float ys[4] = { d.getY(), d.getY() + db, d.getBottom() - db, d.getBottom() };
        const int sx[4] = { 0, sb, W - sb, W };
        const int sy[4] = { 0, sb, H - sb, H };
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
            {
                const float dw = xs[c + 1] - xs[c], dh = ys[r + 1] - ys[r];
                if (dw <= 0 || dh <= 0) continue;
                g.drawImage (img, juce::roundToInt (xs[c]), juce::roundToInt (ys[r]),
                             juce::roundToInt (xs[c + 1]) - juce::roundToInt (xs[c]),
                             juce::roundToInt (ys[r + 1]) - juce::roundToInt (ys[r]),
                             sx[c], sy[r], sx[c + 1] - sx[c], sy[r + 1] - sy[r]);
            }
    }
}

namespace Skin
{
    juce::Font metal (float height)
    {
        static juce::Typeface::Ptr tf = juce::Typeface::createSystemTypefaceFor (SkinData::MetalManiaRegular_ttf,
                                                                                 (size_t) SkinData::MetalManiaRegular_ttfSize);
        return juce::Font (juce::FontOptions (tf).withHeight (height));
    }

    const juce::Image& image (Id id)
    {
        switch (id)
        {
            case Id::Background:     return load (SkinData::bg_texture_png, SkinData::bg_texture_pngSize);
            case Id::Header:         return load (SkinData::header_png, SkinData::header_pngSize);
            case Id::PanelFrame:     return load (SkinData::panel_frame_png, SkinData::panel_frame_pngSize);
            case Id::GraphFrame:     return load (SkinData::graph_frame_png, SkinData::graph_frame_pngSize);
            case Id::Recess:         return load (SkinData::recess_png, SkinData::recess_pngSize);
            case Id::ButtonOff:      return load (SkinData::button_off_png, SkinData::button_off_pngSize);
            case Id::ButtonHover:    return load (SkinData::button_hover_png, SkinData::button_hover_pngSize);
            case Id::ButtonPressed:  return load (SkinData::button_pressed_png, SkinData::button_pressed_pngSize);
            case Id::ButtonOn:       return load (SkinData::button_on_png, SkinData::button_on_pngSize);
            case Id::Column:         return load (SkinData::column_png, SkinData::column_pngSize);
            case Id::ColumnSelected: return load (SkinData::column_selected_png, SkinData::column_selected_pngSize);
            case Id::Combo:          return load (SkinData::combo_png, SkinData::combo_pngSize);
            case Id::ComboArrow:     return load (SkinData::combo_arrow_png, SkinData::combo_arrow_pngSize);
            case Id::BadgeOff:       return load (SkinData::badge_off_png, SkinData::badge_off_pngSize);
            case Id::BadgeOn:        return load (SkinData::badge_on_png, SkinData::badge_on_pngSize);
            case Id::CheckOff:       return load (SkinData::checkbox_off_png, SkinData::checkbox_off_pngSize);
            case Id::CheckOn:        return load (SkinData::checkbox_on_png, SkinData::checkbox_on_pngSize);
            case Id::Knob:           return load (SkinData::knob_png, SkinData::knob_pngSize);
            case Id::MeterOff:       return load (SkinData::meter_off_png, SkinData::meter_off_pngSize);
            case Id::MeterOn:        return load (SkinData::meter_on_png, SkinData::meter_on_pngSize);
            case Id::OldFaceplate:   return load (SkinData::old_faceplate_png, SkinData::old_faceplate_pngSize);
            case Id::OldFaderSlot:   return load (SkinData::old_fader_slot_png, SkinData::old_fader_slot_pngSize);
            case Id::OldFaderCap:    return load (SkinData::old_fader_cap_png, SkinData::old_fader_cap_pngSize);
            case Id::OldVuFace:      return load (SkinData::old_vu_face_png, SkinData::old_vu_face_pngSize);
            case Id::OldLampOff:     return load (SkinData::old_lamp_off_png, SkinData::old_lamp_off_pngSize);
            case Id::OldLampOn:      return load (SkinData::old_lamp_on_png, SkinData::old_lamp_on_pngSize);
            case Id::OldSection:     return load (SkinData::old_section_png, SkinData::old_section_pngSize);
            case Id::OldDivider:     return load (SkinData::old_divider_png, SkinData::old_divider_pngSize);
        }
        static juce::Image none;
        return none;
    }

    void nine (juce::Graphics& g, Id id, juce::Rectangle<float> dest, int srcBorder, float destBorder)
    {
        if (dest.isEmpty()) return;
        const float s = scaleOf (g);
        const int pw = juce::roundToInt (dest.getWidth() * s), ph = juce::roundToInt (dest.getHeight() * s);
        if (pw < 2 || ph < 2) return;
        auto& slot = renderCache()[Key { (int) id * 1000 + juce::roundToInt (destBorder * 4), pw, ph, srcBorder }];
        if (slot.isNull())
        {
            slot = juce::Image (juce::Image::ARGB, pw, ph, true);
            juce::Graphics ig (slot);
            ig.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            drawNineInto (ig, image (id), { 0.0f, 0.0f, (float) pw, (float) ph }, srcBorder, destBorder * s);
        }
        g.setOpacity (1.0f);
        g.drawImage (slot, dest);
    }

    void fit (juce::Graphics& g, Id id, juce::Rectangle<float> dest)
    {
        if (dest.isEmpty()) return;
        const float s = scaleOf (g);
        const int pw = juce::roundToInt (dest.getWidth() * s), ph = juce::roundToInt (dest.getHeight() * s);
        if (pw < 2 || ph < 2) return;
        auto& slot = renderCache()[Key { 500000 + (int) id, pw, ph, 0 }];
        if (slot.isNull())
        {
            slot = juce::Image (juce::Image::ARGB, pw, ph, true);
            juce::Graphics ig (slot);
            ig.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            ig.drawImage (image (id), juce::Rectangle<float> (0.0f, 0.0f, (float) pw, (float) ph),
                          juce::RectanglePlacement::stretchToFit);
        }
        g.setOpacity (1.0f);
        g.drawImage (slot, dest);
    }

    const juce::Image& tintedBadge (juce::Colour c)
    {
        static std::map<juce::uint32, juce::Image> cache;
        auto& slot = cache[c.getARGB()];
        if (slot.isNull())
        {
            slot = image (Id::BadgeOn).createCopy().convertedToFormat (juce::Image::ARGB);
            juce::Image::BitmapData bd (slot, juce::Image::BitmapData::readWrite);
            for (int y = 0; y < bd.height; ++y)
                for (int x = 0; x < bd.width; ++x)
                {
                    const auto p = bd.getPixelColour (x, y);
                    // multiply, then lift a little so dark crevices keep some colour
                    const float l = 0.25f + 0.85f * p.getPerceivedBrightness();
                    bd.setPixelColour (x, y, juce::Colour::fromFloatRGBA (juce::jmin (1.0f, c.getFloatRed() * l),
                                                                         juce::jmin (1.0f, c.getFloatGreen() * l),
                                                                         juce::jmin (1.0f, c.getFloatBlue() * l),
                                                                         p.getFloatAlpha()));
                }
        }
        return slot;
    }

    float headerHeightFor (float width)
    {
        const auto& h = image (Id::Header);
        return h.isValid() ? width * (float) h.getHeight() / (float) h.getWidth() : 0.0f;
    }

    void header (juce::Graphics& g, float width, float y)
    {
        fit (g, Id::Header, { 0.0f, y, width, headerHeightFor (width) });
    }

    void meter (juce::Graphics& g, juce::Rectangle<float> dest, float level, float peak)
    {
        fit (g, Id::MeterOff, dest);
        // LEDs live between ~3% and ~97% of the art height
        const float top = dest.getY() + dest.getHeight() * 0.03f, bot = dest.getBottom() - dest.getHeight() * 0.03f;
        const float litY = bot - (bot - top) * juce::jlimit (0.0f, 1.0f, level);
        {
            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (dest.withTop (litY).toNearestInt());
            fit (g, Id::MeterOn, dest);
        }
        if (peak > 0.01f)
        {
            const float py = bot - (bot - top) * juce::jlimit (0.0f, 1.0f, peak);
            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (juce::Rectangle<float> (dest.getX(), py - 3.0f, dest.getWidth(), 4.0f).toNearestInt());
            fit (g, Id::MeterOn, dest);
        }
    }

    void knob (juce::Graphics& g, juce::Rectangle<float> dest, float angle)
    {
        const auto& img = image (Id::Knob);
        if (img.isNull()) return;
        const float side = juce::jmin (dest.getWidth(), dest.getHeight());
        const float k = side / (float) img.getWidth();
        const auto c = dest.getCentre();
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.setOpacity (1.0f);
        g.drawImageTransformed (img, juce::AffineTransform::translation (-img.getWidth() * 0.5f, -img.getHeight() * 0.5f)
                                         .rotated (angle)
                                         .scaled (k)
                                         .translated (c.x, c.y));
    }
}

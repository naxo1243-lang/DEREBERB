#include "PluginEditor.h"
#include <BinaryData.h>
#include <cmath>

using namespace juce;

namespace
{
    constexpr float kPi    = MathConstants<float>::pi;
    constexpr float kTwoPi = MathConstants<float>::twoPi;

    const Colour cCyan    { 0xff3fe8ff };
    const Colour cIce     { 0xffbdf6ff };
    const Colour cBlue    { 0xff2a7bff };
    const Colour cViolet  { 0xff9a5cff };
    const Colour cMagenta { 0xffff3fd8 };
    const Colour cSun     { 0xffffb347 };
    const Colour cDeep    { 0xff03111c };

    // Pistas de los controles (coordenadas de diseno 1628 x 656)
    constexpr float amtX = 300.0f, amtTop = 190.0f, amtBottom = 470.0f, orbR = 48.0f;
    constexpr float rtY = 510.0f, rtLeft = 470.0f, rtRight = 782.0f;
    constexpr float mixY = 511.0f, mixLeft = 1228.0f, mixRight = 1503.0f;
    const Point<float> knobC { 935.0f, 515.0f };
    const Point<float> powerC { 1177.0f, 109.0f };
    const Point<float> ledC { 776.0f, 623.0f };
    const Point<float> deltaC { 1519.0f, 375.0f };

    const Rectangle<float> boxAmount { 245.0f, 558.0f, 115.0f, 47.0f };
    const Rectangle<float> boxRt     { 561.0f, 559.0f, 131.0f, 47.0f };
    const Rectangle<float> boxFloor  { 1028.0f, 490.0f, 139.0f, 48.0f };
    const Rectangle<float> boxMix    { 1289.0f, 559.0f, 155.0f, 47.0f };

    float smooth (float a, float b, float x)
    {
        x = jlimit (0.0f, 1.0f, (x - a) / (b - a));
        return x * x * (3.0f - 2.0f * x);
    }

    float hash01 (float n)
    {
        const float s = std::sin (n * 127.1f) * 43758.5453f;
        return s - std::floor (s);
    }

    Colour spectrum (float u)   // cian -> azul -> violeta -> magenta
    {
        u = jlimit (0.0f, 1.0f, u);
        if (u < 0.33f) return cCyan.interpolatedWith (cBlue, u / 0.33f);
        if (u < 0.66f) return cBlue.interpolatedWith (cViolet, (u - 0.33f) / 0.33f);
        return cViolet.interpolatedWith (cMagenta, (u - 0.66f) / 0.34f);
    }

    void glowStroke (Graphics& g, const Path& p, Colour c, float w, float k = 1.0f)
    {
        for (int i = 3; i >= 1; --i)
        {
            g.setColour (c.withAlpha (jlimit (0.0f, 1.0f, 0.085f * k)));
            g.strokePath (p, PathStrokeType (w * (1.0f + 1.7f * (float) i), PathStrokeType::curved, PathStrokeType::rounded));
        }
        g.setColour (c.withAlpha (jlimit (0.0f, 1.0f, 0.90f * k)));
        g.strokePath (p, PathStrokeType (w, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (Colours::white.withAlpha (jlimit (0.0f, 1.0f, 0.55f * k)));
        g.strokePath (p, PathStrokeType (jmax (0.8f, w * 0.34f), PathStrokeType::curved, PathStrokeType::rounded));
    }

    void radialGlow (Graphics& g, Point<float> c, float r, Colour col, float a)
    {
        ColourGradient gr (col.withAlpha (jlimit (0.0f, 1.0f, a)), c.x, c.y, col.withAlpha (0.0f), c.x + r, c.y, true);
        g.setGradientFill (gr);
        g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);
    }

    Image softwareScaled (const Image& src, int w, int h)
    {
        Image out (Image::ARGB, jmax (1, w), jmax (1, h), true, SoftwareImageType());
        Graphics g (out);
        g.setImageResamplingQuality (Graphics::highResamplingQuality);
        g.drawImage (src, Rectangle<float> (0.0f, 0.0f, (float) out.getWidth(), (float) out.getHeight()));
        return out;
    }

    Font techFont (float h, bool bold = false)
    {
        static const String name = []
        {
            const auto all = Font::findAllTypefaceNames();
            for (auto n : { "Bahnschrift", "Orbitron", "Eurostile", "Avenir Next Condensed", "DIN Alternate", "Segoe UI" })
                if (all.contains (n))
                    return String (n);
            return String();
        }();

        const int style = bold ? Font::bold : Font::plain;
        return Font (name.isNotEmpty() ? FontOptions (name, h, style) : FontOptions (h, style));
    }

    float textWidth (const Font& f, const String& s)
    {
        GlyphArrangement ga;
        ga.addLineOfText (f, s, 0.0f, 0.0f);
        return ga.getBoundingBox (0, -1, true).getWidth();
    }

    void glowText (Graphics& g, const String& text, Rectangle<float> area, const Font& f, Colour c,
                   Justification j, float glow)
    {
        g.setFont (f);
        if (glow > 0.0f)
        {
            g.setColour (cCyan.withAlpha (0.20f * glow));
            for (auto o : { Point<float> (-1.4f, 0), Point<float> (1.4f, 0), Point<float> (0, -1.4f), Point<float> (0, 1.4f) })
                g.drawText (text, area.translated (o.x, o.y), j, false);
        }
        g.setColour (c);
        g.drawText (text, area, j, false);
    }

    Path powerIcon (Point<float> c, float r)
    {
        Path p;
        p.addCentredArc (c.x, c.y, r, r, 0.0f, 0.25f * kPi, 1.75f * kPi, true);
        p.startNewSubPath (c.x, c.y - r * 1.2f);
        p.lineTo (c.x, c.y - r * 0.1f);
        return p;
    }

    void drawOrb (Graphics& g, Point<float> c, float R, float hover, float t, float level)
    {
        radialGlow (g, c, R * 2.0f, cCyan, 0.28f + 0.22f * hover + 0.25f * level);

        ColourGradient body (Colour (0xffe8ffff).withAlpha (0.94f), c.x - R * 0.38f, c.y - R * 0.45f,
                             Colour (0xff04507a).withAlpha (0.94f), c.x + R * 0.95f, c.y + R * 1.0f, true);
        body.addColour (0.30, Colour (0xff62e6ff).withAlpha (0.92f));
        body.addColour (0.62, Colour (0xff1b8fd0).withAlpha (0.94f));
        g.setGradientFill (body);
        g.fillEllipse (c.x - R, c.y - R, R * 2.0f, R * 2.0f);

        // remolino interior
        for (int k = 0; k < 2; ++k)
        {
            Path a;
            const float rad = R * (0.50f + 0.20f * (float) k);
            const float ang = t * (1.6f - 0.7f * (float) k) + (float) k * 2.6f;
            a.addCentredArc (c.x, c.y, rad, rad, 0.0f, ang, ang + 2.2f, true);
            g.setColour (Colours::white.withAlpha (0.30f - 0.08f * (float) k));
            g.strokePath (a, PathStrokeType (3.0f, PathStrokeType::curved, PathStrokeType::rounded));
        }

        g.setColour (Colours::white.withAlpha (0.60f));
        g.drawEllipse (c.x - R, c.y - R, R * 2.0f, R * 2.0f, 2.2f);
        g.setColour (cCyan.withAlpha (0.55f + 0.35f * hover));
        g.drawEllipse (c.x - R - 2.5f, c.y - R - 2.5f, R * 2.0f + 5.0f, R * 2.0f + 5.0f, 3.0f);

        // brillo especular
        ColourGradient sp (Colours::white.withAlpha (0.85f), c.x - R * 0.35f, c.y - R * 0.55f,
                           Colours::white.withAlpha (0.0f), c.x - R * 0.35f, c.y - R * 0.05f, false);
        g.setGradientFill (sp);
        g.fillEllipse (c.x - R * 0.62f, c.y - R * 0.82f, R * 0.56f, R * 0.40f);
    }

    void drawCapThumb (Graphics& g, Point<float> c, float w, float h, float hover)
    {
        const auto r = Rectangle<float> (w, h).withCentre (c);
        radialGlow (g, c, h * 0.95f, cCyan, 0.20f + 0.22f * hover);

        ColourGradient mg (Colour (0xfff2fbff), r.getX(), 0.0f, Colour (0xff1c2c3a), r.getRight(), 0.0f, false);
        mg.addColour (0.30, Colour (0xff9fb8c8));
        mg.addColour (0.58, Colour (0xff48627a));
        g.setGradientFill (mg);
        g.fillRoundedRectangle (r, w * 0.5f);
        g.setColour (Colour (0xff05101a));
        g.drawRoundedRectangle (r, w * 0.5f, 2.0f);

        const auto slot = Rectangle<float> (w * 0.28f, h * 0.62f).withCentre (c);
        g.setColour (cCyan.withAlpha (0.55f + 0.35f * hover));
        g.fillRoundedRectangle (slot.expanded (2.0f), slot.getWidth());
        g.setColour (Colours::white.withAlpha (0.85f));
        g.fillRoundedRectangle (slot.reduced (slot.getWidth() * 0.28f, 3.0f), 2.0f);
    }
}

// ---------------------------------------------------------------------------
// Zonas de interaccion invisibles: toda la imagen la dibuja el editor.

struct DereverbEditor::HitLookAndFeel : public LookAndFeel_V4
{
    void drawLinearSlider (Graphics&, int, int, int, int, float, float, float, Slider::SliderStyle, Slider&) override {}
    void drawRotarySlider (Graphics&, int, int, int, int, float, float, float, Slider&) override {}
    void drawToggleButton (Graphics&, ToggleButton&, bool, bool) override {}
    int getSliderThumbRadius (Slider&) override { return 0; }

    Slider::SliderLayout getSliderLayout (Slider& s) override
    {
        Slider::SliderLayout l;
        const int px = (int) s.getProperties().getWithDefault ("padX", 0);
        const int py = (int) s.getProperties().getWithDefault ("padY", 0);
        l.sliderBounds = s.getLocalBounds().reduced (px, py);
        return l;
    }
};

// ---------------------------------------------------------------------------

DereverbEditor::DereverbEditor (DereverbProcessor& p)
    : AudioProcessorEditor (&p), proc (p), lnf (std::make_unique<HitLookAndFeel>()),
      vblank (this, [this] { tick(); })
{
    setLookAndFeel (lnf.get());

    skinSrc = ImageFileFormat::loadFrom (BinaryData::skin_jpg, (size_t) BinaryData::skin_jpgSize).convertedToFormat (Image::ARGB);
    maskSrc = ImageFileFormat::loadFrom (BinaryData::fxmask_png, (size_t) BinaryData::fxmask_pngSize).convertedToFormat (Image::ARGB);
    jassert (skinSrc.isValid() && maskSrc.isValid());

    auto addRegion = [this] (int mode, Rectangle<float> r, float warp)
    {
        FxRegion f;
        f.mode = mode;
        f.design = r;
        f.warp = warp;
        regions.push_back (std::move (f));
    };
    addRegion (fxTank,      { 34.0f,   94.0f, 210.0f, 534.0f }, 1.7f);
    addRegion (fxTitle,     { 596.0f,  22.0f, 456.0f,  76.0f }, 0.0f);
    addRegion (fxGlyph,     { 346.0f,  88.0f, 160.0f,  50.0f }, 0.0f);
    addRegion (fxRim,       { 482.0f, 136.0f, 710.0f, 260.0f }, 0.0f);
    addRegion (fxFiber,     { 388.0f, 278.0f,  98.0f, 140.0f }, 0.9f);
    addRegion (fxTentacles, { 1194.0f, 56.0f, 400.0f, 292.0f }, 1.3f);
    addRegion (fxGlyph,     { 1242.0f, 352.0f, 112.0f,  44.0f }, 0.0f);
    addRegion (fxDrop,      { 188.0f,  78.0f,  40.0f,  48.0f }, 0.0f);
    addRegion (fxDrop,      { 206.0f, 492.0f,  40.0f,  50.0f }, 0.0f);
    addRegion (fxLedCyan,   { 810.0f, 392.0f,  56.0f,  26.0f }, 0.0f);
    addRegion (fxLedPink,   { 1148.0f, 392.0f,  26.0f,  20.0f }, 0.0f);

    for (int i = 0; i < 9; ++i)
        bubbles.push_back ({ amtX + (rng.nextFloat() - 0.5f) * 34.0f, 150.0f + rng.nextFloat() * 360.0f,
                             1.4f + rng.nextFloat() * 2.4f, 18.0f + rng.nextFloat() * 42.0f, rng.nextFloat() * kTwoPi });

    auto setupSlider = [this] (Slider& s, Slider::SliderStyle style, const String& tip)
    {
        s.setSliderStyle (style);
        s.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        s.setMouseCursor (MouseCursor::PointingHandCursor);
        s.setTooltip (tip);
        addAndMakeVisible (s);
    };
    setupSlider (amountSlider, Slider::LinearVertical,   "Cantidad: cuanta reverb se resta (doble clic = valor por defecto)");
    setupSlider (rtSlider,     Slider::LinearHorizontal, "RT60 de la sala: tiempo de reverberacion aproximado");
    setupSlider (mixSlider,    Slider::LinearHorizontal, "Mezcla: proporcion de senal procesada");
    setupSlider (floorSlider,  Slider::RotaryHorizontalVerticalDrag, "Reduccion maxima por banda (dB)");
    floorSlider.setRotaryParameters (1.25f * kPi, 2.75f * kPi, true);

    amountAttach = std::make_unique<SliderAttachment> (proc.apvts, "amount", amountSlider);
    rtAttach     = std::make_unique<SliderAttachment> (proc.apvts, "rt60",   rtSlider);
    floorAttach  = std::make_unique<SliderAttachment> (proc.apvts, "floor",  floorSlider);
    mixAttach    = std::make_unique<SliderAttachment> (proc.apvts, "mix",    mixSlider);

    for (auto* b : { &bypassButton, &deltaButton })
    {
        b->setClickingTogglesState (true);
        b->setMouseCursor (MouseCursor::PointingHandCursor);
        addAndMakeVisible (*b);
    }
    bypassButton.setTooltip ("Power: apaga o enciende el efecto");
    deltaButton.setTooltip ("Delta: escuchar solo lo que se esta quitando");
    bypassAttach = std::make_unique<ButtonAttachment> (proc.apvts, "bypass", bypassButton);
    deltaAttach  = std::make_unique<ButtonAttachment> (proc.apvts, "delta",  deltaButton);

    // cajas de valor editables con doble clic (el texto lo dibuja el editor)
    for (auto* l : { &amountBox, &rtBox, &floorBox, &mixBox })
    {
        l->setJustificationType (Justification::centred);
        l->setEditable (false, true, false);
        l->setColour (Label::textColourId, Colours::transparentBlack);
        l->setColour (Label::textWhenEditingColourId, cIce);
        l->setColour (Label::backgroundWhenEditingColourId, Colour (0xff04101a));
        l->setColour (Label::outlineWhenEditingColourId, cCyan);
        l->setTooltip ("Doble clic para escribir un valor");
        addAndMakeVisible (*l);
    }
    amountBox.onTextChange = [this] { commitBox (amountBox, amountSlider); };
    rtBox.onTextChange     = [this] { commitBox (rtBox, rtSlider); };
    floorBox.onTextChange  = [this] { commitBox (floorBox, floorSlider); };
    mixBox.onTextChange    = [this] { commitBox (mixBox, mixSlider); };
    refreshBoxes();

    lastHead = proc.telemetry.head.load();

    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) (designW / designH));
    setResizeLimits (700, (int) (700.0f * designH / designW), 2036, (int) (2036.0f * designH / designW));
    setSize (1100, (int) (1100.0f * designH / designW));
}

DereverbEditor::~DereverbEditor()
{
    setLookAndFeel (nullptr);
}

Rectangle<int> DereverbEditor::toReal (Rectangle<float> r) const
{
    return Rectangle<float> (origin.x + r.getX() * scale, origin.y + r.getY() * scale,
                             r.getWidth() * scale, r.getHeight() * scale).toNearestInt();
}

float DereverbEditor::prop (const Slider& s) const
{
    return (float) s.getNormalisableRange().convertTo0to1 (s.getValue());
}

std::pair<String, String> DereverbEditor::valueParts (const Slider& s) const
{
    const double v = s.getValue();
    if (&s == &amountSlider) return { String (v, 2), {} };
    if (&s == &rtSlider)     return { String (v, 2), "s" };
    if (&s == &floorSlider)  return { String (v, 1), "dB" };
    return { String (v, 2), "WET" };
}

void DereverbEditor::refreshBoxes()
{
    auto upd = [this] (Label& l, const Slider& s)
    {
        if (l.isBeingEdited())
            return;
        const auto [num, unit] = valueParts (s);
        l.setText (unit.isEmpty() ? num : num + " " + unit, dontSendNotification);
    };
    upd (amountBox, amountSlider);
    upd (rtBox, rtSlider);
    upd (floorBox, floorSlider);
    upd (mixBox, mixSlider);
}

void DereverbEditor::commitBox (Label& l, Slider& s)
{
    const auto txt = l.getText();
    if (txt.containsAnyOf ("0123456789"))
    {
        const double v = txt.retainCharacters ("0123456789.-").getDoubleValue();
        s.setValue (jlimit (s.getMinimum(), s.getMaximum(), v), sendNotificationSync);
    }
    refreshBoxes();
}

void DereverbEditor::resized()
{
    scale = jmin ((float) getWidth() / designW, (float) getHeight() / designH);
    origin = { ((float) getWidth() - designW * scale) * 0.5f, ((float) getHeight() - designH * scale) * 0.5f };

    auto pad = [this] (Slider& s, float px, float py)
    {
        s.getProperties().set ("padX", roundToInt (px * scale));
        s.getProperties().set ("padY", roundToInt (py * scale));
    };
    pad (amountSlider, 0.0f, 48.0f);
    pad (rtSlider, 48.0f, 0.0f);
    pad (mixSlider, 40.0f, 0.0f);
    pad (floorSlider, 0.0f, 0.0f);

    amountSlider.setBounds (toReal ({ amtX - 62.0f, amtTop - 48.0f, 124.0f, amtBottom - amtTop + 96.0f }));
    rtSlider.setBounds     (toReal ({ rtLeft - 48.0f, rtY - 44.0f, rtRight - rtLeft + 96.0f, 88.0f }));
    mixSlider.setBounds    (toReal ({ mixLeft - 40.0f, mixY - 44.0f, mixRight - mixLeft + 80.0f, 88.0f }));
    floorSlider.setBounds  (toReal ({ knobC.x - 72.0f, knobC.y - 72.0f, 144.0f, 144.0f }));
    bypassButton.setBounds (toReal ({ 1150.0f, 86.0f, 120.0f, 46.0f }));
    deltaButton.setBounds  (toReal ({ 1492.0f, 348.0f, 54.0f, 54.0f }));

    amountBox.setBounds (toReal (boxAmount));
    rtBox.setBounds     (toReal (boxRt));
    floorBox.setBounds  (toReal (boxFloor));
    mixBox.setBounds    (toReal (boxMix));
    for (auto* l : { &amountBox, &rtBox, &floorBox, &mixBox })
        l->setFont (techFont (30.0f * scale, true));

    cachesDirty = true;
}

// ---------------------------------------------------------------------------
// Caches a la resolucion de la ventana

void DereverbEditor::rebuildCaches()
{
    cachesDirty = false;
    if (scale <= 0.0f || ! skinSrc.isValid() || ! maskSrc.isValid())
        return;

    float disp = 1.0f;
    if (auto* d = Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds()))
        disp = (float) d->scale;

    backing = disp;
    winPx = jmax (0.2f, scale * disp);
    fxPx  = jmax (0.2f, jmin (winPx, 1.0f) * fxQuality);

    const int ww = roundToInt (designW * winPx), wh = roundToInt (designH * winPx);
    const int fw = roundToInt (designW * fxPx),  fh = roundToInt (designH * fxPx);

    skinWin = softwareScaled (skinSrc, ww, wh);
    skinFx  = (fw == ww && fh == wh) ? skinWin : softwareScaled (skinSrc, fw, fh);
    fxImg   = Image (Image::ARGB, fw, fh, true, SoftwareImageType());

    // cubre con suavidad el interior borroso de la ilustracion (una sola vez)
    {
        Graphics cg (skinWin);
        cg.addTransform (AffineTransform::scale ((float) skinWin.getWidth() / designW, (float) skinWin.getHeight() / designH));
        for (int k = 0; k < 9; ++k)
        {
            const float inset = (float) k * 7.0f;
            Path cp;
            cp.addRoundedRectangle (Rectangle<float> (506.0f + inset, 180.0f + inset * 0.6f, 660.0f - 2.0f * inset, 182.0f - inset * 1.2f),
                                    jmax (8.0f, 82.0f - inset * 0.6f));
            cg.setColour (cDeep.withAlpha (0.17f));
            cg.fillPath (cp);
        }
    }
    const Image maskFx = softwareScaled (maskSrc, fw, fh);

    {
        Image::BitmapData md (maskFx, Image::BitmapData::readOnly);
        fxClipLogical.clear();

        for (auto& r : regions)
        {
            r.px = (r.design * fxPx).getSmallestIntegerContainer().getIntersection (Rectangle<int> (0, 0, fw, fh));
            const int w = r.px.getWidth(), h = r.px.getHeight();
            const size_t n = (size_t) jmax (0, w) * (size_t) jmax (0, h);
            r.mr.assign (n, 0);
            r.mg.assign (n, 0);
            r.mb.assign (n, 0);
            r.feather.clear();
            if (r.warp > 0.0f)
                r.feather.assign (n, 0);

            const float ramp = jmax (2.0f, 14.0f * fxPx);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                {
                    const size_t i = (size_t) y * (size_t) w + (size_t) x;
                    const auto px = *(const PixelARGB*) md.getPixelPointer (r.px.getX() + x, r.px.getY() + y);
                    r.mr[i] = px.getRed();
                    r.mg[i] = px.getGreen();
                    r.mb[i] = px.getBlue();
                    if (! r.feather.empty())
                    {
                        const float e = (float) jmin (jmin (x + 1, w - x), jmin (y + 1, h - y)) / ramp;
                        r.feather[i] = (uint8) roundToInt (255.0f * jlimit (0.0f, 1.0f, e));
                    }
                }

            fxClipLogical.addWithoutMerging (toReal (r.design).expanded (2));
        }
    }

    // disco del remolino de la perilla (se hace girar)
    {
        const float R = 56.0f;
        const int S = jmax (8, roundToInt (R * 2.0f * winPx));
        vortexImg = Image (Image::ARGB, S, S, true, SoftwareImageType());
        const int sx = roundToInt ((knobC.x - R) * winPx), sy = roundToInt ((knobC.y - R) * winPx);
        Image::BitmapData src (skinWin, Image::BitmapData::readOnly);
        Image::BitmapData dst (vortexImg, Image::BitmapData::readWrite);
        for (int y = 0; y < S; ++y)
            for (int x = 0; x < S; ++x)
            {
                const float dx = ((float) x + 0.5f) / winPx - R, dy = ((float) y + 0.5f) / winPx - R;
                const float a = 1.0f - smooth (36.0f, 52.0f, std::sqrt (dx * dx + dy * dy));
                const int gx = jlimit (0, ww - 1, sx + x), gy = jlimit (0, wh - 1, sy + y);
                const auto px = *(const PixelARGB*) src.getPixelPointer (gx, gy);
                auto* o = (PixelARGB*) dst.getPixelPointer (x, y);
                o->setARGB ((uint8) roundToInt (255.0f * a), (uint8) roundToInt (px.getRed() * a),
                            (uint8) roundToInt (px.getGreen() * a), (uint8) roundToInt (px.getBlue() * a));
            }
    }
}

// ---------------------------------------------------------------------------
// Efectos por pixel guiados por la mascara (ondulacion liquida + brillos)

void DereverbEditor::renderFx (float t)
{
    if (! fxImg.isValid() || ! skinFx.isValid() || regions.empty())
        return;

    const double t0 = Time::getMillisecondCounterHiRes();
    fxImg.clear (fxImg.getBounds());

    const int fw = skinFx.getWidth(), fh = skinFx.getHeight();
    const float inv = 1.0f / fxPx;
    const float vIn = lvlIn, vOut = lvlOut, vRed = lvlRed;

    const int n = jmax (fw, fh);
    trig.resize ((size_t) n * 8);
    float* colS1 = trig.data();           float* colC1 = colS1 + n;
    float* colS2 = colC1 + n;             float* colC2 = colS2 + n;
    float* rowS1 = colC2 + n;             float* rowC1 = rowS1 + n;
    float* rowS2 = rowC1 + n;             float* rowC2 = rowS2 + n;
    for (int x = 0; x < fw; ++x)
    {
        const float xd = (float) x * inv;
        colS1[x] = std::sin (xd * 0.015f);  colC1[x] = std::cos (xd * 0.015f);
        const float a = xd * 0.047f - t * 1.5f;
        colS2[x] = std::sin (a);            colC2[x] = std::cos (a);
    }
    for (int y = 0; y < fh; ++y)
    {
        const float yd = (float) y * inv;
        const float a = yd * 0.055f + t * 1.9f;
        rowS1[y] = std::sin (a);            rowC1[y] = std::cos (a);
        const float b = yd * 0.012f;
        rowS2[y] = std::sin (b);            rowC2[y] = std::cos (b);
    }

    Image::BitmapData src (skinFx, Image::BitmapData::readOnly);
    Image::BitmapData dst (fxImg, Image::BitmapData::readWrite);

    auto sample = [&] (float sx, float sy, float& r, float& g, float& b)
    {
        sx = jlimit (0.0f, (float) (fw - 1) - 0.01f, sx);
        sy = jlimit (0.0f, (float) (fh - 1) - 0.01f, sy);
        const int x0 = (int) sx, y0 = (int) sy;
        const float ax = sx - (float) x0, ay = sy - (float) y0;
        const auto* p0 = (const PixelARGB*) src.getLinePointer (y0) + x0;
        const auto* p1 = (const PixelARGB*) src.getLinePointer (y0 + 1) + x0;
        const float w00 = (1.0f - ax) * (1.0f - ay), w10 = ax * (1.0f - ay), w01 = (1.0f - ax) * ay, w11 = ax * ay;
        r = w00 * p0[0].getRed()   + w10 * p0[1].getRed()   + w01 * p1[0].getRed()   + w11 * p1[1].getRed();
        g = w00 * p0[0].getGreen() + w10 * p0[1].getGreen() + w01 * p1[0].getGreen() + w11 * p1[1].getGreen();
        b = w00 * p0[0].getBlue()  + w10 * p0[1].getBlue()  + w01 * p1[0].getBlue()  + w11 * p1[1].getBlue();
    };

    for (auto& reg : regions)
    {
        const int rw = reg.px.getWidth(), rh = reg.px.getHeight();
        if (rw <= 0 || rh <= 0 || reg.mr.size() != (size_t) rw * (size_t) rh)
            continue;

        const float amp = reg.warp * fxPx * (0.75f + 0.9f * vIn);
        const bool warps = amp > 0.0f && ! reg.feather.empty();
        const float gx0 = reg.design.getX(), gw = reg.design.getWidth();

        for (int yy = 0; yy < rh; ++yy)
        {
            const int y = reg.px.getY() + yy;
            const float yd = (float) y * inv;
            auto* drow = (PixelARGB*) dst.getLinePointer (y);

            for (int xx = 0; xx < rw; ++xx)
            {
                const size_t i = (size_t) yy * (size_t) rw + (size_t) xx;
                const int x = reg.px.getX() + xx;
                const float rv = (float) reg.mr[i] * (1.0f / 255.0f);
                const float gv = (float) reg.mg[i] * (1.0f / 255.0f);
                const float bv = (float) reg.mb[i] * (1.0f / 255.0f);
                const float fe = warps ? (float) reg.feather[i] * (1.0f / 255.0f) : 1.0f;
                if (fe <= 0.0f)
                    continue;

                float ar = 0.0f, ag = 0.0f, ab = 0.0f;
                if (rv > 0.02f || gv > 0.02f)
                {
                    switch (reg.mode)
                    {
                        case fxTank:
                        {
                            const float flow = 0.55f + 0.45f * std::sin (bv * 9.0f - t * 3.2f + std::sin (yd * 0.05f + t) * 1.5f);
                            const float gain = 0.30f + 1.00f * vIn;
                            const float mc   = 0.5f + 0.5f * std::sin (t * 0.8f + yd * 0.012f);
                            const float k    = gv * gain * flow * 0.85f;
                            ar += 70.0f * k;
                            ag += (225.0f + 30.0f * (1.0f - mc)) * k;
                            ab += (255.0f - 85.0f * (1.0f - mc)) * k;
                            const float rim = rv * (0.20f + 0.60f * vIn) * (0.6f + 0.4f * std::sin (bv * 6.0f - t * 2.4f));
                            ar += 90.0f * rim;  ag += 220.0f * rim;  ab += 255.0f * rim;
                            break;
                        }
                        case fxTitle:
                        {
                            const float s  = std::fmod (t * 0.22f, 1.5f) - 0.25f;
                            const float hl = std::exp (-(bv - s) * (bv - s) / 0.006f);
                            const float k  = rv * (0.08f + 0.40f * vOut + 1.10f * hl);
                            ar += 190.0f * k;  ag += 245.0f * k;  ab += 255.0f * k;
                            break;
                        }
                        case fxGlyph:
                        {
                            const float cell = std::floor (((float) x * inv - gx0) / (gw / 5.0f));
                            const float f = 0.30f + 0.70f * hash01 (cell * 7.13f + std::floor (t * 2.6f + cell * 0.7f) * 3.31f);
                            const float k = rv * f * 0.9f;
                            ar += 110.0f * k;  ag += 235.0f * k;  ab += 255.0f * k;
                            break;
                        }
                        case fxRim:
                        {
                            const float s  = std::fmod (t * 0.18f, 1.4f) - 0.2f;
                            const float hl = std::exp (-(bv - s) * (bv - s) / 0.01f);
                            const float k  = rv * (0.15f + 0.90f * vOut + 1.10f * hl);
                            ar += (60.0f + 195.0f * bv) * k;
                            ag += (225.0f - 155.0f * bv) * k;
                            ab += (255.0f - 40.0f * bv) * k;
                            break;
                        }
                        case fxFiber:
                        {
                            float p = 0.5f + 0.5f * std::sin (bv * 14.0f - t * 5.0f);
                            p = p * p * p;
                            const float k = rv * (0.20f + 1.00f * p + 0.60f * vIn);
                            ar += 130.0f * k;  ag += 235.0f * k;  ab += 255.0f * k;
                            break;
                        }
                        case fxTentacles:
                        {
                            const float flow = 0.5f + 0.5f * std::sin (bv * 12.0f - t * 4.0f + yd * 0.02f);
                            const float kg = gv * (0.22f + 1.10f * vOut) * flow * 0.8f;
                            ar += 80.0f * kg;  ag += 255.0f * kg;  ab += 225.0f * kg;
                            const float kr = rv * (0.10f + 0.50f * vOut);
                            ar += 110.0f * kr;  ag += 225.0f * kr;  ab += 255.0f * kr;
                            break;
                        }
                        case fxDrop:
                        {
                            const float p = 0.5f + 0.5f * std::sin (t * 2.0f + reg.design.getY() * 0.01f);
                            const float k = rv * (0.25f + 0.55f * p + 0.50f * vOut);
                            ar += 110.0f * k;  ag += 235.0f * k;  ab += 255.0f * k;
                            break;
                        }
                        case fxLedCyan:
                        {
                            const float k = rv * (0.25f + 1.40f * vIn);
                            ar += 60.0f * k;  ag += 230.0f * k;  ab += 255.0f * k;
                            break;
                        }
                        case fxLedPink:
                        {
                            const float k = rv * (0.30f + 2.20f * vRed);
                            ar += 255.0f * k;  ag += 70.0f * k;  ab += 215.0f * k;
                            break;
                        }
                        default: break;
                    }
                }

                float r, g, b;
                if (warps)
                {
                    const float d1 = rowS1[y] * colC1[x] + rowC1[y] * colS1[x];
                    const float d2 = colS2[x] * rowC2[y] + colC2[x] * rowS2[y];
                    sample ((float) x + amp * fe * d1, (float) y + amp * fe * 0.8f * d2, r, g, b);
                }
                else
                {
                    if (jmax (ar, jmax (ag, ab)) < 3.0f)
                        continue;
                    sample ((float) x, (float) y, r, g, b);
                }

                r = jmin (255.0f, r + ar);
                g = jmin (255.0f, g + ag);
                b = jmin (255.0f, b + ab);
                drow[x].setARGB ((uint8) roundToInt (255.0f * fe), (uint8) roundToInt (r * fe),
                                 (uint8) roundToInt (g * fe), (uint8) roundToInt (b * fe));
            }
        }
    }

    const float ms = (float) (Time::getMillisecondCounterHiRes() - t0);
    fxMsAvg = fxMsAvg <= 0.0f ? ms : fxMsAvg * 0.9f + ms * 0.1f;
}

// ---------------------------------------------------------------------------
// Animacion

void DereverbEditor::spawnParticle()
{
    if (particles.size() > 240)
        return;

    Particle p;
    p.pos = { 524.0f + rng.nextFloat() * 300.0f, 271.0f + (rng.nextFloat() - 0.5f) * 90.0f };
    const float side = p.pos.y < 271.0f ? -1.0f : 1.0f;
    p.vel = { -6.0f + rng.nextFloat() * 38.0f, side * (8.0f + rng.nextFloat() * 44.0f) };
    p.maxLife = p.life = 1.2f + rng.nextFloat() * 2.0f;
    p.size = 2.4f + rng.nextFloat() * 5.2f;
    p.rot = rng.nextFloat() * kTwoPi;
    p.spin = (rng.nextFloat() - 0.5f) * 6.0f;
    p.bright = rng.nextFloat() < 0.25f;
    particles.push_back (p);
}

void DereverbEditor::step (float dt)
{
    animTime += dt;

    const auto& tel = proc.telemetry;
    const float inL  = tel.inLevel.load (std::memory_order_relaxed);
    const float outL = tel.outLevel.load (std::memory_order_relaxed);
    const float red  = tel.reduction.load (std::memory_order_relaxed);

    auto vis = [] (float v) { return jlimit (0.0f, 1.0f, std::sqrt (jmax (0.0f, v)) * 1.15f); };
    const float kUp = 1.0f - std::exp (-dt * 14.0f), kDown = 1.0f - std::exp (-dt * 5.0f);
    auto follow = [&] (float& cur, float target) { cur += (target > cur ? kUp : kDown) * (target - cur); };
    follow (lvlIn, vis (inL));
    follow (lvlOut, vis (outL));
    follow (lvlRed, jlimit (0.0f, 1.0f, red * 2.5f));

    peakLamp = outL > 0.95f ? 1.0f : jmax (0.0f, peakLamp - dt * 1.6f);
    swirlAngle += dt * (0.7f + 2.4f * prop (floorSlider) + 3.0f * lvlRed);

    spawnAcc += dt * (8.0f + 240.0f * lvlRed + 40.0f * lvlIn);
    while (spawnAcc >= 1.0f)
    {
        spawnParticle();
        spawnAcc -= 1.0f;
    }
    for (auto& p : particles)
    {
        p.pos += p.vel * dt;
        p.vel.x += 10.0f * dt;
        p.vel *= (1.0f - 0.25f * dt);
        p.rot += p.spin * dt;
        p.life -= dt;
    }
    particles.erase (std::remove_if (particles.begin(), particles.end(), [] (const Particle& p) { return p.life <= 0.0f; }),
                     particles.end());

    for (auto& b : bubbles)
    {
        b.y -= b.speed * dt;
        if (b.y < 150.0f)
        {
            b.y = 506.0f;
            b.x = amtX + (rng.nextFloat() - 0.5f) * 34.0f;
        }
    }
}

void DereverbEditor::tick()
{
    const double now = Time::getMillisecondCounterHiRes() * 0.001;
    const float dt = (float) jlimit (0.0, 0.1, lastTime > 0.0 ? now - lastTime : 0.016);
    lastTime = now;

    const int head = proc.telemetry.head.load (std::memory_order_acquire);
    if (head != lastHead)
    {
        lastHead = head;
        lastHeadTime = now;
    }

    if (cachesDirty)
        rebuildCaches();

    step (dt);

    fxAccum += dt;
    if (fxAccum >= 1.0f / 30.0f)
    {
        fxAccum = 0.0f;
        renderFx (animTime);

        // si los efectos cuestan demasiado, baja su resolucion (y sube de nuevo si sobra tiempo)
        if (fxMsAvg > 11.0f && fxQuality > 0.55f && now - lastQualityChange > 3.0)
        {
            fxQuality *= 0.85f;
            lastQualityChange = now;
            fxMsAvg = 0.0f;
            cachesDirty = true;
        }
        else if (fxMsAvg > 0.0f && fxMsAvg < 3.5f && fxQuality < 1.0f && now - lastQualityChange > 10.0)
        {
            fxQuality = jmin (1.0f, fxQuality * 1.15f);
            lastQualityChange = now;
            fxMsAvg = 0.0f;
            cachesDirty = true;
        }
    }

    refreshBoxes();
    repaint();
}

// ---------------------------------------------------------------------------
// Dibujo

void DereverbEditor::paint (Graphics& g)
{
    g.fillAll (Colour (0xff04070e));
    if (scale <= 0.0f)
        return;
    if (cachesDirty)
        rebuildCaches();
    if (! skinWin.isValid())
        return;

    // capas de bitmap a resolucion de pantalla: copia 1:1, sin remuestreo
    {
        const float ox = (float) roundToInt (origin.x * backing) / backing;
        const float oy = (float) roundToInt (origin.y * backing) / backing;
        const Rectangle<float> dest (ox, oy, (float) skinWin.getWidth() / backing, (float) skinWin.getHeight() / backing);
        g.setImageResamplingQuality (Graphics::lowResamplingQuality);
        g.drawImage (skinWin, dest);
        { Graphics::ScopedSaveState ss (g); g.reduceClipRegion (fxClipLogical); g.drawImage (fxImg, dest); }
    }

    g.addTransform (AffineTransform::scale (scale).translated (origin.x, origin.y));
    g.setImageResamplingQuality (Graphics::highResamplingQuality);

    drawField (g);
    drawAmount (g);
    drawRt (g);
    drawKnob (g);
    drawMix (g);
    { drawButtons (g); drawValueBoxes (g); }
}

void DereverbEditor::drawField (Graphics& g)
{
    const float xL = 520.0f, xR = 1160.0f, y0 = 271.0f, hh = 78.0f;
    const float span = xR - xL, xf = xL + 0.60f * span;

    Graphics::ScopedSaveState ss (g);
    Path clip;
    clip.addRoundedRectangle (Rectangle<float> (510.0f, 184.0f, 652.0f, 176.0f), 72.0f);
    g.reduceClipRegion (clip);

    radialGlow (g, { xf, y0 }, 230.0f, cCyan, 0.07f + 0.13f * lvlOut);

    // lineas de campo que convergen en el punto de secado
    for (int k = 0; k < 7; ++k)
        for (int sgn = -1; sgn <= 1; sgn += 2)
        {
            const float d = (18.0f + 11.0f * (float) k) * (float) sgn;
            Path s;
            for (int i = 0; i <= 36; ++i)
            {
                const float u = (float) i / 36.0f;
                const float x = jmap (u, xL, xf);
                const float yy = y0 + d * std::pow (1.0f - u, 1.7f) * (1.0f + 0.07f * std::sin (u * 9.0f - animTime * 1.6f + (float) k));
                if (i == 0) s.startNewSubPath (x, yy); else s.lineTo (x, yy);
            }
            g.setColour (cCyan.withAlpha (0.07f + 0.11f * (1.0f - (float) k / 7.0f)));
            g.strokePath (s, PathStrokeType (1.2f));

            const float u = std::fmod (animTime * (0.16f + 0.02f * (float) k) + 0.137f * (float) k + (sgn > 0 ? 0.5f : 0.0f), 1.0f);
            const float x = jmap (u, xL, xf);
            const float yy = y0 + d * std::pow (1.0f - u, 1.7f);
            g.setColour (cIce.withAlpha (0.55f * std::sin (u * kPi)));
            g.fillEllipse (x - 1.7f, yy - 1.7f, 3.4f, 3.4f);
        }

    g.setColour (cCyan.withAlpha (0.12f));
    g.drawLine (xL, y0, xR, y0, 1.0f);

    // audio real: picos de entrada/salida (el mas nuevo, a la izquierda)
    const auto& tel = proc.telemetry;
    const int head = tel.head.load (std::memory_order_acquire);
    const float dx = span / (float) (histCols - 1);
    float inA[histCols], outA[histCols], outSm[histCols];
    int slots[histCols];
    for (int i = 0; i < histCols; ++i)
    {
        const int slot = ((head - 1 - i) % DereverbTelemetry::cols + DereverbTelemetry::cols) % DereverbTelemetry::cols;
        slots[i] = slot;
        inA[i]  = tel.inPeak[(size_t) slot].load (std::memory_order_relaxed);
        outA[i] = tel.outPeak[(size_t) slot].load (std::memory_order_relaxed);
    }
    for (int i = 0; i < histCols; ++i)
    {
        float acc = 0.0f, wsum = 0.0f;
        for (int k = -2; k <= 2; ++k)
        {
            const float w = 3.0f - (float) std::abs (k);
            acc += w * outA[jlimit (0, histCols - 1, i + k)];
            wsum += w;
        }
        outSm[i] = acc / wsum;
    }

    auto shape = [] (float a) { return std::pow (jlimit (0.0f, 1.0f, a * 2.4f), 0.6f); };

    Path spikes;
    for (int i = 0; i < histCols; ++i)
    {
        const float t = (float) i / (float) (histCols - 1);
        const float s = 1.0f - smooth (0.50f, 0.68f, t);
        if (s <= 0.01f)
            break;
        const float bl = smooth (0.10f, 0.55f, t);
        const float j1 = 0.62f + 0.38f * hash01 ((float) slots[i] * 1.37f);
        const float j2 = 0.62f + 0.38f * hash01 ((float) slots[i] * 2.11f + 5.0f);
        const float idle = 0.03f + 0.025f * hash01 ((float) i * 3.7f) * (0.5f + 0.5f * std::sin (animTime * 2.1f + (float) i));
        const float a = jmap (bl, shape (inA[i]), shape (outA[i]));
        const float x = xL + (float) i * dx;
        spikes.startNewSubPath (x, y0 - (a * j1 + idle) * s * hh);
        spikes.lineTo (x, y0 + (a * j2 + idle) * s * hh);
    }
    {
        const PathStrokeType flat1 (7.0f, PathStrokeType::mitered, PathStrokeType::butt);
        const PathStrokeType flat2 (3.4f, PathStrokeType::mitered, PathStrokeType::butt);
        const PathStrokeType flat3 (1.7f, PathStrokeType::mitered, PathStrokeType::butt);
        g.setColour (cCyan.withAlpha (0.10f));  g.strokePath (spikes, flat1);
        g.setColour (cCyan.withAlpha (0.38f));  g.strokePath (spikes, flat2);
        g.setColour (cIce.withAlpha (0.92f));   g.strokePath (spikes, flat3);
    }

    // onda de salida, suave
    Path wave;
    bool first = true;
    for (float x = xf - 80.0f; x <= xR; x += 3.0f)
    {
        const float t = (x - xL) / span;
        const int ci = jlimit (0, histCols - 1, (int) std::lround (t * (float) (histCols - 1)));
        const float env = smooth (0.46f, 0.70f, t) * (1.0f - 0.45f * smooth (0.86f, 1.0f, t));
        const float A = (0.06f + 0.60f * shape (outSm[ci])) * hh * env;
        const float yv = y0 + A * std::sin ((x - xf) * 0.052f - animTime * 7.0f)
                            + 0.35f * A * std::sin (x * 0.13f + animTime * 4.3f);
        if (first) { wave.startNewSubPath (x, yv); first = false; } else wave.lineTo (x, yv);
    }
    glowStroke (g, wave, cCyan, 2.4f, 0.95f);

    // punto de secado
    radialGlow (g, { xf, y0 }, 46.0f + 26.0f * lvlOut, cCyan, 0.40f + 0.30f * lvlOut);
    radialGlow (g, { xf, y0 }, 16.0f, Colours::white, 0.85f);
    ColourGradient fl (Colours::white.withAlpha (0.0f), xf - 70.0f, y0, Colours::white.withAlpha (0.0f), xf + 70.0f, y0, false);
    fl.addColour (0.5, cIce.withAlpha (0.55f));
    g.setGradientFill (fl);
    g.fillRect (xf - 70.0f, y0 - 1.0f, 140.0f, 2.0f);

    // escombros: lo que el algoritmo esta quitando
    static const Path shard = []
    {
        Path p;
        p.addTriangle (0.0f, -1.0f, 0.6f, 0.7f, -0.5f, 0.8f);
        return p;
    }();
    for (const auto& p : particles)
    {
        const float a = jlimit (0.0f, 1.0f, p.life / p.maxLife) * jmin (1.0f, (p.maxLife - p.life) * 6.0f + 0.2f);
        g.setColour ((p.bright ? cIce : cCyan).withAlpha (0.85f * a));
        g.fillPath (shard, AffineTransform::scale (p.size).rotated (p.rot).translated (p.pos.x, p.pos.y));
    }
}

void DereverbEditor::drawAmount (Graphics& g)
{
    const float p = prop (amountSlider);
    const float y = jmap (p, amtBottom, amtTop);
    const bool hover = amountSlider.isMouseOverOrDragging();

    const Rectangle<float> tube (amtX - 28.0f, 138.0f, 56.0f, 382.0f);
    Path tp;
    tp.addRoundedRectangle (tube, 28.0f);
    {
        Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (tp);

        for (int k = 0; k < 2; ++k)
        {
            Path h;
            bool first = true;
            const float ph = (float) k * kPi;
            for (float yy = tube.getY(); yy <= tube.getBottom(); yy += 5.0f)
            {
                const float x = amtX + (13.0f + 5.0f * lvlIn) * std::sin (yy * 0.05f - animTime * 2.3f + ph);
                if (first) { h.startNewSubPath (x, yy); first = false; } else h.lineTo (x, yy);
            }
            glowStroke (g, h, k == 0 ? cMagenta : cCyan, 2.2f, 0.45f + 0.35f * lvlIn);
        }

        for (const auto& b : bubbles)
        {
            const float bx = b.x + 5.0f * std::sin (animTime * 1.3f + b.phase);
            g.setColour (cIce.withAlpha (0.45f));
            g.drawEllipse (bx - b.r, b.y - b.r, b.r * 2.0f, b.r * 2.0f, 1.0f);
        }

        // "vacio" por encima de la esfera
        ColourGradient vg (Colour (0xff020813).withAlpha (0.84f), 0.0f, tube.getY(),
                           Colour (0xff020813).withAlpha (0.52f), 0.0f, y, false);
        g.setGradientFill (vg);
        g.fillRect (tube.getX(), tube.getY(), tube.getWidth(), y - tube.getY());

        radialGlow (g, { amtX, y }, 64.0f, cMagenta, 0.20f + 0.12f * lvlIn);
    }

    drawOrb (g, { amtX, y }, orbR, hover ? 1.0f : 0.0f, animTime, lvlIn);
}

void DereverbEditor::drawRt (Graphics& g)
{
    const float p = prop (rtSlider);
    const float x = jmap (p, rtLeft, rtRight);
    const bool hover = rtSlider.isMouseOverOrDragging();

    const Rectangle<float> glass (480.0f, 487.0f, 312.0f, 47.0f);
    Path gp;
    gp.addRoundedRectangle (glass, 23.0f);
    {
        Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (gp);

        // ondas del liquido (hasta la perilla)
        for (int k = 0; k < 3; ++k)
        {
            Path w;
            bool first = true;
            for (float xx = glass.getX(); xx <= x; xx += 4.0f)
            {
                const float env = smooth (glass.getX(), glass.getX() + 40.0f, xx);
                const float yv = rtY + env * (9.0f + 4.0f * lvlOut) * std::sin (xx * (0.045f + 0.012f * (float) k) - animTime * (2.4f + 0.7f * (float) k) + (float) k * 2.0f);
                if (first) { w.startNewSubPath (xx, yv); first = false; } else w.lineTo (xx, yv);
            }
            if (! first)
                glowStroke (g, w, k == 1 ? cIce : cCyan, 1.8f - 0.3f * (float) k, 0.55f);
        }

        // lo que queda a la derecha de la perilla esta "vacio"
        ColourGradient vg (Colour (0xff020a12).withAlpha (0.86f), x, 0.0f, Colour (0xff020a12).withAlpha (0.92f), glass.getRight(), 0.0f, false);
        g.setGradientFill (vg);
        g.fillRect (x, glass.getY(), glass.getRight() - x, glass.getHeight());
        radialGlow (g, { x, rtY }, 44.0f, cCyan, 0.25f);
    }

    drawCapThumb (g, { x, rtY }, 30.0f, 80.0f, hover ? 1.0f : 0.0f);
}

void DereverbEditor::drawKnob (Graphics& g)
{
    const float p = prop (floorSlider);
    const float a0 = 1.25f * kPi, a1 = 2.75f * kPi, a = a0 + p * (a1 - a0);
    const auto c = knobC;
    const bool hover = floorSlider.isMouseOverOrDragging();

    if (vortexImg.isValid())
    {
        const float S = (float) vortexImg.getWidth();
        g.drawImageTransformed (vortexImg, AffineTransform::translation (-S * 0.5f, -S * 0.5f)
                                                .scaled (1.0f / winPx).rotated (swirlAngle).translated (c.x, c.y));
    }
    radialGlow (g, c, 64.0f, cCyan, 0.05f + 0.22f * lvlRed + 0.08f * lvlOut);

    const float rr = 66.0f;
    Path track;
    track.addCentredArc (c.x, c.y, rr, rr, 0.0f, a0, a1, true);
    g.setColour (Colour (0xff021018).withAlpha (0.55f));
    g.strokePath (track, PathStrokeType (4.5f));

    Path arc;
    arc.addCentredArc (c.x, c.y, rr, rr, 0.0f, a0, jmax (a0 + 0.001f, a), true);
    glowStroke (g, arc, spectrum (p), 3.0f, 0.9f);

    // marcador triangular que apunta al centro
    Path tri;
    tri.addTriangle (0.0f, -70.0f, 8.5f, -88.0f, -8.5f, -88.0f);
    const auto xf = AffineTransform::rotation (a).translated (c.x, c.y);
    radialGlow (g, { c.x + std::sin (a) * 80.0f, c.y - std::cos (a) * 80.0f }, 22.0f, spectrum (p), 0.45f + 0.3f * (hover ? 1.0f : 0.0f));
    g.setColour (spectrum (p).brighter (0.3f));
    g.fillPath (tri, xf);
}

void DereverbEditor::drawMix (Graphics& g)
{
    const float p = prop (mixSlider);
    const float x = jmap (p, mixLeft, mixRight);
    const bool hover = mixSlider.isMouseOverOrDragging();

    const Rectangle<float> glass (1207.0f, 488.0f, 318.0f, 48.0f);
    Path gp;
    gp.addRoundedRectangle (glass, 24.0f);
    {
        Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (gp);

        // destello que recorre la parte encendida
        const float hx = glass.getX() + std::fmod (animTime * 150.0f, glass.getWidth() + 160.0f) - 80.0f;
        ColourGradient sh (Colours::white.withAlpha (0.0f), hx - 45.0f, 0.0f, Colours::white.withAlpha (0.0f), hx + 45.0f, 0.0f, false);
        sh.addColour (0.5, Colours::white.withAlpha (0.38f));
        g.setGradientFill (sh);
        g.fillRect (glass.getX(), glass.getY(), jmax (0.0f, x - glass.getX()), glass.getHeight());

        g.setColour (Colour (0xff030a14).withAlpha (0.88f));
        g.fillRect (x, glass.getY(), glass.getRight() - x, glass.getHeight());
        radialGlow (g, { x, mixY }, 40.0f, spectrum (p), 0.30f);
    }

    drawCapThumb (g, { x, mixY }, 30.0f, 82.0f, hover ? 1.0f : 0.0f);

    // sol: brilla segun cuanto efecto se oye
    const Point<float> sc (1503.0f, 441.0f);
    const float sa = 0.30f + 0.70f * p;
    radialGlow (g, sc, 26.0f, cSun, 0.30f * sa);
    g.setColour (cSun.withAlpha (sa));
    g.fillEllipse (sc.x - 6.5f, sc.y - 6.5f, 13.0f, 13.0f);
    for (int k = 0; k < 8; ++k)
    {
        const float ang = animTime * 0.5f + (float) k * kPi / 4.0f;
        const float r0 = 10.5f, r1 = 14.5f + 2.5f * std::sin (animTime * 2.0f + (float) k);
        g.drawLine (sc.x + std::cos (ang) * r0, sc.y + std::sin (ang) * r0,
                    sc.x + std::cos (ang) * r1, sc.y + std::sin (ang) * r1, 1.8f);
    }
}

void DereverbEditor::drawButtons (Graphics& g)
{
    const bool bypassed = bypassButton.getToggleState();
    const bool delta = deltaButton.getToggleState();

    if (bypassed)
    {
        g.setColour (Colour (0xff070f19).withAlpha (0.90f));
        g.fillEllipse (powerC.x - 21.0f, powerC.y - 21.0f, 42.0f, 42.0f);
        g.setColour (Colour (0xff6c7c8c));
        g.strokePath (powerIcon (powerC, 10.0f), PathStrokeType (2.2f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (Colour (0xff070f19).withAlpha (0.55f));
        g.fillRoundedRectangle (1200.0f, 96.0f, 62.0f, 26.0f, 8.0f);

        g.setColour (Colour (0xff070f19).withAlpha (0.88f));
        g.fillEllipse (ledC.x - 9.0f, ledC.y - 9.0f, 18.0f, 18.0f);
    }
    else
    {
        radialGlow (g, powerC, 34.0f, cCyan, 0.16f + 0.16f * (bypassButton.isMouseOver() ? 1.0f : 0.0f) + 0.08f * lvlOut);
        radialGlow (g, ledC, 16.0f, Colour (0xffb6ff3f), 0.34f + 0.12f * std::sin (animTime * 3.0f));
    }

    // boton rojo = Delta (escuchar lo que se quita)
    if (delta)
    {
        const float pulse = 0.5f + 0.5f * std::sin (animTime * 6.0f);
        radialGlow (g, deltaC, 46.0f, Colour (0xffff3b5c), 0.45f + 0.25f * pulse);
        g.setColour (Colour (0xffff7d92).withAlpha (0.9f));
        g.strokePath (powerIcon (deltaC, 11.0f), PathStrokeType (2.6f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (Colour (0xffff3b5c).withAlpha (0.5f * pulse));
        g.drawEllipse (deltaC.x - 25.0f, deltaC.y - 25.0f, 50.0f, 50.0f, 2.0f);
    }
    else
    {
        radialGlow (g, deltaC, 38.0f, Colour (0xffff3b5c), (deltaButton.isMouseOver() ? 0.25f : 0.0f) + 0.30f * peakLamp);
    }
}

std::pair<String, String> DereverbEditor::amountWords() const
{
    const double v = amountSlider.getValue();
    const String pct = String (roundToInt ((float) v * 100.0f)) + "%";
    const char* word = v < 0.05 ? "OFF" : (v < 0.95 ? "PARTIAL" : (v <= 1.05 ? "FULL" : "OVER"));
    return { pct, word };
}

void DereverbEditor::drawValueBoxes (Graphics& g)
{
    auto box = [&] (Rectangle<float> r, const Slider& s, const Label& l)
    {
        if (l.isBeingEdited())
            return;
        const auto [num, unit] = valueParts (s);
        const float h = r.getHeight();
        const auto big = techFont (h * 0.64f, true), small = techFont (h * 0.36f, false);
        const float wN = textWidth (big, num);
        const float wU = unit.isEmpty() ? 0.0f : textWidth (small, unit) + 6.0f;
        const float x = r.getCentreX() - (wN + wU) * 0.5f;
        glowText (g, num, { x, r.getY(), wN + 4.0f, h }, big, cIce, Justification::centredLeft, 1.0f);
        if (unit.isNotEmpty())
            glowText (g, unit, { x + wN + 6.0f, r.getY() + h * 0.07f, wU + 4.0f, h }, small, cCyan, Justification::centredLeft, 0.6f);
    };
    box (boxAmount, amountSlider, amountBox);
    box (boxRt, rtSlider, rtBox);
    box (boxFloor, floorSlider, floorBox);
    box (boxMix, mixSlider, mixBox);

    const auto [pct, word] = amountWords();
    glowText (g, pct, { 352.0f, 424.0f, 90.0f, 40.0f }, techFont (31.0f, true), cIce, Justification::centred, 1.0f);
    glowText (g, word, { 352.0f, 459.0f, 90.0f, 28.0f }, techFont (20.0f, false), cCyan, Justification::centred, 0.6f);
}

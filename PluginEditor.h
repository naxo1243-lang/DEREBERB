#pragma once

#include "PluginProcessor.h"
#include <memory>
#include <utility>
#include <vector>

// Interfaz "liquida": la ilustracion (assets/skin.jpg) es el fondo y la mascara
// (assets/fxmask.png) dice donde animar:
//   R = lineas / bordes luminosos, G = nucleo del plasma, B = rampa (0..1) para
//   pulsos que viajan. Encima se dibujan en vivo los controles y el campo de
//   secado, que muestra el audio real (telemetria del procesador).
class DereverbEditor : public juce::AudioProcessorEditor
{
public:
    explicit DereverbEditor (DereverbProcessor&);
    ~DereverbEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

   #if defined (DEREVERB_TESTING)
    void advanceForTesting (float dt)   { step (dt); renderFx (animTime); }
    void prepareForTesting()            { rebuildCaches(); }
    // posicion (coordenadas de diseno) que usa cada slider para un valor: 0 cantidad, 1 RT60, 2 mezcla
    double testSliderPos (int which, double v)
    {
        juce::Slider& s = which == 0 ? amountSlider : (which == 1 ? rtSlider : mixSlider);
        const double pos = s.getPositionOfValue (v);
        return which == 0 ? ((double) s.getY() + pos - origin.y) / scale : ((double) s.getX() + pos - origin.x) / scale;
    }
   #endif

private:
    struct HitLookAndFeel;

    enum FxMode { fxTank, fxTitle, fxGlyph, fxRim, fxFiber, fxTentacles, fxDrop, fxLedCyan, fxLedPink };

    struct FxRegion
    {
        int mode = 0;
        juce::Rectangle<float> design;               // coordenadas de diseno
        float warp = 0.0f;                           // ondulacion maxima (px de diseno)
        juce::Rectangle<int> px;                     // en pixeles FX
        std::vector<uint8_t> mr, mg, mb, feather;    // px.getWidth() * px.getHeight()
    };

    struct Particle
    {
        juce::Point<float> pos, vel;
        float life = 0, maxLife = 1, size = 4, rot = 0, spin = 0;
        bool bright = false;
    };

    struct Bubble { float x, y, r, speed, phase; };

    static constexpr float designW = 1628.0f, designH = 656.0f;
    static constexpr int   histCols = 240;

    void tick();
    void step (float dt);
    void rebuildCaches();
    void renderFx (float t);
    void spawnParticle();
    void commitBox (juce::Label&, juce::Slider&);
    void refreshBoxes();

    // dibujo vectorial en coordenadas de diseno
    void drawField (juce::Graphics&);
    void drawAmount (juce::Graphics&);
    void drawRt (juce::Graphics&);
    void drawKnob (juce::Graphics&);
    void drawMix (juce::Graphics&);
    void drawButtons (juce::Graphics&);
    void drawValueBoxes (juce::Graphics&);

    juce::Rectangle<int> toReal (juce::Rectangle<float>) const;
    float prop (const juce::Slider&) const;
    std::pair<juce::String, juce::String> valueParts (const juce::Slider&) const;
    std::pair<juce::String, juce::String> amountWords() const;

    DereverbProcessor& proc;
    std::unique_ptr<HitLookAndFeel> lnf;

    juce::Slider amountSlider, rtSlider, floorSlider, mixSlider;
    juce::ToggleButton bypassButton, deltaButton;
    juce::Label amountBox, rtBox, floorBox, mixBox;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SliderAttachment> amountAttach, rtAttach, floorAttach, mixAttach;
    std::unique_ptr<ButtonAttachment> bypassAttach, deltaAttach;

    juce::TooltipWindow tooltip { this, 700 };

    // imagenes
    juce::Image skinSrc, maskSrc;
    juce::Image skinWin;        // fondo a resolucion de pantalla
    juce::Image skinFx, fxImg;  // muestreo y salida de efectos (resolucion FX)
    juce::Image vortexImg;      // disco del remolino (se hace girar)
    std::vector<FxRegion> regions;
    std::vector<float> trig;
    juce::RectangleList<int> fxClipLogical;
    float winPx = 1.0f, fxPx = 1.0f, fxQuality = 1.0f, backing = 1.0f;
    int cacheW = 0, cacheH = 0;
    bool cachesDirty = true;

    // geometria de la ventana
    float scale = 1.0f;
    juce::Point<float> origin;

    // animacion y niveles
    double lastTime = 0.0;
    float animTime = 0.0f, fxAccum = 0.0f, swirlAngle = 0.0f;
    float lvlIn = 0.0f, lvlOut = 0.0f, lvlRed = 0.0f;
    float peakLamp = 0.0f;
    float fxMsAvg = 0.0f;
    double lastQualityChange = 0.0;
    juce::Random rng { 20241014 };
    std::vector<Particle> particles;
    std::vector<Bubble> bubbles;
    float spawnAcc = 0.0f;
    int lastHead = 0;
    double lastHeadTime = 0.0;

    // el VBlank va el ultimo: se destruye primero y detiene los callbacks
    juce::VBlankAttachment vblank;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DereverbEditor)
};

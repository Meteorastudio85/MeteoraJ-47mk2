#pragma once
// J-47 MK2 - Copyright (c) 2026 Meteora Plugins. Tutti i diritti riservati.
// Vedi LICENSE.txt. Componenti di terze parti: vedi THIRD_PARTY_NOTICES.txt.
#include "PluginProcessor.h"
#include <array>
#include <memory>
#include <vector>

// i pomelli sono quelli della foto: lo slider resta invisibile, l'editor disegna solo la lancetta
class TapeInvisibleLnF : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override {}
};

class MeteoraTapeEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MeteoraTapeEditor (MeteoraTapeProcessor&);
    ~MeteoraTapeEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void timerCallback() override;
    juce::String valueText (int i) const;
    float P (const char* id) const { return proc.P (id); }
    void setParam (const char* id, float plain);
    void stepChannel (int delta);
    void linkOutput (float newIn);
    void drawSprite (juce::Graphics&, const juce::Image&, float cx, float cy, float angleRad) const;
    void drawReel (juce::Graphics&, const juce::Image& scaled, float cx, float cy, float angleRad) const;

    MeteoraTapeProcessor& proc;
    TapeInvisibleLnF lnf;
    juce::Image bg, bgScaled, reelL, lever, playOn, stopOn, glass;
    std::array<juce::Image, 3> reelR;
    // bobine gia' alla grandezza della finestra: ruotarle costa poco e il movimento resta fluido
    juce::Image reelLScaled;
    std::array<juce::Image, 3> reelRScaled;
    float bgK = 1.0f;
    bool sizeReady = false;
    std::array<juce::Slider, 6> knobs;          // INPUT, OUTPUT, DRY/WET, HISS, LO-FI, BIAS
    std::vector<std::unique_ptr<juce::SliderParameterAttachment>> atts;
    int lastKnob = -1, showText = 0;
    juce::String flash;
    float glow = 0.0f, inNeedle = -40.0f, outNeedle = -40.0f, satRed = 0.0f;
    float reelAngleL = 0.0f, reelAngleR = 0.6f, reelSpeed = 0.0f, leverAngle = 0.0f;
    double lastFrameMs = 0.0;
    void onVBlank();
    juce::VBlankAttachment vblank { this, [this] { onVBlank(); } };
    // INPUT e OUTPUT collegati: alzando l'ingresso l'uscita scende della stessa quantita'
    bool linkDrag = false;
    float linkIn0 = 0.0f, linkOut0 = 0.0f, prevIn = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeteoraTapeEditor)
};

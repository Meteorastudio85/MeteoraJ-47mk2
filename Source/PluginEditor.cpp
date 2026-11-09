// J-47 MK2 - Copyright (c) 2026 Meteora Plugins. Tutti i diritti riservati.
// Vedi LICENSE.txt. Componenti di terze parti: vedi THIRD_PARTY_NOTICES.txt.
#include "PluginEditor.h"
#include <BinaryData.h>
#include <cmath>

namespace
{
    constexpr float kW = 1405.0f, kH = 1405.0f;              // finestra quadrata
    constexpr int kDefaultWidth = 900;
    constexpr const char* kSizeKey = "uiWidthSq";

    // pomelli: centro (area del mouse), centro e raggio della faccia (dove gira la lancetta), corsa
    struct Knob { const char* id; const char* name; float cx, cy, r, fx, fy, fr, sweep; };
    const Knob kKnobs[6] = {
        { "in",      "Input Level",   294.0f,  964.0f, 60.0f,  291.0f,  962.0f, 46.0f, 135.0f },
        { "out",     "Output Level", 1100.0f,  962.0f, 62.0f, 1098.0f,  960.0f, 47.0f, 135.0f },
        { "mix",     "Dry/Wet",       358.0f,  725.0f, 52.0f,  356.0f,  722.0f, 42.0f, 135.0f },
        { "hiss",    "Hiss",         1041.0f,  723.0f, 54.0f, 1039.0f,  720.0f, 43.0f, 135.0f },
        { "lofiamt", "Lo-Fi",        1159.0f,  712.0f, 32.0f, 1158.0f,  710.0f, 25.0f, 135.0f },
        { "bias",    "Bias",          614.0f, 1121.0f, 27.0f,  613.0f, 1119.5f, 25.7f, 135.0f },
    };
    // pulsante di accensione e tasti PLAY / STOP (RECORD e' solo decorativo, sempre acceso)
    constexpr float kPwrX = 247.0f, kPwrY = 1125.0f, kPwrR = 23.0f;
    const juce::Rectangle<float> kPlay (343.0f, 1102.0f, 53.0f, 45.0f), kStop (411.0f, 1102.0f, 50.0f, 45.0f);
    // VU: perno sotto il quadrante, finestra del quadrante
    const float kVuX[2] = { 566.0f, 834.0f };
    constexpr float kVuY = 1040.0f;
    const juce::Rectangle<float> kVuWin[2] = { { 458.0f, 902.0f, 216.0f, 98.0f }, { 726.0f, 902.0f, 216.0f, 98.0f } };
    // bobine (sprite tondi che girano) e leva della velocita'
    constexpr float kReelLX = 380.0f, kReelRX = 1020.0f, kReelY = 329.0f;
    constexpr float kLevX = 1101.0f, kLevY = 1126.0f, kLevBase = -18.0f;
    const float kLevAngle[3] = { -39.0f, -2.0f, 34.0f };
    // contagiri del canale e suo tastino (sopra = su, sotto = giu')
    const float kDigX[3] = { 829.0f, 847.5f, 866.0f };
    constexpr float kDigY = 1122.0f;
    const juce::Rectangle<float> kChSwitch (906.0f, 1105.0f, 24.0f, 37.0f), kCounter (814.0f, 1100.0f, 120.0f, 46.0f);
    const juce::Rectangle<float> kReadout (170.0f, 612.0f, 260.0f, 26.0f);
}

MeteoraTapeEditor::MeteoraTapeEditor (MeteoraTapeProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    bg     = juce::ImageCache::getFromMemory (BinaryData::ui_jpg,          BinaryData::ui_jpgSize);
    reelL  = juce::ImageCache::getFromMemory (BinaryData::reelL_png,       BinaryData::reelL_pngSize);
    reelR[0] = juce::ImageCache::getFromMemory (BinaryData::reelR_75_png,  BinaryData::reelR_75_pngSize);
    reelR[1] = juce::ImageCache::getFromMemory (BinaryData::reelR_15_png,  BinaryData::reelR_15_pngSize);
    reelR[2] = juce::ImageCache::getFromMemory (BinaryData::reelR_30_png,  BinaryData::reelR_30_pngSize);
    lever  = juce::ImageCache::getFromMemory (BinaryData::speed_lever_png, BinaryData::speed_lever_pngSize);
    playOn = juce::ImageCache::getFromMemory (BinaryData::btn_play_on_png, BinaryData::btn_play_on_pngSize);
    stopOn = juce::ImageCache::getFromMemory (BinaryData::btn_stop_on_png, BinaryData::btn_stop_on_pngSize);
    glass  = juce::ImageCache::getFromMemory (BinaryData::vu_glass_png,    BinaryData::vu_glass_pngSize);

    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto& s = knobs[i];
        s.setLookAndFeel (&lnf);
        s.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s.setMouseDragSensitivity (240);
        addAndMakeVisible (s);
        atts.push_back (std::make_unique<juce::SliderParameterAttachment> (*proc.apvts.getParameter (kKnobs[i].id), s));
        s.onValueChange = [this, i]
        {
            lastKnob = (int) i; showText = 60; flash.clear();
            if (i == 0) linkOutput ((float) knobs[0].getValue());
            repaint();
        };
    }
    // doppio clic = posizione di partenza
    knobs[0].setDoubleClickReturnValue (true, 0.0);  knobs[1].setDoubleClickReturnValue (true, 0.0);
    knobs[2].setDoubleClickReturnValue (true, 100.0); knobs[3].setDoubleClickReturnValue (true, 0.0);
    knobs[4].setDoubleClickReturnValue (true, 0.0);  knobs[5].setDoubleClickReturnValue (true, 0.0);
    knobs[0].onDragStart = [this]
    {
        linkDrag = true; linkIn0 = P ("in"); linkOut0 = P ("out");
        if (auto* o = proc.apvts.getParameter ("out")) o->beginChangeGesture();
    };
    knobs[0].onDragEnd = [this]
    {
        linkDrag = false;
        if (auto* o = proc.apvts.getParameter ("out")) o->endChangeGesture();
    };
    prevIn = P ("in");
    leverAngle = kLevAngle[juce::jlimit (0, 2, (int) std::lround (P ("ips")))];

    const int w = juce::jlimit (560, 1405, (int) proc.apvts.state.getProperty (kSizeKey, kDefaultWidth));
    setResizable (true, true);
    setResizeLimits (560, 560, 1405, 1405);
    getConstrainer()->setFixedAspectRatio (1.0);
    setSize (w, w);
    sizeReady = true;
    resized();
    startTimerHz (30);
}

MeteoraTapeEditor::~MeteoraTapeEditor()
{
    for (auto& s : knobs) s.setLookAndFeel (nullptr);
}

void MeteoraTapeEditor::resized()
{
    if (getWidth() <= 0 || getHeight() <= 0) return;
    if (sizeReady) proc.apvts.state.setProperty (kSizeKey, getWidth(), nullptr);
    bgK = juce::jmax (1.0f, juce::Component::getApproximateScaleFactorForComponent (this));
    bgScaled = bg.rescaled (juce::roundToInt ((float) getWidth() * bgK), juce::roundToInt ((float) getHeight() * bgK),
                            juce::Graphics::highResamplingQuality);
    const float s = (float) getWidth() / kW;
    auto prescale = [this, s] (const juce::Image& img)
    {
        const int n = juce::jmax (8, juce::roundToInt ((float) img.getWidth() * s * bgK));
        return img.rescaled (n, n, juce::Graphics::highResamplingQuality);
    };
    reelLScaled = prescale (reelL);
    for (size_t i = 0; i < 3; ++i) reelRScaled[i] = prescale (reelR[i]);
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        const auto& k = kKnobs[i];
        knobs[i].setBounds (juce::Rectangle<float> ((k.cx - k.r) * s, (k.cy - k.r) * s, 2.0f * k.r * s, 2.0f * k.r * s).toNearestInt());
    }
}

void MeteoraTapeEditor::setParam (const char* id, float plain)
{
    if (auto* prm = proc.apvts.getParameter (id))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
        prm->endChangeGesture();
    }
}

void MeteoraTapeEditor::stepChannel (int delta)
{
    const int c = juce::jlimit (1, 64, (int) std::lround (P ("channel")) + delta);
    setParam ("channel", (float) c);
    flash = "Channel   " + juce::String (c); lastKnob = -1; showText = 60;
    repaint();
}

void MeteoraTapeEditor::linkOutput (float newIn)
{
    auto* o = proc.apvts.getParameter ("out");
    if (o == nullptr) { prevIn = newIn; return; }
    if (linkDrag)
        o->setValueNotifyingHost (o->convertTo0to1 (juce::jlimit (-24.0f, 12.0f, linkOut0 - (newIn - linkIn0))));
    else if (knobs[0].isMouseOver())
    {
        o->beginChangeGesture();
        o->setValueNotifyingHost (o->convertTo0to1 (juce::jlimit (-24.0f, 12.0f, P ("out") - (newIn - prevIn))));
        o->endChangeGesture();
    }
    prevIn = newIn;
}

juce::String MeteoraTapeEditor::valueText (int i) const
{
    const double v = knobs[(size_t) i].getValue();
    const juce::String id (kKnobs[i].id);
    if (id == "mix" || id == "hiss" || id == "lofiamt" || id == "bias") return juce::String ((int) std::lround (v)) + " %";
    return (v >= 0.0 ? "+" : "") + juce::String (v, 1) + " dB";
}

// immagine (centrata su cx, cy) ruotata; le immagini sono alla scala dello sfondo originale
void MeteoraTapeEditor::drawSprite (juce::Graphics& g, const juce::Image& img, float cx, float cy, float angleRad) const
{
    const float s = (float) getWidth() / kW;
    const float iw = (float) img.getWidth(), ih = (float) img.getHeight();
    g.drawImageTransformed (img, juce::AffineTransform::translation (-iw * 0.5f, -ih * 0.5f).scaled (s)
                                     .rotated (angleRad).translated (cx * s, cy * s));
}

// bobina gia' ridimensionata: solo rotazione (filtro bilineare, veloce e senza scalini)
void MeteoraTapeEditor::drawReel (juce::Graphics& g, const juce::Image& img, float cx, float cy, float angleRad) const
{
    const float s = (float) getWidth() / kW;
    const float iw = (float) img.getWidth(), ih = (float) img.getHeight();
    g.drawImageTransformed (img, juce::AffineTransform::translation (-iw * 0.5f, -ih * 0.5f).rotated (angleRad)
                                     .scaled (1.0f / bgK).translated (cx * s, cy * s));
}

void MeteoraTapeEditor::paint (juce::Graphics& g)
{
    const float s = (float) getWidth() / kW;
    auto sc = [s] (float v) { return v * s; };
    auto rect = [s] (const juce::Rectangle<float>& r) { return juce::Rectangle<float> (r.getX() * s, r.getY() * s, r.getWidth() * s, r.getHeight() * s); };
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImageTransformed (bgScaled, juce::AffineTransform::scale (1.0f / bgK));

    // bobine: girano con il nastro; quella di destra cambia colore con la velocita' (oro, blu, rossa)
    const int ips = juce::jlimit (0, 2, (int) std::lround (P ("ips")));
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
        drawReel (g, reelLScaled, kReelLX, kReelY, reelAngleL);
        drawReel (g, reelRScaled[(size_t) ips], kReelRX, kReelY, reelAngleR);
    }

    // lancette dei pomelli: riga scura incisa sulla faccia
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        const auto& k = kKnobs[i];
        const double prop = knobs[i].valueToProportionOfLength (knobs[i].getValue());
        const float a = juce::degreesToRadians (-k.sweep + 2.0f * k.sweep * (float) prop);
        auto pt = [&] (float rr) { return juce::Point<float> (sc (k.fx + std::sin (a) * rr), sc (k.fy - std::cos (a) * rr)); };
        const juce::Point<float> hl (sc (0.5f), sc (0.8f));
        const float wdt = sc (k.fr > 35.0f ? 3.2f : 2.4f);
        g.setColour (juce::Colours::white.withAlpha (0.25f));
        g.drawLine (juce::Line<float> (pt (0.38f * k.fr) + hl, pt (0.86f * k.fr) + hl), wdt);
        g.setColour (juce::Colour (0xff1e1c19));
        g.drawLine (juce::Line<float> (pt (0.38f * k.fr), pt (0.86f * k.fr)), wdt);
    }

    // leva del selettore velocita'
    drawSprite (g, lever, kLevX, kLevY, juce::degreesToRadians (leverAngle - kLevBase));

    // tasti: PLAY acceso quando il nastro va, STOP acceso quando e' fermo, RECORD rosso quando il nastro satura
    const bool playing = P ("play") > 0.5f;
    const float on = glow;
    auto lamp = [&] (const juce::Image& img, const juce::Rectangle<float>& r, float k, juce::Colour halo)
    {
        if (k <= 0.003f) return;
        juce::Graphics::ScopedSaveState ss (g);
        g.setOpacity (juce::jlimit (0.0f, 1.0f, k));
        g.drawImage (img, rect (r));
        g.setOpacity (1.0f);
        const auto rr = rect (r).expanded (sc (6.0f));
        juce::ColourGradient cg (halo.withAlpha (0.28f * k), rr.getCentreX(), rr.getCentreY(),
                                 halo.withAlpha (0.0f), rr.getRight(), rr.getCentreY(), true);
        g.setGradientFill (cg);
        g.fillRoundedRectangle (rr, sc (6.0f));
    };
    lamp (playOn, kPlay, playing ? on : 0.0f, juce::Colour (0xffffd9a0));
    lamp (stopOn, kStop, playing ? 0.0f : on, juce::Colour (0xffffd9a0));
    // RECORD: sempre acceso rosso, nessuna funzione (e' gia' cosi' nello sfondo)

    // pulsante di accensione: anello di luce calda quando il plugin e' acceso
    if (P ("power") > 0.5f)
    {
        juce::ColourGradient cg (juce::Colour (0xffffc070).withAlpha (0.0f), sc (kPwrX), sc (kPwrY),
                                 juce::Colour (0xffffc070).withAlpha (0.0f), sc (kPwrX + kPwrR + 8.0f), sc (kPwrY), true);
        cg.addColour (0.62, juce::Colour (0xffffc070).withAlpha (0.0f));
        cg.addColour (0.78, juce::Colour (0xffffb050).withAlpha (0.45f * (0.4f + 0.6f * on)));
        g.setGradientFill (cg);
        g.fillEllipse (sc (kPwrX - kPwrR - 8.0f), sc (kPwrY - kPwrR - 8.0f), sc (2.0f * kPwrR + 16.0f), sc (2.0f * kPwrR + 16.0f));
    }

    // VU: lancette vere (escono da sotto il quadrante), quadranti spenti quando il plugin non lavora
    for (int m = 0; m < 2; ++m)
    {
        const auto win = rect (kVuWin[m]);
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (win.toNearestInt());
        const float vu = m == 0 ? inNeedle : outNeedle;
        const float p = juce::jlimit (0.0f, 1.12f, std::pow (10.0f, vu / 20.0f) / 1.4125f);
        const float a = juce::degreesToRadians (-48.0f + 96.0f * (p - 0.0708f) / 0.9292f);
        auto pt = [&] (float rr) { return juce::Point<float> (sc (kVuX[m] + std::sin (a) * rr), sc (kVuY - std::cos (a) * rr)); };
        const juce::Point<float> off (sc (1.2f), sc (1.4f));
        g.setColour (juce::Colours::black.withAlpha (0.22f));
        g.drawLine (juce::Line<float> (pt (30.0f) + off, pt (97.0f) + off), sc (1.5f));
        g.setColour (juce::Colour (0xff20160e));
        g.drawLine (juce::Line<float> (pt (30.0f), pt (97.0f)), sc (1.5f));
        if (glow < 0.999f)
        {
            g.setColour (juce::Colour (0xff120d08).withAlpha (0.62f * (1.0f - glow)));
            g.fillRect (win);
        }
        // quando il nastro satura i quadranti diventano rossi
        if (satRed * glow > 0.003f)
        {
            const float k = satRed * glow;
            juce::ColourGradient cg (juce::Colour (0xffeb2a1e).withAlpha (0.55f * k), win.getCentreX(), win.getCentreY(),
                                     juce::Colour (0xffb01208).withAlpha (0.30f * k), win.getRight(), win.getCentreY(), true);
            g.setGradientFill (cg);
            g.fillRect (win);
        }
        // vetro: riflesso, filo di luce, ombra della cornice e un po' di polvere
        g.drawImage (glass, win);
    }

    // contagiri: numero del canale, cifre chiare come un contagiri meccanico
    {
        const int ch = juce::jlimit (1, 64, (int) std::lround (P ("channel")));
        const juce::String txt = juce::String (ch).paddedLeft ('0', 3);
        g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), sc (19.0f), juce::Font::bold)));
        for (int i = 0; i < 3; ++i)
        {
            const juce::Rectangle<float> cell (sc (kDigX[i] - 7.5f), sc (kDigY - 10.0f), sc (15.0f), sc (20.0f));
            g.setColour (juce::Colour (0xff1e1e1e));
            g.fillRect (cell);
            g.setColour (juce::Colour (0xffece8dc));
            g.drawText (txt.substring (i, i + 1), cell, juce::Justification::centred, false);
        }
    }

    // valore del comando che si sta toccando
    if (showText > 0 && (lastKnob >= 0 || flash.isNotEmpty()))
    {
        const auto d = rect (kReadout);
        g.setColour (juce::Colour (0xcc14130f));
        g.fillRoundedRectangle (d, sc (5.0f));
        g.setColour (juce::Colour (0xffe8c49a));
        g.setFont (sc (16.0f));
        const juce::String txt = flash.isNotEmpty() ? flash : juce::String (kKnobs[lastKnob].name) + "   " + valueText (lastKnob);
        g.drawText (txt, d.toNearestInt(), juce::Justification::centred);
    }
}

void MeteoraTapeEditor::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        const std::array<int, 6> sizes { 600, 700, 800, 900, 1100, 1300 };
        juce::PopupMenu m;
        for (int i = 0; i < (int) sizes.size(); ++i)
            m.addItem (i + 1, "Size " + juce::String (sizes[(size_t) i]) + " x " + juce::String (sizes[(size_t) i]),
                       true, std::abs (getWidth() - sizes[(size_t) i]) < 4);
        juce::Component::SafePointer<MeteoraTapeEditor> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [safe, sizes] (int r)
        {
            if (safe == nullptr || r <= 0) return;
            safe->setSize (sizes[(size_t) (r - 1)], sizes[(size_t) (r - 1)]);
        });
        return;
    }
    const auto p = e.position / ((float) getWidth() / kW);
    if (kPlay.contains (p))      { setParam ("play", 1.0f); flash = "Play"; }
    else if (kStop.contains (p)) { setParam ("play", 0.0f); flash = "Stop"; }
    else if (p.getDistanceFrom ({ kPwrX, kPwrY }) < kPwrR + 4.0f)
    {
        setParam ("power", P ("power") > 0.5f ? 0.0f : 1.0f);
        flash = juce::String ("Power   ") + (P ("power") > 0.5f ? "On" : "Off");
    }
    else if (kChSwitch.contains (p)) { stepChannel (p.y < kChSwitch.getCentreY() ? 1 : -1); return; }
    else if (p.getDistanceFrom ({ kLevX, kLevY }) < 55.0f)
    {
        const float ang = juce::radiansToDegrees (std::atan2 (p.x - kLevX, -(p.y - kLevY)));
        const int v = ang < -20.0f ? 0 : (ang > 16.0f ? 2 : 1);
        setParam ("ips", (float) v);
        flash = juce::String ("Tape Speed   ") + (v == 0 ? "7.5" : v == 1 ? "15" : "30") + " IPS";
    }
    else return;
    lastKnob = -1; showText = 60;
    repaint();
}

void MeteoraTapeEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const auto p = e.position / ((float) getWidth() / kW);
    if (kCounter.contains (p) && std::abs (w.deltaY) > 0.0f)
        stepChannel (w.deltaY > 0.0f ? 1 : -1);
}

void MeteoraTapeEditor::timerCallback()
{
    const bool running = proc.isRunning();
    const int ips = juce::jlimit (0, 2, (int) std::lround (P ("ips")));
    bool dirty = false;
    if (showText > 0 && --showText == 0) dirty = true;
    const float tgtA = kLevAngle[ips];
    if (std::abs (tgtA - leverAngle) > 0.05f) { leverAngle += (tgtA - leverAngle) * 0.35f; dirty = true; }

    const float gPrev = glow;
    glow += ((running ? 1.0f : 0.0f) - glow) * 0.12f;
    if (std::abs (glow - gPrev) > 0.003f) dirty = true;

    // VU rossi quando il nastro satura: da +6 VU sul nastro, pieni a +14 VU
    {
        const float t = running ? juce::jlimit (0.0f, 1.0f, (proc.inVu.load() - 6.0f) / 8.0f) : 0.0f;
        const float prev = satRed;
        satRed += (t - satRed) * (t > satRed ? 0.5f : 0.08f);
        if (std::abs (satRed - prev) > 0.003f) dirty = true;
    }
    // lancette con la balistica dei VU (~300 ms)
    const float iT = running ? proc.inVu.load() : -40.0f, oT = running ? proc.outVu.load() : -40.0f;
    const float iPrev = inNeedle, oPrev = outNeedle;
    inNeedle += (iT - inNeedle) * 0.22f;
    outNeedle += (oT - outNeedle) * 0.22f;
    if (std::abs (inNeedle - iPrev) > 0.02f || std::abs (outNeedle - oPrev) > 0.02f) dirty = true;

    if (dirty) repaint();
}

// bobine: a ogni aggiornamento dello schermo, con il tempo vero trascorso -> movimento fluido e giri completi.
// Il nastro scorre a velocita' costante: la bobina piena (sinistra) gira piano, quella quasi vuota (destra) piu' veloce.
void MeteoraTapeEditor::onVBlank()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const float dt = lastFrameMs > 0.0 ? (float) juce::jlimit (0.0, 0.1, (now - lastFrameMs) / 1000.0) : 0.0f;
    lastFrameMs = now;
    if (dt <= 0.0f) return;
    const bool running = proc.isRunning();
    const int ips = juce::jlimit (0, 2, (int) std::lround (P ("ips")));
    const float target = running ? (ips == 0 ? 0.225f : ips == 1 ? 0.45f : 0.9f) * proc.tapeSpeed.load() : 0.0f;   // giri/s
    reelSpeed += (target - reelSpeed) * juce::jmin (1.0f, dt * 6.0f);                                               // inerzia
    if (reelSpeed < 0.0005f) return;
    const float twoPi = juce::MathConstants<float>::twoPi;
    reelAngleL = std::fmod (reelAngleL - reelSpeed * twoPi * dt, twoPi);           // senso antiorario, come in riproduzione
    reelAngleR = std::fmod (reelAngleR - reelSpeed * 2.2f * twoPi * dt, twoPi);    // ognuna con il suo angolo: nessun salto
    const float sl = (float) getWidth() / kW;
    repaint (juce::Rectangle<float> (94.0f * sl, 43.0f * sl, 572.0f * sl, 573.0f * sl).getSmallestIntegerContainer());
    repaint (juce::Rectangle<float> (733.0f * sl, 42.0f * sl, 574.0f * sl, 575.0f * sl).getSmallestIntegerContainer());
}

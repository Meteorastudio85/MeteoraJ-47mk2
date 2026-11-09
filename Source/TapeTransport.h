#pragma once
// J-47 MK2 - Copyright (c) 2026 Meteora Plugins. Tutti i diritti riservati. Vedi LICENSE.txt.
// ---------------------------------------------------------------------------
// STOP / PLAY del nastro: con STOP il nastro rallenta fino a fermarsi (il suono scende di tono e si spegne),
// con PLAY riparte (il suono sale di tono fino alla velocita' giusta) e poi si riallinea al tempo reale
// con una dissolvenza breve. In PLAY normale il segnale passa senza ritardo.
// Il buffer si alloca solo in prepare(): niente allocazioni durante l'audio.
// ---------------------------------------------------------------------------
#include <vector>
#include <cmath>
#include <algorithm>

class TapeTransport
{
public:
    void prepare (double sampleRate, bool playing)
    {
        sr = sampleRate;
        size = (int) (sr * 6.5) + 8;                    // STOP fino a 5 s + margine
        buf.assign ((size_t) size * 2, 0.0f);
        w = 0; t = 0.0; readPos = 0.0; xf = 0.0;
        mode = playing ? Playing : Stopped;
        speed = playing ? 1.0 : 0.0;
    }
    void setTimes (double stopSeconds, double startSeconds)
    {
        stopS = std::clamp (stopSeconds, 0.05, 5.0);
        startS = std::clamp (startSeconds, 0.05, 3.0);
    }
    void setPlaying (bool play)
    {
        if (play && (mode == Stopping || mode == Stopped))
        {
            if (mode == Stopped) readPos = (double) w;
            mode = Starting;
            t = std::pow (std::max (speed, 0.0), 1.0 / 1.4);         // riparte dalla velocita' attuale
        }
        else if (! play && (mode == Playing || mode == Starting || mode == Catching))
        {
            if (mode == Playing) readPos = (double) w;
            mode = Stopping;
            t = 1.0 - std::pow (std::max (speed, 0.0), 1.0 / 1.6);
        }
    }
    double currentSpeed() const { return speed; }
    bool isSilent() const { return mode == Stopped; }

    void process (float& L, float& R)
    {
        buf[(size_t) w * 2] = L; buf[(size_t) w * 2 + 1] = R;
        switch (mode)
        {
            case Playing: speed = 1.0; break;
            case Stopped: speed = 0.0; L = R = 0.0f; break;
            case Stopping:
            {
                t += 1.0 / (stopS * sr);
                if (t >= 1.0) { mode = Stopped; speed = 0.0; L = R = 0.0f; break; }
                speed = std::pow (1.0 - t, 1.6);                     // rallenta come un motore che si spegne
                readAt (L, R);
                readPos += speed;
                break;
            }
            case Starting:
            {
                t += 1.0 / (startS * sr);
                speed = std::min (1.0, std::pow (std::min (t, 1.0), 1.4));
                readAt (L, R);
                readPos += speed;
                if (t >= 1.0) { mode = Catching; xf = 0.0; }
                break;
            }
            case Catching:                                          // dal nastro "in ritardo" al tempo reale
            {
                float dL, dR; readAt (dL, dR);
                readPos += 1.0;
                xf += 1.0 / (0.03 * sr);
                const double k = std::min (1.0, xf);
                L = (float) (dL + (L - dL) * k); R = (float) (dR + (R - dR) * k);
                if (k >= 1.0) mode = Playing;
                break;
            }
        }
        w = (w + 1) % size;
    }

private:
    enum Mode { Playing, Stopping, Stopped, Starting, Catching };

    // lettura con interpolazione cubica; a velocita' quasi zero il suono sfuma (niente "tac" finale)
    void readAt (float& L, float& R)
    {
        while (readPos >= (double) size) readPos -= (double) size;
        while (readPos < 0.0) readPos += (double) size;
        const int i1 = (int) readPos; const double f = readPos - (double) i1;
        const int i0 = (i1 - 1 + size) % size, i2 = (i1 + 1) % size, i3 = (i1 + 2) % size;
        auto cub = [&] (int c)
        {
            const double y0 = buf[(size_t) i0 * 2 + c], y1 = buf[(size_t) i1 * 2 + c], y2 = buf[(size_t) i2 * 2 + c], y3 = buf[(size_t) i3 * 2 + c];
            const double c1 = 0.5 * (y2 - y0), c2 = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3, c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
            return ((c3 * f + c2) * f + c1) * f + y1;
        };
        const double g = std::min (1.0, speed / 0.06);
        L = (float) (cub (0) * g); R = (float) (cub (1) * g);
    }

    std::vector<float> buf;
    double sr = 44100.0, stopS = 1.5, startS = 0.5, t = 0.0, readPos = 0.0, xf = 0.0, speed = 1.0;
    int size = 1, w = 0;
    Mode mode = Playing;
};

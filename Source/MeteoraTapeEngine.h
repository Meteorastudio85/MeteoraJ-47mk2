#pragma once
// J-47 MK2 - Copyright (c) 2026 Meteora Plugins. Tutti i diritti riservati.
// Vedi LICENSE.txt. Componenti di terze parti: vedi THIRD_PARTY_NOTICES.txt.
// ---------------------------------------------------------------------------
// Motore di J-47 MK2: ToTape9 di Airwindows + voce "J37" scritta per questo
// plugin (valvola del registratore, curva per ogni velocita'), hiss ispirato al
// Revox B77 MkIII, dry/wet allineato, misure per i VU, canali del multitraccia.
// ---------------------------------------------------------------------------
#include "AirwindowsToTape9.h"
#include <vector>
#include <cmath>
#include <cstdint>
#include <algorithm>

struct TapeSpeedVoice
{
    double tilt, shape, flutter, flutSpd, bias, bump, bumpHz;  // parametri di ToTape9
    double valve;                                                // seconda armonica della valvola (J37)
    double lowpassHz;                                            // morbidezza degli alti in riproduzione (0 = nessuno)
    double hissDb, hissLowpassHz;                                // hiss al 100% (dBFS RMS) e suo colore
};

// 0 = 7.5 IPS, 1 = 15 IPS, 2 = 30 IPS
inline const TapeSpeedVoice kTapeVoices[3] = {
    // tilt  shape flutter flutSpd bias  bump  bumpHz  valve  lowpass  hiss   hissLP
    { 0.47, 0.50, 0.42,   0.42,   0.58, 0.27,  50.0, 0.080, 16000.0, -50.0, 11000.0 },   // 7.5 IPS: bump ~+3.5 dB a 35 Hz
    { 0.50, 0.50, 0.36,   0.50,   0.54, 0.31,  75.0, 0.060, 19000.0, -54.0, 15000.0 },   // 15 IPS:  bump ~+3.5 dB a 45 Hz
    { 0.53, 0.50, 0.30,   0.58,   0.51, 0.33, 110.0, 0.045,     0.0, -57.0, 19000.0 },   // 30 IPS:  bump ~+3 dB a 60 Hz
};

// Canale del registratore multitraccia: ogni canale ha il suo nastro, un filo diverso dagli altri
// (alti, bump dei bassi, valvola, oscillazione, fruscio). Il canale 1 e' il suono di riferimento: identico a prima.
struct TapeChannel
{
    double tilt = 0, valve = 1, bump = 0, bumpHz = 1, lp = 1, flut = 0, fspd = 1, hissDb = 0, driveDb = 0;
    uint32_t seed = 0x9E3779B9u;

    static TapeChannel forChannel (int n)
    {
        TapeChannel c;
        if (n <= 1) return c;
        uint32_t h = 0x811C9DC5u ^ (uint32_t) (n * 2654435761u);
        auto u = [&h]() { h ^= h << 13; h ^= h >> 17; h ^= h << 5; return (double) h / 4294967295.0 * 2.0 - 1.0; };   // -1..1
        for (int i = 0; i < 4; ++i) u();
        c.tilt = 0.012 * u();   c.valve = 1.0 + 0.25 * u();  c.bump = 0.025 * u();  c.bumpHz = 1.0 + 0.08 * u();
        c.lp = 1.0 + 0.07 * u(); c.flut = 0.05 * u();         c.fspd = 1.0 + 0.10 * u(); c.hissDb = 1.5 * u();
        c.driveDb = 0.6 * u();  c.seed = h | 1u;
        return c;
    }
};

class MeteoraTapeEngine
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        tape.prepare (sr);
        latency = tape.latencySamples();
        dryBuf.assign (64, 0.0); dryPos = 0;
        dcL = dcR = dcXL = dcXR = 0.0; lpL = lpR = 0.0;
        for (auto& v : hs) v = 0.0;
        rng = 0x9E3779B9u;
        buildHissNorm();
        buildBrokenSounds();
        voicePos = -1;
        // LO-FI: passa-basso a ~8.5 kHz e passa-alto a ~70 Hz (12 dB/ottava)
        {
            const double f = std::min (8500.0, sr * 0.45), w = 2.0 * pi * f / sr, al = std::sin (w) / (2.0 * 0.7071), c = std::cos (w);
            loLP.set ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
            const double f2 = 70.0, w2 = 2.0 * pi * f2 / sr, al2 = std::sin (w2) / (2.0 * 0.7071), c2 = std::cos (w2);
            loHP.set ((1 + c2) / 2, -(1 + c2), (1 + c2) / 2, 1 + al2, -2 * c2, 1 - al2);
        }
        lastBias = 99.0; designBiasShelf (0.0);
    }
    int latencySamples() const { return latency; }

    // biasN: -1 = UNDER (piu' brillante e piu' distorto), 0 = taratura normale, +1 = OVER (piu' scuro e pulito).
    // lofi: 0..1 (gia' ammorbidito dal processore) = nastro consumato tipo cassetta.
    // Con biasN = 0 e lofi = 0 il suono e' identico alla versione precedente.
    void setParams (double inDb, double outDb, double mix01, double hissPct, int speed, double biasN = 0.0, double lofi = 0.0,
                    const TapeChannel& ch = TapeChannel(), int channel = 1)
    {
        speed = std::clamp (speed, 0, 2);
        TapeSpeedVoice v = kTapeVoices[speed];
        // carattere del canale (sul canale 1 non cambia niente)
        v.tilt += ch.tilt; v.valve *= ch.valve; v.bump = std::clamp (v.bump + ch.bump, 0.0, 1.0); v.bumpHz *= ch.bumpHz;
        if (v.lowpassHz > 0.0) v.lowpassHz = std::min (v.lowpassHz * ch.lp, sr * 0.45);
        v.flutter = std::clamp (v.flutter + ch.flut, 0.0, 1.0); v.flutSpd = std::clamp (v.flutSpd * ch.fspd, 0.0, 1.0);
        v.hissDb += ch.hissDb; inDb += ch.driveDb;
        biasN = std::clamp (biasN, 0.0, 1.0);          // BIAS: solo OVER, 0-100%
        lofiAmt = std::clamp (lofi, 0.0, 1.0);
        inG = std::pow (10.0, inDb / 20.0);
        // spinta in piu' nel nastro (compensata all'uscita del nastro): bias sotto = piu' distorsione, lo-fi = un filo di piu'
        const double satDb = -2.0 * biasN + 2.0 * lofiAmt;
        satG = std::pow (10.0, satDb / 20.0);
        const double flut = std::clamp (v.flutter + 0.22 * lofiAmt, 0.0, 1.0);
        const double flutSpd = std::clamp (v.flutSpd * (1.0 - 0.30 * lofiAmt), 0.0, 1.0);
        const double biasP = std::clamp (v.bias + 0.12 * biasN, 0.0, 1.0);
        if (biasN != lastBias) { lastBias = biasN; designBiasShelf (-3.0 * biasN); }
        hissPct = std::min (100.0, hissPct + 40.0 * lofiAmt);
        outG = std::pow (10.0, outDb / 20.0);
        mix = std::clamp (mix01, 0.0, 1.0);
        valve = v.valve;
        tape.setParams (0.5, v.tilt, v.shape, flut, flutSpd, biasP, v.bump,
                        std::sqrt (std::max (0.0, (v.bumpHz - 25.0) / 175.0)), 0.5 * outG / satG);
        lpCoef = v.lowpassHz > 0.0 ? 1.0 - std::exp (-2.0 * pi * v.lowpassHz / sr) : 1.0;
        if (speed + 3 * channel != hissSpeed) { hissSpeed = speed + 3 * channel; rng = ch.seed; designHiss (v.hissLowpassHz); }
        const double h = std::clamp (hissPct, 0.0, 100.0) / 100.0;
        hissGain = h <= 0.0 ? 0.0 : std::pow (10.0, v.hissDb / 20.0) * h * h * outG;
        dcCoef = 1.0 - (2.0 * pi * 7.0 / sr);
    }

    void triggerBroken (int which) { if (which >= 1 && which <= 3) { voice = which - 1; voicePos = 0; } }

    // run = 1 suona attraverso il nastro, 0 = bypass (STOP). Tra i due c'e' una dissolvenza.
    void process (float* x, double run)
    {
        // dry ritardato quanto il nastro, cosi' dry/wet e bypass sono allineati
        const double inL = x[0], inR = x[1];
        dryBuf[(dryPos * 2) % 64] = inL; dryBuf[(dryPos * 2 + 1) % 64] = inR;
        const int rd = ((dryPos - latency) % 32 + 32) % 32;
        const double dL = dryBuf[rd * 2], dR = dryBuf[rd * 2 + 1];
        dryPos = (dryPos + 1) % 32;

        double L = inL * inG, R = inR * inG;
        inSq = std::max (L * L, R * R); inPk = std::max (std::fabs (L), std::fabs (R));
        if (satG != 1.0) { L *= satG; R *= satG; }

        // valvola del registratore J37: un po' di seconda armonica, poi blocco della continua
        L += valve * L * L; R += valve * R * R;
        L = dcHP (L, dcL, dcXL); R = dcHP (R, dcR, dcXR);

        tape.process (L, R);

        lpL += (L - lpL) * lpCoef; lpR += (R - lpR) * lpCoef;
        L = lpL; R = lpR;

        // BIAS: acuti un po' piu' aperti (UNDER) o piu' chiusi (OVER), come in riproduzione
        if (biasShelfOn) { L = biasShelf.process (L, 0); R = biasShelf.process (R, 1); }
        // LO-FI: banda stretta tipo cassetta, inserita con una dissolvenza (niente click)
        {
            const double fL = loHP.process (loLP.process (L, 0), 0), fR = loHP.process (loLP.process (R, 1), 1);
            if (lofiAmt > 0.0) { L += (fL - L) * lofiAmt; R += (fR - R) * lofiAmt; }
        }

        if (hissGain > 0.0) { L += hissSample (0) * hissGain; R += hissSample (1) * hissGain; }

        const double wetL = dL * outG * (1.0 - mix) + L * mix;
        const double wetR = dR * outG * (1.0 - mix) + R * mix;
        double oL = dL * (1.0 - run) + wetL * run;
        double oR = dR * (1.0 - run) + wetR * run;

        if (voicePos >= 0)
        {
            const auto& b = broken[(size_t) voice];
            if (voicePos < (int) b.size()) { oL += b[(size_t) voicePos]; oR += b[(size_t) voicePos] * 0.9; ++voicePos; }
            else voicePos = -1;
        }
        outSq = std::max (oL * oL, oR * oR); outPk = std::max (std::fabs (oL), std::fabs (oR));
        x[0] = (float) oL; x[1] = (float) oR;
    }

    // ultime misure (per campione): quadrato e picco prima del nastro e in uscita
    double inSq = 0, inPk = 0, outSq = 0, outPk = 0;
    double satG = 1.0;

    // biquad semplice per BIAS e LO-FI
    struct BQ
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1[2] {}, z2[2] {};
        double process (double x, int c) { const double y = b0 * x + z1[c]; z1[c] = b1 * x - a1 * y + z2[c]; z2[c] = b2 * x - a2 * y; return y; }
        void set (double nb0, double nb1, double nb2, double a0, double na1, double na2) { b0 = nb0 / a0; b1 = nb1 / a0; b2 = nb2 / a0; a1 = na1 / a0; a2 = na2 / a0; }
    };

private:
    static constexpr double pi = 3.14159265358979323846;

    void designBiasShelf (double gainDb)
    {
        biasShelfOn = std::abs (gainDb) > 0.01;
        const double A = std::pow (10.0, gainDb / 40.0), w = 2.0 * pi * std::min (5000.0, sr * 0.45) / sr;
        const double al = std::sin (w) / (2.0 * 0.7071), c = std::cos (w), sA = 2.0 * std::sqrt (A) * al;
        biasShelf.set (A * ((A + 1) + (A - 1) * c + sA), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sA),
                       (A + 1) - (A - 1) * c + sA, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sA);
    }
    BQ biasShelf, loLP, loHP;
    bool biasShelfOn = false;
    double lastBias = 99.0, lofiAmt = 0.0;

    double dcHP (double x, double& state, double& last)
    {
        // passa-alto a 1 polo a ~7 Hz: y = x - x[-1] + c*y[-1]
        const double y = x - last + dcCoef * state;
        last = x; state = y;
        return y;
    }
    double white()
    {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return (double) rng / 4294967295.0 * 2.0 - 1.0;
    }
    // hiss: rumore bianco, tolti i bassi sotto ~400 Hz, un po' di presenza verso i 5 kHz, alti chiusi secondo la velocita'
    void designHiss (double lpHz)
    {
        hpC = std::exp (-2.0 * pi * 400.0 / sr);
        const double f = std::min (lpHz, sr * 0.45);
        const double K = std::tan (pi * f / sr), q = 0.7071, n = 1.0 / (1.0 + K / q + K * K);
        lp[0] = K * K * n; lp[1] = 2 * lp[0]; lp[2] = lp[0]; lp[3] = 2 * (K * K - 1) * n; lp[4] = (1 - K / q + K * K) * n;
        presC = std::exp (-2.0 * pi * 5000.0 / sr);
        hissNorm = 1.0;
        double s = 0.0; const int N = (int) sr;
        for (int i = 0; i < N; ++i) { const double v = hissSample (0); s += v * v; }
        hissNorm = 1.0 / std::sqrt (std::max (s / N, 1.0e-12));
        for (auto& v : hs) v = 0.0;
    }
    double hissSample (int ch)
    {
        double* st = hs + ch * 6;
        const double w = white();
        const double hp = w - st[0] + hpC * st[1]; st[0] = w; st[1] = hp;          // passa-alto
        st[2] = hp + presC * (st[2] - hp);                                          // passa-basso a 5 kHz
        const double pres = hp + 0.6 * (hp - st[2]);                                // presenza
        const double out = pres * lp[0] + st[3];                                    // passa-basso finale
        st[3] = pres * lp[1] - out * lp[3] + st[4];
        st[4] = pres * lp[2] - out * lp[4];
        return out * hissNorm;
    }
    void buildHissNorm() { hissSpeed = -1; }

    // suoni dei tasti rotti (REC, REW, FF), generati una volta sola
    void buildBrokenSounds()
    {
        uint32_t r = 12345u;
        auto rnd = [&r]() { r ^= r << 13; r ^= r >> 17; r ^= r << 5; return (double) r / 4294967295.0 * 2.0 - 1.0; };
        auto len = [this] (double s) { return (size_t) (s * sr); };
        for (int k = 0; k < 3; ++k)
        {
            std::vector<double> b (len (0.75), 0.0);
            double bp1 = 0, bp2 = 0;
            auto click = [&] (double t0, double dur, double gain, double fc)
            {
                const double c = std::exp (-2.0 * pi * fc / sr);
                for (size_t i = len (t0); i < std::min (b.size(), len (t0 + dur)); ++i)
                {
                    const double e = std::exp (-(double) (i - len (t0)) / (dur * sr * 0.25));
                    const double w = rnd();
                    bp1 = w + c * (bp1 - w); bp2 = (w - bp1);
                    b[i] += bp2 * e * gain;
                }
            };
            auto thump = [&] (double t0, double f, double dur, double gain)
            {
                for (size_t i = len (t0); i < std::min (b.size(), len (t0 + dur)); ++i)
                {
                    const double t = (double) (i - len (t0)) / sr;
                    b[i] += std::sin (2 * pi * f * t * (1.0 + 0.6 * std::exp (-t / 0.01))) * std::exp (-t / (dur * 0.3)) * gain;
                }
            };
            if (k == 0)   // REC: il tasto scatta a vuoto, il relè batte e qualcosa sbatacchia
            {
                click (0.000, 0.006, 0.55, 2500); thump (0.004, 68, 0.12, 0.45);
                click (0.090, 0.004, 0.18, 4500); click (0.150, 0.003, 0.12, 5200); click (0.230, 0.003, 0.07, 4800);
            }
            else          // REW e FF: il motore prova a partire, gratta e si pianta
            {
                const double f0 = k == 1 ? 140.0 : 175.0, f1 = k == 1 ? 330.0 : 410.0;
                click (0.000, 0.005, 0.45, 2500);
                double ph = 0.0;
                for (size_t i = len (0.01); i < len (0.32); ++i)
                {
                    const double t = (double) i / sr - 0.01, u = t / 0.31;
                    const double f = f0 + (f1 - f0) * std::sqrt (u) * (1.0 - 0.35 * u);
                    ph += 2 * pi * f / sr;
                    const double grind = 0.55 + 0.45 * std::sin (2 * pi * 23.0 * t) * rnd();
                    const double env = std::min (1.0, t / 0.04) * (u > 0.85 ? (1.0 - u) / 0.15 : 1.0);
                    b[i] += (std::sin (ph) * 0.6 + std::sin (ph * 2.01) * 0.25 + rnd() * 0.25) * grind * env * 0.22;
                }
                thump (0.33, 60, 0.10, 0.40); click (0.33, 0.006, 0.40, 2200);
                click (0.42, 0.003, 0.12, 5000); click (0.47, 0.003, 0.08, 4600);
            }
            double pk = 1e-9; for (double v : b) pk = std::max (pk, std::fabs (v));
            for (double& v : b) v *= 0.25 / pk;                                    // picco a circa -12 dBFS
            broken[(size_t) k] = b;
        }
    }

    double sr = 44100.0;
    AirwindowsToTape9 tape;
    int latency = 1;
    std::vector<double> dryBuf; int dryPos = 0;
    double inG = 1, outG = 1, mix = 1, valve = 0.06, lpCoef = 1.0, dcCoef = 0.999;
    double dcL = 0, dcR = 0, dcXL = 0, dcXR = 0, lpL = 0, lpR = 0;
    uint32_t rng = 0x9E3779B9u;
    double hs[12] {}, hpC = 0, presC = 0, lp[5] {}, hissNorm = 1, hissGain = 0;
    int hissSpeed = -1;
    std::vector<double> broken[3];
    int voice = 0, voicePos = -1;
};

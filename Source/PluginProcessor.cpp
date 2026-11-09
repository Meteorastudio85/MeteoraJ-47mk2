// J-47 MK2 - Copyright (c) 2026 Meteora Plugins. Tutti i diritti riservati.
// Vedi LICENSE.txt. Componenti di terze parti: vedi THIRD_PARTY_NOTICES.txt.
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <array>
#include <cmath>

namespace
{
    // canali usati dalle copie del plugin aperte in questo momento: una copia nuova prende il primo canale libero
    std::array<std::atomic<int>, 65> gChannelUse {};
    int firstFreeChannel()
    {
        for (int c = 1; c <= 64; ++c)
            if (gChannelUse[(size_t) c].load() <= 0) return c;
        return 1;
    }
}

MeteoraTapeProcessor::MeteoraTapeProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    // nuova copia su una nuova traccia: canale successivo libero (1, 2, 3...)
    myChannel = firstFreeChannel();
    gChannelUse[(size_t) myChannel]++;
    if (auto* p = apvts.getParameter ("channel"))
        p->setValueNotifyingHost (p->convertTo0to1 ((float) myChannel));
    apvts.addParameterListener ("channel", this);
}

MeteoraTapeProcessor::~MeteoraTapeProcessor()
{
    apvts.removeParameterListener ("channel", this);
    gChannelUse[(size_t) myChannel]--;
}

void MeteoraTapeProcessor::parameterChanged (const juce::String& id, float v)
{
    if (id != "channel") return;
    const int c = juce::jlimit (1, 64, (int) std::lround (v));
    if (c == myChannel) return;
    gChannelUse[(size_t) myChannel]--;
    gChannelUse[(size_t) c]++;
    myChannel = c;
}

juce::AudioProcessorValueTreeState::ParameterLayout MeteoraTapeProcessor::createLayout()
{
    using F = juce::AudioParameterFloat;
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    juce::NormalisableRange<float> inR (-12.0f, 24.0f);  inR.setSkewForCentre (0.0f);
    juce::NormalisableRange<float> outR (-24.0f, 12.0f); outR.setSkewForCentre (0.0f);
    // stessi ID delle versioni precedenti: i vecchi progetti si riaprono con le loro impostazioni
    l.add (std::make_unique<F> (juce::ParameterID { "in", 1 },   "Input Level",  inR, 0.0f));
    l.add (std::make_unique<F> (juce::ParameterID { "mix", 1 },  "Dry/Wet",      juce::NormalisableRange<float> (0.0f, 100.0f), 100.0f));
    l.add (std::make_unique<F> (juce::ParameterID { "out", 1 },  "Output Level", outR, 0.0f));
    l.add (std::make_unique<F> (juce::ParameterID { "hiss", 1 }, "Hiss",         juce::NormalisableRange<float> (0.0f, 100.0f), 0.0f));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "ips", 1 }, "Tape Speed", juce::StringArray { "7.5 IPS", "15 IPS", "30 IPS" }, 1));
    l.add (std::make_unique<F> (juce::ParameterID { "bias", 1 }, "Bias",         juce::NormalisableRange<float> (0.0f, 100.0f), 0.0f));
    // nuovi in questa versione (partono neutri: i vecchi progetti suonano uguali)
    l.add (std::make_unique<F> (juce::ParameterID { "lofiamt", 2 }, "Lo-Fi",     juce::NormalisableRange<float> (0.0f, 100.0f), 0.0f));
    l.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "channel", 2 }, "Channel", 1, 64, 1));
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "play", 2 },  "Play", true));
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "power", 2 }, "Power", true));
    l.add (std::make_unique<F> (juce::ParameterID { "stoptime", 2 },  "Stop Time",  juce::NormalisableRange<float> (0.2f, 5.0f), 1.5f));
    l.add (std::make_unique<F> (juce::ParameterID { "starttime", 2 }, "Start Time", juce::NormalisableRange<float> (0.1f, 3.0f), 0.5f));
    return l;
}

bool MeteoraTapeProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto& out = l.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono())
           && out == l.getMainInputChannelSet();
}

void MeteoraTapeProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    engine.prepare (sr);
    transport.prepare (sr, P ("play") > 0.5f);
    setLatencySamples (engine.latencySamples());
    for (auto* s : { &inSm, &outSm, &mixSm, &biasSm }) s->reset (sr, 0.03);
    runSm.reset (sr, 0.05);
    inSm.setCurrentAndTargetValue (P ("in"));   outSm.setCurrentAndTargetValue (P ("out"));
    mixSm.setCurrentAndTargetValue (P ("mix")); biasSm.setCurrentAndTargetValue (P ("bias"));
    runSm.setCurrentAndTargetValue (P ("power") > 0.5f ? 1.0f : 0.0f);
    lofiSm = P ("lofiamt") / 100.0;
}

void MeteoraTapeProcessor::processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    bypassed.store (false);
    lastProcessMs.store (juce::Time::getMillisecondCounter());
    const int n = buf.getNumSamples(), nch = juce::jmin (2, buf.getNumChannels());
    if (n == 0 || nch == 0) return;

    // LO-FI entra ed esce morbido (filtri e oscillazione senza click)
    {
        const double target = P ("lofiamt") / 100.0;
        const double step = (double) n / (0.08 * sr);
        lofiSm += juce::jlimit (-step, step, target - lofiSm);
    }
    inSm.setTargetValue (P ("in")); outSm.setTargetValue (P ("out"));
    mixSm.setTargetValue (P ("mix")); biasSm.setTargetValue (P ("bias"));
    runSm.setTargetValue (P ("power") > 0.5f ? 1.0f : 0.0f);
    transport.setTimes (P ("stoptime"), P ("starttime"));
    transport.setPlaying (P ("play") > 0.5f);

    const int channel = juce::jlimit (1, 64, (int) std::lround (P ("channel")));
    const TapeChannel ch = TapeChannel::forChannel (channel);
    const int ips = (int) std::lround (P ("ips"));
    const double hiss = P ("hiss");

    float* l = buf.getWritePointer (0);
    float* r = nch > 1 ? buf.getWritePointer (1) : nullptr;
    double inS = 0, outS = 0;
    constexpr int kSub = 32;                      // i comandi si aggiornano ogni 32 campioni: niente "zipper"
    for (int start = 0; start < n; start += kSub)
    {
        const int len = juce::jmin (kSub, n - start);
        engine.setParams (inSm.getCurrentValue(), outSm.getCurrentValue(), mixSm.getCurrentValue() / 100.0, hiss, ips,
                          biasSm.getCurrentValue() / 100.0, lofiSm, ch, channel);
        inSm.skip (len); outSm.skip (len); mixSm.skip (len); biasSm.skip (len);
        for (int i = start; i < start + len; ++i)
        {
            const double run = runSm.getNextValue();
            float x[2] = { l[i], r != nullptr ? r[i] : l[i] };
            engine.process (x, run);
            inS += engine.inSq;
            // STOP / PLAY del nastro (con il plugin spento passa tutto com'e')
            float tL = x[0], tR = x[1];
            transport.process (tL, tR);
            x[0] = (float) (x[0] + (tL - x[0]) * run);
            x[1] = (float) (x[1] + (tR - x[1]) * run);
            outS += (double) juce::jmax (x[0] * x[0], x[1] * x[1]);
            l[i] = x[0];
            if (r != nullptr) r[i] = x[1];
        }
    }
    inVu.store ((float) (10.0 * std::log10 (juce::jmax (inS / n, 1.0e-12)) + 18.0));
    outVu.store ((float) (10.0 * std::log10 (juce::jmax (outS / n, 1.0e-12)) + 18.0));
    tapeSpeed.store ((float) transport.currentSpeed());
}

juce::AudioProcessorEditor* MeteoraTapeProcessor::createEditor() { return new MeteoraTapeEditor (*this); }

void MeteoraTapeProcessor::getStateInformation (juce::MemoryBlock& d)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, d);
}

void MeteoraTapeProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    auto tree = juce::ValueTree::fromXml (*xml);
    auto find = [&tree] (const juce::String& id) -> juce::ValueTree
    {
        for (auto c : tree) if (c.getProperty ("id").toString() == id) return c;
        return {};
    };
    auto addParam = [&tree] (const juce::String& id, double v)
    {
        juce::ValueTree p ("PARAM"); p.setProperty ("id", id, nullptr); p.setProperty ("value", v, nullptr);
        tree.appendChild (p, nullptr);
    };
    // progetti fatti con le versioni precedenti: suonano come prima
    if (! find ("channel").isValid()) addParam ("channel", 1.0);         // canale 1 = suono di sempre
    if (! find ("play").isValid())    addParam ("play", 1.0);
    if (! find ("power").isValid())   addParam ("power", 1.0);
    if (! find ("lofiamt").isValid())                                    // vecchia levetta LO-FI accesa = 100%
    {
        auto old = find ("lofi");
        addParam ("lofiamt", old.isValid() && (double) old.getProperty ("value") > 0.5 ? 100.0 : 0.0);
    }
    if (auto b = find ("bias"); b.isValid() && (double) b.getProperty ("value") < 0.0)
        b.setProperty ("value", 0.0, nullptr);                           // il vecchio BIAS UNDER non c'e' piu'
    apvts.replaceState (tree);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MeteoraTapeProcessor(); }

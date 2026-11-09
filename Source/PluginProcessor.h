#pragma once
// J-47 MK2 - Copyright (c) 2026 Meteora Plugins. Tutti i diritti riservati.
// Vedi LICENSE.txt. Componenti di terze parti: vedi THIRD_PARTY_NOTICES.txt.
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include "MeteoraTapeEngine.h"
#include "TapeTransport.h"

class MeteoraTapeProcessor : public juce::AudioProcessor,
                             private juce::AudioProcessorValueTreeState::Listener
{
public:
    MeteoraTapeProcessor();
    ~MeteoraTapeProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlockBypassed;
    void processBlockBypassed (juce::AudioBuffer<float>& b, juce::MidiBuffer& m) override
    {
        bypassed.store (true);
        lastProcessMs.store (juce::Time::getMillisecondCounter());
        juce::AudioProcessor::processBlockBypassed (b, m);
    }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }   // "J-47 MK2"
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // acceso = la DAW fa passare l'audio, il plugin non e' in bypass e il pulsante di accensione e' acceso
    bool isRunning() const
    {
        return ! bypassed.load() && P ("power") > 0.5f
               && juce::Time::getMillisecondCounter() - lastProcessMs.load() < 500u;
    }
    float P (const char* id) const { return apvts.getRawParameterValue (id)->load(); }

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> inVu { -60.0f }, outVu { -60.0f };    // VU: 0 VU = -18 dBFS
    std::atomic<float> tapeSpeed { 1.0f };                    // 0 = nastro fermo, 1 = velocita' normale (per le bobine)
    std::atomic<juce::uint32> lastProcessMs { 0 };
    std::atomic<bool> bypassed { false };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void parameterChanged (const juce::String& id, float newValue) override;

    MeteoraTapeEngine engine;
    TapeTransport transport;
    double sr = 44100.0, lofiSm = 0.0;
    juce::SmoothedValue<float> inSm, outSm, mixSm, biasSm, runSm;
    int myChannel = 1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeteoraTapeProcessor)
};

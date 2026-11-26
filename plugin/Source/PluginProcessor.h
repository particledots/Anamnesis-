// The plugin wrapper. It talks to Logic, reads the control values and Logic's tempo and passes them to the engine.
// It contains no sound-making code.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <anamnesis/Engine.h>
#include "Parameters.h"

class AnamnesisAudioProcessor : public juce::AudioProcessor
{
public:
    AnamnesisAudioProcessor();
    ~AnamnesisAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return engine.tailSeconds(); }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getValueTreeState() { return apvts; }
    anm::Params readParams() const;                                    // what the engine is given
    const anm::Engine& getEngine() const { return engine; }

    // Panel actions
    void tapSize (double secondsBetweenTaps);                         // Tap: sets Size (Free) to the time between two taps
    void clearNow() { engine.requestClear(); }

    // The panel's zoom (saved with the project). Index into kZooms.
    static constexpr int kNumZooms = 5;
    static constexpr float kZooms[kNumZooms] = { 0.75f, 1.0f, 1.25f, 1.5f, 2.0f };
    static constexpr int kDefaultZoom = 2;
    int getZoom() const { return zoom.load(); }
    void setZoom (int z) { zoom = juce::jlimit (0, kNumZooms - 1, z); }

private:
    std::shared_ptr<AnmParams::ReadoutContext> readout;
    juce::AudioProcessorValueTreeState apvts;
    anm::Engine engine;
    std::atomic<int> zoom { kDefaultZoom };
    bool lastClear = false;
    std::atomic<double> hostBpm { 120.0 }, hostBeatsPerBar { 4.0 };     // from Logic (read by the panel too)
    std::atomic<double> hostBeats { -1.0 };                              // Logic's position and transport
    std::atomic<bool> hostPlaying { false };

    // Fast access to the current control values (audio thread).
    std::atomic<float> *pLevel {}, *pRepeats {}, *pSize {}, *pSizeMode {}, *pModify {}, *pSpread {}, *pScan {}, *pScanMode {};
    std::atomic<float> *pBank {}, *pModifier {}, *pRouting {}, *pEngage {}, *pLoop {}, *pScanHold {}, *pClear {};
    std::atomic<float> *pCollect {}, *pWipe {}, *pAlways {}, *pLatch {}, *pDryKill {}, *pTrails {}, *pMemoryDry {}, *pWide {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnamnesisAudioProcessor)
};

// The plugin wrapper. It talks to Logic, reads the control values and Logic's tempo and passes them to the engine.
// It contains no sound-making code.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <anamnesis/Engine.h>
#include "Parameters.h"

class AnamnesisAudioProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    AnamnesisAudioProcessor();
    ~AnamnesisAudioProcessor() override { stopTimer(); }

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

    // Presets (milestone 3). Two snapshots kept inside the plugin and saved with the project. The Preset switch picks
    // Left / Live / Right: Left and Right play their snapshot at once (also when the switch is automated, with no panel),
    // and the controls then move to show it; Live brings back the settings from before. An empty slot plays Live.
    // A snapshot holds the sound and the options (not Engage, Loop, Hold to scan, Clear memory, the Preset switch, Zoom).
    static constexpr int kSnapshotSize = 19;
    static const char* const kSnapshotIds[kSnapshotSize];
    void saveSlot (int slot);                                         // 0 = Save left, 1 = Save right: the current settings
    bool slotFilled (int slot) const { return slot >= 0 && slot < 2 && filled[slot].load(); }
    float slotValue (int slot, int index) const { return snap[slot][index].load(); }
    // Moves the controls to the chosen snapshot (or back to Live). Runs on its own on the message thread; public for the check.
    void applyPresetNow();
    bool presetApplied() const { return appliedGen.load() == gen.load(); }
    // The panel's zoom (saved with the project). Index into kZooms.
    static constexpr int kNumZooms = 5;
    static constexpr float kZooms[kNumZooms] = { 0.75f, 1.0f, 1.25f, 1.5f, 2.0f };
    static constexpr int kDefaultZoom = 2;
    int getZoom() const { return zoom.load(); }
    void setZoom (int z) { zoom = juce::jlimit (0, kNumZooms - 1, z); }

private:
    void timerCallback() override { applyPresetNow(); }
    void followPresetSwitch();                                        // audio thread: notices the Preset switch moving
    void valuesFrom (const std::atomic<float>* store, anm::Params&) const;
    juce::String slotText (const std::atomic<float>* store) const;
    void slotFromText (std::atomic<float>* store, const juce::String&, bool& ok) const;

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
    std::atomic<float> *pPreset {};

    // Presets: the two snapshots, the Live settings kept while a snapshot plays, which one plays now, and a counter that says
    // whether the controls already show it (gen == appliedGen) or the engine is still reading the store directly.
    std::atomic<float>* pSnap[kSnapshotSize] {};
    std::atomic<float> snap[2][kSnapshotSize];
    std::atomic<float> live[kSnapshotSize];
    std::atomic<bool> filled[2] { false, false };
    std::atomic<int> presetPlaying { anm::PresetLive };
    std::atomic<int> gen { 0 }, appliedGen { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnamnesisAudioProcessor)
};

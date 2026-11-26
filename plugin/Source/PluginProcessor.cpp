#include "PluginProcessor.h"
#include "PluginEditor.h"

AnamnesisAudioProcessor::AnamnesisAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      readout (std::make_shared<AnmParams::ReadoutContext>()),
      apvts (*this, nullptr, "Parameters", AnmParams::createLayout (readout))
{
    using namespace AnmParams;
    auto p = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    pLevel = p (level); pRepeats = p (repeats); pSize = p (size); pSizeMode = p (sizeMode); pModify = p (modify);
    pSpread = p (spread); pScan = p (scan); pScanMode = p (scanMode); pBank = p (bank); pModifier = p (modifier);
    pRouting = p (routing); pEngage = p (engage); pLoop = p (loop); pScanHold = p (scanHold); pClear = p (clearMemory);
    pCollect = p (collect); pWipe = p (wipe); pAlways = p (always); pLatch = p (latch); pDryKill = p (dryKill);
    pTrails = p (trails); pMemoryDry = p (memoryDry); pWide = p (wideSpread);
    readout->bankValue = pBank;
    readout->modifierValue = pModifier;
}

anm::Params AnamnesisAudioProcessor::readParams() const
{
    auto f = [] (const std::atomic<float>* a) { return a->load (std::memory_order_relaxed); };
    auto i = [] (const std::atomic<float>* a) { return juce::roundToInt (a->load (std::memory_order_relaxed)); };
    auto b = [] (const std::atomic<float>* a) { return a->load (std::memory_order_relaxed) > 0.5f; };

    anm::Params P;
    P.level = f (pLevel); P.repeats = f (pRepeats); P.size = f (pSize); P.sizeMode = i (pSizeMode);
    P.modify = f (pModify); P.spread = f (pSpread); P.scan = f (pScan); P.scanMode = i (pScanMode);
    P.bank = i (pBank); P.modifier = i (pModifier); P.routing = i (pRouting);
    P.engage = b (pEngage); P.loop = b (pLoop); P.scanHold = b (pScanHold);
    P.collect = b (pCollect); P.wipe = b (pWipe); P.always = b (pAlways); P.latch = b (pLatch);
    P.dryKill = b (pDryKill); P.trails = b (pTrails); P.memoryDry = i (pMemoryDry); P.wideSpread = b (pWide);
    P.bpm = hostBpm.load(); P.beatsPerBar = hostBeatsPerBar.load();
    P.hostBeats = hostBeats.load(); P.hostPlaying = hostPlaying.load();
    return P;
}

void AnamnesisAudioProcessor::tapSize (double seconds)
{
    if (! (seconds >= anm::kSizeMinSeconds && seconds <= anm::kSizeMaxSeconds)) return;
    if (auto* p = apvts.getParameter (AnmParams::size))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (anm::sizeKnobForSeconds (seconds)));
        p->endChangeGesture();
    }
}

void AnamnesisAudioProcessor::prepareToPlay (double sampleRate, int)
{
    engine.setParams (readParams());
    engine.prepare (sampleRate);          // the memory is reserved here (never on the audio thread); same rate = kept
    lastClear = pClear->load() > 0.5f;    // a project saved with the button down does not clear on loading
}

bool AnamnesisAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Mono in / mono out, or stereo in / stereo out.
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    return in == out && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

void AnamnesisAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear();

    // Logic's tempo and time signature (for Size in Sync). If Logic gives none, the last known values (or 120, 4/4) stay.
    // Its position and transport: while it plays, Size in Sync lines its grid up with Logic's beats.
    bool playing = false;
    double beats = -1.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto bpm = pos->getBpm()) hostBpm = anm::safeBpm (*bpm);
            if (auto sig = pos->getTimeSignature(); sig.hasValue() && sig->denominator > 0)
                hostBeatsPerBar = anm::safeBeatsPerBar (sig->numerator * 4.0 / sig->denominator);
            if (auto ppq = pos->getPpqPosition(); ppq.hasValue() && std::isfinite (*ppq))
                beats = *ppq;
            playing = pos->getIsPlaying();
        }
    hostBeats = beats;
    hostPlaying = playing;

    const anm::Params P = readParams();
    readout->sizeMode = P.sizeMode;
    readout->scanMode = P.scanMode;
    readout->bpm = hostBpm.load();
    readout->beatsPerBar = hostBeatsPerBar.load();

    const bool clearNowOn = pClear->load() > 0.5f;
    if (clearNowOn && ! lastClear) engine.requestClear();       // switching Clear memory on clears it
    lastClear = clearNowOn;

    engine.setParams (P);
    const int numCh = juce::jmin (buffer.getNumChannels(), getTotalNumOutputChannels());
    if (numCh <= 0) return;
    float* chans[2] = { buffer.getWritePointer (0), buffer.getWritePointer (numCh > 1 ? 1 : 0) };
    engine.process (chans, numCh > 1 ? 2 : 1, buffer.getNumSamples());
    for (int c = 2; c < buffer.getNumChannels(); ++c)
        buffer.clear (c, 0, buffer.getNumSamples());
}

juce::AudioProcessorEditor* AnamnesisAudioProcessor::createEditor()
{
    return new AnamnesisAudioProcessorEditor (*this);
}

void AnamnesisAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("zoom", zoom.load(), nullptr);
    if (auto xml = state.createXml())
    {
        xml->setAttribute ("pluginVersion", JucePlugin_VersionString);
        copyXmlToBinary (*xml, destData);
    }
}

void AnamnesisAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            const auto tree = juce::ValueTree::fromXml (*xml);
            apvts.replaceState (tree);
            if (tree.hasProperty ("zoom")) setZoom ((int) tree["zoom"]);
            lastClear = pClear->load() > 0.5f;
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AnamnesisAudioProcessor();
}

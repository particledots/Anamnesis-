#include "PluginProcessor.h"
#include "PluginEditor.h"

// The controls a preset snapshot holds, in this order (the sound and the options).
const char* const AnamnesisAudioProcessor::kSnapshotIds[kSnapshotSize] = {
    AnmParams::level, AnmParams::repeats, AnmParams::size, AnmParams::sizeMode, AnmParams::modify, AnmParams::spread,
    AnmParams::scan, AnmParams::scanMode, AnmParams::bank, AnmParams::modifier, AnmParams::routing, AnmParams::collect,
    AnmParams::wipe, AnmParams::always, AnmParams::latch, AnmParams::dryKill, AnmParams::trails, AnmParams::memoryDry,
    AnmParams::wideSpread
};

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
    pPreset = p (preset);
    for (int i = 0; i < kSnapshotSize; ++i)
    {
        pSnap[i] = p (kSnapshotIds[i]);
        live[i] = pSnap[i]->load();
        snap[0][i] = snap[1][i] = pSnap[i]->load();
    }
    startTimerHz (30);
}

// ------------------------------------------------------------------------------------------------ presets

void AnamnesisAudioProcessor::valuesFrom (const std::atomic<float>* v, anm::Params& P) const
{
    auto i = [&] (int k) { return juce::roundToInt (v[k].load (std::memory_order_relaxed)); };
    auto b = [&] (int k) { return v[k].load (std::memory_order_relaxed) > 0.5f; };
    P.level = v[0].load(); P.repeats = v[1].load(); P.size = v[2].load(); P.sizeMode = i (3); P.modify = v[4].load();
    P.spread = v[5].load(); P.scan = v[6].load(); P.scanMode = i (7); P.bank = i (8); P.modifier = i (9); P.routing = i (10);
    P.collect = b (11); P.wipe = b (12); P.always = b (13); P.latch = b (14); P.dryKill = b (15); P.trails = b (16);
    P.memoryDry = i (17); P.wideSpread = b (18);
}

void AnamnesisAudioProcessor::followPresetSwitch()
{
    int want = juce::jlimit (0, 2, juce::roundToInt (pPreset->load()));
    if (want != anm::PresetLive && ! filled[want].load()) want = anm::PresetLive;     // an empty slot plays Live
    const int was = presetPlaying.load();
    if (want == was) return;
    // leaving Live: keep the Live settings (unless the controls have not yet been put back to them)
    if (was == anm::PresetLive && appliedGen.load() == gen.load())
        for (int k = 0; k < kSnapshotSize; ++k) live[k].store (pSnap[k]->load());
    presetPlaying.store (want);
    gen.fetch_add (1);
}

void AnamnesisAudioProcessor::applyPresetNow()
{
    const int g = gen.load();
    if (appliedGen.load() == g) return;
    const int which = presetPlaying.load();
    const std::atomic<float>* v = which == anm::PresetLive ? live : snap[which];
    for (int k = 0; k < kSnapshotSize; ++k)
        if (auto* prm = apvts.getParameter (kSnapshotIds[k]))
        {
            const float want = prm->convertTo0to1 (v[k].load());
            if (std::fabs (prm->getValue() - want) > 1.0e-7f)
            {
                prm->beginChangeGesture();
                prm->setValueNotifyingHost (want);
                prm->endChangeGesture();
            }
        }
    if (gen.load() == g) appliedGen.store (g);         // (if the switch moved again meanwhile, the next tick shows that)
}

void AnamnesisAudioProcessor::saveSlot (int slot)
{
    if (slot < 0 || slot > 1) return;
    applyPresetNow();                                  // the controls show what is heard
    for (int k = 0; k < kSnapshotSize; ++k) snap[slot][k].store (pSnap[k]->load());
    filled[slot].store (true);
}

juce::String AnamnesisAudioProcessor::slotText (const std::atomic<float>* v) const
{
    juce::StringArray parts;
    for (int k = 0; k < kSnapshotSize; ++k) parts.add (juce::String (kSnapshotIds[k]) + "=" + juce::String (v[k].load(), 7));
    return parts.joinIntoString (";");
}

void AnamnesisAudioProcessor::slotFromText (std::atomic<float>* v, const juce::String& text, bool& ok) const
{
    ok = false;
    juce::StringArray parts;
    parts.addTokens (text, ";", "");
    int found = 0;
    for (const auto& part : parts)
        for (int k = 0; k < kSnapshotSize; ++k)
            if (part.upToFirstOccurrenceOf ("=", false, false) == kSnapshotIds[k])
            {
                const float x = part.fromFirstOccurrenceOf ("=", false, false).getFloatValue();
                if (std::isfinite (x))
                {
                    const auto* prm = dynamic_cast<const juce::RangedAudioParameter*> (apvts.getParameter (kSnapshotIds[k]));
                    v[k].store (prm != nullptr ? prm->getNormalisableRange().snapToLegalValue (x) : x);
                    ++found;
                }
            }
    ok = found > 0;
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
    // A preset switch the controls do not show yet: the snapshot (or Live) is heard at once, straight from its store
    if (appliedGen.load() != gen.load())
    {
        const int which = presetPlaying.load();
        valuesFrom (which == anm::PresetLive ? live : snap[which], P);
    }
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

    followPresetSwitch();
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
    applyPresetNow();                                  // the controls hold what is heard
    auto state = apvts.copyState();
    state.setProperty ("zoom", zoom.load(), nullptr);
    // the preset slots (0.3.0 on), and the Live settings kept while a snapshot plays
    for (int s = 0; s < 2; ++s)
    {
        const char* key = s == 0 ? "presetLeft" : "presetRight";
        if (filled[s].load()) state.setProperty (key, slotText (snap[s]), nullptr);
        else state.removeProperty (key, nullptr);
    }
    state.setProperty ("presetLive", slotText (live), nullptr);
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
            // presets: projects from 0.1.0 and 0.2.0 have none (both slots empty); the controls as saved are what is heard
            for (int s = 0; s < 2; ++s)
            {
                bool ok = false;
                const char* key = s == 0 ? "presetLeft" : "presetRight";
                if (tree.hasProperty (key)) slotFromText (snap[s], tree[key].toString(), ok);
                filled[s].store (ok);
                if (! ok) for (int k = 0; k < kSnapshotSize; ++k) snap[s][k].store (pSnap[k]->load());
            }
            bool liveOk = false;
            if (tree.hasProperty ("presetLive")) slotFromText (live, tree["presetLive"].toString(), liveOk);
            if (! liveOk) for (int k = 0; k < kSnapshotSize; ++k) live[k].store (pSnap[k]->load());
            int want = juce::jlimit (0, 2, juce::roundToInt (pPreset->load()));
            if (want != anm::PresetLive && ! filled[want].load()) want = anm::PresetLive;
            presetPlaying.store (want);
            appliedGen.store (gen.load());
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AnamnesisAudioProcessor();
}

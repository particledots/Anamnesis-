#include "Parameters.h"
#include <anamnesis/Params.h>

namespace AnmParams
{
namespace
{
    using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;
    using TextFn = std::function<juce::String (float, int)>;

    void addFloat (Layout& l, const char* id, const char* name, float def, TextFn text)
    {
        l.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (0.0f, 1.0f, 0.0f), def,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (std::move (text))));
    }
    void addChoice (Layout& l, const char* id, const char* name, const juce::StringArray& items, int def)
    {
        l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, items, def));
    }
    void addBool (Layout& l, const char* id, const char* name, bool def)
    {
        l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, def));
    }

    juce::String secondsText (double s)
    {
        char buf[32];
        anm::formatSeconds (s, buf, (int) sizeof (buf));
        return buf;
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout (std::shared_ptr<ReadoutContext> ctx)
{
    Layout l;
    const anm::Params defaults;

    addFloat (l, level, "Level", defaults.level, [] (float v, int)
    {
        const float db = anm::levelDb (v);
        return db <= -119.0f ? juce::String ("-inf dB") : juce::String (db, 1) + " dB";
    });
    addFloat (l, repeats, "Repeats", defaults.repeats, [] (float v, int)
    {
        return v >= anm::kRepeatsInfiniteFrom ? juce::String ("infinite") : juce::String (juce::roundToInt (v * 100.0f)) + "%";
    });
    addFloat (l, size, "Size", defaults.size, [ctx] (float v, int)
    {
        if (ctx != nullptr && ctx->sizeMode.load() == anm::SizeSync)
            return juce::String (anm::kNoteValues[anm::noteIndexUsed (v, ctx->bpm.load(), ctx->beatsPerBar.load())].name);
        return secondsText (anm::sizeFreeSeconds (v));
    });
    addChoice (l, sizeMode, "Size mode", { "Free", "Sync" }, anm::SizeFree);
    addFloat (l, modify, "Modify", defaults.modify, [] (float v, int)
    {
        const int p = juce::roundToInt ((v - 0.5f) * 200.0f);
        return p == 0 ? juce::String ("neutral") : (p < 0 ? "left " + juce::String (-p) + "%" : "right " + juce::String (p) + "%");
    });
    addFloat (l, spread, "Spread", defaults.spread, [] (float v, int)
    {
        return anm::spreadOn (v) ? secondsText (anm::spreadRawSeconds (v)) + " behind" : juce::String ("off");
    });
    addFloat (l, scan, "Scan", defaults.scan, [ctx] (float v, int)
    {
        if (ctx != nullptr && ctx->scanMode.load() == anm::ScanManual)
        {
            const double s = anm::scanRawSeconds (v);
            return s < 0.0005 ? juce::String ("now") : secondsText (s) + " ago";
        }
        return anm::autoScanOn (v) ? "auto " + juce::String (juce::roundToInt (v * 100.0f)) + "%" : juce::String ("off");
    });
    addChoice (l, scanMode, "Scan mode", { "Auto", "Manual" }, anm::ScanAuto);
    addChoice (l, bank, "Modifier bank", { "A", "Off", "B" }, anm::BankOff);
    addChoice (l, modifier, "Modifier", { "1", "2", "3" }, 0);
    addChoice (l, routing, "Routing", { "In", "Out", "Feed" }, anm::RouteOut);
    addChoice (l, preset, "Preset", { "Left", "Live", "Right" }, anm::PresetLive);
    addBool (l, engage, "Engage", true);
    addBool (l, loop, "Loop", false);
    addBool (l, scanHold, "Hold to scan", false);
    addBool (l, clearMemory, "Clear memory", false);
    addBool (l, collect, "Collect", false);
    addBool (l, wipe, "Wipe", false);
    addBool (l, always, "Always", false);
    addBool (l, latch, "Latch", false);
    addBool (l, dryKill, "Dry kill", false);
    addBool (l, trails, "Trails", false);
    addChoice (l, memoryDry, "Memory dry", { "Scanning", "Off", "On" }, anm::MemoryDryScanning);
    addBool (l, wideSpread, "Wide spread", false);
    return l;
}
}

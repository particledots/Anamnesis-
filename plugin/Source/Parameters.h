// Every control of Anamnesis, as Logic sees it.
//
// IMPORTANT: these ids, their order, ranges and menu lists were fixed in the first build (0.1.0, milestone 1) and must never
// change. Saved projects and automation refer to them. Controls for later milestones are already here (they are stored,
// and shown dimmed on the panel) so nothing has to be added in between. If something must be added one day, add it at
// the END with ParameterID version 2.
//
// Panel-only actions that are not controls: Tap (sets Size), Save left / Save right (milestone 3), Zoom.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <memory>

namespace AnmParams
{
    inline constexpr const char* level       = "level";
    inline constexpr const char* repeats     = "repeats";
    inline constexpr const char* size        = "size";
    inline constexpr const char* sizeMode    = "sizeMode";      // Free / Sync
    inline constexpr const char* modify      = "modify";        // M2
    inline constexpr const char* spread      = "spread";
    inline constexpr const char* scan        = "scan";
    inline constexpr const char* scanMode    = "scanMode";      // Auto / Manual
    inline constexpr const char* bank        = "bank";          // M2: A / Off / B
    inline constexpr const char* modifier    = "modifier";      // M2: 1 / 2 / 3
    inline constexpr const char* routing     = "routing";       // M2: In / Out / Feed
    inline constexpr const char* preset      = "preset";        // M3: Left / Live / Right
    inline constexpr const char* engage      = "engage";        // M3
    inline constexpr const char* loop        = "loop";          // M3
    inline constexpr const char* scanHold    = "scanHold";      // M3
    inline constexpr const char* clearMemory = "clearMemory";   // switching it on clears the memory
    inline constexpr const char* collect     = "collect";       // M3 (the options row)
    inline constexpr const char* wipe        = "wipe";
    inline constexpr const char* always      = "always";
    inline constexpr const char* latch       = "latch";
    inline constexpr const char* dryKill     = "dryKill";
    inline constexpr const char* trails      = "trails";
    inline constexpr const char* memoryDry   = "memoryDry";     // M3: Scanning / Off / On
    inline constexpr const char* wideSpread  = "wideSpread";    // M3

    inline constexpr int kNumControls = 24;

    // What the read-outs Logic shows need to know besides the value itself (written by the processor every block).
    struct ReadoutContext
    {
        std::atomic<int> sizeMode { 0 }, scanMode { 0 };
        std::atomic<double> bpm { 120.0 }, beatsPerBar { 4.0 };
    };

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout (std::shared_ptr<ReadoutContext> context);
}

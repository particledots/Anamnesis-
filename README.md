# Anamnesis

An original AU / VST3 audio effect for Logic Pro: a delay with a 3-minute memory, inspired by the Chase Bliss Habit
"delay with a memory" pedal. It is not a copy, contains no Chase Bliss code or artwork, and is not affiliated with Chase
Bliss. The Habit has no published firmware; its manual and web guide were used only to understand what the controls do.

In Logic: **Audio Units > particledots > Anamnesis** (an audio effect: put it in an Audio FX slot, mono or stereo).

Status: **milestone 1 of 3 done (version 0.1.0, tested in Logic on 2026-11-25 on both Macs: M4 Mac mini with Logic 11.2.2 and Intel Mac with Logic 10.7.8).** Next: milestone 2 (the modifiers and routing).

## Installing

Download the `Anamnesis-macOS` package from the Actions run, unzip it and double-click
`Install-Unofficial-Plugin.command` (it asks for your Mac password once and installs the AU). `INSTALL.txt` in the
package has the manual steps too. The plugin is a universal binary (Apple Silicon and Intel), macOS 11 or newer.

## What it does

Everything you play is recorded into a 3-minute memory, all the time. Two play heads read the memory and feed an echo:

```
input ──► MEMORY (3 minutes, always recording) ──► Scan play head ─────────┐
                                               └─► Spread play head ──┐    │
                                                                      ▼    ▼
                                                     ECHO (Size, Repeats) ──► Level ──► + dry ──► output
```

With Scan and Spread at zero it is a plain, clean delay. Turning up Scan plays echoes of the past; Spread adds a second
echo further back in the memory.

## The controls (milestone 1)

| Control | What it does |
|---|---|
| Level | How loud the echoes are. 0 = silent, 12:00 = -6 dB, about 2:00 = unity, full = +6 dB. The dry sound is always at full level. |
| Repeats | How many echoes. From one echo up to "infinite" (the last 8% of the knob): the echo then loops and new playing piles up on top. A soft limiter in the loop keeps the pile-up from ever running away. |
| Size | The time between echoes. **Free**: 50 ms to 60 s. **Sync**: note values of Logic's tempo, 1/32 to 32 bars (dotted and triplets included); a value longer than 60 s at the current tempo falls back to the longest one that fits. Changing Size never bends the pitch: the echo crossfades (20 ms) to the new length. |
| Tap | Two taps set Size (Free only; in Sync, Logic's tempo sets it). |
| Scan, Manual | How far back in the memory the echo plays (12:00 is about 45 s back, full is 3 minutes back). Like a pre-delay at low settings. Moving it scrubs through the memory (a "cloudy" sound); a big jump crossfades instead. |
| Scan, Auto | The play head wanders off by itself now and then, plays a moment of the past, and snaps back to now. The knob sets both how often (rare at low settings, every echo at full) and how far back (2 s up to 3 minutes). Our own design, after the guide's description. |
| Spread | Left-most = off. Turning it up adds a second echo, further back in the memory by the Spread distance (up to 3 minutes). It follows Scan. |
| Clear memory | Empties the memory and the echo (10 ms fade, no click). Can be automated: it clears when it switches on. |
| Zoom | Panel size 75% to 200% (saved with the project). |

Scan and Spread are linked to Size so they stay in time: their distances snap to a grid made of Size (quarters and thirds
of Size up to 2 Sizes, halves up to 8 Sizes, whole Sizes beyond).

The memory strip at the top shows the 3 minutes (newest at the right), what has been recorded, the echo window (Size),
and where the Scan (amber) and Spread (teal) play heads are.

**Stereo**: stereo in and out (or mono in and out). Both sides use the same heads, times and random choices, so the stereo
picture of what you play is kept. (Wide spread, milestone 3, will spread the two echoes left and right.)

**Memory**: about 93 MB at 48 kHz and 186 MB at 96 kHz (3-minute stereo memory plus a 60 s stereo echo). At 176.4 and
192 kHz the memory keeps every second sample (filtered), about 233 MB. It is reserved when Logic prepares the plugin, never
while audio plays, and kept when Logic prepares again at the same sample rate. If it cannot be reserved, the plugin passes
the dry sound through and says so on the panel.

## Dimmed on the panel (later milestones)

- **Milestone 2**: Modify, Bank A / Off / B, Modifier 1 / 2 / 3, Routing In / Out / Feed, the modifier read-out. The six
  modifiers: Stepped speed, Stability, Trimmer (bank A); Smooth speed, Filter, Dropper (bank B).
- **Milestone 3**: Engage, Loop, Hold to scan, Preset Left / Live / Right with Save left / Save right, and the options:
  Collect, Wipe, Always, Latch, Dry kill, Trails, Wide spread, Memory dry (Scanning / Off / On).

All 24 controls exist from 0.1.0 (ids, order, ranges and menus are fixed for good), so automation and saved projects
never break.

## Plan

1. **Milestone 1 (0.1.0)**: repo, build, installer, the real panel, the memory, Size (Free, Sync, Tap), Repeats, Level,
   Spread, Scan (Auto and Manual), Clear memory, the complete control list. Tested on both Macs.
2. **Milestone 2 (0.2.0)**: Modify and the six modifiers, Bank, Modifier, Routing In / Out / Feed. Tested on the Intel Mac.
3. **Milestone 3 (0.3.0)**: Engage, Loop, Hold to scan, the options, Wide spread, the two preset slots. Tested on the Intel Mac.

Decisions: Stepped speed and Smooth speed play at normal speed at 12:00 (neutral), as for every modifier. The 28-scale
quantizer is not used (a speed change transposes whatever was recorded, whose key the plugin cannot know; octaves and
fifths fit any key). Left out because Logic covers them: ramping and bounce, CV, expression, MIDI CCs and program changes
(Logic's automation and presets), MIDI clock (Logic's tempo). The pedal's two kinds of Dry kill become one: the dry sound is
muted while engaged and comes back when Engage is off.

## Later (ideas, not built unless asked)

- Cosmetic tweaks of the panel once all milestones are finished.
- Bounce / ramp movement of the knobs (Logic's automation does this today).
- Saving and loading the memory as a WAV file.
- An option to pause the memory while Logic is stopped (today it records whatever Logic sends, like the pedal).

## How it is built and checked

GitHub Actions builds everything; nothing is compiled on a Mac by hand. Every push runs:

- **Engine self-test** (`engine/tests`, plain C++, no Logic): 146 checks: echo timing to the sample at several sample
  rates, Level and Repeats, Size free and synced, no pitch bend and no clicks while Size moves, Repeats at infinite
  steady for a minute and bounded through 5 minutes of loud pile-up, Scan reading the right moment up to 3 minutes back
  (also at 192 kHz), Spread, Auto Scan wandering and returning, empty memory silent, Clear, memory size at every rate,
  memory not available, preparing again, Logic's block sizes, bad input, stereo and mono, the strip picture, defaults,
  CPU.
- **Plugin-layer check** (`check/`, `AnamnesisCheck`): 96 checks: the control list against the stored 0.1.0 list
  (`check/golden/`), saved projects from every earlier version, first-load echo, hosting like Logic (mono and stereo,
  44.1 to 192 kHz, odd block sizes, bypass, Logic's tempo, Clear by automation), the panel at every zoom size with the
  title measured and checked in the drawn pixels, dimmed and live controls, Tap, heavy automation of every control. It
  also draws pictures of the panel (the `panel-pictures` download of each run).
- **macOS build**: AU and VST3, universal binary checked with `lipo`, ad-hoc signed, Apple's `auval`, `pluginval`
  (strictness 5, AU and VST3), and the package with the installer and INSTALL.txt.

Layout: `engine/` (the sound: plain C++17, no JUCE, reusable), `plugin/` (the AU / VST3 wrapper, controls, panel),
`check/` (the plugin-layer check), `tools/install/` (the shared installer).

Codes (fixed for good): AU type `aufx`, plugin code `Anms`, manufacturer `Pdot`, bundle id `com.particledots.anamnesis`.

## Reused code (all from particledots repos)

- **Mimic-Phon**: the delay buffer (cubic reading between samples and the instant Clear) became `engine/.../Tape.h`; the
  soft limiter (`Gain.h`); the crossfaded jump between two read points (used for Size and for Scan jumps); the
  Repeats-curve and tail idea.
- **Phenome-Eon**: reserving the long memory with calloc and failing safely (`Tape.h`); the lock-free waveform picture
  idea (the memory strip).
- **COMPLICATOR**: the repo layout, workflow, check program, README and CHANGELOG style, installer.
- New: the memory with its two play heads, Auto Scan, Size sync, the Size grid, the panel.

Fonts: Space Grotesk (SIL Open Font License, `plugin/Fonts/OFL-SpaceGrotesk.txt`), built into the plugin.

## Test results

Milestone 1 (0.1.0, Actions run 45732768630, all jobs green, auval and pluginval strictness 5 passed for AU and VST3):
tested by Bryn on 2026-11-25 on the M4 Mac mini (Logic 11.2.2) and the Intel Mac (Logic 10.7.8). Everything works, no
issues: the plugin opens and the panel looks right; the default echo; Size moved slowly and quickly with no pitch wobble
or clicks; Repeats at full loops without running away; Size Sync follows Logic's tempo and Tap sets Size; Manual Scan
plays the past with the amber marker moving; Spread adds the older echo with the teal marker; Auto Scan wanders and comes
back; Clear memory empties the strip and stops the echoes with no click; Zoom 75% and 200%; settings and zoom come back
after saving and reopening the project.

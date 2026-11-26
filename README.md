# Anamnesis

An original AU / VST3 audio effect for Logic Pro: a delay with a 3-minute memory, inspired by the Chase Bliss Habit
"delay with a memory" pedal. It is not a copy, contains no Chase Bliss code or artwork, and is not affiliated with Chase
Bliss. The Habit has no published firmware; its manual and web guide were used only to understand what the controls do.

In Logic: **Audio Units > particledots > Anamnesis** (an audio effect: put it in an Audio FX slot, mono or stereo).

Status: **milestones 1 and 2 of 3 done.** Milestone 1 (0.1.0) tested in Logic on 2026-11-25 on both Macs (M4 Mac mini with
Logic 11.2.2, Intel Mac with Logic 10.7.8); milestone 2 (0.2.0, the modifiers and routing) tested in Logic on 2026-11-25 on
the Intel Mac. Next: milestone 3 (Engage, Loop, Hold to scan, the options, Wide spread, the preset slots).

## Installing

Download the `Anamnesis-macOS` package from the Actions run, unzip it and double-click
`Install-Unofficial-Plugin.command` (it asks for your Mac password once and installs the AU). `INSTALL.txt` in the
package has the manual steps too. The plugin is a universal binary (Apple Silicon and Intel), macOS 11 or newer.

## What it does

Everything you play is recorded into a 3-minute memory, all the time. Two play heads read the memory and feed an echo:

```
input ──► MEMORY (3 minutes, always recording) ──► Scan play head ─────────┐
  ▲                                            └─► Spread play head ──┐    │
  │                                                                   ▼    ▼
  │                                   [In] ──► ECHO (Size, Repeats) ──► [Out] ──► Level ──► + dry ──► output
  └──────────── Feed: Repeats x the echo ◄─────────────────────────────┘
```

With Scan and Spread at zero it is a plain, clean delay. Turning up Scan plays echoes of the past; Spread adds a second
echo further back in the memory. A modifier (Bank A or B) shapes the echoes, at the place Routing chooses.

## The controls

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
| Bank | A, Off or B: picks a bank of three modifiers, or none. |
| Modifier | 1, 2 or 3: picks the modifier in the bank (the read-out under Preset names it and what it is doing). |
| Modify | Sets the modifier. 12:00 is neutral for every modifier (exactly the same as Bank Off); the two sides do different things (below). Double-click returns to 12:00. |
| Routing | **In**: the modifier sits inside the echo's feedback loop, so it builds up on every repeat. **Out**: after the echo, so every echo sounds the same. **Feed**: as Out, and the modified echo is recorded back into the memory with what you play (the strip shows it); the echo's own feedback is off and Repeats sets the feedback of the whole loop, so Spread becomes a classic multi-tap and Scan reaches back into echoes too. Routing works with Bank Off as well. |

Switching Bank, Modifier or Routing while audio plays crossfades (the old modifier fades out in 15 ms, the new one fades in
in 15 ms; the routing's feedback paths cross over in 20 ms): no clicks.

### The six modifiers (our own designs, after the Habit guide's descriptions)

| | Counter-clockwise | Clockwise |
|---|---|---|
| **A1 Stepped speed** | reverse: 1x, 1.5x, 2x, 3x, 4x | forward: 1.5x, 2x, 3x, 4x (fifths and octaves, so the echoes stay in key) |
| **A2 Stability** | wow, flutter, tape filtering, a little saturation, growing; no noise | the same, plus tape hiss |
| **A3 Trimmer** | cuts the end of every echo | cuts the start of every echo |
| **B1 Smooth speed** | reverse, 1x up to 2x | forward, 1x up to 2x |
| **B2 Filter** | low-pass, closing from 20 kHz to 80 Hz | high-pass, rising from 20 Hz to 8 kHz |
| **B3 Dropper** | random drops (more as it turns) | a pattern of drops, the same in every echo (more drops per echo as it turns) |

- **Speeds**: at 12:00 the echoes play at normal speed. Forward speeds read the echo in 70 ms pieces, crossfaded (like a
  pitch shifter); reverse plays a whole Size backwards (up to 3 s). Smooth speed spends the first stretch of the knob on a
  fine detune (a few cents, for chorus and detune) and reaches an octave at the end. The delay this adds is taken off the
  echo's time, so the echoes stay on their Size grid (when Size is long enough).
- **Trimmer** steps: 1/8, 1/4, 1/3, 3/8, 1/2, 5/8, 2/3, 3/4, 7/8 of Size. With Size in Sync while Logic plays, the Size grid
  lines up with Logic's beats, so "cut the start: 1/2" cuts the first half of every beat (or of every note value).
- **Filter**: its resonance grows as it turns. In In and Feed it can pile up and sing (like the pedal); limiters in the loop
  keep it bounded.
- **Dropper**: some drops are smooth and shallow, some abrupt and deep. Near the end both sides become granular.

Decisions: Trimmer's directions follow the Habit guide's diagram (counter-clockwise cuts the end, clockwise the start; the
milestone 2 brief had them the other way round). The 28-scale quantizer is not used (see Plan).

Scan and Spread are linked to Size so they stay in time: their distances snap to a grid made of Size (quarters and thirds
of Size up to 2 Sizes, halves up to 8 Sizes, whole Sizes beyond).

The memory strip at the top shows the 3 minutes (newest at the right), what has been recorded, the echo window (Size),
and where the Scan (amber) and Spread (teal) play heads are.

**Stereo**: stereo in and out (or mono in and out). Both sides use the same heads, times and random choices, so the stereo
picture of what you play is kept. (Wide spread, milestone 3, will spread the two echoes left and right.)

**Memory**: about 96 MB at 48 kHz and 191 MB at 96 kHz (3-minute stereo memory, a 60 s stereo echo, and two 3.4 s
recordings the modifiers read from). At 176.4 and 192 kHz the memory keeps every second sample (filtered), about 243 MB. It is reserved when Logic prepares the plugin, never
while audio plays, and kept when Logic prepares again at the same sample rate. If it cannot be reserved, the plugin passes
the dry sound through and says so on the panel.

## Dimmed on the panel (later milestones)

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
- Reverse speeds playing more than 3 s backwards for very long Sizes.

## How it is built and checked

GitHub Actions builds everything; nothing is compiled on a Mac by hand. Every push runs:

- **Engine self-test** (`engine/tests`, plain C++, no Logic): 326 checks. Milestone 1: echo timing to the sample at
  several sample rates, Level and Repeats, Size free and synced, no pitch bend and no clicks while Size moves, Repeats at
  infinite steady for a minute and bounded through 5 minutes of loud pile-up, Scan reading the right moment up to 3 minutes
  back (also at 192 kHz), Spread, Auto Scan wandering and returning, empty memory silent, Clear, memory size at every rate,
  memory not available, preparing again, Logic's block sizes, bad input, stereo and mono, the strip picture, defaults, CPU.
  Milestone 2: Modify 12:00 = Bank Off bit for bit (every modifier, every routing, with Bank flipping); every speed step
  measured (1.5x to 4x forward, 1x to 4x reverse, Smooth speed and its fine detune, at 44.1 to 192 kHz); reverse
  playback; In building up and Out staying the same, with the echoes on the Size grid; Stability's wobble, filtering and
  hiss; Trimmer's cut amounts and sides; Filter direction and its singing kept bounded; Dropper's pattern identical in
  every echo, its random side not, granular at the end; Feed's loop timing, Repeats, the memory recording the echoes,
  Spread as a multi-tap; the Size grid on Logic's beats; no clicks when Modify, Bank, Modifier or Routing change; every
  modifier at both ends in every routing with Repeats infinite and loud input; 5 minutes of Feed with the filter singing
  and of In at 4x; block sizes; CPU.
- **Plugin-layer check** (`check/`, `AnamnesisCheck`): 110 checks: the control list against the stored 0.1.0 list
  (`check/golden/`), saved projects from every earlier version, first-load echo, hosting like Logic (mono and stereo,
  44.1 to 192 kHz, odd block sizes, bypass, Logic's tempo, Clear by automation), the panel at every zoom size with the
  title measured and checked in the drawn pixels, dimmed and live controls, Tap, heavy automation of every control;
  milestone 2: Logic's read-out of Modify for each modifier, the panel's Modify text and modifier read-out (and that they
  fit for every modifier and setting), Modify 12:00 = Bank Off bit for bit through the plugin, Bank / Modifier / Routing /
  Modify automated hard without clicks, the Size grid following Logic's beats. It also draws pictures of the panel (the
  `panel-pictures` download of each run, one per modifier included).
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
- **Mimic-Phon** (milestone 2): Flip's chunk reader (play heads reading a recording in crossfaded chunks) became the
  speed reader of Stepped speed and Smooth speed, extended to read forward at any speed as well as backwards; the double
  precision filter form with denormal guarding.
- **Phenome-Eon** (milestone 2): the octaves-and-fifths "Steps" list of Pitch Snap became Stepped speed's steps.
- New: the memory with its two play heads, Auto Scan, Size sync, the Size grid, the panel; milestone 2: Stability (wow,
  flutter, tape filter, hiss), Trimmer, Filter (a state-variable filter), Dropper, the two modifier slots with their
  crossfades, Feed routing, the delay compensation, the Size grid on Logic's beats.

Fonts: Space Grotesk (SIL Open Font License, `plugin/Fonts/OFL-SpaceGrotesk.txt`), built into the plugin.

## Test results

Milestone 1 (0.1.0, Actions run 45732768630, all jobs green, auval and pluginval strictness 5 passed for AU and VST3):
tested by Bryn on 2026-11-25 on the M4 Mac mini (Logic 11.2.2) and the Intel Mac (Logic 10.7.8). Everything works, no
issues: the plugin opens and the panel looks right; the default echo; Size moved slowly and quickly with no pitch wobble
or clicks; Repeats at full loops without running away; Size Sync follows Logic's tempo and Tap sets Size; Manual Scan
plays the past with the amber marker moving; Spread adds the older echo with the teal marker; Auto Scan wanders and comes
back; Clear memory empties the strip and stops the echoes with no click; Zoom 75% and 200%; settings and zoom come back
after saving and reopening the project.

Milestone 2 (0.2.0, Actions run 45738707752, all jobs green, auval and pluginval strictness 5 passed for AU and VST3):
tested by Bryn on 2026-11-25 on the Intel Mac (Logic 10.7.8). Everything works as described, no issues: Stepped speed
steps up to 4x and reverses without clicks; In makes each repeat climb another octave; Stability wobbles and darkens,
with hiss clockwise; Trimmer cuts the start / end of each echo on the beat in Sync; Smooth speed detunes, reaches an
octave and reverses; Filter low-pass / high-pass, singing in Feed without running away; Dropper pattern and random drops,
choppy at full; switching Bank, Modifier and Routing while playing has no clicks; Feed with Bank Off makes the multi-tap
pattern and the strip shows the echoes recorded; settings come back after saving and reopening.

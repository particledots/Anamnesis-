# Changelog

## 0.3.0 — milestone 3 (built and tested in Logic 2026-11-25)

- **Engage, Loop, Hold to scan, Preset Left / Live / Right with Save left / Save right, the options (Collect, Wipe,
  Always, Latch, Dry kill, Trails, Wide spread) and Memory dry (Scanning / Off / On)** are live (their M3 tags removed;
  nothing else on the panel changed). The control list is unchanged (24 controls).
- **Engage**: off = the dry sound only; turning it off empties the memory unless Collect or Always is on; Always keeps
  recording while off; Collect pauses the memory and keeps its audio; Trails lets the echoes fade out (re-engaging clears
  them unless Collect or Always); Repeats is limited while off.
- **Loop**: freezes the echo (an exact copy every pass); held, or latched with Latch; automatable. Size changes become the
  loop's length when the knob rests. **Hold to scan**: Auto Scan at maximum while held, snapping back when let go.
- **Collect** layers the memory every 3 minutes; **Wipe** mutes the echo and lets the memory be heard (in Feed, Repeats
  feeds it back); **Memory dry**; **Dry kill**; **Wide spread** (main echo left, Spread echo right).
- **Presets**: two snapshots saved with the project; switching is heard at once (also by automation), the controls then
  show the snapshot; Live brings the settings from before back.
- Every switch fades (no clicks). With the new controls at rest the sound is bit for bit 0.2.0's (proved against the
  unchanged 0.2.0 engine in the self-test). Projects from 0.1.0 and 0.2.0 load with empty preset slots; Memory dry was
  stored as Scanning (the pedal's default), so moving Scan now also plays the memory directly (Off = the 0.2.0 sound).
- Engine self-test: 386 checks. Plugin-layer check: 137 checks, plus a panel picture with the milestone 3 controls on.
- Build: Actions run 45746277623, all jobs green; auval passed; pluginval strictness 5 passed (AU and VST3).
- Logic test result (Bryn, 2026-11-25, Intel Mac with Logic 10.7.8): everything works as described, no issues (all 8
  test steps passed: panel, Engage / Dry kill / Trails, Loop, Hold to scan, Memory dry and Wipe, Collect, Wide spread,
  presets saved and recalled, also after saving and reopening the project).

## 0.2.0 — milestone 2 (built and tested in Logic 2026-11-25)

- **Modify, Bank A / Off / B, Modifier 1 / 2 / 3, Routing In / Out / Feed** are live, with the modifier read-out on the
  panel (their M2 tags removed; nothing else on the panel changed). The control list is unchanged (24 controls).
- **The six modifiers** (our own designs): A1 Stepped speed (forward 1.5x to 4x, reverse 1x to 4x), A2 Stability (wow,
  flutter, tape filtering; clockwise adds hiss), A3 Trimmer (cuts the end counter-clockwise, the start clockwise, 1/8 to
  7/8 of Size, as the Habit guide's diagram shows), B1 Smooth speed (fine detune to 2x, forward / reverse), B2 Filter
  (low-pass / high-pass with growing resonance), B3 Dropper (random / a pattern repeated in every echo; granular at the
  end). Modify 12:00 is neutral for all of them, bit for bit the same as Bank Off.
- **Routing**: In (builds up on every repeat), Out (every echo the same), Feed (the echo is recorded back into the memory,
  Repeats sets the whole loop, Spread becomes a multi-tap). Switching Bank, Modifier or Routing crossfades.
- The delay the speed modifiers and Stability add is taken off the echo's time, so the echoes stay on the Size grid.
- Size in Sync while Logic plays: the Size grid lines up with Logic's beats (Trimmer, Dropper's pattern, Auto Scan's dice).
- Logic's read-out of Modify names what the chosen modifier does ("forward 4x", "low-pass 1.2 kHz" ...).
- Memory: two 3.4 s recordings for the modifiers (about 3 MB at 48 kHz, 10 MB at 192 kHz).
- Engine self-test: 326 checks. Plugin-layer check: 110 checks, plus a picture of the panel for each modifier.
- Build: the engine-test job's extra result annotations (engine cpu, memory at, auto scan ...) now appear (the pattern
  allows ctest's "1: " in front of each line). Actions run 45738707752, all jobs green; auval passed; pluginval strictness
  5 passed (AU and VST3).
- Logic test result (Bryn, 2026-11-25, Intel Mac with Logic 10.7.8): everything works as described, no issues (all 8
  test steps: Stepped speed and In, Stability, Trimmer in Sync, Smooth speed, Filter singing in Feed, Dropper and
  switching without clicks, Feed multi-tap with Bank Off, save and reopen).

## 0.1.0 — milestone 1 (built and tested in Logic 2026-11-25)

- First build. AU and VST3, universal binary (Apple Silicon and Intel), macOS 11 or newer, ad-hoc signed. In Logic:
  Audio Units > particledots > Anamnesis. Codes: `aufx` / `Anms` / `Pdot`.
- **The complete control list for all milestones** (24 controls, all version 1), stored in `check/golden/layout-0.1.0.txt`.
  Later controls are stored and shown dimmed with an M2 / M3 tag.
- **The memory**: 3 minutes, stereo, always recording; reserved when Logic prepares the plugin and kept when it prepares
  again at the same rate; half-rate storage above 96 kHz; dry pass-through (with a note on the panel) if it cannot be
  reserved.
- **Live controls**: Level, Repeats (to a stable "infinite"), Size (Free 50 ms to 60 s, Sync to Logic's tempo, Tap), Scan
  (Manual and Auto), Spread, Clear memory, Zoom. Defaults give an audible 420 ms echo on first load.
- **The panel** as approved (2026-11-25): memory strip with the Scan and Spread play heads and the echo window, six knobs,
  switches, buttons, options row. Zoom 75% to 200%; the title is checked to fit at every size.
- Engine self-test: 146 checks. Plugin-layer check (`AnamnesisCheck`): 96 checks, plus pictures of the panel in every run.
- Build: Actions run 45732768630, all jobs green; auval passed; pluginval strictness 5 passed (AU and VST3).
- Logic test result (Bryn, 2026-11-25, M4 Mac mini with Logic 11.2.2 and Intel Mac with Logic 10.7.8): everything works,
  no issues (all 8 test steps: panel, default echo, Size without pitch wobble or clicks, Repeats at full, Sync and Tap,
  Manual Scan and Spread with their markers, Auto Scan, Clear memory, Zoom, save and reopen).

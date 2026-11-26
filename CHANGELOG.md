# Changelog

## 0.2.0 — milestone 2 (built, waiting for the Logic test on the Intel Mac)

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
  allows ctest's "1: " in front of each line).

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

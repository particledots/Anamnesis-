# Changelog

## 0.1.0 — milestone 1 (built, not yet tested in Logic)

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

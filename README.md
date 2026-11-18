# Silktone Drum Machine — AU / VST3 Plugin for macOS

A native plugin version of Joel's Silktone drum machine, for **Logic Pro** (Audio Unit)
and any VST3 host. Drop the built bundle into the Plug-Ins folder — no installer.

> **Status:** the project is complete and the DSP core is tested, but **no compiled
> `.component` or `.vst3` exists yet**. This Linux machine has no macOS and no Xcode,
> so the plugin cannot be compiled here. The build step below runs on a Mac only.
> The full instruction manual is in `docs/USER_MANUAL.md` (and as a PDF,
> `docs/Silktone-Drum-Machine-Manual.pdf`).

## What it is

The single-file web app (`~/workspace/your_files/drum-machine/drum-machine.html`)
rebuilt as a real instrument plugin:

- **30 voices** (kick, snare, clap, hats, toms, percussion, hand drums, Iranian and
  Arabian frame/goblet drums, textures) across **31 kits** (808, 909, 707, 606, 626,
  505, 727, LinnDrum, DMX, SP-1200, Simmons, CR-78, acoustic kits and more)
- **166 patterns** (presets) plus 20 percussion loops that layer over the current pattern
- **16-step sequencer** with accents, per-row mute/solo, sound scrolling, copy/paste rows
- **Pad deck** — tap to play, velocity by where you tap; right-click for mute/solo/favorite
- **Mixer** — Logic-style channel strips: volume fader, pan, tone, saturation, two aux
  sends (reverb + echo) per channel, aux mute/solo, level meters
- **Sampler** — load audio onto any voice, record from the sidechain input (your UAD /
  Apollo in Logic, or the audio device in the Standalone build), tempo lock,
  tempo-locked auto-chop by transient or by grid, slices to pads
- **Library** — scan a folder of drum sounds on your Mac, search, load onto pads, favorites
- **Stem splitter** — drop in a full song or loop and split it into six frequency bands
  (low / mid / perc / high / centre / side); play, export, or send a stem to a pad
- **Exports** — MIDI (channel 10), 4-bar WAV loop, per-voice stems as a ZIP
- **Transport** — play/pause toggle + stop, tap tempo, click (live only, never in exports),
  host tempo/transport sync, MIDI out of the pattern on channel 10

The UI keeps the app's look: 70s browns, yellows and wood, Logic-style strips,
clean tap-to-play layout.

## Requirements (Mac only)

- macOS 10.13 or later (Apple Silicon and Intel — universal binary)
- **Xcode** (from the App Store) with the Command Line Tools:
  `xcode-select --install`
- **CMake 3.22 or later** — `brew install cmake` (or from https://cmake.org/download/)
- **Git** (for fetching JUCE) — comes with the Command Line Tools
- Internet on first build only — JUCE 8.0.6 is fetched from GitHub automatically

## Build

### Option A — Xcode (recommended)

```bash
cd ~/workspace/drum-machine-plugin
cmake -S . -B build -G Xcode -DCMAKE_BUILD_TYPE=Release
open build/SilktoneDrumMachine.xcodeproj
```

In Xcode: choose the **SilktoneDrumMachine** scheme, pick **My Mac** as the
destination, then **Product → Build** (⌘B).

### Option B — command line only

```bash
cd ~/workspace/drum-machine-plugin
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Either way the built bundles land in:

```
build/SilktoneDrumMachine_artefacts/Release/
├── AU/Silktone Drum Machine.component
├── VST3/Silktone Drum Machine.vst3
└── Standalone/Silktone Drum Machine.app
```

## Install (drop-in, no installer)

```bash
# Audio Unit for Logic Pro:
cp -R "build/SilktoneDrumMachine_artefacts/Release/AU/Silktone Drum Machine.component" \
      ~/Library/Audio/Plug-Ins/Components/

# VST3 for other hosts:
cp -R "build/SilktoneDrumMachine_artefacts/Release/VST3/Silktone Drum Machine.vst3" \
      ~/Library/Audio/Plug-Ins/VST3/
```

Then:

1. **Logic Pro:** restart Logic (or use `Plug-In Manager → Rescan`). The plugin appears
   as an **instrument** — load it in an instrument slot. It is listed as
   **Silktone Drum Machine** under the manufacturer **Sounds Good Ent**.
2. If Logic asks about an unsigned plugin, open it once with
   right-click → Open (or `System Settings → Privacy & Security → Open Anyway`).
   The project is not code-signed; signing is optional and not needed for local use.
3. **Standalone:** double-click `Silktone Drum Machine.app` — no DAW needed, and
   the audio settings let you pick your interface for recording.

## Recording from your UAD / Apollo (Logic)

The plugin has a **sidechain input bus** called "Record Input" — this is how the
app's "Record From" picker works in a plugin. In Logic:

1. Put the plugin on a software instrument track.
2. In the plugin header, open the **sidechain menu** (top right of the plugin window)
   and choose your Apollo input (e.g. `Input 1`).
3. In the **Sampler** tab, pick the voice to record onto and press **● Record**.
   The input level meter shows the incoming signal. Press again to stop — the take
   lands on the voice as a sample.

In the Standalone app, recording uses the audio device you pick in the audio settings.

## Project layout

```
drum-machine-plugin/
├── CMakeLists.txt                  # JUCE 8.0.6 via FetchContent; AU + VST3 + Standalone
├── README.md                       # this file
├── docs/
│   ├── USER_MANUAL.md              # full instructions (also as a PDF)
│   └── Silktone-Drum-Machine-Manual.pdf
├── Source/
│   ├── SilktoneData.h              # generated data: 30 voices, 31 kits, 166 presets,
│   │                               # 20 perc loops, 17 swing machines, 6 stems, 9 families
│   ├── DspCore.h / DspCore.cpp     # platform-independent DSP: voice synth, FX, WAV encoder
│   ├── DrumEngine.h / DrumEngine.cpp  # sequencer, mixer, sampler, chop, library, stems, export
│   ├── PluginProcessor.h / .cpp    # JUCE wrapper: parameters, host sync, state, exports
│   ├── PluginEditor.h / .cpp       # UI: transport bar + 7 tabs
│   └── LookAndFeel.h / .cpp        # 70s browns/yellows/wood look
├── Tests/
│   └── DspTest.cpp                 # standalone JUCE-free test (32 checks, builds on Linux)
└── tools/
    └── extract_data.py             # regenerates SilktoneData.h from the web app HTML
```

Regenerate the data header from the app (only needed if the app changes):

```bash
python3 tools/extract_data.py \
    ~/workspace/your_files/drum-machine/drum-machine.html \
    Source/SilktoneData.h
```

## Plugin identity

| | AU | VST3 |
|---|---|---|
| Bundle ID | `com.soundsgoodent.silktone-drummachine` | same |
| Manufacturer code | `Sltn` | `Sltn` |
| Plugin code | `SLM8` | `SLM8` |
| AU type | `aumu` (music device / instrument) | — |
| VST3 category | — | Instrument |

The two formats share the same 4-character codes by design (JUCE maps them into
each format's own namespace: an AU subtype `SLM8`/`Sltn` and a VST3 UID derived
from the same pair). They are distinct bundles in distinct folders, so Logic and
VST3 hosts never confuse them.

## Parameters (automation)

221 automatable parameters: 12 globals (Tempo, Swing, Swing Interval, Time Signature,
Kit, Master Volume, Click, Reverb Return, Echo Return, Sync to Host, Follow Host
Transport, MIDI Out) + 7 per voice × 30 (Volume, Tune, Pan, Tone, Saturation,
Send Reverb, Send Echo). Patterns, steps, mute/solo, favorites and loaded samples are
saved with the project/session state, not as automation.

## Honest differences from the web app

- **Reverb** is an algorithmic Freeverb-style hall (~1.8 s decay), not the app's
  generated noise-decay impulse convolver. The character is close, not bit-identical.
- **TONE knob works here.** In the app the tone knob is in the UI but is never read
  by the audio path, so it does nothing audible. The plugin implements it as a real
  tilt EQ (pivot ~800 Hz, −50…+50). This is an improvement, not a faithful copy.
- **Stem splitter is a filter split, not AI separation.** Same as the app — six
  frequency bands (and mid/side extraction). It is labelled as such everywhere.
- **Favorites persist.** In the app they are session-only; the plugin saves them
  with the session.
- **Record From** is a sidechain input in Logic (see above), not a browser mic picker.
- **No gain knob.** Joel asked for one, then dropped it — it is not in the plugin.
- The app's "drum machine as a web page" extras (drag-and-drop of files onto the page,
  phone install) do not apply to a plugin; file loading uses the standard file chooser.

## What was verified here (Linux) and what was not

Verified on this machine (no Mac, no Xcode):

- `Tests/DspTest.cpp` — **44 checks, all passing** (run at both `-O0` and `-O2`):
  all 30 voices × 31 kits render finite, non-silent audio; pattern render, swing,
  stem splitter, MIDI export, auto-chop, filename categorisation, WAV encoding,
  click, remap, copy/paste, sound scroll, offline render + per-voice stems.
- Every source file named in `CMakeLists.txt` exists; every data table in
  `SilktoneData.h` matches the counts parsed from the app HTML.
- Plugin IDs / manufacturer codes are 4-character codes, valid for both formats.

**Not verifiable here** (needs the Mac build):

- That the JUCE project compiles under Xcode, the bundles are produced, and the
  `Info.plist` entries are exactly as JUCE generates them.
- That Logic validates the AU (`auval`) and loads it, that the sidechain input
  appears in Logic's menu, and that recording from the Apollo works.
- That the editor lays out correctly at all sizes, and that state save/load
  round-trips in a real Logic session.
- VST3 loading in a VST3 host, and Standalone audio-device recording.

## Troubleshooting

- **Logic doesn't list it:** check the bundle is in
  `~/Library/Audio/Plug-Ins/Components/` (user folder, not `/Library/Audio/...`),
  then `Plug-In Manager → Rescan Selection`, or run
  `auval -v aumu SLM8 Sltn` in Terminal to see the validation result.
- **No sound:** check the track is record-enabled / monitored, the master volume,
  and that no channel is soloed by accident. The click is live-only and never
  appears in exports.
- **Recording hears nothing:** the sidechain must be set in the plugin header, and
  the Apollo input must be live. The input meter in the Sampler tab shows the level.
- **Build fails fetching JUCE:** check internet access and that Git works
  (`git ls-remote https://github.com/juce-framework/JUCE.git`).

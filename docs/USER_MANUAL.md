# Silktone Drum Machine — User Manual

*Audio Unit for Logic Pro and VST3 — version 1.0*

## 1. Getting started

The Silktone Drum Machine is an instrument plugin. In Logic Pro, load it in an
**instrument slot** on a software instrument track, just like any synth. It makes
its own sound — it does not need an audio input to play.

When it opens you see the transport bar across the top and seven tabs underneath:
**Sequencer, Pads, Mixer, Sampler, Library, Stems, Export**.

Press **▶** to play. The pattern that loads by default is a house groove at
120 BPM. Press **■** to stop and go back to the start. Press **▶** again while
playing to pause where you are; press it once more to pick up from there.

## 2. The transport bar (top)

| Control | What it does |
|---|---|
| ▶ / ■ | Play / pause toggle, and stop (stop rewinds to step 1) |
| TAP | Tap it on the beat — the tempo follows your taps |
| BPM | Tempo, 40–300. Drag it, or type a number |
| Swing | How much the off-beats lag, 50% (straight) to 75% |
| 16th / 8th | Which steps the swing moves |
| Time sig | 4/4, 3/4, 5/4, 6/8, 7/8, 12/8. Changing it re-fits every row beat by beat, so a four-on-the-floor kick stays on the beat |
| Kit | The drum kit — 31 of them, from 808 and 909 to LinnDrum, DMX, SP-1200 and the acoustic kits |
| Pattern | 166 ready-made patterns, grouped by style — Motown, funk, hip-hop, house, Afrobeat, Brazilian, Iranian, and more |
| Master | Overall volume |
| CLICK | Metronome on / off. It is a live guide only — it never appears in exports |
| ? | This information, inside the plugin |

**Following Logic:** by default the plugin follows Logic's tempo and transport.
Start Logic's transport and the drum machine starts with it, at Logic's tempo.
If you want the plugin to run at its own tempo, turn off **Sync to Host Tempo**
and **Follow Host Transport** (they are automatable parameters — see section 9).

## 3. Sequencer tab

Thirty rows, one per voice, left to right in steps. A lit step plays; a dark
step is silent.

- **Click a step** to turn it on. Click it again and it becomes an **accent**
  (orange, hits harder). Click a third time to clear it.
- **Row name** — click it to hear that voice on its own.
- **M / S** — mute or solo the row.
- **◀ ▶** — scroll the sound for that row through its whole family. A kick row
  walks through every kick in every kit; a snare row walks through every snare.
  The sound name in the Mixer tab updates to match.
- **CLR** — clear that row.
- **Copy row / Paste row** — pick the row in the dropdown, copy it, pick another
  row, paste it. **Clear all** empties the whole grid.

The orange highlight shows the step that is playing right now.

## 4. Pads tab

Thirty pads, one per voice. **Tap a pad to play it.** Tap near the top of the
pad for a hard hit, near the bottom for a soft one.

**Right-click a pad** for a menu: favorite it (★), mute it, solo it, or clear
the sample loaded on it. A star in the corner marks a favorite.

## 5. Mixer tab

One Logic-style strip per voice, left to right:

- **Fader** — the voice's volume.
- **PAN** — left / right.
- **TONE** — a tilt EQ. Turn it right for brighter, left for darker.
  (In the web app this knob did nothing — here it works.)
- **SAT** — saturation. A little adds warmth and grit; a lot distorts.
- **REV / DLY** — how much of this voice goes to the hall reverb and to the
  tempo-synced echo. These are sends, Logic-style: the dry sound stays, and the
  effect is added on top.
- **M / S** — mute / solo the channel.
- **AUX M / S** — mute the sends, or solo the sends (soloing the sends drops
  the dry signal, so you hear the effect only — live only).
- **Meter** — the thin bar at the right of each strip shows the level.

Above the strips are the two **returns**: Reverb return (how loud the hall is
overall) and Echo return (how loud the echo is overall).

## 6. Sampler tab

Put your own sounds on the pads.

**Load a file:** pick a voice, press **Load audio file…**, choose any WAV, AIFF,
MP3, FLAC or OGG. The sample replaces the synth sound for that voice. Press
**Clear sample** to go back to the synth.

**Record:** the plugin records from its **sidechain input**. In Logic, open the
sidechain menu in the plugin header and choose your input — for example your
Apollo / UAD input 1. (In the Standalone app, pick your interface in the audio
settings.) Pick the voice, press **● Record**, play or sing into the input —
the **INPUT** meter shows the level — and press **● Record** again to stop.
The take lands on the voice as a sample.

**Tempo lock:** when on, the sample plays in time with the tempo — a loop
recorded at one tempo follows the plugin when the tempo changes.

**Chop:** splits a sample into slices you can send to pads.

1. Pick the voice with the sample.
2. Choose **Transient** (cut where the sound attacks) or **Meter / grid**
   (cut on the bar, half bar, beat, eighth or sixteenth).
3. Pick a sensitivity — Gentle, Balanced or Fine.
4. Press **Chop sample**. The slices appear in the list.
5. Pick a slice, pick a target voice, press **Send selected slice to pad**.

The slice keeps the tempo-lock setting of the sample it came from.

## 7. Library tab

Your own drum sounds, searchable.

1. Press **Scan a folder…** and choose a folder of drum sounds on your Mac.
   The plugin walks the folder (including subfolders) and sorts every file
   into a voice by its name — kicks, snares, hats, toms, congas, tombak,
   darbuka, and so on.
2. Type in the search box to narrow the list, or tick **★ Favorites only**.
3. Pick a voice, select a sound, press **Load selected onto voice** — or
   double-click a sound to load it straight onto the chosen voice.
4. **★ / ☆ Favorite** stars the selected sound so you can find it again.

## 8. Stems tab

Drop in a full song or a drum loop and split it into six parts:

| Stem | What it holds |
|---|---|
| Low | Kick and low end (below ~150 Hz) |
| Snare & Clap | The backbeat body (~180 Hz to 1 kHz) |
| Percussion | Shakers, congas, mid instruments (~1–6.5 kHz) |
| Hats & Cymbals | Top-end air (above ~6.5 kHz) |
| Centre | Whatever sits in the middle of the stereo image — usually the vocal |
| Sides | The stereo sides only — wide parts, doubles, reverb tails |

**This is a frequency-band filter split, not AI separation.** It is useful for
grabbing a kick, a hat, or a vocal-ish centre out of a loop — it will not cleanly
isolate a single instrument the way a dedicated AI stem tool does.

For each stem you can **Play** it, **Export WAV**, or send it straight to a pad
(pick the voice, press **→ Pad**).

## 9. Export tab

- **Export MIDI (.mid)** — the pattern as MIDI notes on channel 10, 96 ticks
  per quarter note. Drop it into Logic and it will play any drum instrument.
- **Export loop WAV (4 bars)** — the pattern rendered as a stereo WAV, 4 bars
  plus a short tail. No click, no sends on the render — the dry pattern.
- **Export per-voice stems (.zip)** — one dry WAV per sounding voice, zipped.

## 10. Automation and saving

These can be automated from Logic, and they are saved with the project:

- Tempo, Swing, Swing Interval, Time Signature, Kit, Master Volume, Click,
  Reverb Return, Echo Return, Sync to Host, Follow Host Transport, MIDI Out
- Per voice: Volume, Tune, Pan, Tone, Saturation, Send Reverb, Send Echo

The pattern itself — steps, accents, mute/solo, favorites, and any samples you
loaded — is saved with the Logic project (or the plugin's own preset), not as
automation. Samples are stored inside the saved state, so a project reopens
with its sounds intact.

**MIDI Out:** when on, the plugin also sends the pattern out as MIDI notes on
channel 10, so Logic can record the groove or drive another drum instrument
with it.

## 11. Troubleshooting

- **No sound:** check the track is record-enabled or monitored, the Master
  volume is up, and no channel is soloed by accident. The click is live-only.
- **Logic doesn't list the plugin:** check the bundle is in
  `~/Library/Audio/Plug-Ins/Components/`, then open Logic's Plug-In Manager
  and rescan. In Terminal, `auval -v aumu SLM8 Sltn` shows the validation result.
- **Recording hears nothing:** the sidechain must be set in the plugin header
  (Logic) or the audio device in the settings (Standalone), and the input must
  be live. Watch the INPUT meter in the Sampler tab.
- **A sample plays at the wrong speed:** turn **Tempo lock** on and make sure
  the tempo matches the tempo the loop was recorded at; or turn it off to play
  the sample at its natural speed.
- **The stem split sounds rough:** that is expected — it is a filter split, not
  AI. The Low stem is the cleanest; Centre and Sides are the roughest.

## 12. What the plugin is (and isn't)

- It is the Silktone drum machine as a native instrument: same 30 voices,
  31 kits, 166 patterns, same sequencer, mixer, sampler, library and exports.
- The reverb is an algorithmic hall, not the web app's impulse convolver —
  close in character, not bit-identical.
- The TONE knob works here; in the web app it did nothing.
- Favorites are saved with the session here; in the web app they lasted only
  until the page was closed.
- There is no gain knob — Joel asked for one, then dropped it.

*Built for Joel Thomas Whitley (Silktone) — Sounds Good Ent.*

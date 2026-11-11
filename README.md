# Tactus

A free, open-source digital audio workstation built around a fast pattern-based workflow.
No subscriptions, no accounts, no feature unlocks, no limits, no watermarks.

"Tactus" is a working title (the Renaissance word for the beat); it is used in one place
per layer and is cheap to change.

**Status: Phases 1 and 2 complete, Phase 3 in progress** — a working desktop application that creates
patterns, arranges them into a song, plays, saves and reopens them, with full undo. See [docs/STATUS.md](docs/STATUS.md) for exactly what works and
what does not yet, and [docs/ROADMAP.md](docs/ROADMAP.md) for the plan.

## What you can do today

- Program drums and melodies in a step sequencer (several channels, several patterns,
  4–128 steps, swing).
- Edit notes in a piano roll: add, move, resize, delete, audition; snap from 1/4 to 1/64 or off.
- Arrange patterns into a song: clips on tracks, move, resize / loop, duplicate, split,
  track mute / solo, loop region; switch between pattern and song playback.
- Import audio files (WAV, AIFF, FLAC, Ogg, MP3) as clips: waveform, trim, split, duplicate.
- Record audio from a microphone or line input straight into the arrangement.
- Backups of earlier saves, autosave with crash recovery, collect audio into the project folder.
- Select several notes or clips; copy, cut, paste, duplicate, transpose, nudge, quantise.
- Undo and redo every edit.
- Mix: master and inserts with faders, balance, mute / solo, meters, buses and sends.
- 12 effects (EQ, compressor, limiter, reverb, delay, distortion, chorus, flanger, phaser,
  stereo imager, noise gate, filter) with presets and savable effect chains.
- Play through a built-in polyphonic synth (two oscillators, sub, noise, unison, filter
  envelope, LFO, glide; 20 presets, and your own saved as files) and four synthesised drum voices.
- Play your own samples: a sampler (pitch by key, loop, reverse, envelope, multi-sample key
  zones) and a 16-pad drum kit; slice a loop into equal parts or at its hits.
- Set tempo and time signature, loop or play once, use a metronome, pause and resume.
- Mix each channel with volume, pan and mute; watch output level, CPU load and underruns.
- Choose the audio device, sample rate and buffer size.
- Save and reopen projects (`.tactus`, documented JSON, written atomically).

## Build

Requirements: CMake 3.24+, Ninja, a C++20 compiler (Visual Studio 2022 or newer on Windows), git.

```powershell
.\dev.ps1 bootstrap   # once: fetches JUCE 9.0.5 into third_party\
.\dev.ps1 test        # builds everything and runs the tests
.\dev.ps1 run         # builds and starts the application
```

`Tactus.exe path\to\song.tactus` opens that project at start.

On Linux and macOS use CMake directly (not yet tested there — see STATUS):

```sh
cmake -S . -B build/Release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/Release
./build/Release/tests/tactus_tests
```

## Layout

| Path | Contents |
|---|---|
| `engine/` | Everything that makes sound or stores music. No GUI code; usable headless. |
| `app/` | The desktop application (JUCE GUI) on top of the engine. |
| `tests/` | Headless unit and workflow tests; they link the engine only. |
| `docs/` | Architecture, project format, roadmap, status. |
| `third_party/JUCE/` | Fetched by `dev.ps1 bootstrap`; not committed. |

## Shortcuts

| Key | Action |
|---|---|
| Space | Play / stop |
| Ctrl+N / Ctrl+O | New / open project |
| Ctrl+S / Ctrl+Shift+S | Save / save as |
| R | Record audio (Stop or Space ends the take) |
| Ctrl+I | Import audio files into the arrangement |
| Ctrl+Z / Ctrl+Y | Undo / redo |
| Ctrl+A / C / X / V / D, Delete | Select all, copy, cut, paste (under the mouse), duplicate, delete — notes or clips |
| Arrows, Q (piano roll) | Transpose (Shift = octave) / nudge the selection; quantise |
| Shift+click, Ctrl+drag on empty space | Add to the selection; rubber-band select |
| L | Switch between pattern and song playback |
| F2 / F3 / F9 | Show piano roll / arrangement / mixer |
| Ctrl+Q | Quit |
| Mouse wheel (piano roll) | Scroll; Shift = sideways; Ctrl = zoom time |
| Right-click (piano roll, arrangement) | Delete note / clip (drag to delete several) |
| Ctrl+drag / Alt+click (arrangement) | Duplicate / split a clip |
| Ruler (arrangement) | Click = move playhead, drag = loop region, right-click = clear it |

## Licence

Tactus is licensed under the **GNU Affero General Public License v3.0** (see [LICENSE](LICENSE)).

This follows from its main dependency: the JUCE framework is dual-licensed under the AGPLv3
and a commercial JUCE licence. Tactus uses JUCE under the AGPLv3, which is free and fits a
free open-source DAW. Anyone who wanted to distribute a closed-source build would need a
commercial JUCE licence instead — see `third_party/JUCE/LICENSE.md` after bootstrapping.

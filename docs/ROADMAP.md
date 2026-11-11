# Roadmap

A phase is finished only when its critical features work in the real application and are
covered by tests. Current state per feature: [STATUS.md](STATUS.md).

## Phase 1 — Functional foundation  ✅ done

Window and main interface, audio device management, real-time engine, transport, project
save/load, piano roll, note playback, pattern step sequencer, built-in synth and drum voices.

## Phase 2 — Arrangement  ✅ functionally complete

Order matters here: undo first, because every later feature adds edit operations.

1. ✅ **Undo/redo** for every edit (whole-project snapshots behind `session.edited()`).
2. ✅ **Arrangement view**: tracks, pattern clips placed on a timeline, move / duplicate / split /
   resize / loop, snap, loop region, song vs pattern playback mode, track mute / solo.
3. ✅ Pattern instances stay linked to their source; duplicating a pattern creates a variation.
4. ✅ Copy / paste and multi-selection in the piano roll and the arrangement; quantise,
   transpose, nudge. (Time selection still open.)
5. ✅ **Audio clips**: import WAV/FLAC/AIFF/MP3/OGG, waveform display, non-destructive trim
   and split. (Open: left-edge trim, fades, streaming long files from disk.)
6. ✅ **Recording**: input device selection, monitoring, latency compensation, recorded files
   stored inside the project folder.
7. ✅ Backups on save, autosave with crash recovery, collect audio into the project's `audio/` folder.
8. Carried forward: ASIO support; CI builds for Windows, Linux and macOS; piano-roll velocity
   lane; recording track arming, manual latency offset and count-in.

## Phase 3 — Mixer and instruments  (in progress)

1. ✅ **Mixer**: master and inserts with fader, balance, mute, solo and meters; channels and
   tracks route into inserts; inserts route to buses to their right; post-fader sends.
2. ✅ **Effects**: parametric EQ, compressor, limiter, reverb, delay, distortion, chorus,
   flanger, phaser, stereo imager, noise gate, filter — real DSP, bypass, ordering, factory
   presets, effect-chain presets.
3. **Sampler** (ADSR, loop, reverse, slicing, multi-sample) and **drum sampler** with pads.  ← next
4. **Synth upgrade**: second oscillator, sub, noise, LFO, unison, portamento; preset library.
5. **Content browser**: search, favourites, user folders, audio preview (SQLite index).

## Phase 4 — Advanced production

Automation clips (linear / curved / stepped) for any parameter with conflict rules; advanced
piano roll (pan, pitch-bend and modulation lanes, probability, chords, scale highlighting,
MIDI file import/export); time-stretch and pitch-shift; MIDI input, recording, MIDI learn and
controller mapping; sidechain and advanced routing.

## Phase 5 — Plugin ecosystem

VST3 hosting (instruments and effects), AU on macOS, LV2 on Linux; out-of-process scanning;
plugin browser; plugin state in projects; missing-plugin handling; MIDI output and clock.

## Phase 6 — Professional polish

Offline export (WAV, FLAC, MP3; bit depth, sample rate, dither, normalise, region), crash
recovery, multi-threaded rendering, themes including light, UI scaling, configurable
shortcuts, saved layouts, accessibility, user documentation.

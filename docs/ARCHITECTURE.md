# Architecture

This document records the decisions that shape the code, and why they were made.

## 1. Technology stack

| Choice | Reason |
|---|---|
| **C++20** | Deterministic memory and timing on the audio thread; the language every plugin SDK (VST3, AU, LV2) is written for. |
| **JUCE 9** for devices, plugin hosting *and* UI | One framework covers WASAPI/ASIO/CoreAudio/ALSA/JACK, MIDI, VST3/AU/LV2 hosting, audio file I/O and a GPU-friendly cross-platform UI. |
| **No Qt** | Qt would add a second event loop, a second build system integration and a second licence to reconcile, and plugin editor windows embed more simply in a JUCE window. A second UI framework is exactly the "technology because it is popular" the brief warns against. |
| **No Rust (for now)** | The real-time core is small and the plugin/host boundary is C++ on both sides. Rust stays an option for isolated, non-real-time parts (e.g. a plugin-scanner process). |
| **JSON project files** | Human-readable, diffable, easy to migrate between versions. Audio stays in separate files next to the project. |
| **No database yet** | SQLite is planned for the sample/plugin browser index (Phase 3/5), where queries over thousands of files justify it. Settings use a small properties file. |
| **CMake + Ninja** | The one build description that works on Windows, Linux and macOS. |
| **Own ~60-line test harness** | No dependency to fetch; the tests are plain functions. Replaceable by Catch2 if it is ever outgrown. |

Licence consequence: JUCE is AGPLv3 or commercial, so Tactus is AGPLv3.

## 2. Layers

```
app/      UI (JUCE GUI)           knows the engine, never the reverse
engine/   model + real-time core  no GUI headers (enforced by CMake at configure time)
tests/    headless                link the engine only
```

All JUCE modules are compiled once into the static library `tactus_juce`. The rule "engine
never sees the GUI" is enforced by a configure-time check in the root `CMakeLists.txt` that
fails the build if any file under `engine/` includes a `juce_gui_*` or `juce_graphics` header.

### Engine modules (`engine/`)

| Module | Files | Responsibility |
|---|---|---|
| Core | `core/Types.h`, `core/SpscQueue.h` | Tick resolution (960 PPQ), `Note`, the wait-free queue. |
| Model | `model/Project.*` | The document: channels, patterns, notes, tempo. Plain data. |
| Persistence | `model/ProjectSerializer.*` | `.tactus` read/write, validation, atomic save. |
| Undo | `model/UndoHistory.*` | Undo / redo stacks of project states. |
| Editing | `model/Editing.*` | Selection-based operations on notes and clips. |
| Audio pool | `engine/AudioPool.*` | Decodes and caches audio files; waveform peaks. |
| Recorder | `engine/Recorder.*` | Captures the input to a WAV file, aligned to the timeline. |
| Project files | `model/ProjectFiles.*` | Backups of earlier saves; collecting audio into the project folder. |
| DSP | `dsp/Instrument.h`, `PolySynth`, `DrumSynth` | Sound generators behind one interface. |
| Effects | `model/EffectInfo.*`, `dsp/Effects.*` | Effect parameter tables and the 12 effect processors. |
| Snapshot | `engine/Snapshot.*` | Immutable compiled form of the project for the audio thread. |
| Audio engine | `engine/AudioEngine.*` | Transport, sequencer, rendering, mixing, metering. |
| Controller | `engine/EngineController.*` | Compiles Project → Snapshot; owns instrument lifetimes. |
| Device host | `engine/DeviceHost.*` | Connects the engine to a sound card. |

Planned modules slot in beside these: `arrangement/` (Phase 2), `mixer/` and `fx/` (Phase 3),
`automation/` (Phase 4), `plugins/` (Phase 5), `export/` (Phase 6).

## 3. Threads and the real-time rule

Two threads matter:

- **Control thread** (the UI/message thread): owns and edits the `Project`.
- **Audio thread**: runs `AudioEngine::process()`.

`process()` never allocates, frees, locks, or performs I/O. The two threads share nothing
mutable except:

1. a wait-free single-producer/single-consumer **command queue** (control → audio):
   play, pause, stop, live note on/off, "here is a new snapshot";
2. a wait-free **garbage queue** (audio → control): snapshots the audio thread has finished with;
3. a few **atomics**: tempo, metronome, loop, master gain (in), playhead, peaks, clip flag (out).

### Snapshots

The audio thread never reads the `Project`. After every edit the controller compiles an
immutable `Snapshot` (sorted notes per channel, gains, synth parameters, loop length) and
queues a pointer to it. The audio thread swaps pointers at the start of a block and sends the
old one back; the control thread deletes it. So:

- edits are atomic from the audio thread's point of view — it sees the old project or the new
  one, never a half-edited one;
- the audio thread never frees memory;
- a burst of edits (dragging a note) cannot overflow anything: an unsent snapshot is simply
  superseded, and the number in flight is capped.

Per-channel `ChannelProcessor`s (the instrument and its ramp state) are shared between
successive snapshots via `shared_ptr`, so an edit never cuts a sounding note. Their reference
counts only change on the control thread.

This rebuilds the whole snapshot per edit. That is trivially cheap at Phase 1 sizes; when
projects grow, the snapshot will be split so that only the edited pattern is recompiled.

## 4. Timing

- Musical time is integer **ticks**, 960 per quarter note.
- The transport position is `anchorTick + samplesSinceAnchor × ticksPerSample`. Counting whole
  samples from an anchor (moved only on loop wrap, tempo change, pause and stop) means no
  rounding error accumulates block after block.
- A note whose time falls between two samples plays on the later one, so every note is at
  most one sample late and never early.
- At a loop wrap the sub-sample remainder is carried into the next pass, so loops do not drift
  (tested over 200 loops at a tempo/sample-rate pair with fractional step lengths).
- `fireFloorTick` records what has already been played when the anchor moves, so a tempo
  change or a pause/resume can neither double-trigger nor skip a note (tested with 20 000
  random tempo changes).
- Blocks are split at note boundaries, so timing is sample-accurate and independent of the
  device buffer size (tested from 1 to 4096 samples).
- Sequenced notes carry their length in samples to the instrument at note-on, so an edit or a
  loop wrap can never leave a note hanging. Known simplification: a note already sounding
  keeps its original length if the tempo changes mid-note.

## 5. Click-free parameter changes

Channel gain (volume, pan, mute) and master gain are ramped linearly across each block; the
synth's filter cutoff moves towards its target in small control-rate steps. A test slams a
fader shut mid-note and checks the output has no step.

## 6. Unified notes

The step sequencer and the piano roll edit the same data: a pattern holds a list of notes per
channel. A lit step is a note starting on that sixteenth. There is no separate "step data" to
keep in sync, which is why a note drawn in the piano roll shows up in the step grid.

## 6a. Undo and redo

Undo is by whole-document snapshot: `UndoHistory` (in the engine library, so it is testable
headless) stores the `Project` as it was before each edit. Undo puts it back; redo swaps the
other way. Because the Project is plain data, *every* edit is undoable by construction —
there is no per-operation undo code that a new feature could forget to write.

- `session.edited ("Name")` records one step. `session.editedContinuous ("Name")` is for drags
  and sliders: all steps of one gesture collapse into one undo step, closed by
  `session.endGesture()` on mouse-up (with a 1.5 s timeout as a fallback).
- Cost: one Project copy per edit, 200 steps kept. This is the simplest thing that is
  correct; when projects carry a lot of data the copies will share unchanged patterns
  (copy-on-write), without changing the interface. Audio never lives in the Project, so
  recordings will not be copied.

## 6b. Arrangement and song playback

The arrangement is part of the Project: `Track`s and `Clip`s. A clip *refers* to a pattern
(by id) and has a start, a length and an offset into the pattern. It never copies notes, so
editing a pattern changes every clip that uses it; a variation is made by duplicating the
pattern. A clip longer than its pattern repeats it; splitting a clip just creates a second
clip whose offset continues where the first one stops.

For playback, `compileSongNotes()` flattens the arrangement into one sorted note list per
channel in song time, and the engine plays that list exactly as it plays a single pattern.
So song mode needed no new real-time code beyond a loop start and a locate command, and it
inherits the sequencer's timing guarantees. Track mute and solo are applied at compile time.

The transport has two modes: **pattern** (loop the pattern being edited) and **song** (play
the arrangement, looping the whole song or the loop region).

## 6c. Editing operations

Selection-based operations (copy, paste, delete, duplicate, transpose, nudge, quantise for
notes; copy, paste, delete, duplicate for clips) are plain functions in `model/Editing.*`.
The editors only translate mouse and key input into calls to them, so the behaviour is
tested headless and is the same wherever it is invoked from. Note selections are index
lists (dropped whenever undo or a new project replaces the notes); clip selections are ids.

## 6d. Audio clips

- **Model.** The project lists the `AudioFile`s it uses; a clip with an `audio` id is an audio
  clip, with an offset into the file (seconds) and a gain. Samples never enter the Project,
  so undo snapshots and project files stay small, and trimming or splitting is by
  construction non-destructive.
- **Loading.** `AudioPool` (control thread) decodes a file completely into memory once and
  shares it immutably (`shared_ptr<const AudioData>`) with snapshots and the waveform
  painter. This keeps the audio thread free of disk I/O with no reader thread to get wrong;
  disk streaming for very long files can replace it behind the same interface.
- **Playback.** Snapshots carry the audio clips of audible tracks. The engine converts each
  clip's start tick to an exact output sample with the same function it uses for notes, then
  reads the file with 4-point Hermite interpolation (exact when the file and device rates
  match).
- **Edges.** A clip fades in over its first 96 samples and fades out over the 96 samples
  *after* its end. Two pieces of one file placed end to start therefore cross-fade back into
  exactly the original signal — a split is inaudible (tested) — and no edge clicks.
- **Missing files** are reported, drawn in red and skipped; the rest of the song plays.

## 6e. Recording

- `Recorder` (engine library) is driven by the device callback right after the engine has
  rendered a block. Its audio-thread half copies the input into a lock-free FIFO; a background
  thread writes the FIFO to a 24-bit WAV. Nothing on the audio thread blocks, allocates or
  touches the disk; if the disk falls behind, blocks are dropped and *counted*, and the user
  is told the take has gaps.
- A take is armed first and starts with the first block during which the transport plays,
  so its first frame has an exact song position. It is placed at that position with the
  driver's round-trip latency skipped at the start of the file.
- While recording, song mode is "open-ended" (no end, no loop), so a take is never cut off
  or wrapped.
- Recordings stream to disk, so their length is limited by free space, not memory.
- The input device is opened only for recording or monitoring and released afterwards: the
  application does not hold the microphone just by being open.

## 6f. Protecting the user's work

Four independent layers, each simple:

1. **Atomic save**: a project file is written beside the target and swapped in.
2. **Backups**: before a save replaces a file, the old version is copied to `backups/`
   (newest 10 kept), named with the time it was written.
3. **Autosave**: every 20 s, unsaved work is written to the settings folder — never over
   the project file. It is deleted when the project is saved or the application closes
   normally, so its presence at start-up means a crash, and the user is offered recovery.
4. **Audio is never embedded or rewritten**: recordings go straight to their own files, and
   "Collect audio" copies (never moves) used files beside the project.

## 6g. Mixer and effects

- **Model.** `Project::mixer[0]` is the master; the rest are inserts in left-to-right order.
  Channels and arrangement tracks name the insert they play into (0 = master). An insert has
  an output and post-fader sends, and **may only route to the master or to an insert to its
  right** (`Project::canRoute`). That one rule makes feedback loops impossible, gives the
  engine a fixed processing order, and keeps routing easy to read. The loader rejects any
  file that breaks it.
- **Engine.** The snapshot lists inserts in processing order with the master last. Per
  segment, sources render into their insert's buffer; each insert then runs its effects,
  applies its fader (ramped), meters, and adds into its output and sends — always buffers
  later in the list. The master's buffer is the output. All buffers live in `InsertState`
  objects that persist across snapshots and are sized in `prepare()`; nothing allocates on
  the audio thread.
- **Effects.** One table (`model/EffectInfo`) describes every effect's parameters — range,
  default, unit, display format, presets. The DSP, the file format and the generic editor
  in the UI all read it, so adding an effect means one table entry and one DSP class.
  Effect instances persist across edits (keyed by slot id) so tails and filter states
  survive parameter changes. Parameters are smoothed in 32-sample steps; switching an effect
  on or off cross-fades over 10 ms; a fully bypassed effect costs nothing and is reset, so
  re-enabling it starts clean. No effect has latency, so no delay compensation is needed yet;
  that changes with look-ahead processing and plugins (Phase 5).
- **Solo** is resolved when the snapshot is compiled: a soloed insert, everything feeding it
  and everything it feeds stay audible; the rest are silenced at their faders.

## 7. UI structure (`app/`)

`Session` holds the open `Project` plus the editing context (current pattern, selected
channel). Every edit is "change the Project, call `session.edited()`", which re-syncs the
engine, marks the document dirty and broadcasts a change; panels listen and refresh from the
Project. Panels never talk to each other.

| Component | Role |
|---|---|
| `MainComponent` | Menu, commands/shortcuts, document open/save flow, layout, status bar. |
| `TransportBar` | Transport, tempo, time signature, pattern selection, position, meters. |
| `ChannelRack` | Step sequencer: one row per channel. |
| `PianoRoll` | Note editor for the selected channel. |
| `ArrangementView` | Tracks and pattern clips; shares the lower half of the window with the piano roll. |
| `MixerView` | Mixer strips and the selected insert's routing, effect chain and effect controls (generated from `EffectInfo`). |
| `InstrumentPanel` | Parameters of the selected channel's instrument. |
| `Theme` | Palette and look-and-feel; components take colours only from here. |

## 8. Error handling

Functions that can fail return a message (empty = success); the UI shows it and logs it.
Nothing is swallowed: transport commands are `[[nodiscard]]`, a failed save aborts whatever
was going to happen next (New/Open/Quit), and a failed load leaves the open project untouched.
The log is at `%APPDATA%\Tactus\tactus.log` (and the platform equivalents).

## 9. Risks and dependencies

| Risk | Plan |
|---|---|
| **ASIO** needs Steinberg's ASIO SDK at build time. | Add as an optional, separately fetched dependency in Phase 2; WASAPI works meanwhile. |
| **Plugin crashes** take the host down. | Phase 5: scan plugins in a child process first (cheap, removes most crashes); full out-of-process hosting is a later, larger step. |
| **Time-stretch / pitch-shift quality** is a research-grade problem. | Phase 4: integrate an existing library (Rubber Band is GPL-compatible) rather than write one. |
| **MP3 export** licensing. | Patents have expired; use LAME (LGPL) as an optional component. WAV and FLAC first. |
| **Undo memory** grows with project size (whole-project snapshots). | Share unchanged patterns between snapshots when measurements call for it. |
| **Snapshot rebuild cost** grows with project size. | Split per pattern/track when profiling says so. |
| **Linux/macOS** are untested so far. | Add CI builds for both in Phase 2. |
| **JUCE 9 is recent.** | The API is checked against the vendored headers rather than memory; the version is pinned. |

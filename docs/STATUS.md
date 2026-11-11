# Status

Last updated: 2026-11-11 — Phase 3: mixer, effects, sampler, drum kit and the synth upgrade done.
Phase 2 is functionally complete; its remaining items are listed below and carried forward.
Build: Windows 10, MSVC 14.51 (VS 2026), JUCE 9.0.5, Release. Tests: 122 of 122 pass.

"Works" below means: implemented in the engine, reachable in the application, and checked —
by an automated test, by driving the running application, or both (stated per line).

## Works

| Feature | Checked by |
|---|---|
| Application window, menus, dark theme, resizable rack / lower-view split | Ran the app |
| Audio output via WASAPI; device, sample rate and buffer selection; setup remembered | Ran the app (device opened at 48 kHz / 480 samples); settings dialog not exercised |
| Play / stop / pause, loop on-off, metronome, playhead display | Tests + ran the app |
| Tempo 20–400 bpm (live), time signature | Tests (tempo) + ran the app |
| Sample-accurate sequencing at any buffer size; no drift; no skipped or doubled notes | Tests |
| Step sequencer: toggle, drag-paint, playhead, 4–128 steps, swing | Tests (model, swing) + ran the app |
| Multiple patterns: new, duplicate, rename, delete, switch | Model tests; menu not exercised in the running app |
| Piano roll: add, move, resize, delete, audition, snap, scroll, zoom, auto-scroll to notes | Ran the app (add / move / delete / auto-scroll); resize, zoom and keyboard audition not exercised |
| Polyphonic synth (4 waveforms, resonant low-pass, ADSR, 16 voices) | Tests |
| **Synth upgrade**: second oscillator (pitch, fine tune), sub oscillator, noise, unison (up to 7 detuned copies, stereo width), filter envelope, LFO (5 shapes) to pitch / cutoff / level, glide, mono legato mode | Tests (each one measured: added partials, detune, loudness and width of unison, glide pitch over time, legato without a new attack, LFO depth, click-free square tremolo, filter envelope) + ran the app (knobs change the sound while playing, each drag is one undo step; Mono button; LFO shape list) |
| The default synth patch is unchanged, sample for sample; projects saved before the upgrade sound as they did | Tests |
| **Instrument view** (F4): all controls of the selected channel's instrument side by side, with envelope graphs; the narrow panel beside the piano roll shows the same controls in one column | Ran the app |
| **Synth presets**: 20 built-in sounds (bass, lead, pad, keys, FX); save / load your own as `.tactussynth` files | Tests (every preset makes sound, round trip, bad files rejected) + ran the app (chose presets and heard them on the meters, saved a preset and loaded it back) |
| Channel rack shows each channel's kind (SYN / SMP / KIT / DRM) in the colour of its mixer insert | Ran the app |
| Drum voices: kick, snare, hi-hat, clap | Tests |
| Channel volume / pan / mute, click-free; add / rename / delete channels | Tests (audio) + ran the app (selection) |
| Output meters, clip light, CPU load, underrun counter | Ran the app (meters) |
| Save / Save As / Open / New, unsaved-changes prompt, atomic save | Tests + ran the app (Ctrl+S save and prompt); Open not exercised in the running app |
| **Undo / redo of every edit** (Ctrl+Z, Ctrl+Y / Ctrl+Shift+Z); a drag is one step | Tests (history) + ran the app (undo of split and move, undo then redo of a mute) |
| **Arrangement view**: tracks, pattern clips, zoom, scroll, bar ruler | Ran the app |
| **Clips**: place, move between tracks, resize / loop, duplicate (Ctrl+drag), split (Alt+click), delete | Tests (model) + ran the app (place, move, split); resize, duplicate and delete not exercised in the running app |
| **Linked patterns**: editing a pattern changes all its clips; duplicate = independent variation | Tests |
| **Song mode** (PAT / SONG switch, key L): plays the arrangement sample-accurately | Tests + ran the app |
| **Track mute / solo**, add / rename / colour / delete tracks | Tests (mute, solo, delete) + ran the app (mute); rename and colour menu not exercised |
| **Loop region** (drag in the ruler), **locate** (click the ruler) | Tests + ran the app (setting the region); locate click not exercised in the running app |
| Project format v2 with arrangement; v1 files still open | Tests |
| **Piano-roll selection**: click, Shift+click, rubber band (Ctrl+drag), select all; move / resize as a group | Ran the app (select all, rubber band); Shift+click and group drag not exercised |
| **Piano-roll keys**: Delete, Ctrl+C / X / V, Ctrl+D duplicate, arrows transpose / nudge, Q quantise | Tests (all operations) + ran the app (copy, paste under the mouse, transpose, delete); cut, duplicate, nudge and quantise keys not exercised |
| **Clip selection and clipboard** in the arrangement: same mouse rules, Delete, Ctrl+A / C / X / V / D; Ctrl+drag duplicates the whole selection | Tests (operations) + ran the app (Ctrl+D); the rest not exercised |
| **Audio clips**: import WAV / AIFF / FLAC / Ogg / MP3 (Ctrl+I or drop files on a track), waveform display | Tests (WAV decode, bit-exact) + ran the app (WAV import by Ctrl+I, waveform); other formats and drag-and-drop not exercised |
| **Audio playback** in song mode: sample-accurate position, gain, any file sample rate, loops with the song, obeys track mute / solo | Tests + ran the app |
| **Non-destructive audio editing**: trim from the right edge, split (inaudible at the cut), duplicate, move | Tests (split, trim) + ran the app (duplicate) |
| **Recording** (Record button or R): audio input to a 24-bit WAV, streamed to disk; the take lands in the arrangement where it was recorded | Tests (bit-exact capture, alignment, overdub workflow) + ran the app (a 2-second take from the default input) |
| Recording details: starts with playback, plays on past the song end, ends with Stop / Space; latency compensation from the driver's figures; mono or stereo input | Tests + ran the app (stereo, latency applied); mono input not exercised |
| Input monitoring and input gain (Options menu); input level in the status bar | Tests (monitor mix, gain); menu items not exercised in the running app |
| The microphone is opened only while recording or monitoring, and released afterwards | Ran the app (log shows the device reopening without input after the take) |
| Takes are saved in the project's `audio` folder (or Documents/Tactus/Recordings for an unsaved project) | Ran the app |
| **Backups**: each save keeps the previous version in `backups/`, newest 10 | Tests; not exercised in the running app |
| **Autosave and crash recovery**: unsaved work is written aside every 20 s; after a crash the next start offers to recover it | Ran the app (killed the process with unsaved edits, relaunched, recovered them — synth settings included; Discard also checked) |
| **Collect audio into project folder** (File menu): copies used audio beside the project and re-points it | Tests; command not exercised in the running app |
| **Mixer**: master + inserts, each with fader (to +6 dB), balance, mute, solo, stereo peak meter, name, colour (F9) | Tests (fader, balance, mute, solo, meters) + ran the app (meters while playing) |
| **Routing**: channels and arrangement tracks play into an insert (right-click menu); inserts output to the master or to a bus to their right; post-fader sends | Tests (routing, sends, rerouting on delete) + ran the app (output menu); right-click routing menus not exercised |
| Routing can never loop: only routes to the right are offered or accepted | Tests |
| Each new channel gets its own insert; new projects have a limiter on the master | Tests (limiter keeps output under the ceiling) + ran the app |
| **12 effects**: parametric EQ, compressor, limiter, reverb, delay (stereo / ping-pong), distortion (soft / hard / fold), chorus, flanger, phaser, stereo imager, noise gate, filter (LP / HP / BP / notch) | Tests (each effect measured: EQ gain, compression ratio, limiter ceiling, exact echo time, reverb decay, filter slopes, gate, width...) + ran the app (distortion) |
| Effect chains: add, reorder, remove, bypass with a 10 ms cross-fade; parameter changes smoothed (no zipper noise) | Tests (order matters, click-free bypass, smoothing) + ran the app (add, bypass, undo); reorder not exercised |
| Factory presets for every effect; effect-chain presets saved / loaded as `.tactuschain` files | Tests (chain round trip) + ran the app (a preset); chain save / load not exercised |
| Project format v3 with the mixer; v1 and v2 projects still open | Tests |
| **Sampler** channel: plays an audio file (any supported format, mono or stereo) at a pitch set by the key, root key and tuning; 32 voices | Tests (bit-exact playback, pitch, file rate, stereo, voice stealing) + ran the app (loaded a file with Load, played it) |
| Sampler shaping: start / end, reverse, loop with a seamless cross-fade, gain, pan, ADSR envelope, one-shot or gated by the note length | Tests (each one measured) + ran the app (loop, reverse and gain controls; each is one undo step); start / end, tune, pan and envelope controls not exercised |
| **Zones**: several samples per sampler, each with a key range and root key; overlapping zones layer; "Spread zones across the keys" and automatic mapping of files named after their notes (multi-sampling) | Tests (key ranges, layering, spreading); adding several files, the key-range controls and name mapping not exercised in the running app |
| **Drum kit** channel: 16 pads from C4 up, one sample each; click a pad to hear it; the step sequencer plays pad 1, the piano roll plays them all | Tests (pads, empty pad) + ran the app (pads shown, pad selected and auditioned, pattern played with level on the meters, pad menu opened); loading onto a pad and clearing it not exercised |
| **Slicing**: cut a sample into 2–32 equal slices or at each hit (transient detection); the slices land on consecutive keys | Tests (slice positions, slices replayed in order rebuild the file, detection within 4 ms on a synthetic loop) + ran the app (a four-hit loop sliced at its hits); equal slicing not exercised in the running app |
| Editing a sampler while it sounds never clicks: voices carry on through changes that do not move them in the file, and are faded when they must stop | Tests |
| Dropping audio files on the sampler panel or on a pad | Not exercised (written, builds) |
| Missing sample file: the zone is silent, the rest plays; pad shown in red; listed in the missing-audio dialog | Tests + ran the app |
| Project format v4 with samplers; v1–v3 projects still open | Tests |
| Project format v5 with the larger synth; v1–v4 projects still open | Tests |
| A project file given on the command line is opened at start | Ran the app |
| **Missing audio**: project still opens and plays, clips shown in red, a dialog lists the files; files moved beside the project are found | Tests (lookup, playback without the file) + ran the app (dialog shown for a missing sample) |
| Log file | Ran the app |

## Carried over from Phase 2

- Audio clips: trim from the left edge, fades, streaming long files from disk
- Recording: choosing the target track, a manual latency offset, count-in, punch in / out,
  several inputs to several tracks at once
- ASIO; CI builds for Linux and macOS
- Time selection
- Piano roll: velocity lane
- Track grouping

## Phase 3 — still to do

- Sampler presets (the synth has them; a sampler's sound can only be saved with its project)
- Sampler: velocity layers, choke groups (open / closed hi-hat), dragging the start / end / loop
  markers in the waveform, writing the notes that replay a sliced loop into a pattern
- Content browser with search, favourites and audio preview
- User presets for single effects (only factory presets and whole-chain presets exist)
- Gain-reduction meters on the compressor, limiter and gate

## Not implemented yet (later phases)

- Automation, MIDI input, pan / pitch / modulation lanes
- Plugins (VST3 / AU / LV2)
- Audio export
- Themes, UI scaling, configurable shortcuts, dockable panels, multiple windows

## Known limitations and bugs

- **Linux and macOS are untested.** The code uses only cross-platform JUCE APIs, but it has
  never been compiled there.
- Undo keeps whole copies of the project (up to 200 steps). Fine at current project sizes;
  it will need structural sharing once projects contain a lot of data.
- Undo does not restore which pattern or channel was selected, only the project content.
- After an undo the project is always marked as changed, even if it now matches the file.
- Clips on the same track may overlap; both play. There is no overlap handling yet.
- Audio files are decoded fully into memory; files longer than about 12 minutes are refused
  with a message. Loading happens on the UI thread, so a large import freezes the window briefly.
- Audio clips play at their recorded speed: changing the tempo moves later clips but does not
  stretch audio (time-stretch is Phase 4). Changing tempo *while* an audio clip plays makes
  it jump.
- Audio clips play into their arrangement track's mixer insert at their clip gain; there is
  no clip-gain control in the UI yet.
- Recording captures one stereo (or mono) input onto the first empty track; there is no
  per-track arming yet.
- Latency compensation uses what the driver reports (20 ms on the test machine with WASAPI
  shared mode). Shared-mode drivers under-report, so takes can still sit slightly late; a
  manual offset setting is not built yet.
- A take longer than about 12 minutes is safely on disk but cannot yet be loaded back into
  the arrangement (no disk streaming). The application says so and gives the file's path.
- Opening the input restarts the audio device, which causes a brief dropout when recording
  starts and ends.
- Sampler: a sample played far above its recorded pitch aliases (interpolation only, no
  band-limiting). Moving a zone's start or end, or reversing it, stops that zone's sounding
  notes (softly). The loop cross-fade is fixed at 10 ms and needs that much audio before the
  loop start. Samples are held in memory like audio clips, with the same 12-minute limit.
- Sampler and synth sound was checked by tests and by the meters in the running application,
  not by ear. The built-in synth presets in particular have never been listened to.
- Synth: one LFO per channel, shared by all its notes and not synced to the tempo. Pitch,
  cutoff and LFO are updated every 32 samples. The triangle wave is not band-limited (saw
  and square are), so very high triangle notes alias slightly. User presets are opened with Load...; they do not
  appear in the preset list.
- On 2026-11-11 the scripted checks could not send Ctrl shortcuts to any program on the test
  machine, so that day's undo checks went through the Edit menu instead of Ctrl+Z.
- A drum kit shows 16 pads (C4 to D#5); samples on other keys (after slicing into more than
  16 pieces) are reached through the sample list and the piano roll.
- The sampler's file dialog starts in the Music folder each session (the folder is not remembered).
- With no sample loaded, the greyed-out sample controls show the values of the sample viewed last.
- Mixer effects are pre-fader and all of them process all the time, even on silent inserts,
  so CPU use grows with the number of effects rather than with what is playing.
- Sends have no level ramp: moving a send level quickly can click slightly.
- The delay's time is not synced to the tempo, and changing it while echoes ring makes them
  glide in pitch (as on a tape delay).
- The distortion has no oversampling, so heavy drive on high notes aliases.
- Up to 128 mixer inserts have meters; more inserts work but show no level.
- Soloing an insert silences channels routed straight to the master.
- The engine still applies a fixed 0.9 output trim after the master fader.
- Autosave has a single slot per user: two copies of the application running at once share it.
- Audio files are referenced by absolute path. A moved project finds them only if they sit
  beside the project file or in an `audio` folder next to it; use Collect audio before moving.
- The clipboards are internal to the application (not the system clipboard).
- Undoing while notes are selected clears the selection.
- With a loop region set, placing the playhead after the region's end jumps back into it.
- New notes always get velocity 0.8; there is no velocity editing yet.
- A note already sounding keeps its original length if the tempo changes while it plays.
- The step grid shows a step as lit if any note starts on it, whatever its pitch; clearing
  the step removes all of them.
- The channel rack scrolls as a whole, so the channel controls scroll away with long patterns.
- Pattern length choices are a fixed list (4–128 steps).
- Switching project or play mode cuts sounding tails.
- No automated UI tests: UI behaviour was checked by driving the real window with scripted input.

## Broken

Nothing known.

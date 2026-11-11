# The `.tactus` project format

Version 3. Older files still open: version 1 files (no `arrangement`) get eight empty tracks,
and files before version 3 (no `mixer`) get a master with everything routed to it. A project is one UTF-8 JSON file. (From Phase 2 a project becomes a folder holding
this file plus its audio; the JSON keeps this shape.)

```json
{
  "format": "tactus-project",
  "version": 3,
  "name": "My song",
  "tempo": 124.0,
  "timeSigNumerator": 4,
  "timeSigDenominator": 4,
  "nextId": 8,
  "channels": [
    {
      "id": 5, "name": "Bass", "kind": "synth",
      "volume": 0.7, "pan": 0.0, "muted": false,
      "synth": { "waveform": 0, "cutoffHz": 900.0, "resonance": 0.35,
                 "attack": 0.004, "decay": 0.18, "sustain": 0.55, "release": 0.16 }
    }
  ],
  "patterns": [
    {
      "id": 6, "name": "Pattern 1", "lengthSteps": 16, "swing": 0.0,
      "tracks": [
        { "channel": 5, "notes": [[0, 360, 33, 0.8], [480, 360, 33, 0.8]] }
      ]
    }
  ],
  "audioFiles": [
    { "id": 16, "path": "C:/Music/takes/vocal.wav" }
  ],
  "arrangement": {
    "loopStart": 0, "loopEnd": 0,
    "tracks": [
      { "id": 7, "name": "Track 1", "muted": false, "solo": false, "colour": 0 }
    ],
    "clips": [
      { "id": 9, "pattern": 6, "track": 0, "start": 0, "length": 15360, "offset": 0 },
      { "id": 17, "pattern": 0, "track": 1, "start": 0, "length": 5952, "offset": 0,
        "audio": 16, "audioOffset": 0.0, "gain": 1.0 }
    ]
  }
}
```

## Fields

| Field | Meaning |
|---|---|
| `format` | Always `"tactus-project"`. Anything else is rejected. |
| `version` | Format version. A file with a higher version than the program knows is rejected with a clear message, never half-loaded. |
| `tempo` | Beats per minute, 20–400. |
| `nextId` | Next free id. On load it is raised above every id in the file. |
| `channels[].kind` | `synth`, `kick`, `snare`, `hihat`, `clap`. Unknown kinds are an error. |
| `channels[].volume` | Linear gain 0–1. `pan` is −1 (left) to +1 (right). |
| `synth.waveform` | 0 saw, 1 square, 2 triangle, 3 sine. |
| `patterns[].lengthSteps` | Length in sixteenth notes. |
| `patterns[].swing` | 0–1: delays every second sixteenth by up to half a step. |
| `tracks[].notes[]` | `[startTick, lengthTicks, key, velocity]`. 960 ticks per quarter note; key is a MIDI note number (60 = C4); velocity 0–1. |

### Arrangement

| Field | Meaning |
|---|---|
| `loopStart`, `loopEnd` | Loop region in ticks. `loopEnd <= loopStart` means no region. |
| `tracks[]` | Lanes, top to bottom. `colour` is an index into the UI's track palette. |
| `clips[].pattern` | Id of the pattern the clip plays. The clip refers to it; it holds no notes. |
| `clips[].track` | Index into `tracks`. |
| `clips[].start`, `length` | Position and length in ticks. Longer than the pattern = the pattern repeats. |
| `clips[].offset` | Ticks into the pattern at which the clip begins (set by splitting). |

| `clips[].audio` | Present on audio clips: id of an entry in `audioFiles` (then `pattern` is 0). |
| `clips[].audioOffset` | Seconds into the audio file at which the clip begins. |
| `clips[].gain` | Linear gain of an audio clip. |
| `audioFiles[].path` | Absolute path of the audio file. Audio is never embedded in the project. |

A clip that names a missing pattern, track or `audioFiles` entry rejects the file. An audio
*file* that is missing from disk does not: the project opens, the clip is shown as missing,
and the file is also looked for beside the project file and in an `audio` folder next to it.

### Mixer

```json
"mixer": [
  { "id": 20, "name": "Master", "colour": 0, "volume": 1.0, "pan": 0.0, "muted": false, "solo": false,
    "output": 0, "sends": [],
    "effects": [ { "id": 21, "type": "limiter", "bypassed": false,
                   "params": { "input": 0.0, "ceiling": -0.3, "release": 50.0 } } ] },
  { "id": 22, "name": "Kick", "colour": 0, "volume": 1.0, "pan": 0.0, "muted": false, "solo": false,
    "output": 0, "sends": [ { "target": 30, "level": 0.5 } ], "effects": [] }
]
```

| Field | Meaning |
|---|---|
| `mixer[0]` | The master. Its `output` is ignored (it feeds the sound card). |
| `volume` | Linear gain, 0–2 (+6 dB). `pan` is balance, −1 to +1. |
| `output`, `sends[].target` | 0 = master, otherwise the id of an insert **later in the list**. Anything else rejects the file. |
| `effects[]` | Processed top to bottom. `type` and parameter names are listed in `engine/src/model/EffectInfo.cpp`; a missing parameter takes its default, an unknown one rejects the file. |
| `channels[].mixer`, `tracks[].mixer` | Insert the channel / the track's audio clips play into; 0 = master. |

Effect-chain presets (`.tactuschain`) hold `{ "format": "tactus-effect-chain", "version": 1, "effects": [...] }`
with effects in the same form.

## Rules

- Ids are unique across channels and patterns and never reused within a project.
- Notes after the end of a pattern are stored but not played.
- Out-of-range numbers are clamped on load; structural problems (unknown channel, malformed
  note, duplicate id) reject the file with a message naming the problem.
- Saving writes a temporary file beside the target and then replaces the target in one step,
  so an interrupted save cannot corrupt an existing project.

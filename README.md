# Elderglass

A VST3 (and standalone) plugin by **Barking Noise Studios** that combines two
ambient pedals in one box, modelled on the Walrus Audio Lore and Fable:

- **Reverse:** reverse delays and reverbs, octave reverbs, dueling pitch delays
- **Granular:** delays feeding granular engines (reverse, octave up/down,
  Grain-Verb, random pitch)

The two pedals can be routed **Reverse → Granular**, **Granular → Reverse**, or
in **Parallel**. In parallel, each pedal sits in its own channel path, and
**Swap** moves the pedals between the left and right paths.

The series routings can run in **Mono** (like the hardware) or **Stereo**:
- Reverse pedal: a left and a right engine, the right one a decorrelated twin
  (slightly longer delays, different reverb taps and modulation phases).
- Granular pedal: one engine whose grains are placed semi-randomly across the
  stereo field (alternating sides, random distance from the centre).
- Mod runs 90 degrees apart on left and right.

**Width** (mid/side, 0-200 %) sets the stereo width of the output in every
routing.

Research notes are in [Docs/](Docs/).

## Download / install

The current Windows build is in [Builds/](Builds/):

- `Builds/Elderglass.vst3`: copy this folder to `C:\Program Files\Common Files\VST3`
- `Builds/Elderglass.exe`: standalone app, runs without a DAW

## Status

| Stage | Content | State |
|---|---|---|
| 1 | Engine shell, routing + swap, bypass/trails, tap tempo, Dive/Rise, tempo sync, Granular pedal (5 programs), UI | done |
| 2 | Reverse pedal (5 programs) | done |
| 3 | Final UI polish, tuning by ear | open |

## Project layout

```
CMakeLists.txt             Fetches JUCE, defines the plugin and test targets
Source/
  PluginProcessor.*        Routing, swap crossfade, latency, state
  PluginEditor.*           WebView editor; one relay per parameter, named by ID
  Parameters.*             Parameter layout (prefix rev_ / gran_), tap tempo
  DSP/
    PedalCore.h            Shared pedal shell: mix, bypass/trails, mod,
                           program fades, Dive/Rise clock
    ClockedResampler.h     Runs a pedal on its own clock (Dive/Rise)
    PedalModel.h           Model interface + analog feedback path
    DelayStage.h           Forward / reverse / analog delay "chip"
    GrainStage.h           Granular "chip"
    GranularModel.h        Granular pedal programs
    ReverseModel.h         Reverse pedal programs
    FdnReverb.h            8-line modulated FDN reverb
    PitchShifter.h         Two-head pitch shifter (octaves, intervals)
Tests/DspTest.cpp          Offline DSP checks (stability, latency, transparency)
UI/                        HTML/CSS/JS, embedded into the binary
Docs/                      Research notes
Builds/                    Latest Release build (updated by every Release build)
```

## Building (Windows)

```powershell
cmake --preset vs2022
cmake --build --preset vs2022-release
build\Release\ElderglassDspTest.exe     # offline DSP checks
```

The first configure clones JUCE via `FetchContent`, so it is slow once.

A Release build also copies both outputs to [Builds/](Builds/):
- `Builds/Elderglass.vst3`
- `Builds/Elderglass.exe` (fastest way to try it)

The plugin is not copied into the system VST3 folder automatically, because that
needs admin rights. Copy it to `C:\Program Files\Common Files\VST3`, or add a
custom scan folder in your DAW.

## Using the pedals

- **Knobs:** drag up/down; Shift for fine control; mouse wheel works too.
- **Tap footswitch:** tap for tempo, hold for Dive/Rise. Pick Dive or Rise with
  the button above it. The LED is blue for Dive and green for Rise.
- **Sync:** follows the DAW tempo with the chosen note value instead of the Time
  knob.
- **Trails:** echoes fade out naturally after bypass.

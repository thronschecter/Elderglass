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
  MidiLearn.*              MIDI CC control and MIDI learn
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

## Building (Linux)

Not built or tested yet. The UI runs in WebKitGTK instead of WebView2. Needs
CMake 3.22+, a C++20 compiler and the development packages.

Debian/Ubuntu (22.04 or newer, for WebKitGTK 4.1):

```bash
sudo apt install build-essential cmake pkg-config git \
    libasound2-dev libjack-jackd2-dev libfreetype-dev libfontconfig1-dev \
    libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev \
    libxrandr-dev libxrender-dev libgtk-3-dev libwebkit2gtk-4.1-dev
```

Arch/CachyOS (with PipeWire, take `pipewire-jack` instead of `jack2`):

```bash
sudo pacman -S --needed base-devel cmake pkgconf git \
    alsa-lib jack2 freetype2 fontconfig \
    libx11 libxcomposite libxcursor libxext libxinerama libxrandr libxrender \
    gtk3 webkit2gtk-4.1
```

Then:

```bash
cmake --preset linux
cmake --build --preset linux-release -j
build-linux/ElderglassDspTest              # offline DSP checks
```

A Release build copies the standalone app to `Builds/Elderglass` and adds the
Linux binary to the shared `Builds/Elderglass.vst3` bundle; copy the bundle to
`~/.vst3`. The installed system also needs the WebKitGTK runtime
(`libwebkit2gtk-4.1`). A binary only runs on systems with the same or a newer
glibc, so build the one committed to `Builds/` on the older system (an Ubuntu
LTS rather than a rolling release like CachyOS).

## Using the pedals

- **Knobs:** drag up/down; Shift for fine control; mouse wheel works too.
- **Tap footswitch:** tap for tempo, hold for Dive/Rise. Pick Dive or Rise with
  the button above it. The LED is blue for Dive and green for Rise.
- **Sync:** follows the DAW tempo with the chosen note value instead of the Time
  knob.
- **Trails:** echoes fade out naturally after bypass.

## MIDI control

Every knob and program selector on the pedals, both pedals' footswitches and
the shared footswitch can be driven by MIDI CC:

1. Click **MIDI Learn** (top right). Learnable controls get a dashed outline.
2. Click a control (it blinks), then move a knob or press a switch on your
   MIDI controller. The control shows its CC number.
3. Right-click a control to clear its mapping. Click **MIDI Learn** again or
   press Esc to finish.

- A CC drives one control; learning it again moves it to the new control.
- **Footswitches** want a momentary CC (127 on press, 0 on release). Bypass
  toggles on every press. Tap: a short press taps the tempo, holding it longer
  than 350 ms is Dive/Rise until you let go, just like clicking the footswitch.
- **Time** while **Sync** is on picks the note value (1/16 … 1/1) instead.
- The mappings are saved with the plugin state (the DAW session, or the
  standalone app's settings).
- In the standalone app, enable your controller under
  *Options → Audio/MIDI Settings*. In a DAW, route a MIDI track to Elderglass.

## License

Elderglass is licensed under the GNU General Public License v3.0; see
[LICENSE](LICENSE). It builds on JUCE (AGPLv3) and the Steinberg ASIO SDK (used
under its GPLv3 option).

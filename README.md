# VST Template

A VST3 (and standalone) audio plugin template: **C++/JUCE 8** for the audio
engine, **HTML/CSS/JS** for the UI, rendered in a native OS WebView.

## Why this stack

**JUCE** is the de facto standard for cross-platform audio plugins — it wraps
VST3/AU/AAX/LV2 hosting differences, ships a real-time-safe DSP library
(`juce_dsp`), and is what most commercial plugins (FabFilter, u-he, Serum-ish
competitors, etc.) and most tutorials/forums are built around. The
alternatives are narrower: iPlug2 gives more low-level control at the cost of
far less documentation; DPF is the leanest choice but is really aimed at
Linux/LV2; Rust's `nih-plug` is excellent if the team already writes Rust
but is a smaller ecosystem. For a general-purpose template, JUCE is the
pragmatic default.

**The UI is web technology (HTML/CSS/JS), not JUCE's native Component
system.** JUCE 8 added a first-class `WebBrowserComponent` with a JS↔C++
parameter bridge (`WebSliderRelay`/`WebComboBoxRelay`/`WebToggleButtonRelay`
+ matching `Attachment` classes), so this is now a fully supported,
non-hacky way to build plugin UIs. The payoff:

- **Flexibility** — any web UI framework (React, Vue, Svelte, or, as in this
  template, plain JS) works, along with the entire CSS layout/animation
  toolset and browser dev tools for live UI debugging.
- **Separation of concerns** — the DSP/audio thread code stays C++ (where it
  must be, for real-time safety), while the UI — the part that changes most
  during design iteration — lives in a much faster edit/reload loop.
- **Portability of UI skills** — front-end work doesn't require learning
  JUCE's `Graphics`/`Component` API.

The trade-off: WebView UIs pull in a per-OS browser runtime (WebView2 on
Windows, WKWebView on macOS) and the JS↔C++ bridge is a young API (JUCE 8+).
If you'd rather avoid that and stay in pure native JUCE components, remove
`Source/PluginEditor.*`'s WebView setup and build a normal `juce::Component`
editor instead — the DSP/processor code is unaffected either way.

## Project layout

```
CMakeLists.txt          Fetches JUCE, defines the plugin target
CMakePresets.json        Build presets (Visual Studio / Ninja)
Source/
  PluginProcessor.h/.cpp  AudioProcessor: parameters (APVTS) + DSP (filter -> gain)
  PluginEditor.h/.cpp     WebView editor: parameter relays + resource provider
UI/
  index.html, style.css, main.js   The actual UI, embedded into the binary
```

The example DSP is a state-variable filter (low/band/high-pass) feeding a
gain stage, with a mute switch — enough real signal processing to build on,
not just a stub.

## Building (Windows)

This machine has Visual Studio 2022 Build Tools with a bundled CMake and
Ninja, but no standalone `cmake` on `PATH`. Either add it to `PATH`:

```
C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin
```

or install CMake separately (`winget install Kitware.CMake`), then:

```powershell
cmake --preset vs2022
cmake --build --preset vs2022-release
```

The first configure clones JUCE (~a few hundred MB) via `FetchContent` —
it's cached in `build/_deps` afterwards, so only the first run is slow.

The built VST3 is copied automatically (via `COPY_PLUGIN_AFTER_BUILD`) to
your system's VST3 folder (`%COMMONPROGRAMFILES%\VST3`). The `Standalone`
format is also built, at
`build/VstTemplate_artefacts/Release/Standalone/VST Template.exe`, which is
the fastest way to test without opening a DAW.

## Before you ship anything

Change `COMPANY_NAME`, `PLUGIN_MANUFACTURER_CODE`, and `PLUGIN_CODE` in
`CMakeLists.txt` — these identify the plugin to every DAW and host, and
can't be changed later without breaking users' saved sessions.

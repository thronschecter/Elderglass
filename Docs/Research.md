# Walrus Audio Lore + Fable: Research Notes

Sources: the official manuals, walrusaudio.com product pages, and reviews
(Sound On Sound, Guitar.com, Premier Guitar, Synthanatomy). Both pedals are discontinued.

The manuals are Walrus Audio's documents and are not part of this repo. Download them into
`Docs/Manuals/` (ignored by git):
- Lore: https://cdn.shopify.com/s/files/1/0906/8480/files/Lore_manual_booklet_web.pdf?v=1653072606
- Fable: https://cdn.shopify.com/s/files/1/0906/8480/files/Fable_Digital_Manual_c82de8ff-2129-4743-aadc-0958cada1831.pdf?v=1721232774

## Common hardware platform (identical on both pedals)

Both pedals share the same platform. Only the algorithms differ.

- **Topology:** `IN -> DSP A -> DSP B -> OUT`
  - **FBK A**: DSP A output, scaled by gain, back into DSP A input (the **Feedback** knob)
  - **FBK B**: DSP B output, scaled by gain, back into DSP A input (the **Regen** knob)
  - Both feedback paths are analog on the hardware, so they likely add soft saturation and filtering.
- **Controls:** Feedback, Regen, Mod, Mix | X, Time, Tone, Program (5-position rotary)
- **Footswitches:**
  - **Bypass.** Holding bypass and turning Mod sets the mod rate. The rate is remembered, and the bypass LED blinks at that rate.
  - **Tap.** Tap sets the tempo. Holding Tap gives **Dive/Rise**: the clock rate shifts down or up momentarily. One review says this means half or double speed, slewed, so it is a tape-style pitch/time warp. Pressing Bypass and Tap together toggles Dive/Rise. The LED shows green for Rise and blue for Dive.
- **Trails / No Trails:** selected at power-up. In the plugin this becomes a simple parameter.
- **Mix:** fully left is dry, fully right is wet.
- **Mod:** modulation depth on the wet signal, probably pitch/chorus-style. About 30% sounds "warm" (SOS).
- **I/O:** mono in, mono out, buffered bypass. No presets, MIDI or USB.
- **Design intent:** with every knob at noon, each program gives a usable sound. Ranges are deliberately constrained.
- **Character:** audible hiss that grows with the mix. Big chords cause the feedback to overload and swell. Dense, gritty feedback.

## Lore: Reverse Soundscape Generator

| Prog | DSP A -> DSP B | Feedback (FBK A) | Regen (FBK B) | X | Time / Tap | Tone |
|---|---|---|---|---|---|---|
| I | Reverse Delay -> Reverse Reverb | delay repeats | reverse reverb feedback | amount of reversal (left = least, right = fully reversed) | delay time | bandpass |
| II | Reverse Delay -> Octave-Up Reverb ("light") | delay repeats | octave-up delay repeats into reverb tank (shimmer) | reverb decay | delay time | reverb lowpass |
| III | Reverse Delay -> Octave-Down Reverb ("dark") | delay repeats | octave-down repeats into reverb tank | reverb decay | delay time | reverb lowpass + harmonic distortion |
| IV | Reverse Reverb -> Forward Reverb | octave-down added to the reverse reverb, plus its feedback | octave-up added to the reverse reverb, plus its feedback | forward reverb decay | **time-stretch** of the reverbs (size and character) | reverb lowpass |
| V | Pitch Delay -> Pitch Delay ("dueling") | pitched repeats | pitched repeats | order of the **4th / 5th / octave** intervals stepped through | delay time | bandpass on output and feedback loops |

Notes:
- Program III uses heavy filtering and harmonic distortion. It is thick and murky.
- Program V behaves like a sequencer at long times and a harmonizer at short times. The two delays shift in complementary or opposing directions.
- Program I: signal is "reversed, re-reversed, reverberated, and reversed again within the feedback network".

## Fable: Granular Soundscape Generator

Global X: **grain size**, the length of each chopped sample. Left gives long, smooth grains. Right gives short, choppy grains.
Global Time: **delay time AND granular buffer size**, the length of the buffer grains are sampled from. Tap sets both.
Global Tone: synth-style **resonant low-pass** on the output, sweeping from "underwater" to a slight high roll-off. At the **top of the range**, a subtle **high-pass is added into the feedback paths**, giving glassy repeats.

| Prog | DSP A -> DSP B | Feedback | Time / Tap | Specifics |
|---|---|---|---|---|
| I | Reverse Delay -> Reverse Granular | delay repeats | delay time + buffer size | grains play backwards |
| II | Forward Delay -> Octave-Up Granular | delay repeats | delay time + buffer size | grains play at 2x speed |
| III | Analog-style Delay (warm, dark) -> Octave-Down Granular | delay repeats | delay time + buffer size | grains play at 0.5x speed |
| IV | Multi-Tap Granular -> Multi-Tap Granular ("Grain-Verb") | feedback of the first multi-tap granular | **time-stretch** of both granular engines | several grains read from each buffer at the same time, giving reverb-like clouds |
| V | Forward Delay -> Randomized Pitch Granular | delay repeats | delay time + buffer size | each grain gets a random speed (0.5x / 1x / 2x) and direction (fwd/rev). X also sets the **rate** of the random pitch changes. |

Regen is documented only globally, as the gain of the second feedback path from DSP B back to DSP A.

Known hardware bug: an artifact at the maximum buffer time. Walrus reproduced it, but it cannot be fixed without firmware updates.

## Not published anywhere (we must choose values by ear)

- Delay time range in ms, reverb decay range, grain size range, buffer length
- Mod rate and depth ranges, modulation type
- Filter cutoff ranges and Q, the shape of the Tone curve
- Reverse window and crossfade method, pitch-shifter method
- Saturation and noise of the analog feedback paths
- Dive/Rise slew time (reported as half or double clock rate)

Suggested starting assumptions, to be tuned later:
- Time: 40 ms to 1.5 s
- Max buffer: about 2 s per DSP
- Grain size: about 20 ms to 500 ms
- Reverb decay: about 1 to 20 s
- Mod rate: 0.05 to 8 Hz

## Plugin concept (beyond the hardware)

- Two engines (Lore, Fable), each with the full control set above plus Bypass, Trails, Tap and Dive/Rise.
- **Routing:** Lore -> Fable, Fable -> Lore, or Parallel (stereo).
- Each engine stays **mono internally**, as on the hardware.
  - **Series modes (Lore -> Fable, Fable -> Lore):** the input is summed to mono, runs through both engines, and the mono result goes to both output channels.
  - **Parallel mode (decided):** each channel has its own path. Left input goes to engine L and comes out on the left. Right input goes to engine R and comes out on the right. A mono source on a stereo track feeds both engines.
  - **Swap (parallel only):** this setting chooses which pedal sits in which path.
    - Default: Lore is in the left path and Fable in the right.
    - Swapped: Fable processes the left input and Lore the right.
    - Swap does not exchange the output channels. It moves the engines themselves, so with a true stereo source each pedal processes the other side's input.
    - Each pedal's settings and running state (buffers, tails) stay with that pedal. The engines are re-patched, not re-created.
    - To avoid clicks, swap with a short crossfade of about 20–50 ms. Both engines feed the old and the new path during the fade.

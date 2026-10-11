import { getSliderState, getToggleState, getComboBoxState, getNativeFunction } from "./juce/index.js";

// Every processor parameter has a relay named after its parameter ID (see
// PluginEditor.cpp), so the UI binds by ID: "<prefix><name>" per pedal.
const PEDALS = [
    {
        prefix: "rev_",
        name: "Reverse",
        cssClass: "pedal--rev",
        hints: [
            "Reverse delay into reverse reverb. X: how reversed the trails are (left least, right fully).",
            "Reverse delay into octave-up reverb. Regen: shimmer. X: reverb decay.",
            "Reverse delay into octave-down reverb. Regen: lower octave. X: reverb decay. Tone adds grit.",
            "Reverse reverb into forward reverb. Feedback: octave down. Regen: octave up. X: decay. Time: stretch.",
            "Two dueling pitch delays. X: order of the 4th / 5th / octave steps.",
        ],
        // Knob tooltips: a string applies to every program, an array has one
        // entry per program (I..V).
        tips: {
            feedback: [
                "Repeats of the reverse delay.",
                "Repeats of the reverse delay.",
                "Repeats of the reverse delay.",
                "Adds an octave-down layer to the reverse reverb and feeds it back into it.",
                "Repeats of the upward pitch delay. Every repeat climbs another interval (sequencer).",
            ],
            regen: [
                "Feeds the reverse reverb back into the delay.",
                "Shimmer: feeds the reverb back into itself an octave up.",
                "Feeds the reverb back into itself an octave down.",
                "Adds an octave-up layer from the forward reverb and feeds it back.",
                "Repeats of the downward pitch delay, fed back to the start.",
            ],
            mod: "Pitch modulation depth on the wet signal. The speed is set with Mod Rate.",
            mix: "Dry / wet balance: left fully dry, right fully wet.",
            x: [
                "How reversed the delay is: left plays forward, right fully reversed.",
                "Reverb decay, from 0.8 s (left) to 20 s (right).",
                "Reverb decay, from 0.8 s (left) to 20 s (right).",
                "Decay of the forward reverb, from 0.8 s (left) to 20 s (right).",
                "Interval order: which of 4th / 5th / octave the first delay shifts up (↑) and the second shifts down (↓).",
            ],
            time: [
                "Delay time. Also sets the length of the reverse-reverb swell.",
                "Delay time of the reverse delay.",
                "Delay time of the reverse delay.",
                "Stretch: size of both reverbs and length of the reverse swell.",
                "Time of both pitch delays. Long times act like a sequencer, short times like a harmonizer.",
            ],
            tone: [
                "Band-pass on the output and the feedback loops: sweeps from dark to bright.",
                "Reverb low-pass: darkens the reverb, the shimmer and the loops.",
                "Low-pass in front of the distortion: lower is darker and murkier.",
                "Reverb low-pass: darkens both reverbs and the loops.",
                "Band-pass on the output and the feedback loops: tames the high pitch steps.",
            ],
            modRate: "Speed of the Mod pitch modulation (on the hardware: hold bypass and turn Mod).",
        },
    },
    {
        prefix: "gran_",
        name: "Granular",
        cssClass: "pedal--gran",
        hints: [
            "Reverse delay into reverse grains. X: grain size (left long, right choppy). Time: delay and grain buffer.",
            "Forward delay into octave-up grains (double speed). X: grain size. Time: delay and grain buffer.",
            "Warm analog-style delay into octave-down grains (half speed). X: grain size. Time: delay and grain buffer.",
            "Multi-tap granular into multi-tap granular: Grain-Verb. X: grain size. Time: time-stretch.",
            "Delay into grains of random pitch and direction. X: grain size and pitch-change rate.",
        ],
        tips: {
            feedback: [
                "Repeats of the reverse delay.",
                "Repeats of the forward delay.",
                "Repeats of the analog-style delay.",
                "Feedback of the first multi-tap grain stage.",
                "Repeats of the forward delay.",
            ],
            regen: [
                "Feeds the reversed grains back into the delay: grains of grains.",
                "Feeds the octave-up grains back into the delay: climbing shimmer.",
                "Feeds the octave-down grains back into the delay: falling, pulsing lows.",
                "Feeds the second grain-verb back into the first: denser clouds.",
                "Feeds the random grains back into the delay: more chaos with every pass.",
            ],
            mod: "Pitch modulation depth on the wet signal. The speed is set with Mod Rate.",
            mix: "Dry / wet balance: left fully dry, right fully wet.",
            x: [
                "Grain size: left long and smooth, right short and choppy.",
                "Grain size: left long and smooth, right short and choppy.",
                "Grain size: left long and smooth, right short and choppy.",
                "Grain size: left long and smooth (reverb-like), right short and fluttery.",
                "Grain size, and with it how often the random pitch and direction change.",
            ],
            time: [
                "Delay time and the length of the buffer the grains are taken from.",
                "Delay time and the length of the buffer the grains are taken from.",
                "Delay time and the length of the buffer the grains are taken from.",
                "Stretch: how slowly both grain stages scan their buffers and how far apart the taps sit.",
                "Delay time and the length of the buffer the grains are taken from.",
            ],
            tone: "Resonant low-pass on the output and loops. The top quarter also adds a high-pass into the loops for glassy repeats.",
            modRate: "Speed of the Mod pitch modulation (on the hardware: hold bypass and turn Mod).",
        },
    },
];

const tapNative = getNativeFunction("tap");
const HOLD_MS = 350;

// ---------------------------------------------------------------------------
// Tooltip: one shared element, shown after a short hover, hidden while a
// knob is dragged. The text is fetched on show, so it follows the program.
// ---------------------------------------------------------------------------
const tooltip = document.createElement("div");
tooltip.className = "tooltip";
tooltip.innerHTML = `<div class="tooltip__title"></div><div class="tooltip__text"></div>`;
document.body.appendChild(tooltip);

function attachTooltip(el, getTip) {
    let timer = null;
    const hide = () => {
        clearTimeout(timer);
        tooltip.classList.remove("is-visible");
    };
    const show = () => {
        const tip = getTip();
        if (!tip) return;
        tooltip.querySelector(".tooltip__title").textContent = tip.title;
        tooltip.querySelector(".tooltip__text").textContent = tip.text;
        tooltip.classList.add("is-visible");

        // Below the knob, kept inside the window; above it if there's no room.
        const r = el.getBoundingClientRect();
        const t = tooltip.getBoundingClientRect();
        let left = r.left + r.width / 2 - t.width / 2;
        left = Math.max(8, Math.min(window.innerWidth - t.width - 8, left));
        let top = r.bottom + 6;
        if (top + t.height > window.innerHeight - 8) top = r.top - t.height - 6;
        tooltip.style.left = `${left}px`;
        tooltip.style.top = `${top}px`;
    };
    el.addEventListener("pointerenter", () => {
        clearTimeout(timer);
        timer = setTimeout(show, 450);
    });
    el.addEventListener("pointerleave", hide);
    el.addEventListener("pointerdown", hide);
    el.addEventListener("wheel", hide, { passive: true });
}

// ---------------------------------------------------------------------------
// Knobs
// ---------------------------------------------------------------------------
const ANGLE = 135;

function polar(cx, cy, r, deg) {
    const a = ((deg - 90) * Math.PI) / 180;
    return [cx + r * Math.cos(a), cy + r * Math.sin(a)];
}

function arcPath(cx, cy, r, from, to) {
    const [x1, y1] = polar(cx, cy, r, from);
    const [x2, y2] = polar(cx, cy, r, to);
    const large = to - from > 180 ? 1 : 0;
    return `M ${x1} ${y1} A ${r} ${r} 0 ${large} 1 ${x2} ${y2}`;
}

function formatValue(paramId, v) {
    if (paramId.endsWith("time")) return v >= 1000 ? `${(v / 1000).toFixed(2)} s` : `${Math.round(v)} ms`;
    if (paramId.endsWith("modRate")) return `${v.toFixed(2)} Hz`;
    if (paramId === "output") return `${v >= 0 ? "+" : ""}${v.toFixed(1)} dB`;
    return `${Math.round(v)}%`;
}

function createKnob(el, paramId, { format } = {}) {
    const state = getSliderState(paramId);
    el.innerHTML = `
        <svg viewBox="0 0 60 60">
            <path class="knob__track" d="${arcPath(30, 30, 26, -ANGLE, ANGLE)}"></path>
            <path class="knob__arc"></path>
            <circle class="knob__body" cx="30" cy="30" r="20"></circle>
            <line class="knob__tick" x1="30" y1="30" x2="30" y2="14"></line>
        </svg>
        <span class="knob__label"></span>
        <span class="knob__value"></span>`;

    const arc = el.querySelector(".knob__arc");
    const tick = el.querySelector(".knob__tick");
    const label = el.querySelector(".knob__label");
    const value = el.querySelector(".knob__value");
    label.textContent = el.dataset.label;

    const fmt = format ?? ((v) => formatValue(paramId, v));
    const refresh = () => {
        const n = state.getNormalisedValue();
        const deg = -ANGLE + n * 2 * ANGLE;
        arc.setAttribute("d", n > 0.001 ? arcPath(30, 30, 26, -ANGLE, deg) : "");
        tick.setAttribute("transform", `rotate(${deg} 30 30)`);
        value.textContent = fmt(state.getScaledValue());
    };

    let startY = 0;
    let startValue = 0;
    el.addEventListener("pointerdown", (e) => {
        el.setPointerCapture(e.pointerId);
        el.classList.add("is-dragging");
        startY = e.clientY;
        startValue = state.getNormalisedValue();
        state.sliderDragStarted();
    });
    el.addEventListener("pointermove", (e) => {
        if (!el.hasPointerCapture(e.pointerId)) return;
        const sensitivity = e.shiftKey ? 0.0008 : 0.005;
        const n = Math.min(1, Math.max(0, startValue + (startY - e.clientY) * sensitivity));
        state.setNormalisedValue(n);
        refresh();
    });
    const end = (e) => {
        if (!el.hasPointerCapture(e.pointerId)) return;
        el.releasePointerCapture(e.pointerId);
        el.classList.remove("is-dragging");
        state.sliderDragEnded();
    };
    el.addEventListener("pointerup", end);
    el.addEventListener("pointercancel", end);
    el.addEventListener("wheel", (e) => {
        e.preventDefault();
        const step = (e.shiftKey ? 0.002 : 0.02) * (e.deltaY < 0 ? 1 : -1);
        state.sliderDragStarted();
        state.setNormalisedValue(Math.min(1, Math.max(0, state.getNormalisedValue() + step)));
        state.sliderDragEnded();
        refresh();
    }, { passive: false });

    state.valueChangedEvent.addListener(refresh);
    state.propertiesChangedEvent.addListener(refresh);
    refresh();
    return { state, refresh, setLabel: (t) => (label.textContent = t) };
}

// ---------------------------------------------------------------------------
// Small binders
// ---------------------------------------------------------------------------
function bindToggle(button, paramId, onChange) {
    const state = getToggleState(paramId);
    const refresh = () => {
        button.setAttribute("aria-pressed", String(state.getValue()));
        onChange?.(state.getValue());
    };
    button.addEventListener("click", () => state.setValue(!state.getValue()));
    state.valueChangedEvent.addListener(refresh);
    refresh();
    return state;
}

function bindSelect(select, paramId, onChange) {
    const state = getComboBoxState(paramId);
    const fill = () => {
        const choices = state.properties.choices ?? [];
        if (select.options.length !== choices.length) {
            select.innerHTML = "";
            choices.forEach((c, i) => select.add(new Option(c, String(i))));
        }
        select.value = String(state.getChoiceIndex());
        onChange?.(state.getChoiceIndex());
    };
    select.addEventListener("change", () => state.setChoiceIndex(parseInt(select.value, 10)));
    state.valueChangedEvent.addListener(fill);
    state.propertiesChangedEvent.addListener(fill);
    fill();
    return state;
}

// ---------------------------------------------------------------------------
// Pedals
// ---------------------------------------------------------------------------
const rack = document.getElementById("rack");
const template = document.getElementById("pedalTemplate");
const panels = {};

function buildPedal(def) {
    const panel = template.content.firstElementChild.cloneNode(true);
    panel.classList.add(def.cssClass);
    panel.querySelector(".pedal__name").textContent = def.name;
    rack.appendChild(panel);
    panels[def.prefix] = panel;

    const p = (suffix) => def.prefix + suffix;
    const knobs = {};

    // Tempo sync: Time knob shows the division while synced.
    const divisionSelect = panel.querySelector(".division");
    const divisionState = bindSelect(divisionSelect, p("division"), () => knobs.time?.refresh());
    let synced = false;
    const timeText = (v) =>
        synced ? `♩ ${divisionState.properties.choices?.[divisionState.getChoiceIndex()] ?? ""}` : formatValue("time", v);
    bindToggle(panel.querySelector('[data-toggle="sync"]'), p("sync"), (on) => {
        synced = on;
        divisionSelect.disabled = !on;
        knobs.time?.refresh();
    });

    let program = 0;
    const xText = (v) => {
        // Matches the interval orders in ReverseModel.h (program V).
        if (def.prefix !== "rev_" || program !== 4) return formatValue("x", v);
        const orders = [["4th", "5th"], ["4th", "oct"], ["5th", "4th"], ["5th", "oct"], ["oct", "4th"], ["oct", "5th"]];
        const [up, down] = orders[Math.min(5, Math.floor((v / 100) * 6))];
        return `↑${up} ↓${down}`;
    };

    panel.querySelectorAll(".knob[data-suffix]").forEach((el) => {
        const suffix = el.dataset.suffix;
        knobs[suffix] = createKnob(el, p(suffix), {
            format: suffix === "time" ? (v) => timeText(v) : suffix === "x" ? xText : undefined,
        });
        attachTooltip(el, () => {
            const tip = def.tips[suffix];
            const text = Array.isArray(tip) ? tip[program] : tip;
            return text ? { title: el.querySelector(".knob__label").textContent, text } : null;
        });
    });

    // Program select + per-program hint and knob naming.
    const hint = panel.querySelector(".pedal__hint");
    const updateProgramLabels = () => {
        hint.textContent = def.hints[program] ?? "";
        knobs.time.setLabel(program === 3 ? "Stretch" : "Time");
        knobs.x.refresh();
    };
    bindSelect(panel.querySelector(".program"), p("program"), (i) => {
        program = i;
        updateProgramLabels();
    });

    bindToggle(panel.querySelector('[data-toggle="trails"]'), p("trails"));

    // Dive / Rise direction (hardware: bypass + tap together).
    const warpDirButton = panel.querySelector("[data-warpdir]");
    const warpDirState = getComboBoxState(p("warpDir"));
    const tapLed = panel.querySelector(".led--tap");
    const refreshWarpDir = () => {
        const rise = warpDirState.getChoiceIndex() === 1;
        warpDirButton.textContent = rise ? "Rise ↑" : "Dive ↓";
        tapLed.classList.toggle("is-rise", rise);
        tapLed.classList.toggle("is-dive", !rise);
    };
    warpDirButton.addEventListener("click", () => warpDirState.setChoiceIndex(warpDirState.getChoiceIndex() === 1 ? 0 : 1));
    warpDirState.valueChangedEvent.addListener(refreshWarpDir);
    refreshWarpDir();

    // Bypass footswitch + LED.
    const enabledState = getToggleState(p("enabled"));
    const bypassLed = panel.querySelector(".led--bypass");
    const refreshEnabled = () => {
        const on = enabledState.getValue();
        bypassLed.classList.toggle("is-on", on);
        panel.classList.toggle("is-bypassed", !on);
    };
    panel.querySelector('[data-stomp="bypass"]').addEventListener("click", () => enabledState.setValue(!enabledState.getValue()));
    enabledState.valueChangedEvent.addListener(refreshEnabled);
    refreshEnabled();

    // Tap footswitch: short press = tap tempo, hold = Dive/Rise while held.
    const tapButton = panel.querySelector('[data-stomp="tap"]');
    const warpState = getToggleState(p("warp"));
    let pressedAt = 0;
    let holdTimer = null;
    let holding = false;
    tapButton.addEventListener("pointerdown", (e) => {
        tapButton.setPointerCapture(e.pointerId);
        tapButton.classList.add("is-down");
        pressedAt = performance.now();
        holding = false;
        holdTimer = setTimeout(() => {
            holding = true;
            warpState.setValue(true);
        }, HOLD_MS);
    });
    const release = () => {
        if (!tapButton.classList.contains("is-down")) return;
        tapButton.classList.remove("is-down");
        clearTimeout(holdTimer);
        if (holding) warpState.setValue(false);
        else tapNative(def.prefix, performance.now() - pressedAt);
        holding = false;
    };
    tapButton.addEventListener("pointerup", release);
    tapButton.addEventListener("pointercancel", release);

    // Tap LED blinks at the Time knob's rate (steady while synced: the host
    // tempo isn't visible to the page).
    let blinkStart = performance.now();
    knobs.time.state.valueChangedEvent.addListener(() => (blinkStart = performance.now()));
    const blink = () => {
        const periodMs = knobs.time.state.getScaledValue();
        const phase = ((performance.now() - blinkStart) % periodMs) / periodMs;
        tapLed.classList.toggle("is-on", synced || holding || phase < 0.18);
        requestAnimationFrame(blink);
    };
    requestAnimationFrame(blink);
}

PEDALS.forEach(buildPedal);

// ---------------------------------------------------------------------------
// Routing: the panels are laid out in signal order. Series shows the first
// pedal on the left; parallel shows the pedal in the left path on the left.
// ---------------------------------------------------------------------------
const routingState = getComboBoxState("routing");
const swapState = getToggleState("swap");
const swapButton = document.getElementById("swapButton");
const flow = document.getElementById("routingFlow");
const routingButtons = [...document.querySelectorAll("#routingButtons button")];

function refreshRouting() {
    const routing = routingState.getChoiceIndex();
    const swapped = swapState.getValue();
    routingButtons.forEach((b) => b.setAttribute("aria-pressed", String(parseInt(b.dataset.routing, 10) === routing)));
    swapButton.setAttribute("aria-pressed", String(swapped));
    swapButton.disabled = routing !== 2;

    const granFirst = routing === 1 || (routing === 2 && swapped);
    panels["rev_"].style.order = granFirst ? 3 : 1;
    panels["gran_"].style.order = granFirst ? 1 : 3;
    document.getElementById("routing").style.order = 2;

    if (routing === 2) {
        flow.innerHTML = swapped ? "L ← Granular<br>Reverse → R" : "L ← Reverse<br>Granular → R";
    } else {
        flow.innerHTML = routing === 0 ? "Reverse<br>↓<br>Granular" : "Granular<br>↓<br>Reverse";
    }
}

routingButtons.forEach((b) => b.addEventListener("click", () => routingState.setChoiceIndex(parseInt(b.dataset.routing, 10))));
swapButton.addEventListener("click", () => swapState.setValue(!swapState.getValue()));
routingState.valueChangedEvent.addListener(refreshRouting);
routingState.propertiesChangedEvent.addListener(refreshRouting);
swapState.valueChangedEvent.addListener(refreshRouting);
refreshRouting();

const outputKnob = document.querySelector('[data-param="output"]');
createKnob(outputKnob, "output");
attachTooltip(outputKnob, () => ({ title: "Output", text: "Overall output level of the plugin." }));

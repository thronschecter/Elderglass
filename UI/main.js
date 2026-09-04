import { getSliderState, getToggleState, getComboBoxState, getNativeFunction } from "./juce";

// Each of these names must match the identifier a WebSliderRelay /
// WebToggleButtonRelay / WebComboBoxRelay was constructed with in
// PluginEditor.cpp — that's the whole wiring contract between C++ and JS.
const gainState = getSliderState("gainSliderState");
const cutoffState = getSliderState("cutoffSliderState");
const filterTypeState = getComboBoxState("filterTypeComboState");
const muteState = getToggleState("muteToggleState");

function bindSlider(inputEl, outputEl, state, format) {
    const refresh = () => {
        inputEl.value = state.getNormalisedValue();
        outputEl.textContent = format(state.getScaledValue());
    };

    inputEl.addEventListener("pointerdown", () => state.sliderDragStarted());
    inputEl.addEventListener("input", () => {
        state.setNormalisedValue(parseFloat(inputEl.value));
        outputEl.textContent = format(state.getScaledValue());
    });
    inputEl.addEventListener("pointerup", () => state.sliderDragEnded());

    state.valueChangedEvent.addListener(refresh);
    state.propertiesChangedEvent.addListener(refresh);
    refresh();
}

bindSlider(
    document.getElementById("gain"),
    document.getElementById("gainValue"),
    gainState,
    (v) => `${v.toFixed(1)} dB`
);

bindSlider(
    document.getElementById("cutoff"),
    document.getElementById("cutoffValue"),
    cutoffState,
    (v) => (v >= 1000 ? `${(v / 1000).toFixed(2)} kHz` : `${v.toFixed(0)} Hz`)
);

const filterTypeSelect = document.getElementById("filterType");
filterTypeSelect.addEventListener("change", () => {
    filterTypeState.setChoiceIndex(parseInt(filterTypeSelect.value, 10));
});
filterTypeState.valueChangedEvent.addListener(() => {
    filterTypeSelect.value = String(filterTypeState.getChoiceIndex());
});
filterTypeSelect.value = String(filterTypeState.getChoiceIndex());

const muteButton = document.getElementById("mute");
const refreshMuteButton = () => {
    const isMuted = muteState.getValue();
    muteButton.setAttribute("aria-pressed", String(isMuted));
    muteButton.textContent = isMuted ? "Muted" : "Mute";
};
muteButton.addEventListener("click", () => muteState.setValue(!muteState.getValue()));
muteState.valueChangedEvent.addListener(refreshMuteButton);
refreshMuteButton();

// Demonstrates calling from JS into a C++ native function registered with
// withNativeFunction("sayHello", ...) in PluginEditor.cpp.
const sayHello = getNativeFunction("sayHello");
document.getElementById("greet").addEventListener("click", async () => {
    const result = await sayHello("web UI");
    document.getElementById("greetResult").textContent = result;
});

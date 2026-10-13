// Pedal artwork: a silhouette with glowing lines that light up with the
// pedal's Mix. The figures come from art-data.js (Tools/generate_art.py).
//
// Every stroke has a delay; a light front moves at one speed over all of
// them, so the glow starts at several points and flows out to every tip.
// With `stages`, the front fills the lines over the first half of the Mix and
// a white core fades in over the second half (the glow gets hotter).

const NS = "http://www.w3.org/2000/svg";

function el(name, attrs = {}, parent) {
    const e = document.createElementNS(NS, name);
    for (const [k, v] of Object.entries(attrs)) e.setAttribute(k, v);
    if (parent) parent.append(e);
    return e;
}

let nextId = 0;

export function mountArt(panel, fig, mixState, { stages = false } = {}) {
    const n = nextId++;
    const layer = document.createElement("div");
    layer.className = "pedal__art";
    panel.prepend(layer);
    const svg = el("svg", { viewBox: fig.viewBox, preserveAspectRatio: "xMidYMid meet", "aria-hidden": "true" }, layer);

    const defs = el("defs", {}, svg);
    const auraGradient = el("radialGradient", { id: `art-aura-${n}` }, defs);
    el("stop", { offset: "0", "stop-color": "#fff1c4", "stop-opacity": "0.9" }, auraGradient);
    el("stop", { offset: "0.25", "stop-color": "#f0c870", "stop-opacity": "0.45" }, auraGradient);
    el("stop", { offset: "1", "stop-color": "#f0c870", "stop-opacity": "0" }, auraGradient);
    const [ax, ay, ar] = fig.aura;
    const aura = el("ellipse", { cx: ax, cy: ay, rx: ar * 1.2, ry: ar, fill: `url(#art-aura-${n})` }, svg);

    if (fig.mist) {
        const mist = el("radialGradient", { id: `art-mist-${n}` }, defs);
        el("stop", { offset: "0", class: "art__mist-stop", "stop-opacity": "0.85" }, mist);
        el("stop", { offset: "1", class: "art__mist-stop", "stop-opacity": "0" }, mist);
        const [mx, my, mrx, mry] = fig.mist;
        el("ellipse", { cx: mx, cy: my, rx: mrx, ry: mry, fill: `url(#art-mist-${n})` }, svg);
    }
    if (fig.ground) el("path", { class: "art__sil", d: fig.ground }, svg);
    el("path", { class: "art__sil", d: fig.fill }, svg);

    const rest = el("g", { class: "art__rest" }, svg);
    const lit = el("g", { class: "art__lit" }, svg);
    const core = stages ? el("g", { class: "art__core" }, svg) : null;

    let end = 0;
    const strokes = fig.strokes.map((s) => {
        el("path", { d: s.d, "stroke-width": s.w * 0.8 }, rest);
        const p = el("path", { d: s.d, "stroke-width": s.w, "stroke-opacity": s.o ?? 1 }, lit);
        const len = p.getTotalLength();
        p.style.strokeDasharray = `${len} ${len}`;
        end = Math.max(end, s.delay + len);
        if (core) el("path", { d: s.d, "stroke-width": Math.max(0.1, s.w * 0.3) }, core);
        return { p, len, delay: s.delay };
    });
    const grains = (fig.grains ?? []).map(([x, y, r, delay]) => {
        end = Math.max(end, delay);
        return { c: el("circle", { class: "art__grain", cx: x, cy: y, r }, svg), delay };
    });
    const motes = (fig.motes ?? []).map(([x, y, r]) => el("circle", { class: "art__mote", cx: x, cy: y, r }, svg));

    const refresh = () => {
        const mix = mixState.getNormalisedValue();
        const front = end * (stages ? Math.min(1, mix / 0.5) : mix);
        for (const s of strokes)
            s.p.style.strokeDashoffset = s.len - Math.min(s.len, Math.max(0, front - s.delay));
        for (const g of grains)
            g.c.classList.toggle("is-lit", front >= g.delay);

        if (stages) {
            const heat = Math.max(0, (mix - 0.5) / 0.5);
            core.style.opacity = heat * heat * (3 - 2 * heat);
            aura.style.opacity = mix <= 0.5 ? Math.pow(mix / 0.5, 1.3) * 0.5 : 0.5 + heat * 0.22;
        } else {
            aura.style.opacity = Math.pow(mix, 1.3) * 0.75;
        }
        motes.forEach((m, i) => (m.style.opacity = Math.max(0, Math.min(1, mix * 1.6 - (i / motes.length) * 0.8)) * 0.9));
    };
    mixState.valueChangedEvent.addListener(refresh);
    refresh();
}

// Frost cover for a frozen pedal: frosted edges, a fine frost texture and ice
// crystals that grow in from the border. It lies over the whole panel but
// never takes clicks, and its centre stays clear so the knobs stay readable.
// Shown by the panel's `is-frozen` class (style.css); the crystals grow
// along their own length (pathLength 1, dash offset 1 -> 0).

const NS = "http://www.w3.org/2000/svg";

function el(name, attrs = {}, parent) {
    const e = document.createElementNS(NS, name);
    for (const [k, v] of Object.entries(attrs)) e.setAttribute(k, v);
    if (parent) parent.append(e);
    return e;
}

function random(seed) {
    let s = seed >>> 0 || 1;
    return () => {
        s ^= s << 13;
        s ^= s >>> 17;
        s ^= s << 5;
        return (s >>> 0) / 4294967296;
    };
}

let nextId = 0;

export function mountFrost(panel) {
    const n = nextId++;
    const layer = document.createElement("div");
    layer.className = "pedal__frost";
    layer.setAttribute("aria-hidden", "true");
    panel.append(layer);

    // The noise texture has its own SVG (and compositor layer), so the
    // growing crystals don't re-run the turbulence filter on every frame.
    const textureSvg = el("svg", { class: "frost__texture-layer" }, layer);
    const defs = el("defs", {}, textureSvg);
    const filter = el("filter", { id: `frost-noise-${n}`, x: "0", y: "0", width: "100%", height: "100%" }, defs);
    el("feTurbulence", { type: "fractalNoise", baseFrequency: "0.9", numOctaves: "3", seed: String(3 + n) }, filter);
    el("feColorMatrix", { values: "0 0 0 0 0.86  0 0 0 0 0.95  0 0 0 0 1  0 0 0 1.6 -0.75" }, filter);
    el("rect", { class: "frost__texture", x: "0", y: "0", width: "100%", height: "100%",
          filter: `url(#frost-noise-${n})` }, textureSvg);

    const svg = el("svg", {}, layer);
    const crystals = el("g", { class: "frost__crystals" }, svg);

    // Dendrites: a main arm pointing inwards, side arms at +-60 degrees
    // (ice grows hexagonally), recursively shorter. Later arms grow later.
    const grow = (w, h) => {
        crystals.replaceChildren();
        const rng = random(0xf205 + n * 104729);
        const reach = Math.min(w, h) * 0.15;

        const branch = (x, y, angle, length, depth, delay) => {
            const x2 = x + Math.cos(angle) * length;
            const y2 = y + Math.sin(angle) * length;
            const p = el("path", { d: `M${x.toFixed(1)} ${y.toFixed(1)} L${x2.toFixed(1)} ${y2.toFixed(1)}`,
                                   pathLength: "1", "stroke-width": (1.6 - depth * 0.35).toFixed(2) }, crystals);
            p.style.transitionDelay = `${delay.toFixed(2)}s`;
            if (depth >= 3) return;
            const arms = 2 + Math.floor(rng() * 3);
            for (let a = 0; a < arms; a++) {
                const t = 0.25 + 0.65 * rng();
                const side = rng() < 0.5 ? -1 : 1;
                branch(x + (x2 - x) * t, y + (y2 - y) * t, angle + side * (Math.PI / 3) * (0.85 + 0.3 * rng()),
                       length * (0.3 + 0.3 * rng()), depth + 1, delay + 0.12 + t * 0.25);
            }
        };

        // Seeds along the border, denser at the corners, pointing inwards.
        const seeds = [];
        for (const [cx, cy] of [[0, 0], [w, 0], [0, h], [w, h]])
            for (let i = 0; i < 3; i++) seeds.push([cx, cy, Math.atan2(h / 2 - cy, w / 2 - cx) + (rng() - 0.5) * 1.2]);
        for (let i = 0; i < 14; i++) {
            const edge = Math.floor(rng() * 4), t = rng();
            const [x, y, a] = [[t * w, 0, Math.PI / 2], [w, t * h, Math.PI], [t * w, h, -Math.PI / 2], [0, t * h, 0]][edge];
            seeds.push([x, y, a + (rng() - 0.5) * 0.9]);
        }
        seeds.forEach(([x, y, a]) => branch(x, y, a, reach * (0.5 + 0.7 * rng()), 0, rng() * 0.25));
    };

    let size = "";
    new ResizeObserver(() => {
        const w = Math.round(layer.clientWidth), h = Math.round(layer.clientHeight);
        if (!w || !h || `${w}x${h}` === size) return;
        size = `${w}x${h}`;
        svg.setAttribute("viewBox", `0 0 ${w} ${h}`);
        grow(w, h);
    }).observe(layer);
}

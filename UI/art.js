// Pedal artwork: a silhouette with glowing lines that light up with the
// pedal's Mix. The figures come from art-data.js (Tools/generate_art.py).
//
// Every stroke has a delay and a speed: a light front moves over all of
// them, and each stroke grows from its delay on at its own speed (easing in
// and out by `fig.ease`), so the glow flows out unevenly, like it catches.
// With `stages`, the front fills the lines over the first half of the Mix and
// a white core fades in over the second half (the glow gets hotter).
//
// Freeze: an ice layer (translucent body, carved facets, a light sheen and
// glints, clipped to the silhouette) turns the figure into a glass sculpture.
// It is shown by the panel's `is-frozen` class (style.css); the glow lines
// stay where the Mix put them and turn icy.
//
// The figure is a stack of SVGs with the same viewBox, so that what changes
// never repaints what doesn't: base (aura, silhouette), ice (fades as one
// compositor layer), lines (thousands of glow-filtered strokes, repainted
// only when the Mix moves) and fx (the animated sheen and glints).

const NS = "http://www.w3.org/2000/svg";

function el(name, attrs = {}, parent) {
    const e = document.createElementNS(NS, name);
    for (const [k, v] of Object.entries(attrs)) e.setAttribute(k, v);
    if (parent) parent.append(e);
    return e;
}

let nextId = 0;

// Small deterministic PRNG, so a figure's facets and glints never change.
function random(seed) {
    let s = seed >>> 0 || 1;
    return () => {
        s ^= s << 13;
        s ^= s >>> 17;
        s ^= s << 5;
        return (s >>> 0) / 4294967296;
    };
}

function mountIce(svg, fx, fig, n) {
    const defs = el("defs", {}, svg);
    const [vx, vy, vw, vh] = fig.viewBox.split(/\s+/).map(Number);
    const rng = random(0x1ce + n * 7919);

    const clip = el("clipPath", { id: `art-ice-clip-${n}` }, defs);
    el("path", { d: fig.fill }, clip);
    if (fig.ground) el("path", { d: fig.ground }, clip);

    const body = el("linearGradient", { id: `art-ice-body-${n}`, x1: "0", y1: "0", x2: "0.35", y2: "1" }, defs);
    el("stop", { offset: "0", "stop-color": "#f2fbff", "stop-opacity": "0.55" }, body);
    el("stop", { offset: "0.45", "stop-color": "#9fd4ee", "stop-opacity": "0.32" }, body);
    el("stop", { offset: "1", "stop-color": "#3d7fa6", "stop-opacity": "0.42" }, body);

    const sheen = el("linearGradient", { id: `art-ice-sheen-${n}`, x1: "0", y1: "0", x2: "1", y2: "0" }, defs);
    el("stop", { offset: "0", "stop-color": "#ffffff", "stop-opacity": "0" }, sheen);
    el("stop", { offset: "0.5", "stop-color": "#ffffff", "stop-opacity": "0.55" }, sheen);
    el("stop", { offset: "1", "stop-color": "#ffffff", "stop-opacity": "0" }, sheen);

    const auraIce = el("radialGradient", { id: `art-ice-aura-${n}` }, defs);
    el("stop", { offset: "0", "stop-color": "#dff4ff", "stop-opacity": "0.5" }, auraIce);
    el("stop", { offset: "1", "stop-color": "#7cc4ea", "stop-opacity": "0" }, auraIce);

    const ice = el("g", {}, svg);
    const [ax, ay, ar] = fig.aura;
    el("ellipse", { cx: ax, cy: ay, rx: ar * 1.3, ry: ar * 1.1, fill: `url(#art-ice-aura-${n})` }, ice);

    const carved = el("g", { "clip-path": `url(#art-ice-clip-${n})` }, ice);
    el("rect", { x: vx, y: vy, width: vw, height: vh, fill: `url(#art-ice-body-${n})` }, carved);

    // Facets: a jittered triangle grid, each triangle its own brightness.
    const cols = 16, rows = 15;
    const pts = [];
    for (let j = 0; j <= rows; j++) {
        pts.push([]);
        for (let i = 0; i <= cols; i++) {
            const edge = i === 0 || j === 0 || i === cols || j === rows;
            const jx = edge ? 0 : (rng() - 0.5) * 0.8;
            const jy = edge ? 0 : (rng() - 0.5) * 0.8;
            pts[j].push([vx + ((i + jx) / cols) * vw, vy + ((j + jy) / rows) * vh]);
        }
    }
    const facets = el("g", { class: "art__facets" }, carved);
    const tri = (a, b, c) => {
        const light = rng();
        el("path", {
            d: `M${a[0].toFixed(1)} ${a[1].toFixed(1)} L${b[0].toFixed(1)} ${b[1].toFixed(1)} L${c[0].toFixed(1)} ${c[1].toFixed(1)} Z`,
            fill: light > 0.55 ? "#ffffff" : "#5aa6cf",
            "fill-opacity": (light > 0.55 ? 0.04 + 0.2 * (light - 0.55) : 0.03 + 0.12 * light).toFixed(3),
        }, facets);
    };
    for (let j = 0; j < rows; j++)
        for (let i = 0; i < cols; i++) {
            const [a, b, c, d] = [pts[j][i], pts[j][i + 1], pts[j + 1][i + 1], pts[j + 1][i]];
            if (rng() < 0.5) { tri(a, b, c); tri(a, c, d); } else { tri(a, b, d); tri(b, c, d); }
        }

    // A band of light that sweeps across the glass now and then, and the
    // glints: both move, so they live in the fx SVG (with their own clip).
    const fxDefs = el("defs", {}, fx);
    const fxClip = el("clipPath", { id: `art-fx-clip-${n}` }, fxDefs);
    el("path", { d: fig.fill }, fxClip);
    fxDefs.append(sheen);
    const fxIce = el("g", {}, fx);
    el("rect", { class: "art__sheen", x: vx - vw * 0.4, y: vy, width: vw * 0.35, height: vh,
                 fill: `url(#art-ice-sheen-${n})`, "clip-path": `url(#art-fx-clip-${n})` }, fxIce);

    // Polished rim.
    el("path", { class: "art__ice-edge", d: fig.fill }, ice);

    // Glints: small four-pointed stars at random spots inside the figure.
    const probe = el("path", { d: fig.fill, fill: "none" }, svg);
    const glints = el("g", { class: "art__glints" }, fxIce);
    for (let tries = 0, made = 0; made < 7 && tries < 400; tries++) {
        const x = vx + rng() * vw, y = vy + rng() * vh;
        let inside = false;
        try { inside = probe.isPointInFill(new DOMPoint(x, y)); } catch { inside = rng() < 0.2; }
        if (!inside) continue;
        const r = 2 + rng() * 3.5, t = r * 0.18;
        const star = el("path", {
            d: `M${x} ${y - r} L${x + t} ${y - t} L${x + r} ${y} L${x + t} ${y + t} L${x} ${y + r} L${x - t} ${y + t} L${x - r} ${y} L${x - t} ${y - t} Z`,
        }, glints);
        star.style.animationDelay = `${(rng() * 4).toFixed(2)}s`;
        made++;
    }
    probe.remove();
}

export function mountArt(panel, fig, mixState, { stages = false } = {}) {
    const n = nextId++;
    const layer = document.createElement("div");
    layer.className = "pedal__art";
    panel.prepend(layer);
    // The figure fills the free space below the option buttons, so it never
    // sits behind the knobs or buttons; it follows the layout on resize.
    const options = panel.querySelector(".options");
    if (options) {
        const place = () => (layer.style.top = `${options.offsetTop + options.offsetHeight + 6}px`);
        new ResizeObserver(place).observe(panel);
        place();
    }
    const layerSvg = (cls) => el("svg", { class: cls, viewBox: fig.viewBox, preserveAspectRatio: "xMidYMid meet" }, layer);
    const svg = layerSvg("art__base");
    const iceSvg = layerSvg("art__ice");
    const lines = layerSvg("art__lines");
    const fx = layerSvg("art__ice art__fx");
    layer.setAttribute("aria-hidden", "true");

    const defs = el("defs", {}, svg);
    const auraGradient = el("radialGradient", { id: `art-aura-${n}` }, defs);
    el("stop", { offset: "0", "stop-color": "#fff1c4", "stop-opacity": "0.9" }, auraGradient);
    el("stop", { offset: "0.25", "stop-color": "#f0c870", "stop-opacity": "0.45" }, auraGradient);
    el("stop", { offset: "1", "stop-color": "#f0c870", "stop-opacity": "0" }, auraGradient);
    const [ax, ay, ar] = fig.aura;
    const warm = el("g", { class: "art__warm" }, svg);
    const aura = el("ellipse", { cx: ax, cy: ay, rx: ar * 1.2, ry: ar, fill: `url(#art-aura-${n})` }, warm);

    if (fig.mist) {
        const mist = el("radialGradient", { id: `art-mist-${n}` }, defs);
        el("stop", { offset: "0", class: "art__mist-stop", "stop-opacity": "0.85" }, mist);
        el("stop", { offset: "1", class: "art__mist-stop", "stop-opacity": "0" }, mist);
        const [mx, my, mrx, mry] = fig.mist;
        el("ellipse", { cx: mx, cy: my, rx: mrx, ry: mry, fill: `url(#art-mist-${n})` }, svg);
    }
    if (fig.ground) el("path", { class: "art__sil", d: fig.ground }, svg);
    el("path", { class: "art__sil", d: fig.fill }, svg);
    // Under the glow lines, so they read as light caught inside the glass.
    mountIce(iceSvg, fx, fig, n);

    const rest = el("g", { class: "art__rest" }, lines);
    const lit = el("g", { class: "art__lit" }, lines);
    const core = stages ? el("g", { class: "art__core" }, lines) : null;

    let end = 0;
    const strokes = fig.strokes.map((s) => {
        el("path", { d: s.d, "stroke-width": s.w * 0.8 }, rest);
        const p = el("path", { d: s.d, "stroke-width": s.w, "stroke-opacity": s.o ?? 1 }, lit);
        const len = p.getTotalLength();
        p.style.strokeDasharray = `${len} ${len}`;
        const speed = s.s ?? 1;
        end = Math.max(end, s.delay + len / speed);
        if (core) el("path", { d: s.d, "stroke-width": Math.max(0.1, s.w * 0.3) }, core);
        return { p, len, delay: s.delay, speed };
    });
    const grains = (fig.grains ?? []).map(([x, y, r, delay]) => {
        end = Math.max(end, delay);
        return { c: el("circle", { class: "art__grain", cx: x, cy: y, r }, lines), delay };
    });
    const motes = (fig.motes ?? []).map(([x, y, r]) => el("circle", { class: "art__mote", cx: x, cy: y, r }, lines));

    const refresh = () => {
        const mix = mixState.getNormalisedValue();
        const front = end * (stages ? Math.min(1, mix / 0.5) : mix);
        const ease = fig.ease ?? 0;
        for (const s of strokes) {
            const t = Math.min(1, Math.max(0, ((front - s.delay) * s.speed) / s.len));
            const grown = t + ease * (t * t * (3 - 2 * t) - t);
            s.p.style.strokeDashoffset = s.len * (1 - grown);
        }
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

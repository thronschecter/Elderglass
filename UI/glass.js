// Glazing for a frozen pedal: a pane of cracked glass that grows over the
// panel like crystals. From all along the panel's edges, jagged cracks grow
// inwards and branch at crystal angles (60 degrees), cross cracks parallel
// to the edges join them into a web, and the shards in between catch the
// light as the front passes. The edges glaze first, the middle last. The
// pane itself (tint, bevel, reflections) fades in over the same time. It
// never takes clicks and stays clear enough that the knobs remain readable.
//
// Shown by the panel's `is-frozen` class (style.css). Every crack grows at
// the same speed (its delay is its distance from the edge), so the glass
// spreads as one front. No per-path filters: after growing it is a static
// layer that the compositor only fades.

const NS = "http://www.w3.org/2000/svg";
const SPEED = 260; // px per second, the speed of the glazing front

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

const pointsToPath = (pts) => "M" + pts.map(([x, y]) => `${x.toFixed(1)} ${y.toFixed(1)}`).join(" L");

let nextId = 0;

export function mountGlass(panel) {
    const n = nextId++;
    const pane = document.createElement("div");
    pane.className = "pedal__glass";
    pane.setAttribute("aria-hidden", "true");
    for (const part of ["glass__body", "glass__bevel", "glass__reflex", "glass__sweep"]) {
        const div = document.createElement("div");
        div.className = part;
        pane.append(div);
    }
    const svg = el("svg", { class: "glass__cracks" }, pane);
    panel.append(pane);

    const build = (w, h) => {
        svg.replaceChildren();
        svg.setAttribute("viewBox", `0 0 ${w} ${h}`);
        const rng = random(0x61a55 + n * 7919);
        const cx = w / 2, cy = h * 0.48;

        const shards = el("g", { class: "glass__shards" }, svg);
        // A crack's profile across the line, as stacked strokes (an SVG
        // stroke can't have a gradient across itself): the refraction band,
        // the shadow (offset down right), the fracture face and the lit edge
        // (offset up left). The same offsets everywhere = one light source.
        const layers = ["crack__band", "crack__shadow", "crack__face", "crack__edge"].map((cls) => el("g", { class: cls }, svg));
        const inside = (x, y) => x > -4 && y > -4 && x < w + 4 && y < h + 4;
        let longest = 0;

        const length = (pts) => {
            let len = 0;
            for (let i = 1; i < pts.length; i++) len += Math.hypot(pts[i][0] - pts[i - 1][0], pts[i][1] - pts[i - 1][1]);
            return len;
        };

        // One stretch of a crack, drawn along its length from `startDist` on.
        const addStretch = (pts, startDist, width) => {
            const len = length(pts);
            if (len < 1) return len;
            longest = Math.max(longest, startDist + len);
            const d = pointsToPath(pts);
            const timing = `${(len / SPEED).toFixed(3)}s`, delay = `${(startDist / SPEED).toFixed(3)}s`;
            for (const group of layers) {
                const p = el("path", { d, pathLength: "1" }, group);
                p.style.setProperty("--w", width.toFixed(2));
                p.style.transitionDuration = timing;
                p.style.transitionDelay = delay;
            }
            return len;
        };

        // A crack tapers towards its tip: up to three stretches, each thinner.
        const addCrack = (pts, startDist, width = 1) => {
            const parts = pts.length >= 7 ? 3 : pts.length >= 4 ? 2 : 1;
            const per = Math.ceil((pts.length - 1) / parts);
            let dist = startDist;
            for (let k = 0; k < parts; k++) {
                const piece = pts.slice(k * per, Math.min(pts.length, (k + 1) * per + 1));
                if (piece.length < 2) break;
                dist += addStretch(piece, dist, width * (1 - 0.28 * k));
            }
        };

        // Jagged walk inwards from (x, y) until `reach`; returns the points
        // with their distance from the edge. Branches split off at crystal
        // angles (about 60 degrees) and grow on their own.
        const walk = (x, y, angle, dist, reach, depth) => {
            const pts = [[x, y]], dists = [dist];
            while (dist < reach && inside(x, y)) {
                // Fairly straight segments: crystal facets, not scribbles.
                const len = 14 + rng() * 16;
                angle += (rng() - 0.5) * 0.28;
                x += Math.cos(angle) * len;
                y += Math.sin(angle) * len;
                dist += len;
                pts.push([x, y]);
                dists.push(dist);
                if (depth < 2 && rng() < 0.2 - depth * 0.07) {
                    const side = rng() < 0.5 ? -1 : 1;
                    const branchReach = dist + (reach - dist) * (0.3 + 0.5 * rng());
                    const branch = walk(x, y, angle + side * (Math.PI / 3) * (0.85 + 0.3 * rng()), dist, branchReach, depth + 1);
                    addCrack(branch.pts, dist, 0.75);
                }
            }
            return { pts, dists };
        };

        // Seeds all around the edge (clockwise from the top left), each
        // growing towards the middle. Most stay near the edge, a few reach
        // further in, so the glass is dense at the rim and open in the middle.
        const perimeter = 2 * (w + h);
        const count = Math.max(14, Math.round(perimeter / 55));
        const rays = [];
        for (let i = 0; i < count; i++) {
            let u = ((i + 0.5 + (rng() - 0.5) * 0.7) / count) * perimeter;
            let x, y;
            if (u < w) [x, y] = [u, 0];
            else if ((u -= w) < h) [x, y] = [w, u];
            else if ((u -= h) < w) [x, y] = [w - u, h];
            else [x, y] = [0, h - (u - w)];
            const toCentre = Math.hypot(cx - x, cy - y);
            const angle = Math.atan2(cy - y, cx - x) + (rng() - 0.5) * 0.6;
            const ray = walk(x, y, angle, 0, toCentre * (0.22 + 0.5 * rng() * rng()), 0);
            addCrack(ray.pts, 0, 1);
            rays.push(ray);
        }

        // Point of a ray at a distance from the edge (along the ray).
        const at = (ray, r) => {
            const i = ray.dists.findIndex((d) => d >= r);
            return i < 0 ? null : ray.pts[i];
        };

        // Cross cracks between neighbouring rays at growing depths, and the
        // shards they enclose (the outermost ones reach to the edge).
        const depths = [];
        for (let r = 18 + rng() * 8; r < Math.min(w, h) * 0.4; r *= 1.5 + rng() * 0.3) depths.push(r);
        for (let i = 0; i < rays.length; i++) {
            const ra = rays[i], rb = rays[(i + 1) % rays.length];
            let prevA = ra.pts[0], prevB = rb.pts[0];
            for (const r of depths) {
                const a = at(ra, r), b = at(rb, r);
                if (!a || !b || Math.hypot(a[0] - b[0], a[1] - b[1]) > 75) break;

                if (rng() < 0.7) {
                    const mid = [(a[0] + b[0]) / 2 + (rng() - 0.5) * 7, (a[1] + b[1]) / 2 + (rng() - 0.5) * 7];
                    addCrack([a, mid, b], r + 6, 0.6);
                }

                const light = rng();
                const shard = el("path", {
                    d: pointsToPath([prevA, a, b, prevB]) + " Z",
                    fill: light > 0.6 ? "#ffffff" : light > 0.25 ? "#bcd7e8" : "#0d1c28",
                    "fill-opacity": (light > 0.6 ? 0.05 + 0.1 * (light - 0.6) : light > 0.25 ? 0.035 : 0.07).toFixed(3),
                }, shards);
                shard.style.transitionDelay = `${(r / SPEED).toFixed(3)}s`;
                [prevA, prevB] = [a, b];
            }
        }
        pane.style.setProperty("--grow", `${(longest / SPEED).toFixed(2)}s`);
    };

    let size = "";
    new ResizeObserver(() => {
        const w = Math.round(pane.clientWidth), h = Math.round(pane.clientHeight);
        if (!w || !h || `${w}x${h}` === size) return;
        size = `${w}x${h}`;
        build(w, h);
    }).observe(pane);
}

# Generates the pedal artwork (UI/art-data.js): for the reverse pedal an old
# tree traced from Tools/art/tree_source.png (Tools/trace_tree.py writes
# Tools/art/tree_trace.json), for the granular pedal a raven traced from
# Tools/art/raven_source.png (Tools/trace_raven.py writes Tools/art/raven_trace.json).
#
# Each figure is a silhouette fill plus many glow strokes, each with a delay.
# A light front travels at one speed from several start points (the roots,
# the raven's eye and heart): stroke i is lit over [delay, delay + length].
# UI/art.js draws the figures and moves the front with the pedal's Mix.
#
# Deterministic for a given seed. Run from the repo root:
#     python Tools/generate_art.py
import math, random, re, json, sys
from pathlib import Path

SEED = int(sys.argv[1]) if len(sys.argv) > 1 else 5
rng = random.Random(SEED)

def lerp(a, b, t):
    return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)

def unit(deg):
    a = math.radians(deg)
    return (math.cos(a), math.sin(a))

def plen(pts):
    return sum(math.dist(pts[i], pts[i + 1]) for i in range(len(pts) - 1))

def smooth(pts, closed=False, tension=0.5):
    if closed:
        pts = pts + pts[:3]
    k = tension / 3.0
    d = [f"M{pts[0][0]:.1f} {pts[0][1]:.1f}"]
    n = len(pts)
    for i in range(n - 1):
        p0 = pts[i - 1] if i > 0 else pts[i]
        p1, p2 = pts[i], pts[i + 1]
        p3 = pts[i + 2] if i + 2 < n else p2
        c1 = (p1[0] + (p2[0] - p0[0]) * k, p1[1] + (p2[1] - p0[1]) * k)
        c2 = (p2[0] - (p3[0] - p1[0]) * k, p2[1] - (p3[1] - p1[1]) * k)
        d.append(f"C{c1[0]:.1f} {c1[1]:.1f} {c2[0]:.1f} {c2[1]:.1f} {p2[0]:.1f} {p2[1]:.1f}")
    if closed:
        d.append("Z")
    return " ".join(d)

def split_at(pts, at):
    """Split a polyline at arc length `at` (the split point is in both halves)."""
    acc = 0.0
    for i in range(len(pts) - 1):
        l = math.dist(pts[i], pts[i + 1])
        if acc + l >= at:
            q = lerp(pts[i], pts[i + 1], (at - acc) / l if l else 0.0)
            return pts[:i + 1] + [q], [q] + pts[i + 1:]
        acc += l
    return pts, pts[-1:] * 2

def poly(pts):
    return "M" + " L".join(f"{x:.1f} {y:.1f}" for x, y in pts) + " Z"

# ====================================================================== tree
def rising(P, h=1.5):
    """Split [x, y, d] points where d turns (by more than `h`), so the light
    runs along each piece in one direction; every piece starts at its lowest d."""
    pieces, start, ext, up = [], 0, 0, None
    for i in range(1, len(P)):
        if P[i][2] == P[ext][2]:
            continue
        if up is None or (P[i][2] > P[ext][2]) == up:
            if up is None and abs(P[i][2] - P[start][2]) < h:
                continue
            if up is None:
                up = P[i][2] > P[start][2]
            ext = i
        elif abs(P[i][2] - P[ext][2]) > h:
            pieces.append(P[start:ext + 1])
            start, up = ext, None
            ext = i
    pieces.append(P[start:])
    return [Q if Q[0][2] <= Q[-1][2] else Q[::-1] for Q in pieces if len(Q) >= 2]

def make_tree():
    """The tree traced by Tools/trace_tree.py: its wood, both edges of the
    thick wood and the centre of each twig as glow lines, the bark strokes as
    finer ones, the grass at its foot. Surface roots spread from the foot; the
    light starts at their tips and climbs the tree through the wood."""
    trace = json.loads((Path(__file__).resolve().parent / "art" / "tree_trace.json").read_text(encoding="utf-8"))
    base = (130.0, 258.0)
    K = 0.36
    OX, OY = base[0] - trace["base"][0] * K, base[1] - trace["base"][1] * K
    W = lambda p: (p[0] * K + OX, p[1] * K + OY)

    # Roots: surface roots spreading over the ground; light starts at their tips.
    # The longest end just short of the bypass and tap footswitches.
    roots = []
    for ang, length in [(178, 40), (163, 34), (148, 22), (32, 24), (17, 36), (2, 38)]:
        pts, a = [(base[0] + math.cos(math.radians(ang)) * 4, base[1] - 3)], float(ang)
        for k in range(int(length / 2.4)):
            a += rng.uniform(-8, 8) + (ang - a) * 0.2
            d = unit(a)
            q = pts[-1]
            pts.append((q[0] + d[0] * 2.4, min(q[1] + d[1] * 2.4 + 0.25, 262)))
        ws = [max(0.4, 9.0 * (1 - i / len(pts)) ** 1.5) for i in range(len(pts))]
        roots.append((pts, ws))
    root_len = max(plen(r[0]) for r in roots)

    fill = [poly([W(p) for p in P]) for P in trace["silhouette"]]
    strokes = []
    for pts, ws in roots:
        n_ = len(pts)
        Lp, Rp = [], []
        for j in range(n_):
            a, b = pts[max(0, j - 1)], pts[min(n_ - 1, j + 1)]
            dx, dy = b[0] - a[0], b[1] - a[1]
            l = math.hypot(dx, dy) or 1
            nx, ny = -dy / l, dx / l
            Lp.append((pts[j][0] + nx * ws[j] / 2, pts[j][1] + ny * ws[j] / 2))
            Rp.append((pts[j][0] - nx * ws[j] / 2, pts[j][1] - ny * ws[j] / 2))
        fill.append(poly(Lp + list(reversed(Rp))))
        # Light runs tip -> trunk, arriving at the trunk exactly at root_len.
        for o in (-0.25, 0.2):
            rev = [(p[0], p[1] + o * w) for p, w in zip(reversed(pts), reversed(ws))]
            strokes.append({"d": smooth(rev), "w": 0.8, "delay": round(root_len - plen(pts), 1)})

    # Up the tree, the front is the distance from the trunk's foot through the wood.
    def emit(P, w, taper=False):
        for Q in rising(P):
            d0 = root_len + Q[0][2] * K
            Q = [W(q) for q in Q]
            if taper and plen(Q) > 4.5:
                # Pointed tip: the last stretch tapers in two steps.
                main, tail = split_at(Q, plen(Q) - 3.0)
                mid_, tip = split_at(tail, 1.6)
                g = len(strokes)       # the three pieces grow as one line
                strokes.append({"d": smooth(main), "w": w, "delay": round(d0, 1), "g": g})
                strokes.append({"d": smooth(mid_), "w": round(w * 0.6, 2), "delay": round(d0 + plen(main), 1), "g": g})
                strokes.append({"d": smooth(tip), "w": round(w * 0.3, 2), "delay": round(d0 + plen(main) + plen(mid_), 1), "g": g})
            else:
                strokes.append({"d": smooth(Q), "w": w, "delay": round(d0, 1)})
    for P in trace["edges"]:
        emit(P, 0.6)
    for P in trace["twigs"]:
        emit(P, 0.5, taper=True)
    for P in trace["bark"]:
        emit(P, 0.3)
    # Grass lights as the light runs in along the roots, from the ground up.
    for P in trace["grass"]:
        P = [W(p) for p in P]
        if P[0][1] < P[-1][1]:
            P = P[::-1]
        strokes.append({"d": smooth(P), "w": 0.3, "delay": round(max(0.0, root_len - math.dist(P[0], base)), 1)})

    # The view starts 18 units lower than the raven's, which lifts the roots
    # up between the footswitches.
    return enliven({"viewBox": "0 48 260 240", "fill": " ".join(fill), "ground": "", "mist": [130, 262, 120, 9], "strokes": strokes,
                    "aura": [round(v, 1) for v in W((265, 250))] + [110], "motes": motes(26, (40, 220), (60, 170))}, "tree")

def path_points(d):
    """The end points of an SVG path's segments (enough to measure it roughly)."""
    v = [float(t) for t in re.findall(r"-?\d+(?:\.\d+)?", d)]
    pts, i = [(v[0], v[1])], 2
    for seg in re.findall(r"[CL]", d):
        i += 6 if seg == "C" else 2
        pts.append((v[i - 2], v[i - 1]))
    return pts

def enliven(fig, name, spread=0.15, early=0.07, ease=0.6):
    """Breaks the even light front up so the glow looks less mechanical:
      - start times shift with a smooth random field over the figure (up to
        `spread` of the whole sweep), so neighbouring lines catch together and
        whole patches light early or late,
      - a few lines (`early`) light almost at once, scattered over the figure,
      - every line grows at its own speed (0.6x to 1.8x) and eases in and out
        (art.js, with `ease`).
    Strokes sharing a "g" (a line cut into pieces) shift and speed up as one.
    UI/art.js stretches the timing so the last line still ends at full glow."""
    r = random.Random(f"{SEED}-{name}")
    strokes = fig["strokes"]
    for st in strokes:
        P = path_points(st["d"])
        st["_p0"], st["_len"] = P[0], plen(P)
    T = max(st["delay"] + st["_len"] for st in strokes)
    waves = [(unit(r.uniform(0, 360)), 2 * math.pi / r.uniform(35, 90), r.uniform(0, 6.3), r.uniform(0.5, 1))
             for _ in range(5)]
    amp = sum(w[3] for w in waves)
    def field(p):
        return sum(a * math.sin((p[0] * u[0] + p[1] * u[1]) * k + ph) for u, k, ph, a in waves) / amp * 1.6
    groups = {}
    for i, st in enumerate(strokes):
        groups.setdefault(st.pop("g", ("own", i)), []).append(st)
    for G in groups.values():
        first = min(G, key=lambda st: st["delay"])
        t0 = first["delay"]
        if r.random() < early:
            start, speed = r.uniform(0, 0.06) * T, r.uniform(0.35, 0.7)
        else:
            shift = max(-1.0, min(1.0, field(first["_p0"]))) * spread * T
            start, speed = max(0.0, t0 + shift + r.gauss(0, 0.02 * T)), math.exp(r.uniform(math.log(0.6), math.log(1.8)))
        for st in G:
            st["delay"] = round(start + (st["delay"] - t0) / speed, 1)
            st["s"] = round(speed, 2)
    for st in strokes:
        del st["_p0"], st["_len"]
    for gr in fig.get("grains", []):
        gr[3] = round(max(0.0, gr[3] + field(gr[:2]) * spread * T), 1)
    fig["ease"] = ease
    return fig

def motes(n, xr, yr):
    return [[round(rng.uniform(*xr), 1), round(rng.uniform(*yr), 1), round(rng.choice([0.5, 0.7, 0.9, 1.2, 1.6]), 2)]
            for _ in range(n)]

# ===================================================================== raven
def make_raven():
    """The raven traced by Tools/trace_raven.py: its silhouette, the line art's
    strokes as the main glow lines, the shaded drawing's highlights as finer
    ones, the branch it perches on and the ink splatter as grains. Placed so the bird stands over
    the footswitches with the branch running out to the right."""
    trace = json.loads((Path(__file__).resolve().parent / "art" / "raven_trace.json").read_text(encoding="utf-8"))
    K = 0.31
    OX, OY = 137 - 555 * K, 192 - 470 * K      # feet between the footswitches, above their LEDs
    W = lambda p: (p[0] * K + OX, p[1] * K + OY)
    # Light starts in the eye and the heart and runs out to every tip.
    seeds = [W((644, 74)), W((560, 260))]
    delay = lambda p: min(math.dist(p, q) for q in seeds)

    fill = " ".join(poly([W(p) for p in P]) for P in trace["silhouette"])
    ground = " ".join(poly([W(p) for p in P]) for P in trace["branch"])

    strokes = []
    flip = random.Random(SEED * 7919 + 2)
    def emit(P, w, inward=0.0):
        # Outward from the seeds; a share of the strokes (`inward`) grows the
        # other way, from its far end in, as if the light gathered into the bird.
        if (delay(P[-1]) < delay(P[0])) != (flip.random() < inward):
            P = P[::-1]
        strokes.append({"d": smooth(P), "w": w, "delay": round(delay(P[0]), 1)})
    for P in trace["lines"]:
        P = [W(p) for p in P]
        # The short feather strokes may gather inward; the long outline lines never do.
        emit(P, round(min(0.8, 0.5 + plen(P) * 0.01), 2), inward=0.2 if plen(P) < 20 else 0.0)
    for P in trace["highlights"]:
        emit([W(p) for p in P], 0.32, inward=0.3)
    # The branch lights after the bird, from under the feet outward.
    feet = W((560, 470))
    reach = max(delay(W(p)) for P in trace["lines"] + trace["highlights"] for p in (P[0], P[-1]))
    for P in trace["branch_lines"]:
        P = [W(p) for p in P]
        if math.dist(P[-1], feet) < math.dist(P[0], feet):
            P = P[::-1]
        strokes.append({"d": smooth(P), "w": 0.5, "delay": round(reach * 0.6 + math.dist(P[0], feet), 1)})

    # Grains: the ink splatter, plus a spray drifting off the tail tips.
    grains = [[round(W((x, y))[0], 1), round(W((x, y))[1], 1), round(max(0.3, r * K * 1.6), 2), round(delay(W((x, y))), 1)]
              for x, y, r in trace["dots"]]
    tips = [(208, 462), (214, 452), (226, 444), (212, 372), (226, 384)]
    for _ in range(140):
        e = W(rng.choice(tips))
        ang = math.radians(rng.gauss(165, 22))
        r = rng.expovariate(1 / 7) + 1.0
        p = (e[0] + math.cos(ang) * r, e[1] + math.sin(ang) * r)
        size = max(0.2, rng.choice([0.25, 0.35, 0.5, 0.7, 0.9]) * (1.2 - min(1, r / 20)))
        grains.append([round(p[0], 1), round(p[1], 1), round(size, 2), round(delay(e) + r, 1)])

    return enliven({"viewBox": "0 30 260 240", "fill": fill, "ground": ground, "strokes": strokes,
                    "aura": list(W((520, 270))) + [100], "motes": [], "grains": grains}, "raven")

if __name__ == "__main__":
    data = json.dumps({"tree": make_tree(), "raven": make_raven()}, separators=(",", ":"))
    out = Path(__file__).resolve().parent.parent / "UI" / "art-data.js"
    out.write_text("// Generated by Tools/generate_art.py - do not edit.\nexport default " + data + ";\n", encoding="utf-8")
    print(f"wrote {out} ({len(data) // 1024} kB)")

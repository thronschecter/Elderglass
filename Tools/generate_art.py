# Generates the pedal artwork (UI/art-data.js): an old tree for the reverse
# pedal and, for the granular pedal, a raven traced from Tools/art/raven_source.png
# (Tools/trace_raven.py writes Tools/art/raven_trace.json).
#
# Each figure is a silhouette fill plus many glow strokes, each with a delay.
# A light front travels at one speed from several start points (the roots,
# the raven's eye and heart): stroke i is lit over [delay, delay + length].
# UI/art.js draws the figures and moves the front with the pedal's Mix.
#
# Deterministic for a given seed. Run from the repo root:
#     python Tools/generate_art.py
import math, random, json, sys
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
class Tree:
    def __init__(self):
        self.pos, self.parent = [], []

    def node(self, p, parent):
        self.pos.append(p)
        self.parent.append(parent)
        return len(self.pos) - 1

    def limb(self, start, ang, length, depth, level, seg=2.4):
        idx, a = start, float(ang)
        steps = max(2, int(length / seg))
        for k in range(steps):
            if level < 2:
                a += (-90 - a) * 0.02
            else:
                a += (a + 90) * 0.01                  # fan outward from vertical
                if math.cos(math.radians(a)) * (1 if a > -90 else -1) > 0.2:
                    a += 0.35 if a > -90 else -0.35   # outer growth droops a little
            if k % 3 == 2:
                a += rng.uniform(-12, 12)
            p = self.pos[idx]
            d = unit(a)
            idx = self.node((p[0] + d[0] * seg, p[1] + d[1] * seg), idx)
            # Lateral spurs along the way: the fine, dense look of an old crown.
            if level >= 2 and depth >= 1 and rng.random() < 0.15:
                side = rng.choice([-1, 1])
                self.limb(idx, a + side * rng.uniform(38, 70), length * rng.uniform(0.3, 0.5),
                          max(0, depth - 3), level + 2, seg)
        if depth == 0:
            return
        n = 3 if level >= 2 and rng.random() < 0.12 else 2
        spread = rng.uniform(22, 36)
        skew = rng.uniform(-8, 8)
        for j in range(n):
            off = (j / (n - 1) - 0.5) * 2 * spread
            self.limb(idx, a + off + skew + rng.uniform(-5, 5), length * rng.uniform(0.74, 0.86),
                      depth - 1, level + 1, seg)

def make_tree():
    T = Tree()
    base = (130.0, 258.0)
    idx = T.node(base, -1)
    a = -91.0
    # Short trunk with a slight sway.
    for k in range(26):
        a += rng.uniform(-3, 3) + (-90 - a) * 0.2
        p = T.pos[idx]
        d = unit(a)
        idx = T.node((p[0] + d[0] * 2.4, p[1] + d[1] * 2.4), idx)
    fork = idx
    for ang, length in [(-152, 30), (-118, 32), (-86, 28), (-54, 32), (-22, 28)]:
        T.limb(fork, ang + rng.uniform(-5, 5), length * rng.uniform(0.9, 1.1), 7, 0)

    n = len(T.pos)
    kids = [[] for _ in range(n)]
    for i, p in enumerate(T.parent):
        if p >= 0:
            kids[p].append(i)
    leaves = [0] * n
    for i in reversed(range(n)):
        leaves[i] = 1 if not kids[i] else sum(leaves[c] for c in kids[i])
    def width(i):
        h = base[1] - T.pos[i][1]
        return max(0.3, 0.62 * leaves[i] ** 0.47) * (1 + 1.0 * math.exp(-h / 6))

    # Roots: surface roots spreading over the ground; light starts at their tips.
    roots = []
    for ang, length in [(178, 52), (163, 34), (148, 22), (32, 24), (17, 38), (2, 56)]:
        pts, a = [(base[0] + math.cos(math.radians(ang)) * 4, base[1] - 3)], float(ang)
        for k in range(int(length / 2.4)):
            a += rng.uniform(-8, 8) + (ang - a) * 0.2
            d = unit(a)
            q = pts[-1]
            pts.append((q[0] + d[0] * 2.4, min(q[1] + d[1] * 2.4 + 0.25, 262)))
        ws = [max(0.4, 9.0 * (1 - i / len(pts)) ** 1.5) for i in range(len(pts))]
        roots.append((pts, ws))
    root_len = max(plen(r[0]) for r in roots)

    # Chains: follow the heaviest child; distance from the trunk base drives the glow.
    dist = [0.0] * n
    for i in range(1, n):
        dist[i] = dist[T.parent[i]] + math.dist(T.pos[i], T.pos[T.parent[i]])
    chains = []
    stack = [0]
    while stack:
        s = stack.pop()
        ch = [s] if T.parent[s] < 0 else [T.parent[s], s]
        node = s
        while kids[node]:
            order = sorted(kids[node], key=lambda c: leaves[c], reverse=True)
            stack.extend(order[1:])
            node = order[0]
            ch.append(node)
        chains.append(ch)

    def relax(pts, passes=4, radius=2, pinned=()):
        """Moving-average smoothing with fixed ends: removes the node kinks,
        keeps every branch where it is."""
        P = list(pts)
        n = len(P)
        if n < 4:
            return P
        for _ in range(passes):
            Q = [P[0]]
            for j in range(1, n - 1):
                if j in pinned:
                    Q.append(P[j])
                    continue
                lo, hi = max(0, j - radius), min(n - 1, j + radius)
                # Taper the window at the ends so attachment points stay put.
                r = min(j - lo, hi - j)
                seg = P[j - r:j + r + 1]
                Q.append((sum(q[0] for q in seg) / len(seg), sum(q[1] for q in seg) / len(seg)))
            Q.append(P[-1])
            P = Q
        return P

    def normals(P, span=3):
        out = []
        n = len(P)
        for j in range(n):
            a_, b_ = P[max(0, j - span)], P[min(n - 1, j + span)]
            dx, dy = b_[0] - a_[0], b_[1] - a_[1]
            l = math.hypot(dx, dy) or 1
            out.append((-dy / l, dx / l))
        return out

    fill = []
    strokes = []
    # Smoothed node positions. Chains come parent-first, so a branch always
    # starts exactly on its parent's smoothed line.
    sm = {}
    twigs = []        # glow line of every centre vein: (stroke indices, length, end point, width, chain)
    total_len = 0.0
    for ci, ch in enumerate(chains):
        ws = [width(i) for i in ch]
        # Lead in from the parent's direction so the branch curves out of it
        # instead of leaving at a hard angle; the lead-in point is dropped after.
        gp = T.parent[ch[0]]
        lead = [sm.get(gp, T.pos[gp])] * 2 if gp >= 0 else []
        start = sm.get(ch[0], T.pos[ch[0]])
        P = relax(lead + [start] + [T.pos[i] for i in ch[1:]], passes=6, radius=3, pinned={len(lead)})[len(lead):]
        for i, p in zip(ch, P):
            sm.setdefault(i, p)
        N = normals(P)
        Lp = [(p[0] + n_[0] * w / 2, p[1] + n_[1] * w / 2) for p, n_, w in zip(P, N, ws)]
        Rp = [(p[0] - n_[0] * w / 2, p[1] - n_[1] * w / 2) for p, n_, w in zip(P, N, ws)]
        fill.append(smooth(Lp + [P[-1]] + list(reversed(Rp)), closed=True, tension=0.4))
        # Fibres: several veins on thick wood, converging as it thins.
        w0 = max(ws)
        k = max(1, min(9, int(w0 / 1.8)))
        offs = [0.0] if k == 1 else [(f / (k - 1) - 0.5) * 1.5 for f in range(k)]
        if 0.0 not in offs:
            offs.append(0.0)   # every branch keeps a centre vein to its tip
        for off in offs:
            ph = rng.uniform(0, 6.28)
            pts = []
            for j, (p, n_) in enumerate(zip(P, N)):
                o = off * ws[j] * 0.5 * 0.85 + min(0.4, 0.05 * ws[j]) * math.sin(j * 0.16 + ph)
                pts.append((p[0] + n_[0] * o, p[1] + n_[1] * o))
            # Side veins merge into the centre one where the wood gets thin.
            if off != 0.0:
                cut = next((j for j, w in enumerate(ws) if w < 1.6 * k * abs(off) + 0.8), len(ws))
                pts = pts[:max(2, cut)]
            elif k > 1 and len(pts) < 2:
                continue
            w_ = round(min(1.2, 0.3 + 0.16 * ws[0] / k ** 0.5), 2)
            o_ = round(min(1.0, 0.45 + 0.22 * ws[0]), 2)
            d0 = root_len + dist[ch[0]]
            total_len += plen(pts)
            if off == 0.0:
                twigs.append((list(range(len(strokes), len(strokes) + (3 if plen(pts) > 7 else 1))),
                              plen(pts), pts[-1], w_, ci))
            if off == 0.0 and plen(pts) > 7:
                # Pointed tip: the last stretch tapers in two steps.
                main, tail = split_at(pts, plen(pts) - 5.0)
                mid_, tip = split_at(tail, 2.6)
                strokes.append({"d": smooth(main, tension=0.5), "w": w_, "o": o_, "delay": round(d0, 1)})
                strokes.append({"d": smooth(mid_), "w": round(w_ * 0.6, 2), "o": o_, "delay": round(d0 + plen(main), 1)})
                strokes.append({"d": smooth(tip), "w": round(w_ * 0.3, 2), "o": o_, "delay": round(d0 + plen(main) + plen(mid_), 1)})
            else:
                strokes.append({"d": smooth(pts, tension=0.5), "w": w_, "o": o_, "delay": round(d0, 1)})
    # Thin out the glow by 10 % of its length, taking twigs from the back of the
    # crown: thin end twigs (nothing grows from them) whose tips lie deep inside
    # it, the most central first. Their wood stays in the silhouette.
    starts = {T.parent[ch[1]] for ch in chains if len(ch) > 1 and T.parent[ch[1]] >= 0}
    tips = [T.pos[i] for i in range(n) if not kids[i]]
    cx = sum(q[0] for q in tips) / len(tips)
    cy = sum(q[1] for q in tips) / len(tips)
    rad = sorted(math.dist(q, (cx, cy)) for q in tips)[int(len(tips) * 0.9)]
    def terminal(ci):
        return not any(i in starts for i in chains[ci][1:])
    back = sorted((t for t in twigs if t[3] <= 0.45 and terminal(t[4])),
                  key=lambda t: math.dist(t[2], (cx, cy)) / rad)
    drop, removed = set(), 0.0
    for idx, length, end, w_, ci in back:
        if removed >= total_len * 0.10:
            break
        drop.update(idx)
        removed += length
    strokes[:] = [st for i, st in enumerate(strokes) if i not in drop]

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

    # Ground mound under the tree. The view starts 14 units lower than the
    # raven's, which lifts the tree so its crown starts level with the raven's head.
    return {"viewBox": "0 44 260 240", "fill": " ".join(fill), "ground": "", "mist": [130, 262, 120, 9], "strokes": strokes,
            "aura": [130, 196, 110], "motes": motes(26, (40, 220), (40, 150))}

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
    def emit(P, w, d0=None):
        if delay(P[-1]) < delay(P[0]):
            P = P[::-1]
        strokes.append({"d": smooth(P), "w": w, "delay": round(delay(P[0]) if d0 is None else d0, 1)})
    for P in trace["lines"]:
        P = [W(p) for p in P]
        emit(P, round(min(0.8, 0.5 + plen(P) * 0.01), 2))
    for P in trace["highlights"]:
        emit([W(p) for p in P], 0.32)
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

    return {"viewBox": "0 30 260 240", "fill": fill, "ground": ground, "strokes": strokes,
            "aura": list(W((520, 270))) + [100], "motes": [], "grains": grains}

if __name__ == "__main__":
    data = json.dumps({"tree": make_tree(), "raven": make_raven()}, separators=(",", ":"))
    out = Path(__file__).resolve().parent.parent / "UI" / "art-data.js"
    out.write_text("// Generated by Tools/generate_art.py - do not edit.\nexport default " + data + ";\n", encoding="utf-8")
    print(f"wrote {out} ({len(data) // 1024} kB)")

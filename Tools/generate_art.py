# Generates the pedal artwork (UI/art-data.js): an old tree for the reverse
# pedal and a raven seen from above for the granular pedal.
#
# Each figure is a silhouette fill plus many glow strokes, each with a delay.
# A light front travels at one speed from several start points (the roots,
# the raven's head and heart): stroke i is lit over [delay, delay + length].
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
    for ch in chains:
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
            if off == 0.0 and plen(pts) > 7:
                # Pointed tip: the last stretch tapers in two steps.
                main, tail = split_at(pts, plen(pts) - 5.0)
                mid_, tip = split_at(tail, 2.6)
                strokes.append({"d": smooth(main, tension=0.5), "w": w_, "o": o_, "delay": round(d0, 1)})
                strokes.append({"d": smooth(mid_), "w": round(w_ * 0.6, 2), "o": o_, "delay": round(d0 + plen(main), 1)})
                strokes.append({"d": smooth(tip), "w": round(w_ * 0.3, 2), "o": o_, "delay": round(d0 + plen(main) + plen(mid_), 1)})
            else:
                strokes.append({"d": smooth(pts, tension=0.5), "w": w_, "o": o_, "delay": round(d0, 1)})
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

    # Ground mound under the tree.
    return {"viewBox": "0 30 260 240", "fill": " ".join(fill), "ground": "", "mist": [130, 262, 120, 9], "strokes": strokes,
            "aura": [130, 196, 110], "motes": motes(26, (40, 220), (40, 150))}

def motes(n, xr, yr):
    return [[round(rng.uniform(*xr), 1), round(rng.uniform(*yr), 1), round(rng.choice([0.5, 0.7, 0.9, 1.2, 1.6]), 2)]
            for _ in range(n)]

# ===================================================================== raven
def feather(root, angle, length, width, bend=0.0, steps=10):
    """Primary feather seen from above: narrow, slightly curved, rounded tip.
    Returns (outline polygon, shaft points)."""
    spine, a, p = [], angle, root
    for i in range(steps + 1):
        spine.append(p)
        d = unit(a)
        p = (p[0] + d[0] * length / steps, p[1] + d[1] * length / steps)
        a += bend / steps
    L, R = [], []
    for i, q in enumerate(spine):
        t = i / steps
        w = width * 0.5 * (0.55 + 0.45 * math.sin(min(1, t * 1.6) * math.pi / 2)) * math.sqrt(max(0, 1 - t ** 4))
        n = unit(angle + bend * t + 90)
        L.append((q[0] + n[0] * w, q[1] + n[1] * w))
        R.append((q[0] - n[0] * w, q[1] - n[1] * w))
    return L + list(reversed(R)), spine

def inside(p, polys):
    x, y = p
    for P in polys:
        c = False
        n = len(P)
        j = n - 1
        for i in range(n):
            xi, yi = P[i]; xj, yj = P[j]
            if (yi > y) != (yj > y) and x < (xj - xi) * (y - yi) / (yj - yi + 1e-12) + xi:
                c = not c
            j = i
        if c:
            return True
    return False

def make_raven():
    CX = 130.0
    mirror = lambda pts: [(2 * CX - x, y) for x, y in pts]
    seeds = [(CX, 48), (CX, 88)]   # head, heart
    def delay(p):
        return min(math.dist(p, q) for q in seeds)

    # Wing sweep: rotate about the shoulder, more toward the tip.
    SH = (122.0, 73.0)
    def sweep(p):
        dx, dy = p[0] - SH[0], p[1] - SH[1]
        d = math.hypot(dx, dy)
        th = math.radians(-(2 + 13 * min(1.0, d / 105) ** 1.6))
        c, s_ = math.cos(th), math.sin(th)
        return (SH[0] + c * dx - s_ * dy, SH[1] + s_ * dx + c * dy)
    sw = lambda pts: [sweep(p) for p in pts]

    half = [(CX, 37), (128.4, 41), (126.6, 46), (125.2, 52), (125.4, 58), (124.4, 64), (121.6, 72), (119.8, 84),
            (120.2, 98), (122, 110), (124.2, 119), (121.2, 130), (117.6, 141), (115.4, 150), (120.4, 153.5), (125.8, 155.6)]
    body = half + [(CX, 157)] + list(reversed(mirror(half)))[:-1]

    bone0 = [(122, 73), (110, 67), (96, 62.5), (82, 59.5), (70, 58.5), (60, 59), (50, 60.5), (40, 62.5), (31, 65)]
    def on_bone(t):
        f = t * (len(bone0) - 1)
        i = min(int(f), len(bone0) - 2)
        return lerp(bone0[i], bone0[i + 1], f - i)
    band0 = bone0 + [(q[0] - 4, q[1] + 15 - 6 * (122 - q[0]) / 91) for q in reversed(bone0)]
    feathers0 = []
    for j in range(12):
        r = on_bone(j / 11 * 0.58)
        feathers0.append(feather((r[0], r[1] + 4), 97 + j * 2.4, 35 + j * 0.3, 10, bend=4))
    for j in range(8):
        r = on_bone(0.6 + j / 7 * 0.4)
        length = 37 + j * 2.0 if j < 6 else 47 - (j - 5) * 3
        feathers0.append(feather((r[0] + 1, r[1] + 3), 116 + j * 8.6, length, 7.6 - j * 0.25, bend=-6))

    bone = sw(bone0)
    band = sw(band0)
    feathers = [(sw(pl), sw(sp)) for pl, sp in feathers0]
    wing_polys = [band] + [pl for pl, _ in feathers]
    left_shapes = [body] + wing_polys
    shapes = [body] + wing_polys + [mirror(p) for p in wing_polys]
    fill = " ".join(smooth(p, closed=True, tension=0.35) for p in shapes)

    # ---- organic glow lines: streamlines through a feather-direction field
    spines = [sp for _, sp in feathers] + [list(reversed(bone))]
    def wing_dir(p):
        vx = vy = 0.0
        for sp in spines:
            best, bi = 1e9, 0
            for i, q in enumerate(sp):
                d = (q[0] - p[0]) ** 2 + (q[1] - p[1]) ** 2
                if d < best:
                    best, bi = d, i
            a_, b_ = sp[max(0, bi - 1)], sp[min(len(sp) - 1, bi + 1)]
            tx, ty = b_[0] - a_[0], b_[1] - a_[1]
            l = math.hypot(tx, ty) or 1
            w = 1.0 / (best + 4.0) ** 1.5
            vx += tx / l * w; vy += ty / l * w
        l = math.hypot(vx, vy) or 1
        return vx / l, vy / l
    def body_dir(p):
        if p[1] > 118:   # tail fans out from its base
            dx, dy = p[0] - CX, p[1] - 112
            l = math.hypot(dx, dy) or 1
            return dx / l, dy / l
        return 0.0, 1.0

    strokes = []
    def stream(start, max_len, field, region, ph):
        pts = [start]
        p = start
        wander = 0.0
        for k in range(int(max_len / 1.1)):
            dx, dy = field(p)
            wander += rng.gauss(0, 0.07)
            wander *= 0.93
            ang = math.atan2(dy, dx) + 0.38 * math.sin(p[0] * 0.09 + ph) * math.cos(p[1] * 0.08 - ph) + wander
            q = (p[0] + math.cos(ang) * 1.1, p[1] + math.sin(ang) * 1.1)
            if not inside(q, region):
                break
            pts.append(q)
            p = q
        return pts

    def emit(pts, w):
        if len(pts) < 6:
            return
        pts = pts[::2] + ([pts[-1]] if len(pts) % 2 == 0 else [])
        for P in (pts, mirror(pts)):
            strokes.append({"d": smooth(P), "w": w, "delay": round(delay(P[0]), 1)})

    # Wing: many lines, mostly starting near the bone, flowing out along the feathers.
    for i in range(24):
        t = (i + rng.random()) / 24
        q = sweep(on_bone(t))
        start = (q[0] + rng.uniform(-1.5, 1.5), q[1] + rng.uniform(2, 6))
        if not inside(start, wing_polys):
            continue
        emit(stream(start, rng.uniform(30, 70), wing_dir, wing_polys, rng.uniform(0, 6.3)),
             round(rng.uniform(0.3, 0.6), 2))
    # Light running out along the arm from the shoulder.
    for _ in range(2):
        pts = [(x + rng.gauss(0, 0.5), y + rng.uniform(-1.5, 3.5) + 1.2 * math.sin(x * 0.12 + rng.uniform(0, 6)))
               for x, y in bone[1:]]
        emit([sweep((123, 74))] + pts, round(rng.uniform(0.45, 0.85), 2))
    # Body and tail.
    for _ in range(7):
        start = (CX + rng.uniform(-3.5, 3.5), rng.uniform(52, 100))
        pts = stream(start, rng.uniform(20, 90), body_dir, [body], rng.uniform(0, 6.3))
        if len(pts) >= 6:
            pts = pts[::2]
            strokes.append({"d": smooth(pts), "w": round(rng.uniform(0.35, 0.75), 2), "delay": round(delay(pts[0]), 1)})

    # ---- spray off the wing tips, trailing edges and tail
    edge = []
    for _, sp in feathers[12:]:
        edge += [sp[-1]] * 3
    edge += [sp[-1] for _, sp in feathers[:12]][::2] + [(117, 151), (123, 155), (CX, 157), (CX, 157)]
    edge += mirror(edge)
    centre = (CX, 92)
    grains = []
    for _ in range(320):
        e = rng.choice(edge)
        ang = math.atan2(e[1] - centre[1], e[0] - centre[0]) + rng.gauss(0, 0.32)
        r = rng.expovariate(1 / 17) + 2.0
        p = (e[0] + math.cos(ang) * r, e[1] + math.sin(ang) * r)
        size = max(0.22, rng.choice([0.3, 0.45, 0.6, 0.8, 1.1, 1.6, 2.2]) * (1.25 - min(1, r / 55)))
        grains.append([round(p[0], 1), round(p[1], 1), round(size, 2), round(delay(e) + r, 1)])
    for _ in range(40):
        e = rng.choice(edge)
        ang = math.atan2(e[1] - centre[1], e[0] - centre[0]) + rng.gauss(0, 0.2)
        r0 = rng.uniform(2, 10)
        r1 = r0 + rng.uniform(5, 18)
        mid_bend = rng.gauss(0, 0.12)
        pts = [(e[0] + math.cos(ang + mid_bend * t) * (r0 + (r1 - r0) * t),
                e[1] + math.sin(ang + mid_bend * t) * (r0 + (r1 - r0) * t)) for t in (0, 0.5, 1)]
        strokes.append({"d": smooth(pts), "w": round(rng.uniform(0.3, 0.6), 2), "delay": round(delay(e) + r0, 1)})

    return {"viewBox": "-40 22 340 170", "fill": fill, "ground": "", "strokes": strokes,
            "aura": [CX, 96, 92], "motes": [], "grains": grains}

if __name__ == "__main__":
    data = json.dumps({"tree": make_tree(), "raven": make_raven()}, separators=(",", ":"))
    out = Path(__file__).resolve().parent.parent / "UI" / "art-data.js"
    out.write_text("// Generated by Tools/generate_art.py - do not edit.\nexport default " + data + ";\n", encoding="utf-8")
    print(f"wrote {out} ({len(data) // 1024} kB)")

# Traces the raven artwork into Tools/art/raven_trace.json, which
# generate_art.py places on the granular pedal. Two drawings of the same bird,
# in register:
#   Tools/art/raven_source.png  shaded drawing -> silhouette, highlights, branch, dots
#   Tools/art/raven_lines.png   line art       -> lines, branch_lines
# Output:
#   - silhouette:   the bird's dark mass (bill and light crown included),
#   - lines:        the line art's ink strokes on the bird, joined into long
#                   connected lines (the main glow lines),
#   - highlights:   the light feather strokes of the shaded drawing (finer glow),
#   - branch:       the branch under the feet, and branch_lines: its ink strokes,
#   - dots:         ink splatter around the bird (become glowing grains).
# All coordinates are source-image pixels.
#
# Needs numpy, scipy, scikit-image and opencv-python-headless (only this
# script; generate_art.py does not). Run from the repo root:
#     python Tools/trace_raven.py
import json
from pathlib import Path

import cv2
import numpy as np
from scipy import ndimage as ndi
from scipy.spatial import cKDTree
from skimage import morphology

HERE = Path(__file__).resolve().parent
SRC = HERE / "art" / "raven_source.png"
LINES = HERE / "art" / "raven_lines.png"
OUT = HERE / "art" / "raven_trace.json"

# Top edge of the branch under the feet (image x -> y): below it only solid
# black is bird, so the branch's shading does not join the silhouette.
BRANCH_TOP = ([300, 430, 560, 640, 760, 1024], [600, 503, 462, 436, 392, 300])
BRANCH_CUT = 500        # image x where the branch is snapped off, at the feet
LINE_THRESHOLD = 20     # how much brighter than its surroundings a stroke must be
MIN_LINE = 10            # shortest stroke kept, px
MIN_HIGHLIGHT = 10     # shortest highlight kept, px, after joining

def disk(r):
    return cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (2 * r + 1, 2 * r + 1))

def largest(m):
    lab, n = ndi.label(m)
    sizes = ndi.sum(m, lab, range(1, n + 1))
    return lab == (1 + int(np.argmax(sizes)))

def holes(m, size):
    return morphology.remove_small_holes(m, max_size=size)

def plen(P):
    return float(sum(np.hypot(P[i + 1][0] - P[i][0], P[i + 1][1] - P[i][1]) for i in range(len(P) - 1)))

def trace(skel):
    """Skeleton -> polylines: cut at junctions, walk each simple piece."""
    nb = ndi.convolve(skel.astype(np.uint8), np.ones((3, 3), np.uint8), mode="constant") - 1
    lab, n = ndi.label(skel & (nb <= 2), structure=np.ones((3, 3)))
    paths = []
    for i, sl in enumerate(ndi.find_objects(lab)):
        ys, xs = np.nonzero(lab[sl] == i + 1)
        pts = set(zip((ys + sl[0].start).tolist(), (xs + sl[1].start).tolist()))
        if len(pts) < 3:
            continue
        def nbrs(p):
            return [(p[0] + dy, p[1] + dx) for dy in (-1, 0, 1) for dx in (-1, 0, 1)
                    if (dy or dx) and (p[0] + dy, p[1] + dx) in pts]
        ends = [p for p in pts if len(nbrs(p)) == 1]
        cur = ends[0] if ends else next(iter(pts))
        path, seen = [cur], {cur}
        while True:
            nx = [q for q in nbrs(cur) if q not in seen]
            if not nx:
                break
            cur = min(nx, key=lambda q: abs(q[0] - cur[0]) + abs(q[1] - cur[1]))
            seen.add(cur)
            path.append(cur)
        paths.append([(x, y) for y, x in path])
    return paths

def simplify(P, eps, closed=False):
    a = cv2.approxPolyDP(np.array(P, np.float32).reshape(-1, 1, 2), eps, closed).reshape(-1, 2)
    return [[round(float(x), 1), round(float(y), 1)] for x, y in a]

def contours(mask, eps=0.8, min_area=30):
    """Outer outlines and holes; holes come out with the opposite winding."""
    cs, hier = cv2.findContours(mask.astype(np.uint8), cv2.RETR_CCOMP, cv2.CHAIN_APPROX_NONE)
    out = []
    for c, h in zip(cs, hier[0]):
        if abs(cv2.contourArea(c)) < min_area:
            continue
        P = simplify(c.reshape(-1, 2), eps, True)
        a = cv2.contourArea(np.array(P, np.float32))
        hole = h[3] >= 0
        if (a < 0) != hole:            # outer: positive area, hole: negative
            P.reverse()
        out.append(P)
    return out

def join(paths, gap=4.5, max_turn=55):
    """Reconnect skeleton pieces: an end meets another end within `gap` px
    when both run on in about the same direction (turn below `max_turn`)."""
    paths = [[tuple(q) for q in P] for P in paths]
    cos_t = np.cos(np.radians(max_turn))
    def outward(P, at_end):
        a, b = (P[-1], P[max(0, len(P) - 5)]) if at_end else (P[0], P[min(len(P) - 1, 4)])
        d = np.subtract(a, b, dtype=float)
        return d / (np.hypot(*d) or 1.0)
    changed = True
    while changed:
        changed = False
        ends = [(i, e, P[-1] if e else P[0]) for i, P in enumerate(paths) for e in (True, False)]
        pairs = cKDTree([q for _, _, q in ends]).query_pairs(gap, output_type="ndarray")
        pairs = sorted(pairs.tolist(), key=lambda pq: np.hypot(*np.subtract(ends[pq[0]][2], ends[pq[1]][2])))
        used = set()
        for a, b in pairs:
            (i, ei, pi), (j, ej, pj) = ends[a], ends[b]
            if i == j or i in used or j in used:
                continue
            da, db = outward(paths[i], ei), outward(paths[j], ej)
            if np.dot(da, db) > -cos_t:
                continue
            v = np.subtract(pj, pi, dtype=float)
            if np.hypot(*v) > 1.5 and np.dot(v / np.hypot(*v), da) < 0.3:
                continue
            A = paths[i] if ei else paths[i][::-1]          # ends at the joint
            B = paths[j][::-1] if ej else paths[j]          # starts at the joint
            paths[i], paths[j] = A + B, None
            used |= {i, j}
            changed = True
        paths = [P for P in paths if P]
    return paths

def ink_lines(g, region, min_len):
    """The line art's ink strokes inside `region`, joined into long lines."""
    gs = cv2.GaussianBlur(g, (0, 0), 0.8)
    hat = cv2.morphologyEx(gs, cv2.MORPH_CLOSE, disk(3)) - gs
    m = region & (hat > 22) & (gs < 175)
    m = morphology.remove_small_objects(m, max_size=12)
    paths = join(trace(morphology.skeletonize(m)))
    return [simplify(P, 0.6) for P in paths if plen(P) >= min_len]

def lines_in(g, region, threshold, min_len, bright=True, joined=False):
    """Thin strokes inside `region`: bright ones (white top-hat) or dark ink;
    `joined` reconnects the pieces into longer lines first."""
    gs = cv2.GaussianBlur(g, (0, 0), 0.9)
    if bright:
        hat = gs - cv2.morphologyEx(gs, cv2.MORPH_OPEN, disk(3))
        m = region & (hat > threshold) & (gs > 55)
    else:
        hat = cv2.morphologyEx(gs, cv2.MORPH_CLOSE, disk(3)) - gs
        m = region & (hat > threshold) & (gs < 150)
    m = morphology.remove_small_objects(m, max_size=10)
    paths = trace(morphology.skeletonize(m))
    if joined:
        paths = join(paths)
    return [simplify(P, 0.7) for P in paths if plen(P) >= min_len]

def main():
    g = cv2.imread(str(SRC), cv2.IMREAD_GRAYSCALE).astype(np.float32)
    H, W = g.shape
    yy, xx = np.mgrid[0:H, 0:W]
    below = yy > np.interp(xx, *BRANCH_TOP)
    gb = cv2.GaussianBlur(g, (0, 0), 1.2)

    # ---- the bird: solid black core, plus the light parts its ink encloses
    core = largest(cv2.morphologyEx((gb < 85).astype(np.uint8), cv2.MORPH_CLOSE, disk(3)) > 0)
    core = holes(core, 1500)
    ink = cv2.morphologyEx((gb < 150).astype(np.uint8), cv2.MORPH_CLOSE, disk(2)) > 0
    near = cv2.dilate(core.astype(np.uint8), disk(14)) > 0
    raven = largest(core | holes(ink & near & ~below, 700))
    raven = holes(cv2.morphologyEx(raven.astype(np.uint8), cv2.MORPH_CLOSE, disk(4)) > 0, 700)
    inner = cv2.erode(raven.astype(np.uint8), disk(2)) > 0

    # ---- the branch: its ink, closed into a solid limb, without the bird
    ink_b = (gb < 165) & ~cv2.dilate(raven.astype(np.uint8), disk(1)).astype(bool)
    limb = cv2.morphologyEx(ink_b.astype(np.uint8), cv2.MORPH_CLOSE, disk(13)) > 0
    limb = holes(limb, 20000) & (yy > np.interp(xx, *BRANCH_TOP) - 40)
    # Snapped off just behind the feet (a slanted cut), so it stays clear of
    # the bypass footswitch.
    limb &= (xx > BRANCH_CUT - (yy - 470) * 0.35) & (yy < H - 26)   # and clear of the image edge
    limb = largest(cv2.morphologyEx(limb.astype(np.uint8), cv2.MORPH_OPEN, disk(6)) > 0)
    limb = ndi.binary_fill_holes(limb)
    # Smooth the lumpy outline: blur the mask and threshold it again.
    branch = (cv2.GaussianBlur(limb.astype(np.float32), (0, 0), 4) > 0.5) & ~raven

    # ---- ink splatter: small dark dots off the bird and branch
    spots = (gb < 120) & ~cv2.dilate((raven | branch).astype(np.uint8), disk(6)).astype(bool)
    lab, n = ndi.label(spots)
    dots = []
    for i, sl in enumerate(ndi.find_objects(lab)):
        area = int((lab[sl] == i + 1).sum())
        if 2 <= area <= 60:
            cy, cx = ndi.center_of_mass(lab[sl] == i + 1)
            x, y = cx + sl[1].start, cy + sl[0].start
            if 150 < x < 900 and 30 < y < 540:
                dots.append([round(float(x), 1), round(float(y), 1), round(float(np.sqrt(area / np.pi)), 2)])

    # ---- the line art: its strokes on the bird (outline included) and branch
    la = cv2.imread(str(LINES), cv2.IMREAD_GRAYSCALE).astype(np.float32)
    assert la.shape == g.shape, "the line art must be in register with the shaded drawing"
    on_bird = cv2.dilate(raven.astype(np.uint8), disk(3)) > 0
    on_branch = (cv2.dilate(branch.astype(np.uint8), disk(3)) > 0) & ~on_bird

    data = {
        "size": [W, H],
        "silhouette": contours(raven),
        "lines": ink_lines(la, on_bird, MIN_LINE),
        "highlights": lines_in(g, inner, LINE_THRESHOLD, MIN_HIGHLIGHT, joined=True),
        "branch": contours(branch, eps=1.0, min_area=200),
        "branch_lines": ink_lines(la, on_branch, 12),
        "dots": dots,
    }
    OUT.write_text(json.dumps(data, separators=(",", ":")), encoding="utf-8")
    print(f"wrote {OUT}: {len(data['silhouette'])} outlines, {len(data['lines'])} lines "
          f"({int(sum(plen(P) for P in data['lines']))} px), {len(data['highlights'])} highlights, "
          f"{len(data['branch_lines'])} branch lines, {len(dots)} dots")

if __name__ == "__main__":
    main()

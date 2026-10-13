# Traces the tree artwork into Tools/art/tree_trace.json, which
# generate_art.py places on the reverse pedal. Source: Tools/art/tree_source.png,
# an ink drawing of a bare old tree standing in a tuft of grass.
# Output:
#   - silhouette:   the wood (trunk, limbs and twigs) as outlines with holes,
#   - edges:        the glow lines along both edges of the thick wood and
#   - twigs:        a single centre line along each thin twig (together they
#                   read like a line drawing of the tree),
#   - bark:         the drawing's ink strokes inside the thick wood (finer glow),
#   - grass:        the grass blades around the trunk's foot,
#   - base:         the foot of the trunk, where the light enters the tree.
# Edge, twig and bark points are [x, y, d] with d the distance from the base
# measured through the wood.
# All coordinates are source-image pixels.
#
# Needs numpy, scipy, scikit-image and opencv-python-headless (only this
# script; generate_art.py does not). Run from the repo root:
#     python Tools/trace_tree.py
import json
from pathlib import Path

import cv2
import numpy as np
from scipy import ndimage as ndi
from skimage import graph, morphology

from trace_raven import contours, disk, holes, join, largest, plen, simplify, trace

HERE = Path(__file__).resolve().parent
SRC = HERE / "art" / "tree_source.png"
OUT = HERE / "art" / "tree_trace.json"

BASE = (267, 538)       # foot of the trunk
GRASS_Y = 462           # below it the grass and its wash start: only the trunk is wood
TRUNK_FOOT = [(252, GRASS_Y), (282, GRASS_Y), (290, 541), (246, 541)]
UP = 2                  # traced at twice the drawing's size, for smoother lines
INK = 175               # darker than this is ink
THICK = 5.0             # wood at least this wide gets two edge lines, thinner a centre line
MIN_LINE = 6            # shortest glow line kept, px

def smooth_line(P, r=2):
    """Moving average with fixed ends: takes the pixel steps out of a traced line."""
    P = np.asarray(P, np.float32)
    if len(P) < 2 * r + 2:
        return P
    k = np.ones(2 * r + 1, np.float32) / (2 * r + 1)
    Q = P.copy()
    for a in (0, 1):
        Q[r:-r, a] = np.convolve(P[:, a], k, mode="valid")
    return Q

def main():
    S = UP
    g = cv2.imread(str(SRC), cv2.IMREAD_GRAYSCALE).astype(np.float32)
    g = cv2.resize(g, None, fx=S, fy=S, interpolation=cv2.INTER_CUBIC)
    H, W = g.shape
    yy, xx = np.mgrid[0:H, 0:W]
    gb = cv2.GaussianBlur(g, (0, 0), 1.2)
    base = (BASE[0] * S, BASE[1] * S)

    # ---- the wood: its ink; the hatched inside of the thick wood filled
    foot = np.zeros((H, W), np.uint8)
    cv2.fillPoly(foot, [np.array(TRUNK_FOOT, np.int32) * S], 1)
    foot = foot.astype(bool)
    ink = (gb < INK) & ((yy < GRASS_Y * S) | foot)
    ink = morphology.remove_small_objects(ink, max_size=20)
    body = cv2.morphologyEx(ink.astype(np.uint8), cv2.MORPH_OPEN, disk(4)) > 0
    body = holes(cv2.morphologyEx(body.astype(np.uint8), cv2.MORPH_CLOSE, disk(5)) > 0, 2500)
    body |= ndi.binary_fill_holes(cv2.morphologyEx((ink & foot).astype(np.uint8), cv2.MORPH_CLOSE, disk(8)) > 0) & foot
    wood = largest(ink | body)
    wood = holes(wood, 60)
    # Smooth the ragged edge without losing the fine twigs.
    wood = largest((cv2.GaussianBlur(wood.astype(np.float32), (0, 0), 1.2) > 0.5) | morphology.skeletonize(wood))

    # ---- distance from the foot of the trunk, through the wood
    mcp = graph.MCP_Geometric(np.where(wood, 1.0, np.inf))
    dist, _ = mcp.find_costs([(base[1], base[0])])
    # Outside the wood (smoothed lines stray a little): the nearest wood's.
    _, (iy, ix) = ndi.distance_transform_edt(~wood, return_indices=True)
    dist = np.where(np.isfinite(dist), dist, 0)[iy, ix] / S

    # ---- glow lines: the edges of the thick wood, the centre of the thin
    dt = ndi.distance_transform_edt(wood)
    thick = ndi.maximum_filter(dt, size=4 * S + 1) * 2 >= THICK * S
    edge_lines = []
    cs, _ = cv2.findContours(wood.astype(np.uint8), cv2.RETR_CCOMP, cv2.CHAIN_APPROX_NONE)
    for c in cs:
        P = c.reshape(-1, 2)
        keep = thick[P[:, 1], P[:, 0]] & (P[:, 1] < BASE[1] * S - 3)
        if keep.all():
            edge_lines.append(P.tolist() + [P[0].tolist()])
            continue
        # Runs of kept points, starting after a gap so none wraps around.
        k0 = int(np.argmin(keep))
        P, keep = np.roll(P, -k0, 0), np.roll(keep, -k0)
        run = []
        for p, k in zip(P.tolist(), keep):
            if k:
                run.append(p)
            elif run:
                edge_lines.append(run)
                run = []
        if run:
            edge_lines.append(run)
    skel = morphology.skeletonize(wood) & ~thick
    centre = join(trace(skel), gap=3 * S)
    def finish(paths, min_len, eps=0.35):
        out = []
        for P in paths:
            P = smooth_line(P, 2 * S) / S
            if plen(P.tolist()) >= min_len:
                out.append(simplify(P.tolist(), eps))
        return out
    edges, twigs = finish(edge_lines, MIN_LINE), finish(centre, MIN_LINE)

    # ---- bark: the ink strokes inside the thick wood
    inner = (cv2.erode(wood.astype(np.uint8), disk(2 * S)) > 0) & (dt >= 2.5 * S)
    hat = cv2.morphologyEx(gb, cv2.MORPH_CLOSE, disk(3 * S)) - gb
    marks = morphology.remove_small_objects(inner & (hat > 16) & (gb < 160), max_size=10 * S * S)
    bark = finish(join(trace(morphology.skeletonize(marks)), gap=3 * S), 5)

    # ---- grass: thin dark blades around the foot, off the trunk
    near = (yy > (GRASS_Y - 30) * S) & (np.hypot(xx - base[0], (yy - base[1]) * 1.6) < 120 * S)
    off = ~cv2.dilate(wood.astype(np.uint8), disk(2 * S)).astype(bool)
    blades = morphology.remove_small_objects(near & off & (hat > 22) & (gb < 175), max_size=10 * S * S)
    grass = finish(join(trace(morphology.skeletonize(blades)), gap=3 * S), 7)

    silhouette = [[[round(x / S, 1), round(y / S, 1)] for x, y in P] for P in contours(wood, eps=0.8, min_area=8 * S * S)]
    H, W = H // S, W // S

    def with_dist(P):
        return [[x, y, round(float(dist[min(H * S - 1, int(round(y * S))), min(W * S - 1, int(round(x * S)))]), 1)] for x, y in P]

    data = {
        "size": [W, H],
        "base": list(BASE),
        "silhouette": silhouette,
        "edges": [with_dist(P) for P in edges],
        "twigs": [with_dist(P) for P in twigs],
        "bark": [with_dist(P) for P in bark],
        "grass": grass,
    }
    OUT.write_text(json.dumps(data, separators=(",", ":")), encoding="utf-8")
    print(f"wrote {OUT}: {len(data['silhouette'])} outlines, {len(edges)} edge lines, {len(twigs)} twigs "
          f"({int(sum(plen(P) for P in edges + twigs))} px), {len(bark)} bark strokes, {len(grass)} grass blades")

if __name__ == "__main__":
    main()

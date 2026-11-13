"""Market Mesa v2: a sculpted Spyro 2 level from Season of Ice's layout (PC S2WORLD + mesh for PS1).

  python tools/soi/mesa_build.py [--out data/soi/pc/market-mesa.s2world]

Replaces the box blockout export (s2world_export.py) with a level built to the retail measurements
in data/soi/ps1/terrain_metrics.json (tools/soi/s2_terrain_metrics.py):
- Layout: SoI's solids on an 8-unit lattice (cell centre sampling; <= 4 units shift, see report).
  Walkable tops keep SoI's heights. Faces are 16 x 16 SoI units (1.63 world units, the retail
  median quad edge is 1.87) or 8 x 8 where the layout needs it.
- Watertight: every face is built on shared integer vertices; any vertex lying on another face's
  edge is inserted into that face (no T-junctions), and such polygons are split into quads/triangles.
- Surfaces come from SoI's own art projected top-down (roof tiles, plaza, roof lips, hatches,
  bridges); walls by context (plaster under roofs, brick risers on the plaza, cobble cliffs).
- The islands float: cliffs run down to the cloud line, then a closed, tapering rock underside.
- Sculpt: cliff faces and undersides are displaced (low-frequency noise), top edges are rounded.
- Texture: retail Spyro 2 tiles, one tile per 16 units (square texels on tops; walls 16 x 48 z).
- Light: vertex colours baked from a sun, sky ambient and heightfield ambient occlusion.
"""
from __future__ import annotations

import argparse
import colorsys
import json
import math
import sys
from collections import defaultdict
from pathlib import Path

import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parents[1]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(PROJECT / "tools"))
import soi_extract  # noqa: E402

V = 1.0 / 24.0               # SoI z unit -> world units (one 24-unit terrace = 1.0)
H = V * math.sqrt(6.0)       # SoI x/y unit -> world units (keeps SoI's projection exact)
G = 8                        # lattice step
BLK = 16                     # face size (one texture tile)
ROW = 40                     # wall texture height (z units, 16 * H / V = 39.2); rows split at 40k + 1
CLOUD_Z = 25                 # island cliffs run down to here; the underside hangs below
BRIDGE_T = 12                # bridge slab thickness (z units)
KERB = 12                    # the kerb row under a cliff-top edge (z units)

# Tiles: (scene slot, tile index) = a retail tile (data/soi/retail-atlas/atlas-<slot>.png); a name =
# an authored tile painted after SoI's art (tools/soi/mesa_textures.py -> data/soi/pc/textures/).
MAT = {
    "plaza": (15, 68), "plaza_var": (33, 7), "plaza_edge": (15, 67), "roof": "slate", "lip": "slate",
    "hatch": "hatch_soi", "bridge": (15, 68), "bridge_edge": (15, 67), "wall": "plaster", "window": "window_soi",
    "door": "door_soi", "banner": "banner_orange", "cornice": "plaster", "riser": "cobble_sand",
    "riser_top": "cobble_sand", "kerb": "cobble_sand", "snow": "snow_puff", "cliff": "cobble_sand",
    "cliff2": "cobble_sand_b", "cliff_foot": "cobble_snowfoot", "under": "under_rock", "bridge_side": (15, 52),
    "bridge_rail": (15, 52), "foliage": "pine", "bark": "bark_soi", "post": "wood_soi", "counter": (39, 44),
    "crate": (61, 78), "awning": "awning_soi", "pole": "wood_soi", "flag": "flag_cyan", "cloth": "cloth_pink",
    "coping_side": "coping_side", "coping_top": "coping_top", "far_rock": "far_rock", "far_top": "snow_puff",
}
CROW, CBASE = 48, CLOUD_Z + 24    # cliffs: a 24-z snow row at the base, then 48-z rows (49 + 48k, on the terraces)
COPING = (-4.0, 0.0), (2.0, 4.0), (0.0, -6.0)   # (outward offset, dz): on the roof, outer top, on the wall

CLASSES = ["void", "plaza", "roof", "lip", "hatch", "bridge", "green", "other"]


def art_class(rgb: np.ndarray) -> np.ndarray:
    """Per pixel class from SoI's art colour (top-down projection)."""
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    mx, mn = rgb.max(-1), rgb.min(-1)
    v = mx
    s = np.where(mx > 0, (mx - mn) / np.maximum(mx, 1e-6), 0)
    hue = np.zeros_like(v)
    d = np.maximum(mx - mn, 1e-6)
    hue = np.where(mx == r, ((g - b) / d) % 6, hue)
    hue = np.where(mx == g, (b - r) / d + 2, hue)
    hue = np.where(mx == b, (r - g) / d + 4, hue)
    hue = hue * 60
    out = np.full(v.shape, 7, np.int8)
    out[(hue >= 200) & (hue < 265) & (s >= 0.25)] = 2                      # roof tiles
    out[(hue >= 170) & (hue < 265) & (s < 0.25) & (v > 0.75)] = 3         # light roof lip
    out[(hue >= 225) & (hue < 290) & (s < 0.2) & (v > 0.55) & (v <= 0.95)] = 5  # bridge stone
    out[((hue >= 300) | (hue < 20)) & (s >= 0.12)] = 1                    # plaza brick
    out[(hue >= 20) & (hue < 55) & (s >= 0.2)] = 4                        # wooden hatch
    out[(hue >= 70) & (hue < 170) & (s >= 0.25)] = 6                      # trees / grass
    out[v < 0.08] = 0
    return out


class Mesh:
    """Polygons on shared integer vertices (SoI units). Faces: dict(keys, n, kind, mat, uv)."""

    def __init__(self):
        self.index: dict[tuple, int] = {}
        self.pos: list[tuple] = []
        self.faces: list[dict] = []
        self.copings: list[tuple] = []   # building wall tops: (axis, c, a0, a1, z, d)
        self.rails: list[tuple] = []     # bridge edges over the void: (axis, c, a0, a1, z, d)

    def v(self, key, index=None):
        k = tuple(key)
        index = self.index if index is None else index
        if k not in index:
            index[k] = len(self.pos)
            self.pos.append(k)
        return index[k]

    def face(self, keys, normal, kind, mat, frame):
        """keys in order around the face; frame = (origin, u_axis, v_axis, u_len, v_len, u0, v0).
        Decor (visual props) keeps its own vertices, never welded to the terrain."""
        if kind == "decor":
            self.dindex = getattr(self, "dindex", {})
            vs = [self.v(k, self.dindex) for k in keys]
        else:
            vs = [self.v(k) for k in keys]
        self.faces.append(dict(vs=vs, n=normal, kind=kind, mat=mat, frame=frame))


def build_layout(level: dict, mapimg: Image.Image):
    coll = level["collision"]
    hm, (x0, y0) = soi_extract.heightmap(coll)
    mp = np.asarray(mapimg.convert("RGB")).astype(float) / 255
    Hh, Ww = hm.shape
    ys, xs = np.mgrid[0:Hh, 0:Ww]
    X, Y = xs + x0 + 0.5, ys + y0 + 0.5
    px, py = (2 * (X - Y)).astype(int), (X + Y - hm).astype(int)
    ok = (hm >= 0) & (px >= 0) & (px < mp.shape[1]) & (py >= 0) & (py < mp.shape[0])
    art = np.zeros(hm.shape + (3,))
    art[ok] = mp[py[ok], px[ok]]
    cls = art_class(art)
    cls[hm < 0] = 0
    ox, oy = (x0 // BLK) * BLK, (y0 // BLK) * BLK
    nx, ny = -(-(x0 + Ww - ox) // BLK) * 2 + 2, -(-(y0 + Hh - oy) // BLK) * 2 + 2
    hc = np.full((ny, nx), -1, np.int32)
    cc = np.zeros((ny, nx), np.int8)
    for j in range(ny):
        for i in range(nx):
            cx, cy = ox + G * i + G // 2 - x0, oy + G * j + G // 2 - y0
            if 0 <= cy < Hh and 0 <= cx < Ww and hm[cy, cx] >= 0:
                hc[j, i] = int(hm[cy, cx])
                block = cls[max(0, cy - 4): cy + 4, max(0, cx - 4): cx + 4].ravel()
                block = block[block > 0]
                cc[j, i] = np.bincount(block, minlength=8).argmax() if len(block) else 1
    return dict(hm=hm, x0=x0, y0=y0, art=art, cls=cls, ox=ox, oy=oy, hc=hc, cc=cc)


def top_class(c: int) -> str:
    return {1: "plaza", 2: "roof", 3: "lip", 4: "hatch", 5: "bridge", 6: "plaza", 7: "plaza"}.get(int(c), "plaza")


def separate_hatches(surf, hc, valid):
    """SoI draws its wooden hatches as separate small panels. Each hatch blob in the art becomes one
    or more 16 x 16 framed panels on aligned, level blocks, a block apart; the rest of the blob
    takes its surroundings' surface (roof or paving)."""
    from scipy import ndimage
    out = surf.copy()
    hat = surf == "hatch"
    lab, n = ndimage.label(hat)
    ny, nx = surf.shape
    for k in range(1, n + 1):
        cells = lab == k
        ring = ndimage.binary_dilation(cells, iterations=2) & ~cells & valid
        around = surf[ring]
        roofs = np.isin(around, ["roof", "lip"]).sum()
        out[cells] = "roof" if roofs > (around == "plaza").sum() else "plaza"
        js, is_ = np.nonzero(cells)
        cy, cx = js.mean(), is_.mean()
        cands = []
        for j in range(max(0, js.min() - 1) // 2 * 2, min(ny - 1, js.max() + 1), 2):
            for i in range(max(0, is_.min() - 1) // 2 * 2, min(nx - 1, is_.max() + 1), 2):
                blk = (slice(j, j + 2), slice(i, i + 2))
                if valid[blk].all() and (hc[blk] == hc[j, i]).all() and cells[blk].sum() >= 3:
                    cands.append(((j + 0.5 - cy) ** 2 + (i + 0.5 - cx) ** 2, j, i))
        picked = []
        for _, j, i in sorted(cands):
            if all(abs(j - pj) > 2 or abs(i - pi) > 2 for pj, pi in picked):
                picked.append((j, i))
                out[j:j + 2, i:i + 2] = "hatch"
    return out


def build_mesh(L: dict) -> Mesh:
    hc, cc, ox, oy = L["hc"], L["cc"], L["ox"], L["oy"]
    ny, nx = hc.shape
    M = Mesh()
    X = lambda i: ox + G * i
    Y = lambda j: oy + G * j
    valid = hc >= 0

    # roofs: plaza-coloured or hatch cells whose same-height neighbourhood is mostly roof are
    # roof (SoI's art shows wall tops and shadows at roof edges)
    surf = np.vectorize(top_class)(cc)
    isroof = np.isin(surf, ["roof", "lip"])
    fixed = surf.copy()
    for j in range(ny):
        for i in range(nx):
            if valid[j, i] and surf[j, i] in ("plaza", "hatch"):
                sl = (slice(max(0, j - 2), j + 3), slice(max(0, i - 2), i + 3))
                same = hc[sl] == hc[j, i]
                r = (isroof[sl] & same).sum()
                pl = ((surf[sl] == "plaza") & same).sum()
                if r > pl:
                    fixed[j, i] = "roof" if surf[j, i] == "plaza" else "hatch"
    surf = fixed
    surf = separate_hatches(surf, hc, valid)

    # ---- tops: 16 x 16 blocks when level (material by majority), else 8 x 8 cells
    used = np.zeros_like(valid)
    for j in range(0, ny - 1, 2):
        for i in range(0, nx - 1, 2):
            b = hc[j:j + 2, i:i + 2]
            s = surf[j:j + 2, i:i + 2]
            if (b >= 0).all() and (b == b[0, 0]).all():
                z = int(b[0, 0])
                vals, counts = np.unique(s, return_counts=True)
                m = vals[counts.argmax()]
                if m == "lip":
                    m = "roof"
                keys = [(X(i), Y(j), z), (X(i), Y(j + 2), z), (X(i + 2), Y(j + 2), z), (X(i + 2), Y(j), z)]
                M.face(keys, (0, 0, 1), "top", m, ((X(i), Y(j), z), (1, 0, 0), (0, 1, 0), BLK, BLK, 0, 0))
                used[j:j + 2, i:i + 2] = True
    for j in range(ny):
        for i in range(nx):
            if valid[j, i] and not used[j, i]:
                z = int(hc[j, i])
                keys = [(X(i), Y(j), z), (X(i), Y(j + 1), z), (X(i + 1), Y(j + 1), z), (X(i + 1), Y(j), z)]
                u0, v0 = ((X(i) - ox) % BLK) / BLK, ((Y(j) - oy) % BLK) / BLK
                M.face(keys, (0, 0, 1), "top", surf[j, i], ((X(i), Y(j), z), (1, 0, 0), (0, 1, 0), BLK, BLK, u0, v0))

    # ---- walls: each cell is a solid interval [bot, top] (islands from the cloud line, bridge
    # slabs BRIDGE_T thick); between neighbours, each side shows its interval minus the other's.
    bridge = surf == "bridge"
    L["bridge"] = bridge
    bot = np.where(bridge, hc - BRIDGE_T, CLOUD_Z)
    roofish = np.isin(surf, ["roof", "lip"])
    from scipy.ndimage import binary_dilation
    building = binary_dilation(roofish, iterations=2) & valid

    # ground level under a building: the lowest open (non-building) top nearby; its walls are
    # plaster down to there and island cliff below
    from scipy.ndimage import minimum_filter
    openz = np.where(valid & ~bridge & ~building, hc, 10 ** 6)
    ground_lvl = minimum_filter(openz, size=13, mode="constant", cval=10 ** 6)

    def cell(j, i):
        if 0 <= j < ny and 0 <= i < nx and valid[j, i]:
            top = int(hc[j, i])
            # no open ground near: plaster for the top 120 z at most, sandstone cliff below
            g = int(ground_lvl[j, i]) if ground_lvl[j, i] < 10 ** 6 else max(CLOUD_Z + 48, top - 120)
            return int(bot[j, i]), top, bool(bridge[j, i]), bool(building[j, i]), g
        return None

    def minus(a, b):
        lo, hi = a
        if b is None or b[1] <= lo or b[0] >= hi:
            return [(lo, hi)]
        out = []
        if b[1] < hi:
            out.append((b[1], hi))
        if b[0] > lo:
            out.append((lo, b[0]))
        return out

    def kinds(A, B):
        """Visible parts of A's side facing B: [(lo, hi, kind)]."""
        res = []
        for lo, hi in minus((A[0], A[1]), None if B is None else (B[0], B[1])):
            if A[2]:
                kind = "bridge_side"
            elif hi == A[1] and B is not None and lo >= B[1] - 0 and not B[2]:
                kind = "wall" if A[3] else "riser"
            elif hi == A[1] and B is not None and B[2] and lo >= B[1]:
                kind = "wall" if A[3] else "riser"
            elif A[3] and hi == A[1] and A[4] < hi:
                # building over a drop: plaster down to its ground level, cliff below
                if lo < A[4]:
                    res.append((lo, A[4], "cliff", False))
                res.append((max(lo, A[4]), hi, "wall", False))
                continue
            else:
                kind = "cliff"
            res.append((lo, hi, kind, kind == "cliff" and hi == A[1]))
        return tuple(res)

    def rows(lo, hi, step=ROW, base=1):
        zs = [lo] + [z for z in range(((lo - base) // step + 1) * step + base, hi, step) if lo < z < hi] + [hi]
        return sorted(set(zs))

    # roof cells (and their wall-top border cells at the same height) carry a coping
    def roof_near(j, i):
        sl = (slice(max(0, j - 2), j + 3), slice(max(0, i - 2), i + 3))
        return bool((roofish[sl] & (hc[sl] == hc[j, i])).any())

    for axis in ("x", "y"):
        lines = range(nx + 1) if axis == "x" else range(ny + 1)
        along = ny if axis == "x" else nx
        for l in lines:
            segs = []
            for k in range(along):
                A = cell(k, l - 1) if axis == "x" else cell(l - 1, k)
                B = cell(k, l) if axis == "x" else cell(l, k)
                fa = kinds(A, B) if A else ()
                fb = kinds(B, A) if B else ()
                segs.append((fa, fb))
            k = 0
            while k < along:
                s = segs[k]
                if not s[0] and not s[1]:
                    k += 1
                    continue
                k1 = k + 1
                if k % 2 == 0 and k1 < along and segs[k1] == s:
                    k1 += 1
                c = (X(l) if axis == "x" else Y(l))
                a0, a1 = ((Y(k), Y(k1)) if axis == "x" else (X(k), X(k1)))
                u0 = ((a0 - (oy if axis == "x" else ox)) % BLK) / BLK
                for d, parts in ((+1, s[0]), (-1, s[1])):
                    owner = ((k, l - 1) if axis == "x" else (l - 1, k)) if d > 0 else ((k, l) if axis == "x" else (l, k))
                    other = ((k, l) if axis == "x" else (l, k)) if d > 0 else ((k, l - 1) if axis == "x" else (l - 1, k))
                    for lo, hi, kind, edge in parts:
                        step, base = (CROW, CBASE) if kind == "cliff" else (CROW, CLOUD_Z) if kind == "riser" else (ROW, 1)
                        zs = rows(lo, hi, step, base) if kind != "bridge_side" else [lo, hi]
                        if kind == "wall" and hi == hc[owner] and roof_near(*owner):
                            M.copings.append((axis, c, a0, a1, hi, d))
                        oj0, oi0 = other
                        void = not (0 <= oj0 < ny and 0 <= oi0 < nx and valid[oj0, oi0])
                        if kind == "bridge_side" and hi == hc[owner] and void:
                            M.rails.append((axis, c, a0, a1, hi, d))
                        # the floor in front of this wall part (doors stand on it)
                        oj, oi = other
                        front = int(hc[oj, oi]) if 0 <= oj < ny and 0 <= oi < nx and valid[oj, oi] else -1
                        for za, zb in zip(zs[:-1], zs[1:]):
                            if axis == "x":
                                pts = [(c, a0, za), (c, a1, za), (c, a1, zb), (c, a0, zb)]
                                n, org, uax = (d, 0, 0), (c, a0, za), (0, 1, 0)
                            else:
                                pts = [(a0, c, za), (a1, c, za), (a1, c, zb), (a0, c, zb)]
                                n, org, uax = (0, d, 0), (a0, c, za), (1, 0, 0)
                            zb0 = (za - base) // step * step + base
                            top_row = zb == hi
                            vl, v0 = (step, (za - zb0) / step) if kind != "bridge_side" else (zb - za, 0.0)
                            if kind == "cliff" and za == CLOUD_Z:
                                vl, v0 = zb - za, 0.0       # the snow row maps its whole tile
                            M.face(pts, n, kind, kind + ("_top" if top_row else ""),
                                   (org, uax, (0, 0, 1), BLK, vl, u0, v0))
                            M.faces[-1]["front"] = front if za == lo else -1
                k = k1

    # ---- undersides: bridge slabs flat here; islands get one cap each (island_caps, after the
    # T-junction pass) on the cliffs' own bottom vertices
    big = valid & ~bridge
    for j in range(0, ny - 1, 2):
        for i in range(0, nx - 1, 2):
            blk = (slice(j, j + 2), slice(i, i + 2))
            if big[blk].all():
                continue
            zb = bot[blk]
            if valid[blk].all() and not big[blk].any() and (zb == zb[0, 0]).all() and (bridge[blk] == bridge[j, i]).all():
                z = int(zb[0, 0])
                keys = [(X(i), Y(j), z), (X(i + 2), Y(j), z), (X(i + 2), Y(j + 2), z), (X(i), Y(j + 2), z)]
                M.face(keys, (0, 0, -1), "under", "bridge_under" if bridge[j, i] else "under",
                       ((X(i), Y(j), z), (1, 0, 0), (0, 1, 0), BLK, BLK, 0, 0))
                continue
            for jj in (j, j + 1):
                for ii in (i, i + 1):
                    if valid[jj, ii] and not big[jj, ii]:
                        z = int(bot[jj, ii])
                        keys = [(X(ii), Y(jj), z), (X(ii + 1), Y(jj), z), (X(ii + 1), Y(jj + 1), z), (X(ii), Y(jj + 1), z)]
                        u0, v0 = ((X(ii) - ox) % BLK) / BLK, ((Y(jj) - oy) % BLK) / BLK
                        M.face(keys, (0, 0, -1), "under", "bridge_under" if bridge[jj, ii] else "under",
                               ((X(ii), Y(jj), z), (1, 0, 0), (0, 1, 0), BLK, BLK, u0, v0))
    return M


def island_caps(M: Mesh, L: dict, step: float = 40.0) -> dict:
    """One underside per island: a Delaunay triangulation of the cliffs' bottom vertices (shared,
    so the island stays closed) and a coarse interior grid; sculpt() hangs the interior down.
    Returns the closure check: every outline edge used once, every other edge twice."""
    from scipy.ndimage import distance_transform_edt
    from scipy.spatial import Delaunay
    hc, ox, oy = L["hc"], L["ox"], L["oy"]
    island = (hc >= 0) & ~L["bridge"]
    outline = defaultdict(int)
    for f in M.faces:
        if f["kind"] not in ("cliff", "wall"):
            continue
        vs = f["vs"]
        for a, b in zip(vs, vs[1:] + vs[:1]):
            if M.pos[a][2] == CLOUD_Z and M.pos[b][2] == CLOUD_Z:
                outline[frozenset((a, b))] += 1
    ring = sorted({v for e in outline for v in e})
    dist = distance_transform_edt(np.pad(island, 1))[1:-1, 1:-1] * G
    rng = np.random.default_rng(3)
    inner = []
    ny, nx = hc.shape
    for y in np.arange(oy + step / 2, oy + ny * G, step):
        for x in np.arange(ox + step / 2, ox + nx * G, step):
            px, py = x + rng.uniform(-6, 6), y + rng.uniform(-6, 6)
            j, i = int((py - oy) // G), int((px - ox) // G)
            if 0 <= j < ny and 0 <= i < nx and island[j, i] and dist[j, i] >= 26:
                inner.append((round(px), round(py)))
    idx = ring + [M.v((px, py, CLOUD_Z)) for px, py in inner]
    xy = np.array([M.pos[v][:2] for v in idx], float)
    tri = Delaunay(xy)

    # inside / outside by parity: flood from the hull, toggling across every outline edge
    from collections import deque
    loc = {v: k for k, v in enumerate(idx)}
    cons = {frozenset((loc[a], loc[b])) for a, b in (tuple(e) for e in outline)}
    depth = np.full(len(tri.simplices), -1)
    dq = deque()
    for t_, nb in enumerate(tri.neighbors):
        if (nb == -1).any():
            k = int(np.nonzero(nb == -1)[0][0])
            e = frozenset(int(v) for v in np.delete(tri.simplices[t_], k))
            depth[t_] = 1 if e in cons else 0
            dq.append(t_)
    while dq:
        t_ = dq.popleft()
        for k, nb in enumerate(tri.neighbors[t_]):
            if nb < 0:
                continue
            e = frozenset(int(v) for v in np.delete(tri.simplices[t_], k))
            dd = depth[t_] + (1 if e in cons else 0)
            if depth[nb] < 0 or dd < depth[nb]:
                depth[nb] = dd
                (dq.appendleft if dd == depth[t_] else dq.append)(nb)
    used = defaultdict(int)
    kept = 0
    for t_, s in enumerate(tri.simplices):
        if depth[t_] % 2 == 0:
            continue
        P = xy[s]
        vs = [idx[k] for k in s]
        org = (float(P[:, 0].min()), float(P[:, 1].min()), CLOUD_Z)
        M.face([M.pos[v] for v in vs], (0, 0, -1), "under", "under", (org, (1, 0, 0), (0, 1, 0), 4 * BLK, 4 * BLK, 0, 0))
        kept += 1
        for a, b in ((0, 1), (1, 2), (2, 0)):
            used[frozenset((vs[a], vs[b]))] += 1
    open_outline = sum(1 for e in outline if used.get(e, 0) != 1)
    open_inner = sum(1 for e, n in used.items() if e not in outline and n != 2)
    return dict(cap_triangles=kept, outline_edges=len(outline), interior_points=len(inner),
                open_outline_edges=open_outline, open_inner_edges=open_inner)


def merge_runs(segs):
    """(axis, c, a0, a1, z, d) segments -> straight runs, with each end's corner type: +1 convex
    (the perpendicular run there faces along this run), -1 concave, None free."""
    groups = defaultdict(list)
    for axis, c, a0, a1, z, d in segs:
        groups[(axis, c, z, d)].append((a0, a1))
    runs = []
    for (axis, c, z, d), ab in groups.items():
        ab.sort()
        cur = list(ab[0])
        for a0, a1 in ab[1:]:
            if a0 <= cur[1]:
                cur[1] = max(cur[1], a1)
            else:
                runs.append((axis, c, z, d, cur[0], cur[1]))
                cur = [a0, a1]
        runs.append((axis, c, z, d, cur[0], cur[1]))

    def pt(axis, c, a):
        return (c, a) if axis == "x" else (a, c)

    ends = defaultdict(list)
    for r in runs:
        axis, c, z, d, a0, a1 = r
        ends[(pt(axis, c, a0), z)].append(r)
        ends[(pt(axis, c, a1), z)].append(r)
    out = []
    for r in runs:
        axis, c, z, d, a0, a1 = r
        t = np.array((0.0, 1.0)) if axis == "x" else np.array((1.0, 0.0))
        kinds = []
        for a, sgn in ((a0, -1), (a1, 1)):
            q = [o for o in ends[(pt(axis, c, a), z)] if o[0] != axis]
            if not q:
                kinds.append(None)
                continue
            nq = np.array((q[0][3], 0.0)) if q[0][0] == "x" else np.array((0.0, q[0][3]))
            kinds.append(1 if float(nq @ (t * sgn)) > 0 else -1)
        out.append((r, kinds))
    return out


def profile_strips(runs, profile, mats, flags=1, piece=32.0, caps=True):
    """Extrude a clockwise cross-section [(outward offset, dz)] along each run (mitred at corners,
    capped at free ends) in pieces of <= `piece` units, one tile each; strip k = profile[k] ->
    profile[k + 1] with mats[k]. Normals in SoI space: (-dz * n_xy, d_offset)."""
    parts = []
    for (axis, c, z, d, a0, a1), (k0, k1) in runs:
        nrm = np.array((d, 0.0)) if axis == "x" else np.array((0.0, d))
        t = np.array((0.0, 1.0)) if axis == "x" else np.array((1.0, 0.0))
        cuts = [float(a) for a in np.arange(a0, a1, piece)] + [float(a1)]
        if len(cuts) > 2 and cuts[-1] - cuts[-2] < piece / 2:
            cuts.pop(-2)        # no sliver at the end

        def P(a, o, dz, end):
            ext = 0.0
            if end == 0 and k0 is not None:
                ext = -k0 * o           # convex: the line runs on by its offset; concave: back
            if end == 1 and k1 is not None:
                ext = k1 * o
            xy = (np.array((c, a)) if axis == "x" else np.array((a, c))) + nrm * o + t * ext
            return (float(xy[0]), float(xy[1]), float(z + dz))

        for s, e in zip(cuts[:-1], cuts[1:]):
            es, ee = (0 if s == a0 else None), (1 if e == a1 else None)
            for k in range(len(profile) - 1):
                (oa, za), (ob, zb) = profile[k], profile[k + 1]
                q = [P(s, oa, za, es), P(e, oa, za, ee), P(e, ob, zb, ee), P(s, ob, zb, es)]
                n = np.array((-(zb - za) * nrm[0], -(zb - za) * nrm[1], ob - oa), float)
                uv = [(0, 0), (1, 0), (1, 1), (0, 1)] if zb >= za else [(0, 1), (1, 1), (1, 0), (0, 0)]
                parts.append(dict(pts=q, n=tuple(n / np.linalg.norm(n)), mat=mats[k], flags=flags, uv=uv))
        if caps:
            for end, a, kk in ((0, a0, k0), (1, a1, k1)):
                if kk is None:
                    poly = [P(a, o, dz, end) for o, dz in profile]
                    sgn = -1 if end == 0 else 1
                    uv = [(0, 0), (1, 0), (0.5, 1)] if len(poly) == 3 else [(0, 0), (0, 1), (1, 1), (1, 0)]
                    parts.append(dict(pts=poly, n=(t[0] * sgn, t[1] * sgn, 0.0), mat=mats[-1], flags=flags, uv=uv))
    return parts


def bridge_arches(L: dict, R=26.0, D=30.0, segs=3):
    """Where a bridge meets an island, a quarter arch springs from the cliff under the slab:
    two spandrels in the bridge's side planes and the curved soffit between them (visual)."""
    hc, ox, oy, bridge = L["hc"], L["ox"], L["oy"], L["bridge"]
    valid = hc >= 0
    island = valid & ~bridge
    ny, nx = hc.shape
    junction = []
    for j in range(ny):
        for i in range(nx):
            for dj, di, axis in ((0, 1, "x"), (1, 0, "y")):
                jj, ii = j + dj, i + di
                if jj >= ny or ii >= nx:
                    continue
                a, b = (j, i), (jj, ii)
                for br, isl, d in ((b, a, 1), (a, b, -1)):
                    if bridge[br] and island[isl]:
                        z = int(hc[br]) - BRIDGE_T
                        if axis == "x":
                            junction.append(("x", ox + G * ii, oy + G * j, oy + G * (j + 1), z, d))
                        else:
                            junction.append(("y", oy + G * jj, ox + G * i, ox + G * (i + 1), z, d))
    parts = []
    curve = [(R * (1 - math.cos(math.pi / 2 * k / segs)), D * (1 - math.sin(math.pi / 2 * k / segs))) for k in range(segs + 1)]
    def is_bridge(x, y):
        j, i = int((y - oy) // G), int((x - ox) // G)
        return 0 <= j < ny and 0 <= i < nx and bool(bridge[j, i])

    for (axis, c, z, d, a0, a1), _ in merge_runs(junction):
        m = (a0 + a1) / 2
        out = [(c + d * (4 + G * k), m) if axis == "x" else (m, c + d * (4 + G * k)) for k in range(4)]
        if a1 - a0 > 32 or not all(is_bridge(*q) for q in out):
            continue                    # a bridge's side along an island, not its end
        def W(s, a, dz):
            return (c + d * s, a, z - dz) if axis == "x" else (a, c + d * s, z - dz)
        nb = (0, 1, 0) if axis == "x" else (1, 0, 0)
        for a, sgn in ((a0, -1), (a1, 1)):
            for k in range(segs):
                (s0, h0), (s1, h1) = curve[k], curve[k + 1]
                tri = [W(0, a, 0), W(s0, a, h0), W(s1, a, h1)]
                parts.append(dict(pts=tri, n=tuple(sgn * v for v in nb), mat="bridge_side", flags=1,
                                  uv=[(0, 1), (s0 / R, 1 - h0 / D), (s1 / R, 1 - h1 / D)]))
        for k in range(segs):
            (s0, h0), (s1, h1) = curve[k], curve[k + 1]
            q = [W(s0, a0, h0), W(s1, a0, h1), W(s1, a1, h1), W(s0, a1, h0)]
            ds, dh = s1 - s0, h1 - h0
            # the soffit faces into the arch opening: along the bridge and down
            n = np.array((-d * dh, 0, -ds)) if axis == "x" else np.array((0, -d * dh, -ds))
            parts.append(dict(pts=q, n=tuple(n / np.linalg.norm(n)), mat="bridge_side", flags=1,
                              uv=[(0, 0), (1, 0), (1, 1), (0, 1)]))
    return parts


def wall_banners(M: Mesh, banners, size=(10.0, 28.0)):
    """SoI's orange banners hang under the coping, just in front of the wall."""
    parts = []
    lines = [(axis, c, z, d, a0, a1) for (axis, c, z, d, a0, a1), _ in merge_runs(M.copings)]
    w, hgt = size
    for bx, by, bz in banners:
        best = None
        for axis, c, z, d, a0, a1 in lines:
            if abs(z - bz) > 3 or a1 - a0 < w + 2:
                continue
            along, across = (by, bx) if axis == "x" else (bx, by)
            if a0 - 4 <= along <= a1 + 4:
                dd = abs(across - c)
                if dd < 12 and (best is None or dd < best[0]):
                    best = (dd, axis, c, z, d, min(max(along, a0 + w / 2 + 1), a1 - w / 2 - 1))
        if best is None:
            continue
        _, axis, c, z, d, m = best
        o = c + d * 0.9
        za, zb = z - 5 - hgt, z - 5
        if axis == "x":
            q = [(o, m - w / 2, za), (o, m + w / 2, za), (o, m + w / 2, zb), (o, m - w / 2, zb)]
            n = (d, 0, 0)
        else:
            q = [(m - w / 2, o, za), (m + w / 2, o, za), (m + w / 2, o, zb), (m - w / 2, o, zb)]
            n = (0, d, 0)
        parts.append(dict(pts=q, n=n, mat="banner", flags=1, uv=[(0, 0), (1, 0), (1, 1), (0, 1)]))
    return parts


def fix_tjunctions(M: Mesh):
    """Insert every vertex that lies on a face edge into that face (axis-aligned edges)."""
    lines = defaultdict(list)
    for idx, (x, y, z) in enumerate(M.pos):
        lines[("x", y, z)].append((x, idx))
        lines[("y", x, z)].append((y, idx))
        lines[("z", x, y)].append((z, idx))
    for k in lines:
        lines[k].sort()
    inserted = 0
    for f in M.faces:
        out = []
        vs = f["vs"]
        for a, b in zip(vs, vs[1:] + vs[:1]):
            out.append(a)
            pa, pb = M.pos[a], M.pos[b]
            diff = [c for c in range(3) if pa[c] != pb[c]]
            if len(diff) != 1:
                continue
            c = diff[0]
            key = ("xyz"[c],) + tuple(pa[k] for k in range(3) if k != c)
            lo, hi = sorted((pa[c], pb[c]))
            mids = [(t, i) for t, i in lines[key] if lo < t < hi]
            if pa[c] > pb[c]:
                mids.reverse()
            out += [i for _, i in mids]
            inserted += len(mids)
        f["vs"] = out
    return inserted


def split_polygons(M: Mesh):
    """Faces with more than 4 vertices -> quads / triangles. Rectangle sides: zipper between
    two opposite point runs, else a fan from an added centre vertex."""
    new = []
    for f in M.faces:
        vs = f["vs"]
        if len(vs) <= 4:
            new.append(f)
            continue
        P = [M.pos[i] for i in vs]
        corners = [k for k in range(len(vs)) if not collinear(P[k - 1], P[k], P[(k + 1) % len(vs)])]
        assert len(corners) == 4, (P, corners)
        sides = []
        for s in range(4):
            a, b = corners[s], corners[(s + 1) % 4]
            run = [vs[(a + t) % len(vs)] for t in range(((b - a) % len(vs)) + 1)]
            sides.append(run)
        busy = [len(s) > 2 for s in sides]
        pairs = [(0, 2), (1, 3)]
        done = False
        for s0, s1 in pairs:
            if not any(busy[k] for k in range(4) if k not in (s0, s1)):
                A, B = sides[s0], list(reversed(sides[s1]))
                for poly in zipper(M, A, B):
                    new.append(dict(f, vs=poly))
                done = True
                break
        if not done:
            c = tuple(sum(M.pos[i][k] for i in [vs[q] for q in corners]) / 4 for k in range(3))
            ci = M.v(c)
            for a, b in zip(vs, vs[1:] + vs[:1]):
                new.append(dict(f, vs=[a, b, ci]))
    M.faces = new


def collinear(a, b, c):
    ab = np.subtract(b, a)
    bc = np.subtract(c, b)
    return np.linalg.norm(np.cross(ab, bc)) < 1e-9


def zipper(M, A, B):
    """A and B run in the same direction along opposite sides; emit quads where they align."""
    def t(run, k):
        p0, p1 = np.array(M.pos[run[0]], float), np.array(M.pos[run[-1]], float)
        return float(np.dot(np.array(M.pos[run[k]], float) - p0, p1 - p0) / max(np.dot(p1 - p0, p1 - p0), 1e-9))
    i = j = 0
    out = []
    while i < len(A) - 1 or j < len(B) - 1:
        ta = t(A, i + 1) if i < len(A) - 1 else 2
        tb = t(B, j + 1) if j < len(B) - 1 else 2
        if abs(ta - tb) < 1e-6:
            out.append([A[i], A[i + 1], B[j + 1], B[j]])
            i += 1
            j += 1
        elif ta < tb:
            out.append([A[i], A[i + 1], B[j]])
            i += 1
        else:
            out.append([A[i], B[j + 1], B[j]])
            j += 1
    return out


# ---------------------------------------------------------------- sculpt, texture, light, export
def noise3(p: np.ndarray, seed: int, scale: float) -> np.ndarray:
    """Smooth deterministic noise in about [-1, 1] (sum of oriented sines)."""
    rng = np.random.default_rng(seed)
    out = np.zeros(len(p))
    amp, tot = 1.0, 0.0
    for octave in range(3):
        for _ in range(3):
            d = rng.normal(size=3)
            d /= np.linalg.norm(d)
            out += amp * np.sin(p @ d * (2 * math.pi / scale) * (2 ** octave) + rng.uniform(0, 2 * math.pi))
            tot += amp
        amp *= 0.5
    return out / tot * 1.6


def sculpt(M: Mesh, L: dict) -> np.ndarray:
    """Final positions (SoI units) after displacement; topology positions stay in M.pos."""
    from scipy.ndimage import distance_transform_edt
    P = np.array(M.pos, float)
    n = len(P)
    kinds = [set() for _ in range(n)]
    normals = np.zeros((n, 3))
    for f in M.faces:
        for i in f["vs"]:
            kinds[i].add(f["kind"])
            if f["kind"] in ("cliff", "wall", "riser"):
                normals[i] += f["n"]
    out = P.copy()
    hc, ox, oy = L["hc"], L["ox"], L["oy"]
    valid = hc >= 0
    # distance (SoI units) from each lattice vertex to the footprint outline
    vin = np.zeros((hc.shape[0] + 1, hc.shape[1] + 1), bool)
    vin[1:-1, 1:-1] = valid[:-1, :-1] & valid[1:, :-1] & valid[:-1, 1:] & valid[1:, 1:]
    dist = distance_transform_edt(vin) * G
    nz = noise3(P, 7, 60.0)
    nr = noise3(P, 11, 34.0)
    for i in range(n):
        x, y, z = P[i]
        k = kinds[i]
        if "decor" in k:
            continue
        if "under" in k and z == CLOUD_Z and not (k & {"cliff", "bridge_side"}):
            jj, ii = int(round((y - oy) / G)), int(round((x - ox) / G))
            d = dist[jj, ii] if 0 <= jj < dist.shape[0] and 0 <= ii < dist.shape[1] else 0
            out[i, 2] = z - min(150.0, 9.0 * d ** 0.62) * (1 + 0.18 * nz[i])
            continue
        hn = normals[i].copy()
        hn[2] = 0
        ln = np.linalg.norm(hn)
        if ln < 1e-6:
            continue
        hn /= ln
        if k <= {"cliff", "under"} and not (k == {"under"}):
            if z > CLOUD_Z:
                out[i, :2] += hn[:2] * 4.0 * nr[i]          # rock relief
            else:
                out[i, :2] += hn[:2] * (14.0 + 3.0 * nr[i])  # snow drift at the cloud line
        if "wall" in k and not k & {"cliff", "riser"}:
            # convex building corners (the cell beyond the corner is lower): bevel them in
            hcv = L["hc"]
            jj, ii = int((y + hn[1] * 4 - L["oy"]) // G), int((x + hn[0] * 4 - L["ox"]) // G)
            beyond = hcv[jj, ii] if 0 <= jj < hcv.shape[0] and 0 <= ii < hcv.shape[1] else -1
            if abs(hn[0]) > 0.5 and abs(hn[1]) > 0.5 and beyond < z - 1:
                # v2g: no bevel. Pulling the corner in made the fanned wall triangles beside it tilt
                # into overhangs that stopped a jump rising along the wall (agent 3, the 409 step).
                out[i, :2] -= hn[:2] * 0.0
        if "top" in k and k & {"cliff", "riser"} and "wall" not in k:   # wall tops carry a coping
            # rounded top edge: pull out a little and drop (walk height kept inside the edge)
            out[i, 2] -= 4.0 if "cliff" in k else 2.0
            out[i, :2] += hn[:2] * (1.2 if "cliff" in k else 0.6)
    return out


def face_material(f: dict, M: Mesh, banners=()) -> str:
    kind, mat = f["kind"], f["mat"]
    x, y, z = M.pos[f["vs"][0]]
    h = int(x * 7 + y * 13 + z * 3) // 8 % 8
    if kind == "decor":
        return mat
    if kind == "top":
        return {"plaza": "plaza", "roof": "roof", "lip": "lip", "hatch": "hatch", "bridge": "bridge"}.get(mat, "plaza")
    if kind == "wall":
        P = [M.pos[v] for v in f["vs"]]
        z0, z1 = min(p[2] for p in P), max(p[2] for p in P)
        if f.get("front", -1) == z0 and z1 - z0 >= 24 and h in (3, 6):
            return "door"               # on the floor in front of it
        if mat.endswith("_top") and z1 - z0 < 30:
            return "wall"               # under the coping
        return "window" if h in (1, 5) and z1 - z0 >= 24 else "wall"
    if kind == "cliff":
        if min(M.pos[v][2] for v in f["vs"]) <= CLOUD_Z:
            return "cliff_foot"         # SoI: snow only at the very base
        return "cliff2" if h in (2, 5) else "cliff"
    if kind == "riser":
        return "riser"
    if kind == "bridge_side":
        return kind
    if mat == "bridge_under":
        return "bridge_side"
    return "under"                      # the cliff foot carries the snow; caps are rock


RIM: set = set()   # vertices on an island's outline (shared with cliff faces): snow ring


TINT = {
    "plaza": (1.36, 0.97, 0.98), "plaza_var": (1.36, 0.97, 0.98), "plaza_edge": (1.36, 1.0, 1.02),
    "bridge": (0.95, 1.0, 1.12), "bridge_edge": (0.95, 1.0, 1.12), "roof": (1.5, 1.6, 1.85), "lip": (1.3, 1.45, 1.6),
    "wall": (1.3, 1.27, 1.38), "window": (1.25, 1.22, 1.32), "door": (1.15, 1.05, 1.0), "cornice": (1.35, 1.4, 1.5),
    "riser": (1.35, 1.12, 1.25), "riser_top": (1.15, 1.18, 1.3), "kerb": (1.15, 1.18, 1.3), "snow": (1.15, 1.2, 1.35),
    "hatch": (1.25, 1.18, 1.05), "crate": (1.45, 1.3, 1.1), "cliff": (1.08, 0.98, 0.92), "cliff2": (1.08, 0.98, 0.92),
    "under": (0.9, 0.86, 0.95), "flag": (1.45, 1.05, 1.3), "far_rock": (0.95, 0.8, 1.0), "far_top": (1.1, 1.1, 1.25),
}
# authored tiles are painted at their final colour: near-neutral tints
TINT.update({
    "wall": (0.98, 0.96, 1.06), "window": (0.98, 0.96, 1.06), "door": (0.98, 0.96, 1.06), "cornice": (0.98, 0.96, 1.06),
    "banner": (1.0, 1.0, 1.0), "hatch": (0.9, 0.88, 0.86), "roof": (0.86, 0.88, 0.94), "lip": (0.86, 0.88, 0.94),
    "cliff": (1.43, 1.21, 1.14), "cliff2": (1.43, 1.21, 1.14), "cliff_foot": (1.43, 1.21, 1.14), "riser": (1.43, 1.21, 1.14),
    "riser_top": (1.43, 1.21, 1.14), "kerb": (1.43, 1.21, 1.14), "snow": (1.0, 1.0, 1.0), "under": (1.0, 0.95, 1.0),
    "coping_top": (1.0, 1.0, 1.0), "coping_side": (1.3, 1.3, 1.3), "foliage": (1.05, 1.1, 1.05), "bark": (1.0, 1.0, 1.0),
    "cloth": (1.0, 1.0, 1.0), "flag": (1.0, 1.0, 1.0), "awning": (1.0, 1.0, 1.0), "post": (1.0, 1.0, 1.0),
    "pole": (1.0, 1.0, 1.0), "far_rock": (1.05, 1.05, 1.05), "far_top": (1.0, 1.0, 1.0),
})
CLOTH = {"flag", "cloth", "banner", "awning"}    # thin cloth: lit the same from both sides


def bake_light(M: Mesh, P: np.ndarray, L: dict) -> list:
    """Per face-vertex colour (0..2, 1 = neutral PS1 128): sun with heightfield shadows, sky
    ambient by normal, heightfield ambient occlusion; undersides dark."""
    hm, x0, y0 = L["hm"], L["x0"], L["y0"]
    H_, W_ = hm.shape
    hmf = np.where(hm >= 0, hm, -999.0)
    sun = np.array([-0.5, 0.42, 0.76])
    warm, sky = np.array([1.06, 1.0, 0.88]), np.array([0.92, 0.96, 1.08])
    sun /= np.linalg.norm(sun)

    def ground(x, y):
        xi, yi = int(x - x0), int(y - y0)
        return hmf[yi, xi] if 0 <= yi < H_ and 0 <= xi < W_ else -999.0

    def shadow(p):
        for step in range(1, 160):
            q = p + sun * (3.0 * step)
            if q[2] > 600:
                return 1.0
            if ground(q[0], q[1]) > q[2] + 1:
                return 0.0
        return 1.0

    dirs = [(math.cos(a), math.sin(a)) for a in np.linspace(0, 2 * math.pi, 8, endpoint=False)]

    def occlusion(p, n):
        base = p + np.array(n) * 2.0
        occ = 0.0
        for dx, dy in dirs:
            best = 0.0
            for r in (6, 14, 28):
                h = ground(base[0] + dx * r, base[1] + dy * r)
                if h > base[2]:
                    best = max(best, min(1.0, (h - base[2]) / (r * 1.2)))
            occ += best
        return occ / len(dirs)

    cache = {}
    cols = []
    for f in M.faces:
        n = np.array(f["n"], float)
        if f["matname"] in CLOTH and np.dot(n, sun) < 0:
            n = n - 2 * np.dot(n, sun) * sun          # mirror into the sun: both faces lit alike
        tint = np.array(TINT.get(f["matname"], (1, 1, 1)))
        fc = []
        for vi in f["vs"]:
            key = (vi, tuple(f["n"]), f["matname"] == "cliff_foot")
            if key not in cache:
                p = P[vi]
                if f["kind"] == "under":
                    c = 0.38 + 0.12 * max(0.0, min(1.0, (p[2] + 100) / 125))
                else:
                    lit = shadow(p + n * 1.5) if np.dot(n, sun) > 0 else 0.0
                    diff = max(0.0, float(np.dot(n, sun))) * lit
                    ao = occlusion(p, n)
                    c = ((0.40 + 0.2 * n[2]) * sky + 0.68 * diff * warm) * (1 - 0.75 * ao)
                    if f["kind"] in ("wall", "riser"):
                        z0 = min(M.pos[v][2] for v in f["vs"])
                        c = c * (0.78 + 0.22 * min(1.0, (P[vi][2] - z0 + 6) / 40))
                    if f["kind"] == "cliff":
                        c *= 0.82 + 0.18 * max(0.0, min(1.0, (p[2] - CLOUD_Z) / 120))
                    if f["matname"] == "cliff_foot":
                        # SoI's snow rim glows white on every side
                        c = np.maximum(c, 0.78 if M.pos[vi][2] == CLOUD_Z else 0.66) * np.array([1.0, 1.0, 1.05]) / np.array(TINT["cliff_foot"])
                cache[key] = c
            fc.append(np.clip(np.asarray(cache[key]) * tint * 1.2, 0.12, 1.9))
        cols.append(fc)
    return cols


def bridge_edges(M: Mesh, L: dict):
    """Paving and bridge tops beside a drop take the flagstone edge tile (border outward); inner
    paving mixes two flagstone variants and quarter turns, as retail floors do."""
    hc, ox, oy = L["hc"], L["ox"], L["oy"]

    def h(x, y):
        i, j = int((x - ox) // G), int((y - oy) // G)
        return hc[j, i] if 0 <= j < hc.shape[0] and 0 <= i < hc.shape[1] else -1

    for f in M.faces:
        if f["kind"] != "top" or f["matname"] not in ("bridge", "plaza"):
            continue
        if f["matname"] == "plaza":
            P = np.array([M.pos[v] for v in f["vs"]], float)
            (xa, ya, z), (xb, yb, _) = P.min(0), P.max(0)
            side = [h(xb + 2, (ya + yb) / 2) < z - 18, h((xa + xb) / 2, ya - 2) < z - 18,
                    h(xa - 2, (ya + yb) / 2) < z - 18, h((xa + xb) / 2, yb + 2) < z - 18]
            k = int(xa * 7 + ya * 11) // 16
            if any(side):
                f["matname"], f["rot"] = "plaza_edge", side.index(True)
            else:
                f["matname"] = "plaza_var" if k % 5 in (1, 3) else "plaza"
                f["rot"] = k % 4
            continue
        P = np.array([M.pos[v] for v in f["vs"]], float)
        (xa, ya, z), (xb, yb, _) = P.min(0), P.max(0)
        # quarter turns put the tile's right-hand border on +x, -y, -x, +y
        side = [h(xb + 2, (ya + yb) / 2) < z - 18, h((xa + xb) / 2, ya - 2) < z - 18,
                h(xa - 2, (ya + yb) / 2) < z - 18, h((xa + xb) / 2, yb + 2) < z - 18]
        if any(side):
            f["matname"] = "bridge_edge"
            f["rot"] = side.index(True)


def uv_of(f, p):
    """Tile-local UV from the face's frame, then the face's quarter turns (rock, edge tiles)."""
    if f.get('stone_uv_kind')=='floor':
        import mesa_stone
        ox,oy=f['stone_floor_origin']
        return mesa_stone.wave(p[0],ox),mesa_stone.wave(p[1],oy)
    if f.get('stone_uv_kind')=='masonry':
        axes,base,extent=f['stone_chart']
        world=np.asarray(p)*[1,1,V/H]
        return tuple((world[list(axes)]-base)/extent)
    if f.get('stone_uv_kind')=='panel':
        org,ua,va,base,extent=f['stone_panel']
        d=np.asarray(p)-org
        u,v=(np.array([d@ua,d@va])-base)/extent
        return float(u),float(1-v)
    org, ua, va, ul, vl, u0, v0 = f["frame"]
    d = np.subtract(p, org)
    u = min(1.0, max(0.0, u0 + float(np.dot(d, ua)) / ul))
    v = min(1.0, max(0.0, v0 + float(np.dot(d, va)) / vl))
    for _ in range(f.get("rot", 0)):
        u, v = 1 - v, u
    return u, v


def eng(p):
    x, y, z = p
    return (x * H, z * V, -y * H)


def export(M: Mesh, P: np.ndarray, cols, L: dict, level: dict, out: Path, disc: Path):
    from s2world_export import donor_records
    mats, tris = [], []
    for f, fc in zip(M.faces, cols):
        key = MAT[f["matname"]]
        if key not in mats:
            mats.append(key)
        m = mats.index(key)
        vs = f["vs"]
        outward = np.array([f["n"][0] * H, f["n"][2] * V, -f["n"][1] * H])
        for a, b, c in ([(0, 1, 2), (0, 2, 3)] if len(vs) == 4 else [(0, 1, 2)]):
            pts = [np.array(eng(P[vs[t]])) for t in (a, b, c)]
            nrm = np.cross(pts[1] - pts[0], pts[2] - pts[0])
            if np.linalg.norm(nrm) < 1e-7:
                continue
            # engine convention (s2world_export): cross(b - a, c - a) points into the solid
            order = (a, b, c) if np.dot(nrm, outward) < 0 else (a, c, b)
            row = []
            for t in order:
                X, Y, Z = eng(P[vs[t]])
                u, w = f["uvs"][t] if "uvs" in f else uv_of(f, M.pos[vs[t]])
                u, w = min(1.0, max(0.0, u)), min(1.0, max(0.0, w))
                r, g, b_ = fc[t]
                row.append(f"{X:.4f} {Y:.4f} {Z:.4f} {r:.3f} {g:.3f} {b_:.3f} {u:.4f} {w:.4f}")
            tris.append(f"{f.get('flags', 3)} {m} " + " ".join(row))
    glimmer, summer = donor_records(disc, 17), donor_records(disc, 15)
    hc, ox, oy = L["hc"], L["ox"], L["oy"]

    def ground(x, y):
        i, j = int((x - ox) // G), int((y - oy) // G)
        if 0 <= j < hc.shape[0] and 0 <= i < hc.shape[1] and hc[j, i] >= 0:
            return float(hc[j, i])
        return None

    actors, skipped = [], {}
    for e in level["objects"]["objects"]:
        s2 = e.get("s2")
        if not s2:
            skipped[e["family"]] = skipped.get(e["family"], 0) + 1
            continue
        scene, cls = s2["scene"], int(s2["cls"], 16)
        donor = glimmer if scene == 17 else summer
        if cls not in donor:
            skipped[e["family"]] = skipped.get(e["family"], 0) + 1
            continue
        x, y, z = e["pos"]
        gz = ground(x, y)
        z = gz if gz is not None and abs(gz - z) < 30 else z
        X, Y, Z = eng((x, y, z))
        actors.append((scene, cls, donor[cls], X, Y + (0.25 if cls == 1 else 0.0), Z, 0.0,
                       e.get("gem_value", 5 if cls == 0x0C8 else 1)))
    sx, sy, sz = level["objects"]["start"]["pos"]
    ex, ey = sx + 18, sy - 18
    X, Y, Z = eng((ex, ey, ground(ex, ey) or sz))
    actors.append((17, 0x1AA, glimmer[0x1AA], X, Y, Z, 0.0, 1))
    imports = sorted({(s, c) for s, c, *_ in actors if s != 17})
    spawn = eng((sx, sy, ground(sx, sy) or sz))
    route = [spawn] + [eng(e["pos"]) for e in level["objects"]["objects"] if e.get("gem_value")][:40]
    lines = ["S2WORLD 4", "17", f"IMPORTS {len(imports)}"] + [f"{s} {c}" for s, c in imports]
    lines.append('"Market Mesa (Season of Ice)"')
    lines.append(f"{spawn[0]:.4f} {spawn[1] + 0.05:.4f} {spawn[2]:.4f} 0.785 0.529 0.620 0.788")
    lines.append(str(len(mats)))
    lines += [f'-2 "textures/{m}.rgba" 0' if isinstance(m, str) else f"{m[0]} {m[1]} 0" for m in mats]
    lines.append(str(len(tris)))
    lines += tris
    lines.append(str(len(actors)))
    for s, c, rec, X, Y, Z, yaw, gem in actors:
        lines.append(f"{s} {c} {rec} {X:.4f} {Y:.4f} {Z:.4f} {yaw:.3f} {gem} 255 1000")
    lines.append(str(len(route)))
    lines += [f"{p[0]:.4f} {p[1]:.4f} {p[2]:.4f}" for p in route]
    lines += ["0", "0", "0", "0"]
    out.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return dict(materials=len(mats), triangles=len(tris), actors=len(actors), skipped=skipped)


def build_model(level_path: Path, stone_polish=False):
    """The whole Market Mesa model (shared by the PC export and the PS1 encoder, tools/soi/mesa_ps1.py).
    Returns a dict: M (faces after splitting), P (sculpted positions), cols (per-face corner colours), L, level,
    polys (the faces before T-junction splitting, as copies), and the counts the PC report uses."""
    level = json.loads(level_path.read_text(encoding="utf-8"))
    import mesa_decor
    mapimg = Image.open(level_path.parent / "map.png")
    L = build_layout(level, mapimg)
    poles = mesa_decor.find_pillars(L)
    decor = mesa_decor.find_decor(L, mapimg)
    M = build_mesh(L)
    t = fix_tjunctions(M)
    before = len(M.faces)
    polys = [dict(f, vs=list(f["vs"])) for f in M.faces]
    split_polygons(M)
    caps = island_caps(M, L)
    RIM.update(v for f in M.faces if f["kind"] == "cliff" for v in f["vs"] if M.pos[v][2] == CLOUD_Z)
    for f in M.faces:
        f["matname"] = face_material(f, M, decor["banners"])
        if f["matname"] in ("cliff", "cliff2", "under", "cloud", "cloud2"):
            x, y, z = M.pos[f["vs"][0]]
            f["rot"] = int(x * 3 + y * 5 + z * 7) // 8 % 4
    bridge_edges(M, L)
    parts = []
    props = mesa_decor.load_props(PROJECT / "data/soi/pc/mesa_props.json")
    rng = np.random.default_rng(17)
    for p, sc in decor["trees"]:
        mesa_decor.place(parts, props, "pine_big" if sc >= 0.95 else "pine_small", p, rng.uniform(0, 2 * math.pi),
                         1.5 if sc >= 0.95 else sc / 0.7 * 1.25)
    for k, p in enumerate(decor["stalls"]):
        yaw = 0.0 if k % 2 else math.pi / 2
        mesa_decor.place(parts, props, "stall", p, yaw)
        dx, dy = (math.cos(yaw), math.sin(yaw))
        mesa_decor.place(parts, props, "crate", (p[0] + 11 * dx - 6 * dy, p[1] + 11 * dy + 6 * dx, p[2]), 0.3 * k)
        if k % 2 == 0:
            mesa_decor.place(parts, props, "crate", (p[0] + 11 * dx + 1 * dy, p[1] + 11 * dy - 1 * dx, p[2]), 0.7 * k, 0.8)
    for p, yaw in decor["racks"]:
        mesa_decor.place(parts, props, "rack", p, yaw)
    for x, y, z0, z1 in poles:
        mesa_decor.place(parts, props, "flagpole", (x, y, z0), rng.uniform(0, 2 * math.pi), 1.4)
    copings = merge_runs(M.copings)
    parts += profile_strips(copings, list(COPING), ["coping_top", "coping_side"])
    parts += wall_banners(M, decor["banners"])
    rails = merge_runs(M.rails)
    parts += profile_strips(rails, [(-2.5, 0.0), (-2.5, 7.0), (0.0, 7.0), (0.0, 0.0)],
                            ["bridge_rail", "bridge_rail", "bridge_rail"], flags=3)
    parts += bridge_arches(L)
    nparts = len(parts)
    mesa_decor.far_scenery(parts, sea=False, centre= (L["ox"] + L["hc"].shape[1] * G / 2, L["oy"] + L["hc"].shape[0] * G / 2), ground_z=CLOUD_Z)
    for q in parts:
        pts = q["pts"]
        o = np.array(pts[0], float)
        ua = np.subtract(pts[1], pts[0]); ul = np.linalg.norm(ua)
        va = np.subtract(pts[-1], pts[0]); va = va - ua * np.dot(va, ua) / ul ** 2; vl = max(np.linalg.norm(va), 1e-6)
        M.face(pts, q["n"], "decor", q["mat"], (tuple(o), tuple(ua / ul), tuple(va / vl), ul, vl, 0, 0))
        M.faces[-1]["matname"] = q["mat"]
        M.faces[-1]["flags"] = q["flags"]
        if "uv" in q:
            M.faces[-1]["uvs"] = [tuple(u) for u in q["uv"]]
    P = sculpt(M, L)
    cols = bake_light(M, P, L)
    result=dict(M=M, P=P, cols=cols, L=L, level=level, polys=polys, decor=decor, poles=poles, caps=caps, t=t,
                before=before, copings=copings, rails=rails, parts=parts, nparts=nparts)
    if stone_polish:
        import mesa_stone
        mesa_stone.apply(result)
    return result


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--level", type=Path, default=PROJECT / "data/soi/levels/06-market-mesa/level.json")
    ap.add_argument("--out", type=Path, default=PROJECT / "data/soi/pc/market-mesa.s2world")
    ap.add_argument("--disc", type=Path, default=PROJECT / "data/disc")
    a = ap.parse_args()
    B = build_model(a.level)
    M, P, cols, L, level = B["M"], B["P"], B["cols"], B["L"], B["level"]
    decor, poles, caps, t, before = B["decor"], B["poles"], B["caps"], B["t"], B["before"]
    copings, rails, parts, nparts = B["copings"], B["rails"], B["parts"], B["nparts"]
    info = export(M, P, cols, L, level, a.out, a.disc)
    quads = sum(len(f["vs"]) == 4 for f in M.faces)
    tri_mat = defaultdict(int)
    for f in M.faces:
        tri_mat[f["matname"]] += len(f["vs"]) - 2
    info.update(trees=len(decor["trees"]), stalls=len(decor["stalls"]), banners=len(decor["banners"]),
                racks=len(decor["racks"]), poles=len(poles), coping_runs=len(copings), rail_runs=len(rails),
                underside=caps, tjunction_vertices=t, polygons=before, faces=len(M.faces), quads=quads,
                tris=len(M.faces) - quads, vertices=len(M.pos),
                triangles_by_kind={k: sum(len(f["vs"]) - 2 for f in M.faces if f["kind"] == k)
                                   for k in ("top", "wall", "riser", "cliff", "under", "bridge_side", "decor")},
                triangles_far_scenery=sum(len(f["vs"]) - 2 for f in M.faces[len(M.faces) - (len(parts) - nparts):]),
                triangles_by_material=dict(sorted(tri_mat.items(), key=lambda kv: -kv[1])))
    np.savez_compressed(a.out.with_suffix(".mesh.npz"), pos=np.array(M.pos, float), final=P,
                        faces=np.array(json.dumps([dict(vs=f["vs"], n=f["n"], kind=f["kind"], mat=f["matname"],
                                                        frame=f["frame"]) for f in M.faces])))
    a.out.with_suffix(".json").write_text(json.dumps(info, indent=1))
    print(json.dumps(info))


if __name__ == "__main__":
    main()

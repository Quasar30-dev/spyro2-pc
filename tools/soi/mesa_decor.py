"""Market Mesa decor placed from Season of Ice's own art: trees, market stalls, flagpoles, banners.

SoI draws these into its background layers, so their places come from the map (map.png):
coloured blobs (tree foliage, stall awning stripes, yellow banner cloth) are found, and their
base pixel is inverse-projected onto the level (x' = 2(x - y), y' = x + y - z, so
x = (y' + z) / 2 + x' / 4 and y = (y' + z) / 2 - x' / 4 for the highest surface z that matches).
Flagpoles stand where SoI has a pole-sized solid (a one- or two-cell pillar above its neighbours).
Meshes are low-poly in Spyro 2's manner (a few dozen triangles each).
"""
from __future__ import annotations

import math

import numpy as np
from scipy import ndimage

G = 8


def hsv(im):
    mx, mn = im.max(-1), im.min(-1)
    s = np.where(mx > 0, (mx - mn) / np.maximum(mx, 1e-6), 0)
    d = np.maximum(mx - mn, 1e-6)
    r, g, b = im[..., 0], im[..., 1], im[..., 2]
    h = np.where(mx == r, ((g - b) / d) % 6, np.where(mx == g, (b - r) / d + 2, (r - g) / d + 4)) * 60
    return h, s, mx


def blobs(mask, min_size, merge=0):
    if merge:
        mask = ndimage.binary_closing(mask, iterations=merge)
    lab, n = ndimage.label(ndimage.binary_closing(mask, iterations=2))
    sizes = ndimage.sum(mask, lab, range(1, n + 1))
    out = []
    for i, o in enumerate(ndimage.find_objects(lab)):
        if sizes[i] >= min_size:
            out.append(((o[1].start + o[1].stop) / 2, o[0].start, o[0].stop))
    return out


def unproject(L, px, py, slack=0):
    """Highest lattice surface hit by SoI's view ray through map pixel (px, py)."""
    hc, ox, oy = L["hc"], L["ox"], L["oy"]
    for z in sorted({int(v) for v in np.unique(hc) if v >= 0}, reverse=True):
        x, y = (py + z) / 2 + px / 4, (py + z) / 2 - px / 4
        for dx, dy in ((0, 0), (-slack, -slack)):
            i, j = int((x + dx - ox) // G), int((y + dy - oy) // G)
            if 0 <= j < hc.shape[0] and 0 <= i < hc.shape[1] and hc[j, i] == z:
                return (x, y, float(z))
    return None


def find_decor(L, mapimg) -> dict:
    im = np.asarray(mapimg.convert("RGB")).astype(float) / 255
    h, s, v = hsv(im)
    trees = []
    for cx, top, bottom in blobs((h > 80) & (h < 170) & (s > 0.35) & (v > 0.35), 150):
        p = unproject(L, cx, bottom + 2, slack=6) or unproject(L, cx, bottom - 6, slack=6)
        if p:
            # a cluster of conifers in the art: one large and two small trees
            trees.append((p, 1.0))
            if bottom - top > 40:
                trees += [((p[0] - 10, p[1] + 6, p[2]), 0.7), ((p[0] + 6, p[1] - 10, p[2]), 0.6)]
    stalls = []
    for cx, top, bottom in blobs(((h < 12) | (h > 348)) & (s > 0.55) & (v > 0.55), 40, merge=4):
        p = unproject(L, cx, bottom + 12)
        if p and all(math.dist(p[:2], q[:2]) > 14 for q in stalls):
            stalls.append(p)
    banners = []
    for cx, top, bottom in blobs((h > 40) & (h < 62) & (s > 0.6) & (v > 0.8), 20):
        p = unproject(L, cx, top, slack=6)
        if p:
            banners.append(p)
    # SoI's pink cloth racks on the roofs: magenta blobs; their long axis in the art gives the
    # direction (SoI x runs along screen (2, 1), y along (-2, 1))
    racks = []
    mask = (h > 285) & (h < 335) & (s > 0.45) & (v > 0.45)
    lab, n = ndimage.label(ndimage.binary_closing(mask, iterations=2))
    for k, o in enumerate(ndimage.find_objects(lab), start=1):
        ys, xs = np.nonzero((lab[o] == k) & mask[o])
        if len(xs) < 500:           # trims and tassels are smaller than a rack's cloth
            continue
        xs, ys = xs + o[1].start, ys + o[0].start
        c = np.cov(np.stack([xs, ys]))
        w, vec = np.linalg.eigh(c)
        major = vec[:, -1]
        along_x = abs(major @ np.array([2, 1]) / math.sqrt(5)) >= abs(major @ np.array([-2, 1]) / math.sqrt(5))
        # the A-frame feet stand about 10 px below the cloth in the art; take the nearest hit
        p = None
        for dy in (10, 6, 14, 2, 18, -2, -6, -12, -20):
            p = unproject(L, xs.mean(), ys.max() + dy, slack=6)
            if p:
                break
        if p:
            yaw = 0.0 if along_x else math.pi / 2
            if len(xs) > 1500:      # two racks end to end in the art
                dx, dy = math.cos(yaw) * 10, math.sin(yaw) * 10
                racks += [((p[0] - dx, p[1] - dy, p[2]), yaw), ((p[0] + dx, p[1] + dy, p[2]), yaw)]
            else:
                racks.append((p, yaw))
    return dict(trees=trees, stalls=stalls, banners=banners, racks=racks)


# ------------------------------------------------------------------ Blender props (mesa_props_blender.py)
def load_props(path):
    import json
    return json.loads(path.read_text())


def place(out, props, name, pos, yaw=0.0, scale=1.0):
    """Instance a Blender prop (SoI units) at pos, turned by yaw about z, uniformly scaled."""
    pr = props[name]
    cy, sy = math.cos(yaw), math.sin(yaw)
    P = []
    for x, y, z in pr["verts"]:
        x, y, z = x * scale, y * scale, z * scale
        P.append((pos[0] + x * cy - y * sy, pos[1] + x * sy + y * cy, pos[2] + z))
    for f in pr["faces"]:
        nx, ny, nz = f["n"]
        n = (nx * cy - ny * sy, nx * sy + ny * cy, nz)
        out.append(dict(pts=[P[i] for i in f["v"]], n=n, mat=f["mat"], flags=f["flags"], uv=f["uv"]))


def find_pillars(L):
    """Pole-sized solids: equal-height components of <= 2 cells standing >= 20 above all
    neighbours. They become flagpoles; the cell takes its neighbours' height."""
    hc = L["hc"]
    poles = []
    for z in np.unique(hc[hc >= 0]):
        lab, n = ndimage.label(hc == z)
        for k, o in enumerate(ndimage.find_objects(lab), start=1):
            comp = lab == k
            if comp.sum() > 4:
                continue
            ring = ndimage.binary_dilation(comp) & ~comp
            nb = hc[ring]
            nb = nb[nb >= 0]
            if len(nb) and z >= nb.max() + 20:
                js, is_ = np.nonzero(comp)
                x = L["ox"] + G * (is_.mean() + 0.5)
                y = L["oy"] + G * (js.mean() + 0.5)
                poles.append((x, y, float(nb.max()), float(z)))
                hc[comp] = nb.max()
    return poles


# ------------------------------------------------------------------ meshes (SoI units, z up)
def _quad(out, p, n, mat, flags=3):
    out.append(dict(pts=p, n=n, mat=mat, flags=flags))


def box(out, c, w, d, z0, z1, mat, top_mat=None, sides=True, flags=3):
    x, y = c
    a = [(x - w, y - d), (x + w, y - d), (x + w, y + d), (x - w, y + d)]
    if sides:
        for k in range(4):
            (xa, ya), (xb, yb) = a[k], a[(k + 1) % 4]
            n = np.cross(np.subtract((xb, yb, 0), (xa, ya, 0)), (0, 0, 1))
            n = tuple(n / np.linalg.norm(n))
            _quad(out, [(xa, ya, z0), (xb, yb, z0), (xb, yb, z1), (xa, ya, z1)], n, mat, flags)
    if top_mat:
        _quad(out, [(x - w, y - d, z1), (x + w, y - d, z1), (x + w, y + d, z1), (x - w, y + d, z1)], (0, 0, 1), top_mat, flags)


def cone(out, c, r, z0, z1, mat, sides=6, flags=3, cap=True):
    x, y = c
    ring = [(x + r * math.cos(2 * math.pi * k / sides), y + r * math.sin(2 * math.pi * k / sides), z0) for k in range(sides)]
    apex = (x, y, z1)
    for k in range(sides):
        a, b = ring[k], ring[(k + 1) % sides]
        n = np.cross(np.subtract(b, a), np.subtract(apex, a))
        _quad(out, [a, b, apex], tuple(n / np.linalg.norm(n)), mat, flags)
        if cap:
            _quad(out, [b, a, (x, y, z0 + 1)], (0, 0, -1), mat, flags)   # underside cap


def round_tier(out, c, r, z0, z1, mat, sides=6, cap=False, twist=0.0):
    """A conifer tier with a rounded silhouette: base ring, a bulging shoulder ring, apex."""
    x, y = c
    ring = lambda rr, zz, a0: [(x + rr * math.cos(2 * math.pi * k / sides + a0), y + rr * math.sin(2 * math.pi * k / sides + a0), zz) for k in range(sides)]
    base, mid = ring(r, z0, twist), ring(r * 0.78, z0 + (z1 - z0) * 0.42, twist + 0.25)
    apex = (x, y, z1)
    for k in range(sides):
        a0, a1, b0, b1 = base[k], base[(k + 1) % sides], mid[k], mid[(k + 1) % sides]
        n = np.cross(np.subtract(a1, a0), np.subtract(b0, a0))
        _quad(out, [a0, a1, b1, b0], tuple(n / np.linalg.norm(n)), mat)
        n = np.cross(np.subtract(b1, b0), np.subtract(apex, b0))
        _quad(out, [b0, b1, apex], tuple(n / np.linalg.norm(n)), mat)
        if cap:
            _quad(out, [a1, a0, (x, y, z0 + 1)], (0, 0, -1), mat)


def tree(out, p, scale):
    x, y, z = p
    box(out, (x, y), 1.6 * scale, 1.6 * scale, z, z + 10 * scale, "bark")
    round_tier(out, (x, y), 12 * scale, z + 8 * scale, z + 30 * scale, "foliage", cap=True)
    round_tier(out, (x, y), 8.5 * scale, z + 22 * scale, z + 46 * scale, "foliage", twist=0.45)


def crates(out, p, k):
    """One or two wooden crates beside a stall."""
    x, y, z = p
    box(out, (x + 12, y + 2), 3.2, 3.2, z, z + 7, "crate", top_mat="crate")
    if k % 2 == 0:
        box(out, (x + 11.5, y - 5), 2.6, 2.6, z, z + 6, "crate", top_mat="crate")


def stall(out, p):
    x, y, z = p
    for dx, dy in ((-7, -5), (7, -5), (7, 5), (-7, 5)):
        box(out, (x + dx, y + dy), 0.8, 0.8, z, z + 22, "post")
    box(out, (x, y), 7.5, 5.5, z, z + 9, "counter", top_mat="counter")
    # awning: a gable roof along x, hanging past the posts
    e, w, d, zr, zt = 9.5, 9.5, 7.5, z + 20, z + 27
    for sy in (-1, 1):
        pts = [(x - w, y + sy * d, zr), (x + w, y + sy * d, zr), (x + w, y, zt), (x - w, y, zt)]
        n = (0, sy * 0.7, 0.7)
        _quad(out, pts if sy < 0 else pts[::-1], n, "awning")
        _quad(out, pts[::-1] if sy < 0 else pts, (0, -n[1], -n[2]), "awning", 1)   # underside, visual only
    for sx in (-1, 1):
        tri = [(x + sx * w, y - d, zr), (x + sx * w, y + d, zr), (x + sx * w, y, zt)]
        _quad(out, tri if sx > 0 else tri[::-1], (sx, 0, 0), "awning")


def flagpole(out, pole):
    x, y, z0, z1 = pole
    box(out, (x, y), 1.1, 1.1, z0, z1 + 6, "pole", top_mat="pole")
    # banner cloth hung from the pole, both faces (visual only)
    b = [(x + 1.1, y, z1 - 14), (x + 9, y, z1 - 14), (x + 9, y, z1 + 4), (x + 1.1, y, z1 + 4)]
    _quad(out, b, (0, -1, 0), "flag", 1)
    _quad(out, b[::-1], (0, 1, 0), "flag", 1)


def far_scenery(out, sea=True, centre=(0, 0), ground_z=25):
    """Distant mesas around the islands (and optionally a cloud sea below; visual only)."""
    cx, cy = centre
    zc, n, step = ground_z - 185, (12 if sea else 0), 340.0
    x0, y0 = cx - n * step / 2, cy - n * step / 2
    for j in range(n):
        for i in range(n):
            xa, ya = x0 + i * step, y0 + j * step
            out.append(dict(pts=[(xa, ya, zc), (xa + step, ya, zc), (xa + step, ya + step, zc), (xa, ya + step, zc)],
                            n=(0, 0, 1), mat="cloud" if (i + j) % 3 else "cloud2", flags=1))
    rng = np.random.default_rng(5)
    for k in range(9):
        a = 2 * math.pi * k / 9 + rng.uniform(-0.2, 0.2)
        r = rng.uniform(1500, 1900)
        mx, my = cx + r * math.cos(a), cy + r * math.sin(a)
        w = rng.uniform(110, 200)
        top = zc + rng.uniform(700, 1300)
        S = 8
        rings = []
        for lvl, (sc, zz) in enumerate(((1.35, zc), (1.0, zc + (top - zc) * 0.55), (0.72, top))):
            wob = rng.uniform(0.85, 1.15, S)
            rings.append([(mx + w * sc * wob[s] * math.cos(2 * math.pi * s / S + 0.2 * lvl),
                           my + w * sc * wob[s] * math.sin(2 * math.pi * s / S + 0.2 * lvl), zz) for s in range(S)])
        for lo, hi in zip(rings[:-1], rings[1:]):
            for s in range(S):
                a0, a1, b0, b1 = lo[s], lo[(s + 1) % S], hi[s], hi[(s + 1) % S]
                nn = np.cross(np.subtract(a1, a0), np.subtract(b0, a0))
                out.append(dict(pts=[a0, a1, b1, b0], n=tuple(nn / np.linalg.norm(nn)), mat="far_rock", flags=1))
        for s in range(S):
            out.append(dict(pts=[rings[-1][s], rings[-1][(s + 1) % S], (mx, my, top + 20)], n=(0, 0, 1), mat="far_top", flags=1))

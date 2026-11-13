"""Export an extracted Season of Ice level as an S2WORLD 4 custom world for the PC engine.

  python tools/soi/s2world_export.py [--level data/soi/levels/06-market-mesa/level.json]
                                     [--out data/soi/pc/market-mesa.s2world]

Nothing in the runtime changes: the file is read by src/custom_level.cpp (bank 17, Glimmer).

Geometry: the level's solids are columns from the world floor to their top, so the terrain is a
heightfield. It is rasterised at 1 SoI unit, top areas of equal height are merged into rectangles,
height steps become vertical walls, and every face is cut into texture-tile sized quads (UVs are
tile-local in S2WORLD). The same triangles are visual and collision.

Scale (docs/SEASON_OF_ICE_PORT.md, "Scale calibration"): engine (X, Y up, Z) =
(x * H, z * V, -y * H), V = 1/24 so one SoI terrace (24 z) is 1.0 Spyro 2 world unit, below the
retail held-jump peak of 1.2807 and above half of it; H = V * sqrt(6) keeps SoI's own projection an
exact 30-degree orthographic view.

Surfaces: each top and wall samples the SoI map colour at its projected point
(x' = 2(x - y), y' = x + y - z) and picks a retail Shady Oasis (WAD 47) tile by surface type.
Objects: data/soi/families.json stand-ins; donor records are the first retail record of each class.
"""
from __future__ import annotations

import argparse
import colorsys
import json
import math
import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parents[1]
sys.path.insert(0, str(PROJECT / "tools"))
sys.path.insert(0, str(HERE))
import soi_extract  # noqa: E402
from spyrodisc import wad_index  # noqa: E402

V = 1.0 / 24.0
H = V * math.sqrt(6.0)
TILE = 20  # SoI units per texture tile (about 2.04 world units)
FLOOR_Z = -24  # underside of the outer walls, in SoI z

# surface type -> (retail scene, tile index) in Shady Oasis' atlas (data/soi/retail-atlas/retail-47)
SURFACE_TILE = {"roof": (47, 25), "paving": (47, 4), "stone": (47, 12), "sand": (47, 9),
                "plaster": (47, 12), "cliff": (47, 1), "dark": (47, 2), "grass": (17, 0)}


def classify(rgb) -> str:
    r, g, b = [c / 255 for c in rgb]
    h, s, v = colorsys.rgb_to_hsv(r, g, b)
    if v < 0.18:
        return "dark"
    if s < 0.18:
        return "stone"
    hd = h * 360
    if 190 <= hd <= 260:
        return "roof"
    if 70 <= hd < 170:
        return "grass"
    if 25 <= hd < 70:
        return "sand"
    return "paving"


def donor_records(disc: Path, slot: int) -> dict[int, int]:
    """First record index of each class in a retail scene (8001c734 layout, OBJECTS_AND_GEMS)."""
    f = (disc / "WAD.WAD").open("rb")
    head = f.read(0x800)
    off, size = struct.unpack_from("<II", head, 8 * slot)
    f.seek(off)
    e = wad_index(f.read(0x10000), size)[3]
    f.seek(off + e.offset)
    g = f.read(e.size)
    p = 0x2C
    for _ in range(8):
        p += struct.unpack_from("<I", g, p)[0]
    n = struct.unpack_from("<I", g, p + 4)[0]
    out: dict[int, int] = {}
    for k in range(n):
        out.setdefault(struct.unpack_from("<H", g, p + 8 + 88 * k + 0x36)[0], k)
    return out


def merge_rects(mask: np.ndarray):
    """Greedy rectangles covering a boolean mask: yields (r0, c0, r1, c1) exclusive ends."""
    m = mask.copy()
    R, C = m.shape
    for r in range(R):
        c = 0
        row = m[r]
        while c < C:
            if not row[c]:
                c += 1
                continue
            c1 = c
            while c1 < C and row[c1]:
                c1 += 1
            r1 = r + 1
            while r1 < R and m[r1, c:c1].all():
                r1 += 1
            m[r:r1, c:c1] = False
            yield r, c, r1, c1
            c = c1


def splits(a: float, b: float, step: float):
    v = a
    while v < b - 1e-6:
        w = min(b, (math.floor(v / step + 1e-6) + 1) * step)
        yield v, w
        v = w


class World:
    def __init__(self):
        self.mats: list[tuple[int, int]] = []
        self.tris: list[str] = []

    def mat(self, surface: str) -> int:
        key = SURFACE_TILE[surface]
        if key not in self.mats:
            self.mats.append(key)
        return self.mats.index(key)

    def quad(self, p, uv, mat, shade):
        """p: 4 engine-space points (counter-clockwise seen from the visible side)."""
        c = f"{shade:.3f} {shade:.3f} {shade:.3f}"
        v = [f"{x:.4f} {y:.4f} {z:.4f} {c} {u:.4f} {w:.4f}" for (x, y, z), (u, w) in zip(p, uv)]
        for a, b, d in ((0, 1, 2), (0, 2, 3)):
            self.tris.append(f"3 {mat} {v[a]} {v[b]} {v[d]}")


def E(x, y, z):
    return (x * H, z * V, -y * H)


def build(level: dict, soi_map: Image.Image, world: World):
    coll = level["collision"]
    hm, (x0, y0) = soi_extract.heightmap(coll)
    hm = np.where(hm >= 0, hm, np.nan)
    rows, cols = hm.shape
    mp = np.asarray(soi_map.convert("RGB"))

    def sample(x, y, z):
        px, py = int(2 * (x - y)), int(x + y - z)
        if 0 <= px < mp.shape[1] and 0 <= py < mp.shape[0]:
            return mp[py, px]
        return (128, 128, 128)

    tops = 0
    for h in np.unique(hm[~np.isnan(hm)]):
        for r0, c0, r1, c1 in merge_rects(hm == h):
            for ya, yb in splits(y0 + r0, y0 + r1, TILE):
                for xa, xb in splits(x0 + c0, x0 + c1, TILE):
                    surf = classify(sample((xa + xb) / 2, (ya + yb) / 2, h))
                    m = world.mat(surf)
                    u0, u1 = (xa % TILE) / TILE, ((xb - 1e-6) % TILE + 1e-6) / TILE
                    w0, w1 = (ya % TILE) / TILE, ((yb - 1e-6) % TILE + 1e-6) / TILE
                    # engine: +X = +x, +Z = -y; seen from above, (x, y) order a,b,c,d is clockwise
                    world.quad([E(xa, ya, h), E(xa, yb, h), E(xb, yb, h), E(xb, ya, h)],
                               [(u0, w0), (u0, w1), (u1, w1), (u1, w0)], m, 1.0)
                    tops += 1
    walls = 0
    full = np.nan_to_num(hm, nan=FLOOR_Z)

    def wall_runs(a: np.ndarray, b: np.ndarray):
        """For neighbouring lines a (inside) and b (outside): runs where a is higher than b."""
        n = len(a)
        i = 0
        while i < n:
            if a[i] > b[i]:
                j = i
                while j < n and a[j] == a[i] and b[j] == b[i]:
                    j += 1
                yield i, j, b[i], a[i]
                i = j
            else:
                i += 1

    pad = np.pad(full, 1, constant_values=FLOOR_Z)
    # walls facing -y / +y (between rows) and -x / +x (between columns)
    for r in range(rows + 1):
        for side in (-1, 1):
            inside = pad[r + (1 if side == -1 else 0), 1:-1] if side == -1 else pad[r, 1:-1]
            outside = pad[r, 1:-1] if side == -1 else pad[r + 1, 1:-1]
            if side == -1 and r == rows:
                continue
            if side == 1 and r == 0:
                continue
            yline = y0 + r
            for i, j, lo, hi in wall_runs(inside, outside):
                for xa, xb in splits(x0 + i, x0 + j, TILE):
                    for za, zb in splits(lo, hi, TILE * H / V):
                        top_s = classify(sample((xa + xb) / 2, yline, hi))
                        surf = {"roof": "plaster", "paving": "cliff", "sand": "cliff"}.get(top_s, "stone")
                        m = world.mat(surf)
                        su = lambda v: (v % TILE) / TILE
                        sv = lambda z: ((z - lo) % (TILE * H / V)) / (TILE * H / V)
                        uv = [(su(xa), sv(za)), (min(1, su(xa) + (xb - xa) / TILE), sv(za)),
                              (min(1, su(xa) + (xb - xa) / TILE), min(1, sv(za) + (zb - za) / (TILE * H / V))),
                              (su(xa), min(1, sv(za) + (zb - za) / (TILE * H / V)))]
                        if side == 1:   # face +y (towards the SoI camera's left side)
                            p = [E(xa, yline, za), E(xb, yline, za), E(xb, yline, zb), E(xa, yline, zb)]
                        else:
                            p = [E(xb, yline, za), E(xa, yline, za), E(xa, yline, zb), E(xb, yline, zb)]
                        world.quad(p, uv, m, 0.72 if side == 1 else 0.6)
                        walls += 1
    for c in range(cols + 1):
        for side in (-1, 1):
            if side == -1 and c == cols:
                continue
            if side == 1 and c == 0:
                continue
            inside = pad[1:-1, c + 1] if side == -1 else pad[1:-1, c]
            outside = pad[1:-1, c] if side == -1 else pad[1:-1, c + 1]
            xline = x0 + c
            for i, j, lo, hi in wall_runs(inside, outside):
                for ya, yb in splits(y0 + i, y0 + j, TILE):
                    for za, zb in splits(lo, hi, TILE * H / V):
                        top_s = classify(sample(xline, (ya + yb) / 2, hi))
                        surf = {"roof": "plaster", "paving": "cliff", "sand": "cliff"}.get(top_s, "stone")
                        m = world.mat(surf)
                        su = lambda v: (v % TILE) / TILE
                        sv = lambda z: ((z - lo) % (TILE * H / V)) / (TILE * H / V)
                        uv = [(su(ya), sv(za)), (min(1, su(ya) + (yb - ya) / TILE), sv(za)),
                              (min(1, su(ya) + (yb - ya) / TILE), min(1, sv(za) + (zb - za) / (TILE * H / V))),
                              (su(ya), min(1, sv(za) + (zb - za) / (TILE * H / V)))]
                        if side == 1:   # face +x
                            p = [E(xline, yb, za), E(xline, ya, za), E(xline, ya, zb), E(xline, yb, zb)]
                        else:
                            p = [E(xline, ya, za), E(xline, yb, za), E(xline, yb, zb), E(xline, ya, zb)]
                        world.quad(p, uv, m, 0.85 if side == 1 else 0.6)
                        walls += 1
    return tops, walls, (x0, y0, hm)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--level", type=Path, default=PROJECT / "data/soi/levels/06-market-mesa/level.json")
    ap.add_argument("--out", type=Path, default=PROJECT / "data/soi/pc/market-mesa.s2world")
    ap.add_argument("--disc", type=Path, default=PROJECT / "data/disc")
    a = ap.parse_args()
    L = json.loads(a.level.read_text(encoding="utf-8"))
    world = World()
    tops, walls, (x0, y0, hm) = build(L, Image.open(a.level.parent / "map.png"), world)
    glimmer, summer = donor_records(a.disc, 17), donor_records(a.disc, 15)
    actors, skipped = [], {}
    sx, sy, sz = L["objects"]["start"]["pos"]

    def ground(x, y):
        r, c = int(round(y - y0)), int(round(x - x0))
        if 0 <= r < hm.shape[0] and 0 <= c < hm.shape[1] and not np.isnan(hm[r, c]):
            return float(hm[r, c])
        return None

    for e in L["objects"]["objects"]:
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
        gem = e.get("gem_value", 5 if cls == 0x0C8 else 1)
        X, Y, Z = E(x, y, z)
        actors.append((scene, cls, donor[cls], X, Y + (0.25 if cls == 1 else 0.0), Z, 0.0, gem))
    # exit: a Return Home arch a few units from the start
    ex, ey = sx + 18, sy - 18
    ez = ground(ex, ey) or sz
    X, Y, Z = E(ex, ey, ez)
    actors.append((17, 0x1AA, glimmer[0x1AA], X, Y, Z, 0.0, 1))
    imports = sorted({(s, c) for s, c, *_ in actors if s != 17})
    gz = ground(sx, sy)
    spawn = E(sx, sy, gz if gz is not None else sz)
    route = [spawn] + [E(*e["pos"]) for e in L["objects"]["objects"] if e.get("gem_value")][:40]
    lines = ["S2WORLD 4", "17", f"IMPORTS {len(imports)}"] + [f"{s} {c}" for s, c in imports]
    lines.append('"Market Mesa (Season of Ice)"')
    lines.append(f"{spawn[0]:.4f} {spawn[1] + 0.05:.4f} {spawn[2]:.4f} 0.785 0.529 0.620 0.788")
    lines.append(str(len(world.mats)))
    lines += [f"{s} {t} 0" for s, t in world.mats]
    lines.append(str(len(world.tris)))
    lines += world.tris
    lines.append(str(len(actors)))
    for i, (s, c, rec, X, Y, Z, yaw, gem) in enumerate(actors):
        lines.append(f"{s} {c} {rec} {X:.4f} {Y:.4f} {Z:.4f} {yaw:.3f} {gem} 255 1000")
    lines.append(str(len(route)))
    lines += [f"{p[0]:.4f} {p[1]:.4f} {p[2]:.4f}" for p in route]
    lines += ["0", "0", "0", "0"]  # trees, doors, conversations, goals
    a.out.parent.mkdir(parents=True, exist_ok=True)
    a.out.write_text("\n".join(lines) + "\n", encoding="utf-8")
    info = dict(file=str(a.out), H=H, V=V, tile_units=TILE, top_quads=tops, wall_quads=walls,
                triangles=len(world.tris), materials=[list(m) for m in world.mats], actors=len(actors),
                skipped=skipped, imports=[list(i) for i in imports], spawn=spawn)
    a.out.with_suffix(".json").write_text(json.dumps(info, indent=1))
    print(json.dumps(info))


if __name__ == "__main__":
    main()

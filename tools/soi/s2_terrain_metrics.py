"""What a typical retail Spyro 2 level's terrain looks like, measured (the quality bar for SoI levels).

  python tools/soi/s2_terrain_metrics.py [--slots 17 47 ...]  -> data/soi/ps1/terrain_metrics.json

Per scene: regions, near (HP) quads / triangles, far (LP) faces, surface orientation by area,
dihedral angles between neighbouring HP faces (hard 90-degree edges vs. soft bevels / curves),
quad edge length (one 64x64 tile per face: texel density), materials, vertex-colour lighting range.
Units: world units (1 = 1024 retail units).
"""
from __future__ import annotations

import argparse
import json
from collections import defaultdict
from pathlib import Path

import numpy as np

from s2scene import PROJECT, hp_mesh, read_scene

NAMES = {15: "Summer Forest", 17: "Glimmer", 19: "Idol Springs", 21: "Colossus", 23: "Hurricos",
         25: "Aquaria Towers", 27: "Sunny Beach", 29: "Ocean Speedway", 31: "Crush's Dungeon",
         33: "Autumn Plains", 35: "Skelos Badlands", 37: "Crystal Glacier", 39: "Breeze Harbor",
         41: "Zephyr", 43: "Metro Speedway", 45: "Scorch", 47: "Shady Oasis", 49: "Magma Cone",
         51: "Fracture Hills", 53: "Icy Speedway", 55: "Gulp's Overlook", 57: "Winter Tundra",
         59: "Mystic Marsh", 61: "Cloud Temples", 63: "Canyon Speedway", 65: "Robotica Farms",
         67: "Metropolis", 69: "Dragon Shores", 71: "Ripto's Arena"}


def measure(disc: Path, slot: int) -> dict:
    sc = read_scene(disc, slot)
    tris, cols, mats, quads, ntri, edges_len = [], [], set(), 0, 0, []
    lp_faces = 0
    for r in sc.regions:
        lp_faces += len(r.lp_faces)
        v, faces = hp_mesh(r)
        colors = np.array([[c[0], c[1], c[2]] for c in r.hp_colors], float) if r.hp_colors else np.zeros((0, 3))
        for quad, idx, cidx, mat, raw in faces:
            mats.add(mat)
            P = v[idx] / 1024.0
            if quad:
                quads += 1
                tris += [(P[0], P[1], P[2]), (P[2], P[1], P[3])]
                edges_len += [np.linalg.norm(P[0] - P[1]), np.linalg.norm(P[0] - P[2])]
            else:
                ntri += 1
                tris.append((P[0], P[1], P[2]))
                edges_len += [np.linalg.norm(P[0] - P[1]), np.linalg.norm(P[1] - P[2])]
            for c in cidx:
                if c < len(colors):
                    cols.append(colors[c])
    T = np.array(tris)
    n = np.cross(T[:, 1] - T[:, 0], T[:, 2] - T[:, 0])
    area = np.linalg.norm(n, axis=1) / 2
    ok = area > 1e-9
    nz = np.abs(n[ok, 2] / (2 * area[ok]))
    A = area[ok]
    tot = A.sum()
    bands = {"flat (<14 deg)": nz > 0.97, "gentle (14-45)": (nz > 0.707) & (nz <= 0.97),
             "steep (45-80)": (nz > 0.17) & (nz <= 0.707), "vertical (>80)": nz <= 0.17}
    # dihedral angles across shared edges (positions rounded to 1/64 unit)
    key = lambda p: tuple(np.round(p * 64).astype(int))
    emap = defaultdict(list)
    nn = n[ok] / (2 * area[ok])[:, None]
    Tk = T[ok]
    for i, t in enumerate(Tk):
        k = [key(p) for p in t]
        for a, b in ((0, 1), (1, 2), (2, 0)):
            emap[tuple(sorted((k[a], k[b])))].append(i)
    ang = []
    for e, fs in emap.items():
        if len(fs) == 2:
            d = float(np.clip(np.dot(nn[fs[0]], nn[fs[1]]), -1, 1))
            ang.append(np.degrees(np.arccos(d)))
    ang = np.array(ang)
    creased = ang[ang > 2]
    C = np.array(cols) if cols else np.zeros((1, 3))
    lum = C.mean(1)
    return {"slot": slot, "name": NAMES.get(slot, ""), "regions": len(sc.regions), "materials": len(mats),
            "texture_descriptors": sc.texture_count, "hp_quads": quads, "hp_triangles": ntri,
            "hp_render_triangles": len(tris), "lp_faces": lp_faces,
            "area_m2": round(float(tot), 1),
            "orientation_share": {k: round(float(A[m].sum() / tot), 3) for k, m in bands.items()},
            "creases_deg_share": {"2-20 (soft/curved)": round(float(((creased > 2) & (creased <= 20)).mean()), 3),
                                  "20-60 (bevel/slope)": round(float(((creased > 20) & (creased <= 60)).mean()), 3),
                                  "60-120 (hard)": round(float(((creased > 60) & (creased <= 120)).mean()), 3),
                                  ">120": round(float((creased > 120).mean()), 3)},
            "smooth_edge_share": round(float((ang <= 2).mean()), 3),
            "edge_length_median": round(float(np.median(edges_len)), 3),
            "edge_length_p10_p90": [round(float(np.percentile(edges_len, 10)), 3), round(float(np.percentile(edges_len, 90)), 3)],
            "vertex_colour_luma_p5_p50_p95": [round(float(np.percentile(lum, q)), 1) for q in (5, 50, 95)],
            "vertex_colour_chroma_mean": round(float((C.max(1) - C.min(1)).mean()), 1)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--disc", type=Path, default=PROJECT / "data/disc")
    ap.add_argument("--slots", type=int, nargs="*", default=sorted(NAMES))
    ap.add_argument("--out", type=Path, default=PROJECT / "data/soi/ps1/terrain_metrics.json")
    a = ap.parse_args()
    rows = []
    for s in a.slots:
        try:
            m = measure(a.disc, s)
        except AssertionError as e:
            print(s, "failed", e)
            continue
        rows.append(m)
        print(json.dumps(m))
    keys = ["regions", "materials", "hp_quads", "hp_triangles", "hp_render_triangles", "lp_faces", "edge_length_median"]
    summary = {k: {"min": min(r[k] for r in rows), "median": float(np.median([r[k] for r in rows])),
                   "max": max(r[k] for r in rows)} for k in keys}
    for band in rows[0]["orientation_share"]:
        summary["orientation " + band] = float(np.median([r["orientation_share"][band] for r in rows]))
    for band in rows[0]["creases_deg_share"]:
        summary["creases " + band] = float(np.median([r["creases_deg_share"][band] for r in rows]))
    if len(a.slots) > 3:
        a.out.write_text(json.dumps({"summary": summary, "levels": rows}, indent=1))
    print(json.dumps(summary, indent=1))


if __name__ == "__main__":
    main()

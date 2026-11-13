"""Measure Spyro 2 retail level budgets for the Season of Ice port (read-only).

  python tools/soi/s2_budgets.py [--disc data/disc] [--inspect data/soi/ps1/inspect-all-levels.txt]

Inputs: the extracted retail copy (WAD.WAD, SCUS_944.25) and the output of the project's existing
`spyro_inspect.exe <disc> --all-levels` (terrain/collision/object counts per scene). Level names are
read from the executable's own table at 800649b4 (29 campaign names, slot = 15 + 2 * index).
WAD slots are read the way the executable does (80011b70: 197 x (offset, size) at LBA 500).
"""
from __future__ import annotations

import argparse
import json
import re
import statistics
import struct
import sys
from pathlib import Path

PROJECT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT / "tools"))
from spyrodisc import wad_index  # noqa: E402

EXE_BASE = 0x80010000
NAMES_TABLE = 0x800649B4


def exe_names(exe: bytes) -> list[str]:
    off = lambda a: a - EXE_BASE + 0x800
    names = []
    for k in range(29):
        p = struct.unpack_from("<I", exe, off(NAMES_TABLE + 4 * k))[0]
        names.append(exe[off(p):off(p) + 40].split(b"\0")[0].decode("latin1"))
    return names


def parse_inspect(text: str) -> dict[int, dict]:
    levels, pending = {}, {}
    cur = None
    for line in text.splitlines():
        if m := re.match(r"\s*Sky: (\d+) regions, (\d+) triangles", line):
            pending = dict(sky_regions=int(m[1]), sky_triangles=int(m[2]))
        elif m := re.match(r"\s*Terrain: (\d+) regions, (\d+) faces, (\d+) native triangles", line):
            pending.update(terrain_regions=int(m[1]), terrain_faces=int(m[2]), terrain_triangles=int(m[3]))
        elif m := re.match(r"\s*Terrain materials: (\d+); (\d+) translucent, (\d+) blended", line):
            pending.update(terrain_materials=int(m[1]))
        elif m := re.match(r"Level WAD (\d+): (\d+) collision triangles", line):
            cur = int(m[1])
            levels[cur] = dict(pending, collision_triangles=int(m[2]))
            pending = {}
        elif cur is not None and (m := re.match(r"\s*Objects: (\d+) records, (\d+) loose gems \(value (\d+)\); models (\d+) / (\d+) static, (\d+) meshes, (\d+) faces, (\d+) texture palettes", line)):
            levels[cur].update(object_records=int(m[1]), loose_gems=int(m[2]), loose_gem_value=int(m[3]),
                               model_classes=int(m[4]), static_classes=int(m[5]), static_meshes=int(m[6]),
                               static_faces=int(m[7]), texture_palettes=int(m[8]))
        elif cur is not None and (m := re.match(r"\s*Animated objects: (\d+) classes, (\d+) clips, (\d+) decoded frames", line)):
            levels[cur].update(animated_classes=int(m[1]), animated_clips=int(m[2]), animated_frames=int(m[3]))
    return levels


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--disc", type=Path, default=PROJECT / "data" / "disc")
    ap.add_argument("--inspect", type=Path, default=PROJECT / "data" / "soi" / "ps1" / "inspect-all-levels.txt")
    ap.add_argument("--out", type=Path, default=PROJECT / "data" / "soi" / "ps1" / "s2_budgets.json")
    a = ap.parse_args()
    exe = (a.disc / "SCUS_944.25").read_bytes()
    names = exe_names(exe)
    wad = (a.disc / "WAD.WAD").open("rb")
    head = wad.read(0x800)
    toc = [struct.unpack_from("<II", head, 8 * i) for i in range(197)]
    stats = parse_inspect(a.inspect.read_text(encoding="utf-8", errors="replace"))
    rows = []
    for i, name in enumerate(names):
        slot = 15 + 2 * i
        off, size = toc[slot]
        wad.seek(off)
        children = [dict(index=e.index, offset=e.offset, size=e.size) for e in wad_index(wad.read(0x10000), size)]
        ov_off, ov_size = toc[slot - 1]
        row = dict(campaign_index=i, name=name, scene_slot=slot, scene_bytes=size,
                   overlay_slot=slot - 1, overlay_bytes=ov_size,
                   children_bytes=[c["size"] for c in children[:4]], **stats.get(slot, {}))
        rows.append(row)
    keys = ["scene_bytes", "overlay_bytes", "terrain_faces", "terrain_triangles", "terrain_regions",
            "terrain_materials", "collision_triangles", "sky_triangles", "object_records",
            "model_classes", "texture_palettes", "loose_gems", "animated_classes"]
    summary = {}
    for k in keys:
        v = [r[k] for r in rows if k in r]
        summary[k] = dict(min=min(v), median=statistics.median(v), max=max(v),
                          max_level=max(rows, key=lambda r: r.get(k, -1))["name"])
    child = {}
    for j, label in enumerate(["vram_image", "scene", "model_bank", "gameplay"]):
        v = [r["children_bytes"][j] for r in rows if len(r["children_bytes"]) > j]
        child[label] = dict(min=min(v), median=statistics.median(v), max=max(v))
    out = dict(source=dict(disc=str(a.disc), inspect=str(a.inspect)), wad_slots=197,
               levels=rows, summary=summary, scene_children=child)
    a.out.parent.mkdir(parents=True, exist_ok=True)
    a.out.write_text(json.dumps(out, indent=1))
    for k, v in summary.items():
        print(f"{k:22s} min {v['min']:>10} median {v['median']:>10} max {v['max']:>10} ({v['max_level']})")
    for k, v in child.items():
        print(f"child {k:16s} min {v['min']:>10} median {v['median']:>10} max {v['max']:>10}")
    print("wrote", a.out)


if __name__ == "__main__":
    main()

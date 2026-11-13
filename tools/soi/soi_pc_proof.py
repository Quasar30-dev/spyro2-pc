"""Scripted hidden PC proof runs of Market Mesa (Season of Ice) in the private exe copy.

  python tools/soi/soi_pc_proof.py probe ROUTE FRAMES...   # player position after N frames (tuning)
  python tools/soi/soi_pc_proof.py shoot ROUTE [--seq]     # the scripted run; shots in data/soi/pc/shots/

Runs build-soi/spyro_workbench.exe (a private copy; build/Release is never run) with
--custom-level data/soi/pc/market-mesa.s2world and an S2SHOT file (src/desktop_capture_shot.hpp):
a route of waypoints the real player controller walks, and timed button actions (jump, flame).
Bounded runs never show the workbench window (desktop.cpp shows it only without --frames).
Coordinates are SoI units (x, y, z); E() is the exporter's mapping (s2world_export.py).
"""
from __future__ import annotations

import argparse
import json
import math
import shutil
import subprocess
import sys
import time
from pathlib import Path

import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parents[1]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(PROJECT / 'tools'))
from s2world_export import E  # noqa: E402

EXE = PROJECT / "build-soi/spyro_workbench.exe"
WORLD = PROJECT / "data/soi/pc/market-mesa.s2world"
SHOTS = PROJECT / "data/soi/pc/shots"
SCRATCH = Path(r"C:\Users\Shadow\AppData\Local\Temp\claude\C--Users-Shadow-Documents-Theia"
               r"\54661888-0620-48ae-b3d6-5a75bbb8c40f\scratchpad\soi")


def eng(p):
    x, y, z = p
    X, Y, Z = E(x, y, z)
    return (X, Y + 0.05, Z)


def yaw_to(a, b):
    A, B = eng(a), eng(b)
    return math.atan2(B[0] - A[0], B[2] - A[2])


# Each route: spawn and look-at point (SoI units), waypoints, actions (begin, end, kind), frames.
# Kinds: jump (held for the span), flame (pressed at begin).
ROUTES = {
    # The staircase below the top terrace: meet the Rhynoc stand-in (record 17, small lizard 0x1F0
    # at (690, 71, 480)), flame it, walk down three terraces (481 -> 457 -> 433) collecting their
    # gems, jump the 16-unit gap over the 385 trench onto the 433 block, drop to 385 and jump up
    # one terrace to the 409 step's gem.
    "mesa": dict(
        spawn=(664, 86, 481), look=(690, 71, 480),
        route=[(684, 74, 481), (680, 108, 481), (704, 110, 457), (728, 104, 433), (712, 148, 433),
               (708, 154, 433), (714, 140, 433), (736, 150, 385), (764, 130, 385), (758, 110, 409)],
        actions=[(14, 15, "flame"), (30, 31, "flame"), (153, 156, "jump"), (288, 304, "jump")], frames=420),
}


def shot_text(r: dict, frames: int, hud=True, fov=50, mode=0, cameras=(), size=(1280, 720)) -> str:
    sp = eng(r["spawn"])
    lines = ["S2SHOT 4", f"{size[0]} {size[1]} {frames}",
             " ".join(f"{v:.4f}" for v in sp) + f" {yaw_to(r['spawn'], r['look']):.4f} {int(hud)} {fov} {mode}"]
    route = [sp] + [eng(p) for p in r["route"]]
    lines.append(str(len(route)))
    lines += [" ".join(f"{v:.4f}" for v in p) for p in route]
    acts = [a for a in r["actions"] if a[0] < frames]
    lines.append(str(len(acts)))
    for b, e, kind in acts:
        e = min(e, frames)
        lines.append(f"{b} {e} 0 {int(kind == 'jump')} {int(kind == 'flame')} 0 0 0 0 0 0 0 0 0 0 0")
    lines.append(str(len(cameras)))
    for t, eye, tgt in cameras:
        lines.append(f"{t} " + " ".join(f"{v:.4f}" for v in (*eye, *tgt)))
    return "\n".join(lines) + "\n"


def run(name: str, text: str, out_bmp: Path, sequence: Path | None = None, timeout=300,
        world: Path | None = WORLD, slot: int | None = None) -> dict:
    SCRATCH.mkdir(parents=True, exist_ok=True)
    shot = SCRATCH / f"{name}.s2shot"
    shot.write_text(text)
    cmd = [str(EXE), "--game", str(PROJECT / "data/disc")]
    cmd += ["--slot", str(slot)] if slot else ["--custom-level", str(world)]
    cmd += ["--cinematic-shot", str(shot), "--capture", str(out_bmp)]
    if sequence:
        sequence.mkdir(parents=True, exist_ok=True)
        cmd += ["--sequence", str(sequence)]
    t = time.time()
    p = subprocess.run(cmd, cwd=PROJECT, creationflags=subprocess.CREATE_NO_WINDOW,
                       capture_output=True, text=True, timeout=timeout)
    if p.returncode:
        raise SystemExit(f"run failed ({p.returncode}): {p.stdout[-2000:]} {p.stderr[-2000:]}")
    rep = json.loads(out_bmp.with_suffix(".json").read_text())
    rep["_seconds"] = round(time.time() - t, 1)
    return rep


def soi_of(P):
    """Engine position -> SoI units (inverse of eng without the lift)."""
    from s2world_export import H, V
    return (P[0] / H, -P[2] / H, P[1] / V)


def iso_eye(target, dist):
    """SoI's projection looks along (1, 1, 2) in SoI units: x' = 2(x - y), y' = x + y - z."""
    t = np.array(target, float)
    return tuple(t + dist * np.array([1.0, 1.0, 2.0]) / math.sqrt(6))


VIEWS = {
    # name: (eye SoI, target SoI, fov)
    "iso": (None, (560, 230, 200), 22),
    "front": ((900, 820, 520), (560, 230, 200), 50),
    "left": ((180, 760, 480), (580, 260, 220), 50),
    "back": ((300, -350, 640), (600, 250, 220), 50),
    "right": ((1200, 120, 520), (600, 300, 220), 50),
    "below": ((820, 700, -260), (600, 300, 60), 55),
    "pueblo": ((760, 420, 330), (560, 190, 330), 55),
    "plaza": ((820, 560, 230), (620, 420, 170), 60),
}


def views(names, frames_per=6, size=(1280, 720), prefix="v2", out_dir=None):  # all views of one run share a fov
    """One hidden run, a fixed camera per view (camera_mode 1), the frame sequence kept."""
    keys, picks = [], {}
    t = 4
    for nm in names:
        eye, tgt, fov = VIEWS[nm]
        if eye is None:
            eye = iso_eye(tgt, 2600)
        e, g = eng(eye), eng(tgt)
        keys += [(t, e, g), (t + frames_per - 1, e, g)]
        picks[nm] = (t + frames_per // 2) // 2 * 2
        t += frames_per + 2
    r = dict(spawn=(644, 603, 169), look=(700, 603, 169), route=[], actions=[])
    text = shot_text(r, t + 2, hud=False, fov=VIEWS[names[0]][2], mode=1, cameras=keys, size=size)
    seq = SCRATCH / "views_seq"
    run("views", text, SCRATCH / "views.bmp", seq)
    out = {}
    for nm, fr in picks.items():
        src = seq / f"frame_{fr:05d}.bmp"
        dst = (out_dir or SHOTS) / f"{prefix}-{nm}.png"
        Image.open(src).convert("RGB").save(dst)
        out[nm] = str(dst)
    return out


def camera_run(tag, spawn_eng, cams, fov=55, world=WORLD, slot=None, size=(960, 540)):
    """Fixed engine-space cameras [(eye, target)] in one hidden run; returns PIL images."""
    keys, picks, t = [], [], 4
    for e, g in cams:
        keys += [(t, e, g), (t + 5, e, g)]
        picks.append((t + 3) // 2 * 2)
        t += 8
    sp = " ".join(f"{v:.4f}" for v in spawn_eng)
    lines = ["S2SHOT 4", f"{size[0]} {size[1]} {t + 2}", f"{sp} 0 0 {fov} 1", "1", sp, "0", str(len(keys))]
    lines += [f"{k} " + " ".join(f"{v:.4f}" for v in (*e, *g)) for k, e, g in keys]
    seq = SCRATCH / f"seq_{tag}"
    run(tag, "\n".join(lines) + "\n", SCRATCH / f"{tag}.bmp", seq, world=world, slot=slot)
    return [Image.open(seq / f"frame_{fr:05d}.bmp").convert("RGB") for fr in picks]


def spots(spawn, yaw):
    """Comparable cameras from a level's start: ground level and elevated, along the start heading."""
    f = np.array([math.sin(yaw), 0, math.cos(yaw)])
    s = np.array(spawn)
    up = np.array([0, 1.0, 0])
    return [(tuple(s - 3 * f + 2.4 * up), tuple(s + 10 * f + 1.0 * up)),
            (tuple(s - 10 * f + 10 * up), tuple(s + 18 * f))]


def retail_start(slot):
    from s2scene import read_archive
    import struct
    sys.path.insert(0, str(PROJECT / "tools"))
    from spyrodisc import wad_index
    arc = read_archive(PROJECT / "data/disc", slot)
    ch = wad_index(arc[:0x10000], len(arc))
    g = arc[ch[3].offset:]
    x, y, z = struct.unpack_from("<3i", g, 0)
    yaw = math.pi / 2 - g[14] * 16 * 2 * math.pi / 4096
    return (x / 1024, z / 1024 - 0x164 / 1024, y / 1024), yaw


def label(im, text):
    from PIL import ImageDraw
    im = im.copy()
    d = ImageDraw.Draw(im)
    d.rectangle((0, 0, 8 + 7 * len(text), 18), fill=(0, 0, 0))
    d.text((4, 3), text, fill=(255, 255, 0))
    return im


def grid(rows, path, w=640):
    cells = [[label(im.resize((w, int(im.height * w / im.width))), t) for im, t in row] for row in rows]
    hh = [max(c.height for c in r) for r in cells]
    cols = max(len(r) for r in cells)
    out = Image.new("RGB", (cols * (w + 6), sum(hh) + 6 * len(hh)), (24, 24, 28))
    y = 0
    for r, h in zip(cells, hh):
        for k, c in enumerate(r):
            out.paste(c, (k * (w + 6), y))
        y += h + 6
    out.save(path)
    return path


def sheets(tag):
    """Before/after (blockout vs current) from the same spots, SoI's isometric angle beside the
    original map, and a retail comparison (Glimmer, Shady Oasis) from comparable start spots."""
    sx, sy, sz = 644, 603, 169
    mm_spawn = eng((sx, sy, sz))
    mm_cams = spots(mm_spawn, 0.785)
    tgt = (720, 230, 200)
    iso = (eng(iso_eye(tgt, 3300)), eng(tgt))
    old = camera_run("before", mm_spawn, mm_cams, world=PROJECT / "data/soi/pc/market-mesa-blockout.s2world")
    new = camera_run("after", mm_spawn, mm_cams)
    iso_old = camera_run("before_iso", mm_spawn, [iso], fov=22, world=PROJECT / "data/soi/pc/market-mesa-blockout.s2world")[0]
    iso_new = camera_run("after_iso", mm_spawn, [iso], fov=22)[0]
    soi_map = Image.open(PROJECT / "data/soi/levels/06-market-mesa/map_half.png").convert("RGB")
    a = grid([[(old[0], "before: ground"), (new[0], f"{tag}: ground")],
              [(old[1], "before: elevated"), (new[1], f"{tag}: elevated")],
              [(iso_old, "before: SoI angle"), (iso_new, f"{tag}: SoI angle"), (soi_map, "SoI original map (GBA)")]],
             SHOTS / f"sheet-{tag}-before-after.png")
    rows = []
    for slot, name in ((17, "Glimmer"), (47, "Shady Oasis")):
        sp, yaw = retail_start(slot)
        if slot == 47:
            yaw += math.pi   # Shady Oasis' start faces a wall: look back over the courtyard
        ims = camera_run(f"retail{slot}", sp, spots(sp, yaw), slot=slot)
        rows.append([(ims[0], f"retail {name}: ground"), (ims[1], f"retail {name}: elevated")])
    rows.append([(new[0], f"Market Mesa {tag}: ground"), (new[1], f"Market Mesa {tag}: elevated")])
    b = grid(rows, SHOTS / f"sheet-{tag}-vs-retail.png")
    return [str(a), str(b)]


SHADY_CAMS = [((40.0, 8.5, 64.0), (46.0, 6.0, 88.0)),      # Shady Oasis' central courtyard, standing
              ((44.0, 18.0, 60.0), (44.0, 6.0, 95.0))]    # the same, elevated (hand-placed spots)


def sheets3(tag, before_world: Path, before_tag="v2e"):
    """The three review sheets: before / after (ground, elevated, building and plaza close-ups),
    versus retail (Glimmer from its start, Shady Oasis from hand-placed courtyard spots), and SoI's
    angle beside the original map."""
    sx, sy, sz = 644, 603, 169
    mm_spawn = eng((sx, sy, sz))
    cams = spots(mm_spawn, 0.785)
    close = [(eng(VIEWS["pueblo"][0]), eng(VIEWS["pueblo"][1])), (eng((760, 600, 178)), eng((700, 520, 166)))]
    old = camera_run("before", mm_spawn, cams + close, world=before_world)
    new = camera_run("after", mm_spawn, cams + close)
    names = ["ground", "elevated", "buildings", "plaza close-up"]
    a = grid([[(old[k], f"{before_tag}: {names[k]}"), (new[k], f"{tag}: {names[k]}")] for k in range(4)],
             SHOTS / f"sheet-{tag}-before-after.png")
    rows = []
    sp, yaw = retail_start(17)
    g = camera_run("retail17", sp, spots(sp, yaw), slot=17)
    rows.append([(g[0], "retail Glimmer: ground"), (g[1], "retail Glimmer: elevated")])
    sp, _ = retail_start(47)
    so = camera_run("retail47", sp, SHADY_CAMS, slot=47)
    rows.append([(so[0], "retail Shady Oasis: courtyard, ground"), (so[1], "retail Shady Oasis: courtyard, elevated")])
    rows.append([(new[0], f"Market Mesa {tag}: ground"), (new[1], f"Market Mesa {tag}: elevated")])
    rows.append([(new[2], f"Market Mesa {tag}: buildings"), (new[3], f"Market Mesa {tag}: plaza close-up")])
    b = grid(rows, SHOTS / f"sheet-{tag}-vs-retail.png")
    tgt = (720, 230, 200)
    iso_new = camera_run("after_iso", mm_spawn, [(eng(iso_eye(tgt, 2100)), eng(tgt))], fov=22, size=(1200, 800))[0]
    soi_map = Image.open(PROJECT / "data/soi/levels/06-market-mesa/map_half.png").convert("RGB")
    c = grid([[(iso_new, f"{tag}: SoI angle (PC, hidden run)"), (soi_map, "SoI original map (GBA)")]],
             SHOTS / f"sheet-{tag}-soi-angle.png", w=960)
    return [str(a), str(b), str(c)]


def gap_hunt(tag):
    """360-degree sets (8 headings, level and looking down) from reachable spots, one sheet."""
    spots_soi = [("plaza", (700, 560, 169)), ("roof", (600, 200, 433)), ("bridge", (420, 100, 220)),
                 ("cliff edge", (880, 600, 145))]
    rows = []
    for name, p in spots_soi:
        e = np.array(eng(p)) + np.array([0, 1.4, 0])
        cams = []
        for k in range(8):
            a = 2 * math.pi * k / 8
            d = np.array([math.sin(a), -0.35, math.cos(a)])
            cams.append((tuple(e), tuple(e + d * 10)))
        ims = camera_run(f"gap_{name.replace(' ', '_')}", tuple(e - np.array([0, 1.4, 0])), cams, fov=70, size=(480, 270))
        rows.append([(im, f"{name} {k * 45} deg") for k, im in enumerate(ims)])
    return str(grid(rows, SHOTS / f"sheet-{tag}-gap-hunt.png", w=320))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mode", choices=["probe", "shoot", "views", "sheets", "sheets3"])
    ap.add_argument("route")
    ap.add_argument("frames", nargs="*", type=int)
    ap.add_argument("--seq", action="store_true")
    ap.add_argument("--before", type=Path, help="the previous iteration's .s2world for sheets3")
    a = ap.parse_args()
    if a.mode == "sheets":
        print(sheets(a.route), gap_hunt(a.route))
        return
    if a.mode == "sheets3":
        print(sheets3(a.route, a.before), gap_hunt(a.route))
        return
    if a.mode == "views":
        names = a.route.split(",") if a.route != "all" else list(VIEWS)
        for fov in sorted({VIEWS[n][2] for n in names}):
            print(json.dumps(views([n for n in names if VIEWS[n][2] == fov]), indent=1))
        return
    r = ROUTES[a.route]
    if a.mode == "probe":
        for f in a.frames:
            out = SCRATCH / f"probe_{a.route}_{f}.bmp"
            rep = run(f"probe_{a.route}", shot_text(r, f, size=(640, 360)), out)
            s = soi_of(rep["player_position"])
            print(f"frame {f}: soi ({s[0]:.1f}, {s[1]:.1f}, {s[2]:.1f}) gems {rep['gem_total']} "
                  f"health {rep['health']} grounded {rep.get('grounded_capture_frames')} t {rep['_seconds']}s")
    else:
        out = SCRATCH / f"shoot_{a.route}.bmp"
        seq = SCRATCH / f"seq_{a.route}" if a.seq else None
        rep = run(f"shoot_{a.route}", shot_text(r, r["frames"]), out, seq)
        print(json.dumps({k: rep[k] for k in ("frames", "gem_total", "collected_retail_gems", "health",
                                               "player_position", "mapped_enemies", "_seconds")}))


if __name__ == "__main__":
    main()

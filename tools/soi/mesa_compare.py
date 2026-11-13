"""PS1 (DuckStation) vs PC sheet for Market Mesa from the same cameras.

Reads the play run's views (data/soi/ps1/ds/<name>-views.json: the PS1 camera 0x80067eac and Spyro 0x80069ff0 at
each shot), converts them to engine space with the encoder's offset (retail = (x H, -y H, z V) * 1024 + offset;
engine = ((rx - ox), (rz - oz), (ry - oy)) / 1024) and renders the PC level from those cameras with the private
exe copy (soi_pc_proof.camera_run, hidden). Output: data/soi/ps1/ds/shots/<name>-vs-pc.png.

    python tools/soi/mesa_compare.py [NAME]
"""
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import soi_pc_proof as pc  # noqa: E402

PROJECT = HERE.parents[1]
OUT = PROJECT / "data/soi/ps1/ds"


MOD_CUE = "D:/Theia-staging/spyro2-mod/Spyro2-SoI-MarketMesa-test02.cue"


def camera_run_disc(tag, spawn_eng, cams, game, slot=17, fov=50, size=(640, 480), run=None):
    """soi_pc_proof.camera_run, but the PC build loads `slot` from `game` (the mod disc's BIN/CUE): Market Mesa
    through the PC's own retail loader, not the S2WORLD custom level."""
    import subprocess
    keys, picks, t = [], [], 4
    for e, g in cams:
        keys += [(t, e, g), (t + 5, e, g)]
        picks.append((t + 3) // 2 * 2)
        t += 8
    sp = " ".join(f"{v:.4f}" for v in spawn_eng)
    lines = ["S2SHOT 4", f"{size[0]} {size[1]} {t + 2}", f"{sp} 0 0 {fov} 1", "1", sp, "0", str(len(keys))]
    lines += [f"{k} " + " ".join(f"{v:.4f}" for v in (*e, *g)) for k, e, g in keys]
    shot = pc.SCRATCH / f"{tag}.s2shot"
    shot.write_text(chr(10).join(lines) + chr(10))
    seq = pc.SCRATCH / f"seq_{tag}"
    seq.mkdir(parents=True, exist_ok=True)
    out = pc.SCRATCH / f"{tag}.bmp"
    cmd = [str(pc.EXE), "--game", str(game), "--slot", str(slot), "--cinematic-shot", str(shot), "--capture", str(out),
           "--sequence", str(seq)]
    p = (run or subprocess.run)(cmd, cwd=PROJECT, creationflags=subprocess.CREATE_NO_WINDOW, capture_output=True, text=True,
                       timeout=300)
    if p.returncode:
        raise SystemExit(f"PC run failed ({p.returncode}): {p.stdout[-1500:]} {p.stderr[-1500:]}")
    rep = json.loads(out.with_suffix(".json").read_text())
    return [Image.open(seq / f"frame_{fr:05d}.bmp").convert("RGB") for fr in picks], rep


def main(name="mesa-play", game=MOD_CUE):
    views = json.loads((OUT / f"{name}-views.json").read_text())
    off = np.array(json.loads((PROJECT / "data/soi/ps1/mesa-ps1-report.json").read_text())["frame_offset"])

    def eng(r):
        """Retail units -> PC engine space. Loaded from the disc (--slot 17) the PC keeps retail coordinates
        (world_position: (x, z, y) / 1024); the S2WORLD custom level uses the offset-free frame."""
        r = np.asarray(r, float)
        return np.array([r[0], r[2], r[1]]) / 1024.0

    cams = [(eng(v["camera"]), eng(v["spyro"]) + np.array([0, 0.35, 0])) for v in views]
    pc.SCRATCH = PROJECT / "tmp/soi4/pc"          # scratch for the hidden PC runs (listed for the user)
    pc.SCRATCH.mkdir(parents=True, exist_ok=True)
    ims, prep = camera_run_disc(f"{name}_pcdisc", tuple(eng(views[0]["spyro"])), [tuple(map(tuple, c)) for c in cams],
                                game)
    print("PC run on the mod disc:", json.dumps({k: prep.get(k) for k in ("level", "slot", "scene", "player_position",
                                                                          "gem_total", "frames", "terrain_faces")}))
    w, h = 480, 360
    sheet = Image.new("RGB", (2 * w, h * len(views)), (20, 20, 20))
    dr = ImageDraw.Draw(sheet)
    for k, v in enumerate(views):
        sheet.paste(Image.open(v["path"]).convert("RGB").resize((w, h)), (0, k * h))
        sheet.paste(ims[k].resize((w, h)), (w, k * h))
        for x, lab in ((0, f"PS1 DuckStation, {Path(game).stem}: {v['tag']}"), (w, f"PC build, same disc, same camera: {v['tag']}")):
            dr.rectangle([x, k * h, x + 7 * len(lab) + 6, k * h + 14], fill=(0, 0, 0))
            dr.text((x + 3, k * h + 1), lab, fill=(255, 255, 0))
    out = OUT / "shots" / f"{name}-vs-pc.png"
    sheet.save(out)
    print(out)
    import time as _t
    log = OUT / "logs" / f"pc-{name}-{_t.strftime('%Y%m%d-%H%M%S')}.log"
    ok = len(ims) == len(views) and prep.get("frames", 0) > 0
    nl = chr(10)
    log.write_text(f"PC build {pc.EXE} --game {game} --slot 17 (hidden S2SHOT, {len(views)} cameras from the PS1 run)" + nl
                   + f"report: {json.dumps({k: prep.get(k) for k in ('frames', 'player_position', 'gem_total')})}" + nl
                   + f"sheet: {out}" + nl
                   + f"PC RUN {'PASS' if ok else 'FAIL'}: Market Mesa loaded from the mod disc's slot 17, {len(ims)} frames" + nl)
    print(log)


if __name__ == "__main__":
    main(*sys.argv[1:])

"""Far-terrain probe for the Season of Ice PS1 mod (TEST FIXTURE: hidden DuckStation, input and reads over GDB).

Boots a disc image hidden, reaches Glimmer's slot (Market Mesa) by pad input, takes the start shot, then dumps
what the PS1 really has while it draws the distant islands:
  - a DuckStation save state (F2, the private profile's SaveSelectedSaveState hotkey); its GPU section carries the
    whole 1024 x 512 VRAM, located by matching the archive's own VRAM image (`vram_from_state`);
  - main RAM (2 MB) over GDB, paused at the controller logic tick;
  - the far-renderer globals: the fade factor 0x80067ec4 (IR0 of the far / near DPCS, 80024568 / 80028d8c), the
    far colours 0x80067408..10 (RFC / GFC / BFC there) and the region pointer table 0x800673d4.

    python tools/soi/ds_far_probe.py CUE NAME [--turn R1xN]

Outputs: data/soi/ps1/ds/logs/<NAME>-*.log, shots in data/soi/ps1/ds/shots/, dumps in
D:/Theia-staging/spyro2-mod/work/probe/<NAME>/ (ram.bin, state.sav, vram.bin).
"""
from __future__ import annotations

import struct
import sys
import time
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import ds_harness as d  # noqa: E402
import ds_pad_proof as pp  # noqa: E402

WORK = Path("D:/Theia-staging/spyro2-mod/work/probe")
STATES = d.EXE.parent / "savestates"


def save_state(s: d.Session, dest: Path, timeout=20.0):
    """F2 on the hidden display window; copies the newest .sav when DuckStation has finished writing it."""
    STATES.mkdir(exist_ok=True)
    before = {p: p.stat().st_mtime for p in STATES.glob("*")}
    d.post_key(s.win, 0x71)                     # VK_F2
    t0 = time.time()
    last = None
    while time.time() - t0 < timeout:
        new = [p for p in STATES.glob("*.sav") if p not in before or p.stat().st_mtime != before[p]]
        if new:
            p = max(new, key=lambda q: q.stat().st_mtime)
            size = p.stat().st_size
            if last == size and size > 0:
                try:
                    data = p.read_bytes()
                except PermissionError:
                    time.sleep(0.3)
                    continue
                dest.parent.mkdir(parents=True, exist_ok=True)
                dest.write_bytes(data)
                return dest
            last = size
        time.sleep(0.5)
    return None


def state_payloads(raw: bytes):
    """Candidate decompressed payloads of a DuckStation save state (zstd frames, raw deflate / zlib, or plain)."""
    out = [raw]
    i = raw.find(b"\x28\xb5\x2f\xfd")
    while i >= 0:
        try:
            import zstandard
            out.append(zstandard.ZstdDecompressor().decompress(raw[i:], max_output_size=64 << 20))
        except Exception:  # noqa: BLE001
            pass
        i = raw.find(b"\x28\xb5\x2f\xfd", i + 4)
    import zlib
    for k in range(0, min(len(raw), 4096)):
        for wb in (-15, 15):
            try:
                dec = zlib.decompressobj(wb).decompress(raw[k:])
                if len(dec) > 1 << 20:
                    out.append(dec)
                    return out
            except zlib.error:
                pass
    return out


def vram_from_state(raw: bytes, ref_vram: np.ndarray):
    """The 1024 x 512 VRAM inside a save state, located by a row of the archive's VRAM image (x 512..1023).
    Returns (vram u16 array, payload index, base offset) or None."""
    probes = []
    for y in (300, 200, 400, 100, 40):
        row = ref_vram[y, 64:128]
        if len(set(row.tolist())) > 8:
            probes.append((y, 512 + 64, row.astype("<u2").tobytes()))
    for pi, pay in enumerate(state_payloads(raw)):
        for y, x, pat in probes:
            o = pay.find(pat)
            if o >= 0:
                base = o - (y * 1024 + x) * 2
                if base >= 0 and base + 1024 * 512 * 2 <= len(pay):
                    v = np.frombuffer(pay, "<u2", 1024 * 512, base).reshape(512, 1024)
                    return v, pi, base
    return None


def main(cue: str, name: str, turns: str = ""):
    log = d.Log(name)
    out = WORK / name
    out.mkdir(parents=True, exist_ok=True)
    with d.Session(cue, log) as s:
        t0 = time.time()
        while time.time() - t0 < 12:
            time.sleep(0.25)
        pad = d.Pad(s)
        if not pp.to_glimmer(pad, log, limit=1500):
            log("PROBE FAIL (route)")
            return
        pad.run(0, 20)
        for b in [t for t in turns.split(",") if t]:
            btn, n = b.split("x")
            pad.run(d.buttons(btn), int(n))
            pad.run(0, 10)
        cam = struct.unpack("<iii", pad.g.read(0x80067EAC, 12))
        spy = struct.unpack("<iii", pad.g.read(d.SPYRO, 12))
        log(f"camera {cam} spyro {spy}")
        shot = pad.shot(d.OUT / "shots" / f"{name}-start.png", 0.5)
        log("shot", shot)
        # globals of the far renderer, read at the logic tick
        g = pad.g
        fade = struct.unpack("<i", g.read(0x80067EC4, 4))[0]
        fc = struct.unpack("<3i", g.read(0x80067408, 12))
        log(f"far DPCS fade 0x80067ec4 = {fade} (0: far colours drawn raw), far colour 0x80067408 = {fc}")
        ram = g.read(0x80000000, 0x200000)
        (out / "ram.bin").write_bytes(ram)
        log("ram", out / "ram.bin", len(ram))
        # save state (needs the emulator running), then a second frame for comparison
        pad.release()
        time.sleep(0.5)
        st = save_state(s, out / "state.sav")
        log("save state", st, st.stat().st_size if st else None)
        shot2 = s.screenshot(d.OUT / "shots" / f"{name}-at-state.png")
        log("shot at state", shot2)
    if st:
        sys.path.insert(0, str(HERE.parent))
        from spyrodisc import wad_index  # noqa: E402
        arc = Path("D:/Theia-staging/spyro2-mod/work/slot17-mesa.bin").read_bytes()
        vram_at = struct.unpack_from("<I", arc, 0)[0]
        ref = np.frombuffer(arc, "<u2", 512 * 512, vram_at).reshape(512, 512)
        r = vram_from_state(st.read_bytes(), ref)
        if r is None:
            log("VRAM not located in the save state")
        else:
            v, pi, base = r
            (out / "vram.bin").write_bytes(v.astype("<u2").tobytes())
            right = v[:, 512:]
            same = float((right == ref).mean())
            log(f"VRAM located (payload {pi}, offset {base}); right half equals the archive image on {same * 100:.2f} % "
                f"of halfwords")
            from PIL import Image
            rgb = np.stack([(v & 31), (v >> 5) & 31, (v >> 10) & 31], -1).astype(np.uint8) * 8
            Image.fromarray(rgb).save(out / "vram.png")
            diff = (right != ref)
            Image.fromarray((diff * 255).astype(np.uint8)).save(out / "vram-diff-right.png")
            log("vram", out / "vram.png", "diff mask", out / "vram-diff-right.png")
    log("PROBE DONE")


if __name__ == "__main__":
    a = sys.argv[1:]
    main(a[0], a[1], a[2] if len(a) > 2 else "")

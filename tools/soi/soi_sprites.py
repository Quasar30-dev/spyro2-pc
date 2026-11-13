"""Season of Ice sprite banks: decode and render (read-only ROM).

  python tools/soi/soi_sprites.py [--out data/soi/sprites]

Format, as the ROM's code reads it:
  0801584c(L)  loads sprite slots: 0x00-0x22 shared, 0x23-0x29 per level, each with
               08004e3c(src, size, slot). Sources are THUMB literals in 0801584c (parsed from the
               Ghidra export, data/soi/analysis/ghidra/decompiled.c).
  08004e3c     bank = 12-byte header copied into the slot record (stride 0x18 at state 03000884+0xcac):
               u8 flags0, u8 placement, u8 codec (bits 2-4: 0/1 raw, 2/3 LZ77, 4/5 Huffman; bit 1 = 8bpp),
               "CRS", u16 frames, u16 tiles, u16 bits; then frames*5 (rounded to 4) frame records,
               frames*bits/8 (rounded) presence bits, tiles*32 4bpp tile data.
  0800540c     sprite object: slot = (u16 +8 & 0xfff) >> 6, frame = u16 +4; frame record at
               slot+0xc + frame*5.
  Frame record: u16 first tile, u8 dimension exponents, s8 x, s8 y.
  Width = 8 << ((attr >> 3) & 7), height = 8 << (attr & 7), per 08005284.
  08000628 expands presence bits in stream order. 080003d4 emits large sprites as contiguous
  64x64 blocks (top-left, top-right, bottom-left, bottom-right), each internally row-major.
  The palette is actor state, NOT frame attr. Unknown 4bpp palettes render as labelled greyscale.
  08015134(L)  OBJ palettes: banks 0-7, 14, 15 shared; 8-13 per level.
"""
from __future__ import annotations

import argparse
import json
import pickle
import re
import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parent))
from gba import Rom, huffman, lz77  # noqa: E402

PROJECT = Path(__file__).resolve().parents[2]
DECOMP = PROJECT / "data" / "soi" / "analysis" / "ghidra" / "decompiled.c"
OAM = {(0, 0): (1, 1), (0, 1): (2, 2), (0, 2): (4, 4), (0, 3): (8, 8),
       (1, 0): (2, 1), (1, 1): (4, 1), (1, 2): (4, 2), (1, 3): (8, 4),
       (2, 0): (1, 2), (2, 1): (1, 4), (2, 2): (2, 4), (2, 3): (4, 8)}

# Only initial palettes established by the original actor initializers. Effects and
# semantic animation states may change them; do not infer other actors from slot/frame.
INITIAL_PALETTES = {(6, 0x16): 15, (6, 0x23): 10, (6, 0x25): 11,
                    (6, 0x26): 12, (15, 0x25): 11, (16, 0x25): 11}


def functions() -> dict[str, str]:
    src = DECOMP.read_text(encoding="utf-8")
    return {m.group(1): m.group(3) for m in
            re.finditer(r"^// ==== ([0-9a-f]{8}) (\S+)\n(.*?)(?=^// ==== |\Z)", src, re.S | re.M)}


def lit(r: Rom, name: str) -> int:
    return r.u32(int(name[4:], 16))


def slot_sources(r: Rom, fn: dict) -> dict:
    """{level or None: {slot: (src, size)}} from 0801584c."""
    out: dict = {None: {}}
    case = None
    for line in fn["0801584c"].splitlines():
        if m := re.match(r"\s*case (\w+):", line):
            case = int(m.group(1), 0)
            out[case] = {}
        if m := re.search(r"FUN_08004e3c\((DAT_\w+),(DAT_\w+),(\w+)\)", line):
            out[case][int(m.group(3), 0)] = (lit(r, m.group(1)), lit(r, m.group(2)))
        if m := re.search(r"FUN_08016588\((\w+),(DAT_\w+)\)", line):
            out[case][int(m.group(1), 0)] = (lit(r, m.group(2)), None)
    return out


def obj_palettes(r: Rom, fn: dict) -> dict:
    """{level: 256x3 OBJ palette} following 08015134's statements in order."""
    text = fn["08015134"]
    head, _, body = text.partition("switch(param_1)")
    common = [(int(a, 0), lit(r, b)) for a, b in re.findall(r"FUN_08006dc4\((\w+),(DAT_\w+),0x20\)", head)]
    blocks = re.split(r"\n\s*case (\w+):", body)
    cases = {int(blocks[i], 0): blocks[i + 1] for i in range(1, len(blocks) - 1, 2)}
    label_30 = cases.get(0xC, "")
    label_30 = label_30[label_30.find("LAB_08015730:"):]

    def run(stmts: str, env: dict, loads: list):
        for st in [s.strip() for s in stmts.split(";")]:
            if m := re.search(r"FUN_08006dc4\((\w+),(\w+),0x20\)", st):
                a, b = m.group(1), m.group(2)
                idx = env[a] if a in env else int(a, 0)
                src = env[b] if b in env else lit(r, b)
                loads.append((idx, src))
            elif m := re.search(r"(uVar[12]) = (\w+)$", st):
                v = m.group(2)
                env[m.group(1)] = int(v, 0) if not v.startswith("DAT_") else lit(r, v)
                if v.startswith("DAT_") and m.group(1) == "uVar1":
                    pass
            elif "goto LAB_08015730" in st:
                run(label_30.split(":", 1)[1], env, loads)
                return
            elif "goto LAB_0801573c" in st:
                loads.append((0x1D0, env["uVar1"]))
                return
            elif st.startswith("break"):
                loads.append((env["uVar1"], env["uVar2"]))
                return
            elif "return" in st or "goto switchD" in st:
                return

    pals = {}
    for lv, stmts in cases.items():
        loads = list(common)
        run(stmts.replace("LAB_08015730:", "").replace("LAB_0801573c:", ""), {}, loads)
        pal = np.zeros((256, 3), np.uint8)
        for idx, src in loads:
            p = np.frombuffer(r.bytes(src, 0x20), "<u2").astype(np.uint32)
            rgb = np.stack([(p & 31), (p >> 5) & 31, (p >> 10) & 31], 1) * 255 // 31
            pal[idx - 0x100: idx - 0x100 + 16] = rgb
        pals[lv] = pal
    return pals


def bank(r: Rom, src: int) -> dict:
    h = r.bytes(src, 12)
    if h[3:6] != b"CRS":
        raise ValueError(f"no CRS header at {src:#x}")
    frames, tiles, bits = struct.unpack_from("<3H", h, 6)
    codec = (h[2] & 0x1F) >> 2
    n = frames if h[2] & 0x1C else 1
    fsz, bsz = (n * 5 + 3) & ~3, ((n * bits) >> 3) + 3 & ~3
    tsz = (tiles << 5) << ((h[2] & 3) >> 1)
    if codec in (2, 3):
        data, _ = lz77(r.data, r.off(src + 12))
    elif codec in (4, 5):
        data, _ = huffman(r.data, r.off(src + 12))
    else:
        data = r.bytes(src + 12, fsz + bsz + tsz)
    fr = [struct.unpack_from("<HBbb", data, 5 * i) for i in range(n)]
    pres = np.unpackbits(np.frombuffer(data[fsz:fsz + bsz], np.uint8), bitorder="little")
    t = np.frombuffer(data[fsz + bsz:fsz + bsz + tsz], np.uint8)
    if h[2] & 2:
        til = t.reshape(-1, 8, 8)
    else:
        til = np.zeros((len(t) // 32, 8, 8), np.uint8)
        til[:, :, 0::2] = (t & 15).reshape(-1, 8, 4)
        til[:, :, 1::2] = (t >> 4).reshape(-1, 8, 4)
    if int(pres[:n * bits].sum()) != tiles:
        raise ValueError("presence bits do not match the tile count")
    return dict(frames=fr, bits=bits, presence=pres, tiles=til, bpp8=bool(h[2] & 2), n=n, header=h.hex())


def frame_image(b: dict, i: int, pal: np.ndarray, palette_bank: int | None = None):
    """Reconstruct the full uncropped frame, retaining its original signed origin.

    Presence/DMA stream order and emitted OAM block order are independently
    established by soi_sprite_quad_check.py. An unbound 4bpp palette is greyscale.
    """
    tile0, attr, dx, dy = b["frames"][i]
    width, height = 8 << ((attr >> 3) & 7), 8 << (attr & 7)
    if width > 128 or height > 128 or width * height // 64 > b["bits"]:
        raise ValueError(f"unsupported frame dimensions {width}x{height} in frame {i}")
    if palette_bank is not None and not 0 <= palette_bank < 16:
        raise ValueError("4bpp palette bank must be 0..15")
    img = np.zeros((height, width, 4), np.uint8)
    colours = pal if b["bpp8"] else (pal[palette_bank * 16:(palette_bank + 1) * 16]
              if palette_bank is not None else np.repeat(np.arange(16, dtype=np.uint8)[:, None] * 17, 3, axis=1))
    t = tile0
    base = i * b["bits"]
    cells = [(x, y) for by in range(0, height, 64) for bx in range(0, width, 64)
             for y in range(by, min(by + 64, height), 8)
             for x in range(bx, min(bx + 64, width), 8)]
    for k, (x, y) in enumerate(cells):
        if b["presence"][base + k]:
            if t >= len(b["tiles"]):
                raise ValueError(f"tile stream out of bounds in frame {i}: {t}")
            cell = b["tiles"][t]
            reg = img[y:y + 8, x:x + 8]
            reg[:, :, :3] = colours[cell]
            reg[:, :, 3] = (cell != 0) * 255
            t += 1
        else:
            # Match original zero DMA tile including RGB under transparent pixels.
            img[y:y + 8, x:x + 8, :3] = colours[0]
    return img, dx, dy


def strip(b: dict, pal: np.ndarray, count: int = 6, palette_bank: int | None = None) -> Image.Image:
    n = b["n"]
    picks = sorted({int(round(j * (n - 1) / max(1, count - 1))) for j in range(count)}) if n > 1 else [0]
    ims = [Image.fromarray(frame_image(b, i, pal, palette_bank)[0]) for i in picks]
    W = sum(im.width + 4 for im in ims)
    H = max(im.height for im in ims)
    out = Image.new("RGBA", (W, H), (40, 40, 48, 255))
    x = 0
    for im in ims:
        out.alpha_composite(im, (x, H - im.height))
        x += im.width + 4
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(PROJECT / "data" / "soi" / "sprites"))
    a = ap.parse_args()
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    r = Rom()
    fn = functions()
    srcs = slot_sources(r, fn)
    pals = obj_palettes(r, fn)
    index = {}
    for lv in range(17):
        slots = dict(srcs[None])
        slots.update(srcs.get(lv, {}))
        pal = pals[lv]
        rows = []
        for slot in sorted(slots):
            src, size = slots[slot]
            try:
                b = bank(r, src)
            except Exception as e:  # noqa: BLE001
                rows.append((slot, None, str(e)))
                continue
            palette_bank = INITIAL_PALETTES.get((lv, slot))
            rows.append((slot, strip(b, pal, palette_bank=palette_bank),
                         f"initial palette {palette_bank}" if palette_bank is not None else "unbound palette: greyscale"))
            index.setdefault(f"{src:08x}", dict(slot=slot, frames=b["n"], tiles=len(b["tiles"]),
                                                 header=b["header"], levels=[]))["levels"].append(lv)
        scale = 1
        W = max((im.width for _, im, _ in rows if im), default=100) * scale + 70
        H = sum((im.height * scale if im else 12) + 6 for _, im, _ in rows)
        sheet = Image.new("RGB", (min(W, 2400), H), (24, 24, 28))
        dr = ImageDraw.Draw(sheet)
        y = 0
        for slot, im, note in rows:
            dr.text((4, y + 2), f"{slot:#04x}", fill=(255, 255, 0) if slot >= 0x23 else (200, 200, 200))
            if im:
                big = im.resize((im.width * scale, im.height * scale), Image.NEAREST)
                sheet.paste(big.convert("RGB"), (60, y))
                y += big.height + 6
            else:
                dr.text((60, y + 2), note, fill=(255, 80, 80))
                y += 18
        sheet.save(out / f"level{lv:02d}_slots.png")
    (out / "banks.json").write_text(json.dumps(index, indent=1))
    print("banks", len(index))


if __name__ == "__main__":
    main()


def level_sheet(out: Path, levels=range(17), slots=range(0x23, 0x2A), frames=3, name="level_slots_overview.png"):
    """One row per level: the first-frame-ish strip of each per-level slot, labelled."""
    r = Rom()
    fn = functions()
    srcs = slot_sources(r, fn)
    pals = obj_palettes(r, fn)
    rows = []
    for lv in levels:
        cells = []
        for slot in slots:
            s = srcs.get(lv, {}).get(slot)
            if not s:
                cells.append(None)
                continue
            try:
                b = bank(r, s[0])
                cells.append(strip(b, pals[lv], frames, INITIAL_PALETTES.get((lv, slot))))
            except Exception:  # noqa: BLE001
                cells.append(None)
        rows.append((lv, cells))
    cw = [max((c[i].width if c[i] else 0) for _, c in rows) + 6 for i in range(len(slots))]
    rh = [max((c.height for c in cells if c), default=10) + 6 for _, cells in rows]
    img = Image.new("RGB", (40 + sum(cw), 14 + sum(rh)), (24, 24, 28))
    dr = ImageDraw.Draw(img)
    x = 40
    for i, s in enumerate(slots):
        dr.text((x, 2), f"{s:#04x}", fill=(255, 255, 0))
        x += cw[i]
    y = 14
    for (lv, cells), h in zip(rows, rh):
        dr.text((2, y + 2), f"L{lv:02d}", fill=(220, 220, 220))
        x = 40
        for i, c in enumerate(cells):
            if c:
                img.paste(c.convert("RGB"), (x, y))
            x += cw[i]
        y += h
    img.save(out / name)
    return out / name

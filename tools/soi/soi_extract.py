"""Extract every isometric Spyro: Season of Ice level from the user's own ROM (read-only).

Usage:
  python tools/soi/soi_extract.py [--out data/soi] [--levels 0,6] [--layers]

Every table below is read the way the ROM's own code reads it (addresses are THUMB functions
in the USA ROM, ASYE, SHA-256 d331abe1...; see docs/SEASON_OF_ICE_PORT.md for the evidence):

  08016d38 load_level(L)        calls the per-level loaders below with the same index L (0..16)
  08016780 bg_load(L)           palette  [081ab680+4L] (0x200 B) -> palette RAM
                                tiles    [081ab6c4+4L], size [08061b24+4L] -> VRAM 06000000 (DMA)
                                4 layers [081ab570+16L+4k] LZ77 (BIOS SWI 0x11), sizes [08061a14+16L+4k]
  08005970 bg_layer(k, buf)     12-byte header: u16 flags, u16 prio, u16 w8, u16 h8, u8 mtw, u8 mth,
                                u16 count; flags&3 != 0 -> 'count' metatiles (mtw/8 x mth/8 screen
                                entries, u16 when flags bit2) then the metatile map; char base
                                ((flags>>9)&3)*16 KiB
  08014e30 -> 08013cd0(P)       volumes: P=[081ab49c+4L]: u32 n, u32 n_occ, n x 12-byte records,
                                then edge lists (16 B each: s16 nx, ny; s32 d; s16 x0,y0,x1,y1),
                                then 08013bd4 broadphase grid (s32 cell, s32 cell, s16 cols, rows,
                                cols*rows count bytes, s16 record lists)
  080137e0 -> 08013528/08013380 solids: the grid lists only records 0..n-n_occ-1 (checked for all 17
                                levels). Each is a vertical prism from the floor of the world up to
                                top z = (u32 & 0xffffff) >> 14; edges = byte3 >> 3, 0 -> axis box
                                (the code synthesises its 4 edges); a point is inside when
                                n.p <= d for every edge
  08013e54                      occluders: the last n_occ records, never in the grid. Spawners
                                (e.g. gems, 0801c474) call it to pick the sprite's OAM priority:
                                kind (bits 12-13) 1 -> plane normal (-514,-514,728)/1024 through
                                edge0.p0 at z; kind 3 -> (-724,0,724) if edge0.x0 == edge1.x0 else
                                (0,-724,724), through edge2.p0 at z; a point inside the footprint and
                                below the plane gets priority = kind (drawn behind BG layers)
  08016594 objects(L)           list [081ab74c+4L]: u32 n, n x {u16 type, s16 param, s32 x, y, z}
                                (world units << 10); player start [08061b68+12L]; spawn switch
                                080170a4(type, pos, param)
  0801ec60                      projection: screen_x = 2(x - y), screen_y = x + y - z (BG pixels)
  08027b3c                      level -> game-order id (table 081ac190, literal at 08027b58) -> atlas 081aace0
  08002c50 / 08002b20           text: u16 offsets at 08270434 (relative to it), string = len byte + chars
"""
from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parent))
from gba import BASE, Rom, lz77  # noqa: E402
from soi_families import family  # noqa: E402

LEVELS = 17
T_PAL, T_TILES, T_TILESZ = 0x081AB680, 0x081AB6C4, 0x08061B24
T_LAYERS, T_LAYERSZ = 0x081AB570, 0x08061A14
T_COLL = 0x081AB49C
T_OBJ, T_START = 0x081AB74C, 0x08061B68

LIT_ORDER = 0x08027B58
T_ATLAS = 0x081AACE0
T_TEXT = 0x08270434
TEXT_COUNT = 461
GEM_VALUES = 0x081AB8F0  # read by 0801c474 (types 117..120)

# 080170a4: object type -> spawn handler (the THUMB function the switch calls).
SPAWN = [((0, 0), 0x0801838C), ((1, 1), 0x08018AD0), ((2, 2), 0x0801B7D0), ((3, 3), 0x0801BC38),
         ((10, 10), 0x080210E8), ((11, 11), 0x08020964), ((15, 15), 0x0801CB78),
         ((16, 16), 0x08021EC0), ((21, 21), 0x08024D60), ((40, 81), 0x0801D428),
         ((90, 90), 0x08036D90), ((115, 116), 0x08033B80), ((117, 120), 0x0801C474),
         ((126, 129), 0x0803694C), ((130, 139), 0x0801FBC4), ((140, 144), 0x0801FFE0),
         ((145, 149), 0x08022B38), ((150, 154), 0x08025658), ((155, 159), 0x080233BC),
         ((160, 164), 0x0802BE90), ((165, 169), 0x0802CA0C), ((170, 174), 0x0802E00C),
         ((175, 179), 0x08037980), ((180, 184), 0x0802BE90), ((185, 189), 0x0802C478),
         ((190, 194), 0x0802D330), ((195, 199), 0x0802DA1C), ((200, 204), 0x0802EFD4),
         ((205, 209), 0x08030C10), ((210, 214), 0x080317D4), ((215, 219), 0x0802F7EC),
         ((220, 224), 0x08030608), ((225, 229), 0x0802E90C), ((230, 234), 0x08031FE0),
         ((240, 244), 0x08030044), ((245, 245), 0x080217DC)]


def spawn_handler(t: int):
    for (lo, hi), fn in SPAWN:
        if lo <= t <= hi:
            return fn, lo
    return None, None


PUNCT = {0: " ", 1: "!", 5: "'", 8: ",", 10: ".", 0x16: ":"}


def decode_text(bs: bytes) -> str:
    out = []
    for b in bs:
        if 25 <= b <= 50:
            out.append(chr(b - 25 + 65))
        elif 51 <= b <= 76:
            out.append(chr(b - 51 + 97))
        elif b in PUNCT:
            out.append(PUNCT[b])
        else:
            out.append("{%02x}" % b)
    return "".join(out)


def strings(r: Rom) -> list[str]:
    offs = [r.u16(T_TEXT + 2 * i) for i in range(TEXT_COUNT + 1)]
    res = []
    for k in range(TEXT_COUNT):
        a = T_TEXT + offs[k]
        n = r.u8(a)
        res.append(decode_text(r.bytes(a + 1, n)))
    return res


def level_names(r: Rom, text: list[str]):
    order_tab = r.u32(LIT_ORDER)
    order = list(r.bytes(order_tab, 24))
    atlas = {}
    for world in range(4):
        ids = struct.unpack_from("<12I", r.data, r.off(T_ATLAS + 0x30 * world))
        for slot in range(6):
            atlas[ids[6 + slot]] = (world, slot, ids[slot], text[ids[slot]])
    names = []
    for lv in range(24):
        g = order[lv]
        w, slot, sid, name = atlas.get(g, (None, None, None, None))
        names.append(dict(level=lv, game_order=g, world=w, world_slot=slot, string_id=sid, name=name))
    return names


# ---------------------------------------------------------------- graphics

def palette(r: Rom, lv: int) -> np.ndarray:
    p = np.frombuffer(r.bytes(r.u32(T_PAL + 4 * lv), 0x200), "<u2").astype(np.uint32)
    rgb = np.stack([(p & 31), (p >> 5) & 31, (p >> 10) & 31], 1)
    return ((rgb * 255 + 15) // 31).astype(np.uint8)


def tiles4(r: Rom, lv: int) -> np.ndarray:
    size = r.u32(T_TILESZ + 4 * lv)
    b = np.frombuffer(r.bytes(r.u32(T_TILES + 4 * lv), size), np.uint8)
    t = np.zeros((len(b) // 32, 8, 8), np.uint8)
    t[:, :, 0::2] = (b & 15).reshape(-1, 8, 4)
    t[:, :, 1::2] = (b >> 4).reshape(-1, 8, 4)
    # pad with an empty tile used for out-of-range indices; build the 4 flip variants
    t = np.concatenate([t, np.zeros((1, 8, 8), np.uint8)])
    return np.stack([t, t[:, :, ::-1], t[:, ::-1, :], t[:, ::-1, ::-1]])


def layer_header(buf: bytes) -> dict:
    flags, prio, w8, h8 = struct.unpack_from("<4H", buf, 0)
    return dict(flags=flags, prio=prio, width_px=w8 * 8, height_px=h8 * 8, mt_w=buf[8], mt_h=buf[9],
                count=struct.unpack_from("<H", buf, 10)[0], char_base=(flags >> 9) & 3)


def layer_entries(buf: bytes):
    """Screen-entry grid (rows x cols of u16) for one layer, as 08005970 lays it out."""
    h = layer_header(buf)
    if h["flags"] & 3 == 0:
        n = (len(buf) - 12) // 2
        side = int(round(n ** 0.5))
        return h, np.frombuffer(buf[12:12 + 2 * side * side], "<u2").reshape(side, side)
    mw, mh, cnt = h["mt_w"] // 8, h["mt_h"] // 8, h["count"]
    md = np.frombuffer(buf[12:12 + mw * mh * cnt * 2], "<u2").reshape(cnt, mh, mw)
    cols, rows = h["width_px"] // h["mt_w"], h["height_px"] // h["mt_h"]
    mp = np.frombuffer(buf[12 + mw * mh * cnt * 2:], "<u2")[:cols * rows].reshape(rows, cols)
    if mp.max() >= cnt:
        raise ValueError("metatile index out of range")
    e = md[mp]  # rows, cols, mh, mw
    return h, e.transpose(0, 2, 1, 3).reshape(rows * mh, cols * mw)


def render_entries(ent: np.ndarray, char_base: int, T4: np.ndarray, pal: np.ndarray) -> np.ndarray:
    ti = (ent & 0x3FF).astype(np.int64) + char_base * 512
    ti[ti >= T4.shape[1] - 1] = T4.shape[1] - 1
    flip = ((ent >> 10) & 3).astype(np.int64)
    pb = (ent >> 12).astype(np.int64)
    px = T4[flip, ti]  # R, C, 8, 8
    R, C = ent.shape
    idx = px.transpose(0, 2, 1, 3).reshape(R * 8, C * 8).astype(np.int64)
    bank = np.repeat(np.repeat(pb, 8, 0), 8, 1)
    rgba = np.zeros((R * 8, C * 8, 4), np.uint8)
    rgba[..., :3] = pal[bank * 16 + idx]
    rgba[..., 3] = np.where(idx != 0, 255, 0)
    return rgba


def level_layers(r: Rom, lv: int):
    out = []
    for k in range(4):
        buf, used = lz77(r.data, r.off(r.u32(T_LAYERS + 16 * lv + 4 * k)))
        if len(buf) > r.u32(T_LAYERSZ + 16 * lv + 4 * k):  # the table size is the buffer allocation
            raise ValueError("layer larger than its buffer")
        out.append(buf)
    return out


# ---------------------------------------------------------------- collision

def collision(r: Rom, lv: int) -> dict:
    p = r.u32(T_COLL + 4 * lv)
    n, n_ramps = r.u32(p), r.u32(p + 4)
    q = p + 8
    recs = [struct.unpack_from("<IhhhhI", r.data, r.off(q + 12 * k)) for k in range(n)]
    q += 12 * n
    prisms = []
    for k, (w, x0, y0, x1, y1, _) in enumerate(recs):
        a = w & 0xFFFF
        top = (w & 0xFFFFFF) >> 14
        ne = (w >> 27) & 31
        kind = (a >> 12) & 3
        edges = []
        for j in range(ne):
            nx, ny, d, ex0, ey0, ex1, ey1 = struct.unpack_from("<hhihhhh", r.data, r.off(q))
            q += 16
            edges.append(dict(n=[nx, ny], d=d, p0=[ex0, ey0], p1=[ex1, ey1]))
        if ne:
            poly = [e["p0"] for e in edges]
        else:
            poly = [[x0, y0], [x1, y0], [x1, y1], [x0, y1]]
        solid = k < n - n_ramps
        pr = dict(index=k, raw=w & 0xFFFF, role="solid" if solid else "occluder", kind=kind, top=top,
                  bbox=[x0, y0, x1, y1], polygon=poly, edges=edges)
        if not solid:
            if kind == 1:
                normal, anchor = [-514, -514, 728], edges[0]["p0"]
            else:
                normal = [-724, 0, 724] if edges[0]["p0"][0] == edges[1]["p0"][0] else [0, -724, 724]
                anchor = edges[2]["p0"]
            pr["sprite_priority"] = kind
            pr["plane"] = dict(normal=normal, point=[anchor[0], anchor[1], top])
        prisms.append(pr)
    # 08013bd4: broadphase grid
    g = q
    g0, g1 = r.u32(g), r.u32(g + 4)
    gw, gh = r.s16(g + 8), r.s16(g + 10)
    counts = r.bytes(g + 12, gw * gh)
    lists_bytes = sum(counts) * 2
    listed = struct.unpack_from("<%dh" % sum(counts), r.data, r.off(g + 12 + gw * gh))
    if max(listed) >= n - n_ramps:
        raise ValueError("broadphase grid lists an occluder record")
    return dict(count=n, solids=n - n_ramps, occluders=n_ramps, prisms=prisms,
                grid=dict(origin_or_cell=[g0, g1], cols=gw, rows=gh, cells=gw * gh,
                          max_per_cell=max(counts), list_entries=sum(counts)),
                rom_extent=[p, g + 12 + gw * gh + lists_bytes])


def plane_z(pr: dict, x: float, y: float) -> float:
    nx, ny, nz = pr["plane"]["normal"]
    px, py, pz = pr["plane"]["point"]
    return pz - (nx * (x - px) + ny * (y - py)) / nz


def top_z(pr: dict, x: float, y: float) -> float:
    return plane_z(pr, x, y) if pr["role"] == "occluder" else pr["top"]


# ---------------------------------------------------------------- objects

def objects(r: Rom, lv: int, gem_values) -> dict:
    p = r.u32(T_OBJ + 4 * lv)
    n = r.u32(p)
    ents = []
    for k in range(n):
        t, prm, x, y, z = struct.unpack_from("<Hh3i", r.data, r.off(p + 4 + 16 * k))
        fn, fam = spawn_handler(t & 0xFF)
        e = dict(index=k, type=t, param=prm, raw=[x, y, z], pos=[x / 1024, y / 1024, z / 1024],
                 handler=f"{fn:08x}" if fn else None, family_base=fam)
        if 117 <= t <= 120:
            e["gem_value"] = gem_values[t - 117]
        f = family(t)
        e["family"], e["family_name"], e["s2"], e["s2_placeholder"] = f["family"], f["name"], f["s2"], f["placeholder"]
        ents.append(e)
    sx, sy, sz = struct.unpack_from("<3i", r.data, r.off(T_START + 12 * lv))
    return dict(count=n, start=dict(raw=[sx, sy, sz], pos=[sx / 1024, sy / 1024, sz / 1024]), objects=ents)


def proj(x, y, z):
    return 2 * (x - y), (x + y) - z


# ---------------------------------------------------------------- outputs

def heightmap(coll: dict):
    """Top-down raster, 1 world unit per pixel: highest prism top under each point."""
    solids = [p for p in coll["prisms"] if p["role"] == "solid"]
    xs = [v for p in solids for v in (p["bbox"][0], p["bbox"][2])]
    ys = [v for p in solids for v in (p["bbox"][1], p["bbox"][3])]
    x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
    W, H = x1 - x0 + 1, y1 - y0 + 1
    hm = np.full((H, W), -1.0, np.float32)
    gx, gy = np.meshgrid(np.arange(W) + x0 + 0.5, np.arange(H) + y0 + 0.5)
    for p in solids:
        bx0, by0, bx1, by1 = p["bbox"]
        sl = (slice(by0 - y0, by1 - y0), slice(bx0 - x0, bx1 - x0))
        X, Y = gx[sl], gy[sl]
        inside = np.ones(X.shape, bool)
        if p["edges"]:
            for e in p["edges"]:
                nx, ny = e["n"]
                inside &= (nx * X + ny * Y) / 1024.0 <= e["d"] / 1024.0 + 0.01
        Z = np.full(X.shape, float(p["top"]))
        cur = hm[sl]
        cur[inside] = np.maximum(cur[inside], Z[inside])
    return hm, (x0, y0)


def colour_heights(hm: np.ndarray, lo: float, hi: float) -> np.ndarray:
    import colorsys
    out = np.zeros(hm.shape + (3,), np.uint8)
    valid = hm >= 0
    t = np.clip((hm - lo) / max(1.0, hi - lo), 0, 1)
    lut = np.array([colorsys.hsv_to_rgb(0.66 * (1 - v), 0.75, 0.35 + 0.65 * v) for v in np.linspace(0, 1, 256)])
    out[valid] = (lut[(t[valid] * 255).astype(int)] * 255).astype(np.uint8)
    return out


def slug(s: str) -> str:
    return "".join(c.lower() if c.isalnum() else "-" for c in s).strip("-").replace("--", "-")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(Path(__file__).resolve().parents[2] / "data" / "soi"))
    ap.add_argument("--levels", default=",".join(str(i) for i in range(LEVELS)))
    ap.add_argument("--layers", action="store_true", help="also write each BG layer as its own PNG")
    a = ap.parse_args()
    out = Path(a.out)
    r = Rom()
    import hashlib
    if hashlib.sha256(r.data).hexdigest() != "d331abe19e3f02e2855aa4bbc2de14626810aca83e0c216862eb1d8ca8d024b0":
        raise SystemExit("unexpected ROM (expected Spyro: Season of Ice USA, ASYE)")
    text = strings(r)
    (out / "text").mkdir(parents=True, exist_ok=True)
    (out / "text" / "strings.json").write_text(json.dumps(
        dict(source="ROM 08270434 (u16 offsets relative to the table; len byte + chars)",
             strings=[dict(id=i, text=s) for i, s in enumerate(text)]), indent=1), encoding="utf-8")
    names = level_names(r, text)
    gem_values = list(r.bytes(GEM_VALUES, 4))
    inventory = []
    for lv in [int(x) for x in a.levels.split(",")]:
        nm = names[lv]
        d = out / "levels" / f"{lv:02d}-{slug(nm['name'])}"
        d.mkdir(parents=True, exist_ok=True)
        pal, T4 = palette(r, lv), tiles4(r, lv)
        bufs = level_layers(r, lv)
        layers = []
        comp = None
        for k in (3, 2, 1, 0):
            h, ent = layer_entries(bufs[k])
            img = render_entries(ent, h["char_base"], T4, pal)
            layers.append(dict(layer=k, **{x: h[x] for x in h}, entries=list(ent.shape)))
            if k == 3:
                Image.fromarray(img).save(d / "bg3_backdrop.png")
                continue
            if a.layers:
                Image.fromarray(img).save(d / f"bg{k}.png")
            if comp is None:
                comp = Image.new("RGBA", (img.shape[1], img.shape[0]), (0, 0, 0, 255))
            comp.alpha_composite(Image.fromarray(img))
        comp = comp.convert("RGB")
        comp.save(d / "map.png", optimize=True)
        comp.resize((comp.width // 2, comp.height // 2), Image.LANCZOS).save(d / "map_half.png")
        thumb = comp.resize((300, 200), Image.LANCZOS)
        thumb.save(d / "thumb.png")
        coll = collision(r, lv)
        obj = objects(r, lv, gem_values)
        # overlay: prism tops + objects on the map
        ov = comp.copy()
        dr = ImageDraw.Draw(ov)
        for p in coll["prisms"]:
            if p["role"] == "solid":
                dr.polygon([proj(x, y, p["top"]) for x, y in p["polygon"]], outline=(255, 40, 40))
        for e in obj["objects"]:
            sx, sy = proj(*e["pos"])
            col = (0, 255, 255) if "gem_value" in e else (255, 0, 255)
            dr.ellipse([sx - 5, sy - 5, sx + 5, sy + 5], outline=col, width=2)
        sx, sy = proj(*obj["start"]["pos"])
        dr.rectangle([sx - 7, sy - 7, sx + 7, sy + 7], outline=(255, 255, 255), width=3)
        ov.resize((ov.width // 2, ov.height // 2), Image.LANCZOS).save(d / "overlay_half.png")
        hm, (hx0, hy0) = heightmap(coll)
        valid = hm[hm >= 0]
        lo, hi = float(valid.min()), float(valid.max())
        Image.fromarray(colour_heights(hm, lo, hi)).save(d / "heights_topdown.png")
        hm16 = np.where(hm >= 0, np.round(hm * 64), 0).astype(np.uint16)
        Image.fromarray(hm16).save(d / "heights_topdown_x64.png")
        gems = [e for e in obj["objects"] if "gem_value" in e]
        tcount, fcount = {}, {}
        for e in obj["objects"]:
            tcount[e["type"]] = tcount.get(e["type"], 0) + 1
            key = f"{e['family']}: {e['family_name']}"
            fcount[key] = fcount.get(key, 0) + 1
        area = float((hm >= 0).sum())
        summary = dict(level=lv, name=nm["name"], world=nm["world"], world_slot=nm["world_slot"],
                       game_order=nm["game_order"], dir=d.name,
                       bg_px=[layers[1]["width_px"], layers[1]["height_px"]],
                       footprint_units2=area, xy_extent=[int(hx0), int(hy0), int(hx0 + hm.shape[1]), int(hy0 + hm.shape[0])],
                       height_min=lo, height_max=hi, solids=coll["solids"], occluders=coll["occluders"],
                       height_levels=sorted({p["top"] for p in coll["prisms"] if p["role"] == "solid"}), objects=obj["count"], gems=len(gems),
                       gem_total=sum(e["gem_value"] for e in gems), types=dict(sorted(tcount.items())), families=dict(sorted(fcount.items())),
                       start=obj["start"]["pos"])
        level = dict(summary=summary, layers=layers,
                     heightmap=dict(file="heights_topdown_x64.png", scale="value/64 = world z",
                                    origin_xy=[int(hx0), int(hy0)], unit="1 px = 1 world unit"),
                     collision=coll, objects=obj)
        (d / "level.json").write_text(json.dumps(level, indent=1), encoding="utf-8")
        inventory.append(summary)
        print(f"L{lv:02d} {nm['name']:<22} solids {coll['solids']:3d} occluders {coll['occluders']:3d} "
              f"z {lo:5.0f}..{hi:5.0f} objs {obj['count']:3d} gems {len(gems):3d}={summary['gem_total']:4d} "
              f"tops {len(summary['height_levels'])}")
    if len(inventory) == LEVELS:
        sheet = Image.new("RGB", (5 * 300, 4 * 220), (16, 16, 16))
        dr = ImageDraw.Draw(sheet)
        for s in inventory:
            im = Image.open(out / "levels" / s["dir"] / "thumb.png")
            x, y = (s["level"] % 5) * 300, (s["level"] // 5) * 220
            sheet.paste(im, (x, y))
            dr.text((x + 4, y + 202), f"{s['level']:02d} {s['name']}", fill=(255, 255, 255))
        sheet.save(out / "levels" / "contact_sheet.png")
        (out / "inventory.json").write_text(json.dumps(dict(levels=inventory, names=names), indent=1), encoding="utf-8")


if __name__ == "__main__":
    main()

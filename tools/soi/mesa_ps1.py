"""Market Mesa -> retail Spyro 2 (PS1) level in Glimmer's slot (scene 17, overlay 16): the PS1 encoder.

Quick route (SEASON_OF_ICE_PORT.md Â§10.10 / Â§10.12): Glimmer's archive is the template. Child 0 keeps Glimmer's VRAM
image with Market Mesa's tiles written into Glimmer's terrain texture areas; child 1 (scene) is rebuilt (materials,
terrain regions, conservative visibility, collision) with Glimmer's sky, special-surface table, part 5 and tail; child 2
(models) is Glimmer's; child 3 (gameplay) is Glimmer's with Market Mesa's spawn, the seven terrain-animation
sections emptied (count 0), unsupported actors parked, all 110 loose gems and original object indices retained.

Formats, all read from the retail code (tools/soi/mipsdis.py; addresses in the doc):
  region header  +0 centre y/16, +2 centre x/16, +4 radius/16 (12 bits) | flags, +6 centre z/16, +8 y0/16,
                 +10 x0/16, +12 1 + the near faces of the regions before it (per-face byte buffer, 80024620),
                 +14 z0/16, +16 far counts, +20 near counts + far words, +24 0xffffffff (no animation link)
  near face      bytes 0-3 vertices (strip 3,2,0,1), 4-7 colours, 8-15 four 13-bit neighbour face indices
                 (8002626x; quads: edges (3,2) (2,1) (1,0) (0,3); slot 2 = w2 bits 0-5 + w3 bits 12-18),
                 w3 bits 0-6 material, 7-9 depth bias, 10 flip, 11 two-sided; triangles keep slot 2's high
                 bits in byte 4 >> 1 and a UV quarter turn in w3 bits 12-13
  near colours   two arrays: A (light, 128 = 1.0) then B (flat colour used with distance, 80024c84)
  far face       w0 = a<<24 | b<<17 | c<<10 | d<<3 (7-bit vertices, c == d: triangle, bit 31 two-sided),
                 w1 = colours a<<25 | b<<18 | c<<11 | d<<4 | depth (4 bits) (80028e2c)
  winding        front faces have cross(B - A, C - A) . outward < 0 in retail coordinates (near, far, collision)

    python tools/soi/mesa_ps1.py build      # writes D:/Theia-staging/spyro2-mod/work/slot17-mesa.bin + report
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import struct
import sys
import time
from collections import defaultdict
from pathlib import Path

import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parents[1]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent))
import mesa_build as MB  # noqa: E402
import s2collision  # noqa: E402
import s2regions  # noqa: E402
import s2scene_build  # noqa: E402
from s2scene import read_archive  # noqa: E402

DISC = PROJECT / "data" / "disc"
STAGE = Path("D:/Theia-staging/spyro2-mod")
WORK = STAGE / "work"
SLOT, OVERLAY = 17, 16
UPW = 1024.0
MAX_REGION_VERTS = 170          # the near renderer's scratchpad: xy words at 0x1f800000, z at 0x1f8002a8 (retail max 170)
MAX_REGION_FACES = 145          # retail max
MAX_REGION_COLOURS = 220        # retail max 221
FOG_RGB = (240, 176, 236)       # Market Mesa's horizon (sampled from the DuckStation shots: 255 182 237 / 208 158 254)
FAR_WORDS = 255                 # header +23, consumed by the original region loader/renderer
NO_COLLISION = {"foliage", "awning", "flag", "cloth", "banner"}   # visual-only decor
LOW_CLIFF_Z = 73                # Market Mesa's lowest walkable top: cliff faces wholly below it are only seen
FACE_BUFFER = 0x2000            # per-face bytes at the frame buffer base + 1 .. (the near list starts at +0x2000)


# ------------------------------------------------------------------ coordinates
class Frame:
    """SoI units -> retail units: x * H, -y * H, z * V (world units) * 1024, then an offset that keeps every
    coordinate positive (collision bases are 14-bit, region origins unsigned)."""

    def __init__(self, pts: np.ndarray):
        r = self.raw(pts)
        lo = r.min(0)
        self.off = np.array([4096 - lo[0], 4096 - lo[1], 2048 - lo[2]])
        self.off = np.round(self.off / 32) * 32

    @staticmethod
    def raw(p):
        p = np.asarray(p, float)
        return np.stack([p[..., 0] * MB.H * UPW, -p[..., 1] * MB.H * UPW, p[..., 2] * MB.V * UPW], -1)

    def __call__(self, p):
        return self.raw(p) + self.off

    @staticmethod
    def normal(n):
        """Normals transform by the inverse transpose of the (anisotropic, mirroring) scale."""
        n = np.asarray(n, float)
        return np.array([n[0] / MB.H, -n[1] / MB.H, n[2] / MB.V])


# ------------------------------------------------------------------ faces for the PS1 renderer
def polygon_corners(pts: np.ndarray, normal: np.ndarray):
    """Outline order of a convex point set in its plane, collinear points removed."""
    c = pts.mean(0)
    n = normal / (np.linalg.norm(normal) + 1e-12)
    a = pts[np.argmax(np.linalg.norm(pts - c, axis=1))] - c
    a -= n * np.dot(a, n)
    a /= np.linalg.norm(a) + 1e-12
    b = np.cross(n, a)
    ang = np.arctan2((pts - c) @ b, (pts - c) @ a)
    order = list(np.argsort(ang))
    keep = []
    for k, i in enumerate(order):
        p0, p1, p2 = pts[order[k - 1]], pts[i], pts[order[(k + 1) % len(order)]]
        if np.linalg.norm(np.cross(p1 - p0, p2 - p1)) > 1e-6 * (np.linalg.norm(p1 - p0) * np.linalg.norm(p2 - p1) + 1e-12):
            keep.append(i)
    return keep


def ps1_faces(B: dict):
    """Merge the PC build's split faces back into one PS1 face per tile frame (quads / triangles whose UVs
    are rectangle corners). Returns a list of dicts: vs (outline vertex ids, front winding), uv, mat, kind,
    flags, colours (per outline vertex), n."""
    M, cols = B["M"], B["cols"]
    far_start = len(M.faces) - (len(B["parts"]) - B["nparts"])
    groups = defaultdict(list)
    for i, f in enumerate(M.faces):
        if i >= far_start:
            continue                                   # distant mesas: the sky's job on PS1
        key = (json.dumps(f["frame"]), f["kind"], f.get('stone_group_material',f["matname"]), f.get('stone_group_rotation',f.get("rot", 0)), tuple(f["n"]), f.get("flags", 3),
               i if "uvs" in f or f["kind"] in ("decor", "under") else -1)
        groups[key].append(i)
    out, split_back = [], 0
    for key, idx in groups.items():
        f0 = M.faces[idx[0]]
        vcol = {}
        for i in idx:
            for v, c in zip(M.faces[i]["vs"], cols[i]):
                vcol.setdefault(v, []).append(c)
        verts = sorted(vcol)
        n = np.array(f0["n"], float)
        pts = np.array([M.pos[v] for v in verts], float)
        corner = polygon_corners(pts, n) if len(verts) > 3 else list(range(len(verts)))
        outs = []
        if 3 <= len(corner) <= 4:
            outs.append([verts[k] for k in corner])
        else:                                          # not a simple polygon: keep the split faces
            split_back += 1
            outs = [list(M.faces[i]["vs"]) for i in idx]
        for vs in outs:
            uv = [f0["uvs"][f0["vs"].index(v)] if "uvs" in f0 and v in f0["vs"] else MB.uv_of(f0, M.pos[v]) for v in vs]
            out.append(dict(vs=vs, uv=uv, mat=f0["matname"], kind=f0["kind"], flags=f0.get("flags", 3), n=n,
                            col=[np.mean(vcol[v], 0) for v in vs],uv_exact=f0.get('stone_uv_kind')=='panel',
                            uv_canonical=bool(B.get('stone_polish'))))
    return out, split_back


# ------------------------------------------------------------------ UV rectangles and orientation
D4 = [(lambda s, t: (s, t)), (lambda s, t: (1 - t, s)), (lambda s, t: (1 - s, 1 - t)), (lambda s, t: (t, 1 - s)),
      (lambda s, t: (t, s)), (lambda s, t: (1 - s, t)), (lambda s, t: (1 - t, 1 - s)), (lambda s, t: (s, 1 - t))]


def snap_rect(lo, hi):
    """A 1-D UV range a material shows: the whole tile or one half (the four 32 x 32 quadrant descriptors are
    cut at the tile's middle; finer ranges would cost too many of the 128 materials)."""
    lo, hi = max(0.0, lo), min(1.0, hi)
    if hi - lo > 0.75:
        return 0.0, 1.0
    if hi <= 0.5 + 0.1 and (hi - lo) <= 0.6:
        return 0.0, 0.5
    if lo >= 0.5 - 0.1 and (hi - lo) <= 0.6:
        return 0.5, 1.0
    return 0.0, 1.0


WHOLE = (0.0, 1.0, 0.0, 1.0)


def far_clut_fade(c4: np.ndarray) -> np.ndarray:
    """The 16 rows of a far (4 bpp) CLUT. The far texture path (80024e28) adds a depth-dependent row (0xf - depth
    >> 8) to the descriptor's CLUT, so retail stores 16 rows fading the palette to neutral grey (5-bit 16 = 1.0
    under texture modulation). Fitted on the retail scenes 15, 17, 29, 33, 35, 47 (443 CLUTs): row r =
    c + round((16 - c) (2r - 1) / 32) for r >= 1, row 0 = the palette. 98.8995% of RGB5 channels on rows 2-14
    match the six-scene sample; outliers exist (max 14 steps). This is an explicit generated fade design,
    not the recovered exact retail generation formula. Transparent entries (0x0000) stay transparent."""
    c = c4.astype(np.int32)
    ch = np.stack([c & 31, (c >> 5) & 31, (c >> 10) & 31], -1).astype(float)
    rows = []
    for r in range(16):
        f = max(2 * r - 1, 0) / 32.0
        x = np.clip(np.round(ch + (16 - ch) * f), 0, 31).astype(np.uint16)
        w = x[:, 0] | (x[:, 1] << 5) | (x[:, 2] << 10) | (c4 & 0x8000)
        w = np.where(w == 0, np.uint16(0x0421), w)
        rows.append(np.where(c4 == 0, np.uint16(0), w).astype(np.uint16))
    return np.stack(rows)


DIRECTIONAL = {"window", "door", "banner", "flag", "awning", "plaza_edge", "bridge_edge", "crate", "counter",
               "coping_side", "hatch"}


def assign(face):
    """Choose the PS1 corner order and the material orientation for a face. Returns (rect, orientation g,
    strip vertex order [v3, v2, v0, v1] or [v3, v2, v1], triangle rotation, corner error). Only directional
    tiles may use a mirrored copy; undersides (planar UVs) take the whole tile."""
    uv = np.clip(np.array(face["uv"], float), 0, 1)
    if face["kind"] == "under":
        (u0, u1), (v0, v1) = (0.0, 1.0), (0.0, 1.0)
    else:
        (u0, u1), (v0, v1) = snap_rect(uv[:, 0].min(), uv[:, 0].max()), snap_rect(uv[:, 1].min(), uv[:, 1].max())
    gs = range(8) if face["mat"] in DIRECTIONAL else (0, 1, 2, 3)
    # each corner -> nearest rect corner (s, t) in {0, 1}^2
    st = [(int(round((u - u0) / max(u1 - u0, 1e-6))), int(round((v - v0) / max(v1 - v0, 1e-6)))) for u, v in
          np.clip(uv, [u0, v0], [u1, v1])]
    vs = face["vs"]
    if len(vs) == 4:
        # outline w0..w3 (front winding) -> strip v3=w_k, v2=w_k+1, v1=w_k+2, v0=w_k+3; square corners
        # v3 (0,0), v2 (1,0), v1 (1,1), v0 (0,1). Find k and an orientation g with g(square) = rect corners.
        best = None
        for g in gs:
            for k in range(4):
                sq = [(0, 0), (1, 0), (1, 1), (0, 1)]
                err = sum((D4[g](*sq[j])[0] != st[(k + j) % 4][0]) + (D4[g](*sq[j])[1] != st[(k + j) % 4][1])
                          for j in range(4))
                cand = (err, g >= 4, g, k)
                if best is None or cand < best:
                    best = cand
        err, _, g, k = best
        w = [vs[(k + j) % 4] for j in range(4)]
        return (u0, u1, v0, v1), g, [w[0], w[1], w[3], w[2]], 0, err
    # triangle: strip v3, v2, v1 with square corners rot^r of (0,0), (1,0), (0,1)
    best = None
    for g in gs:
        for k in range(3):
            for r in range(4):
                sq = [(0, 0), (1, 0), (0, 1)]
                for _ in range(r):
                    sq = [(1 - t, s) for s, t in sq]
                err = sum((D4[g](*sq[j])[0] != st[(k + j) % 3][0]) + (D4[g](*sq[j])[1] != st[(k + j) % 3][1])
                          for j in range(3))
                cand = (err, g >= 4, g, k, r)
                if best is None or cand < best:
                    best = cand
    err, _, g, k, r = best
    w = [vs[(k + j) % 3] for j in range(3)]
    return (u0, u1, v0, v1), g, w, r, err


# ------------------------------------------------------------------ textures
_ATLAS = {}


def _retail_atlas(slot):
    if slot not in _ATLAS:
        import s2scene
        _ATLAS[slot] = s2scene.atlas(s2scene.read_scene(DISC, slot))
    return _ATLAS[slot]


def tile_rgba(key) -> np.ndarray:
    if isinstance(key, str):
        raw=(PROJECT / "data/soi/pc/textures" / f"{key}.rgba").read_bytes()
        size=math.isqrt(len(raw)//4)
        assert size*size*4==len(raw),'square RGBA source texture required'
        return np.frombuffer(raw,np.uint8).reshape(size,size,4)
    slot, tile = key
    a = _retail_atlas(slot)              # decoded from the retail scene (the atlas PNGs carry tile-number labels)
    return a[(tile // 16) * 64:(tile // 16) * 64 + 64, (tile % 16) * 64:(tile % 16) * 64 + 64].copy()


def orient(img: np.ndarray, g: int) -> np.ndarray:
    """Texture for orientation g: texel (s, t) of the result = texel D4[g](s, t) of the tile."""
    n = img.shape[0]
    s, t = np.meshgrid(np.arange(n), np.arange(n))      # s = x (u), t = y (v)
    S, T = s / (n - 1), t / (n - 1)
    u, v = D4[g](S, T)
    return img[np.clip(np.round(v * (n - 1)).astype(int), 0, n - 1), np.clip(np.round(u * (n - 1)).astype(int), 0, n - 1)]


def to15(rgba: np.ndarray) -> np.ndarray:
    r, g, b = (rgba[..., 0] >> 3).astype(np.uint16), (rgba[..., 1] >> 3).astype(np.uint16), (rgba[..., 2] >> 3).astype(np.uint16)
    c = r | (g << 5) | (b << 10)
    c = np.where(c == 0, np.uint16(0x0421), c)           # 0x0000 is transparent on the PS1
    return np.where(rgba[..., 3] < 128, np.uint16(0), c).astype(np.uint16)


def quantize(img: np.ndarray, colours: int):
    """Palette (<= colours, entry 0 transparent if needed) and index image."""
    alpha = img[..., 3] >= 128
    rgb = Image.fromarray(img[..., :3].astype(np.uint8))
    q = rgb.quantize(colors=colours - (0 if alpha.all() else 1), method=Image.Quantize.MEDIANCUT)
    pal = np.array(q.getpalette()[:3 * (colours)], np.uint8).reshape(-1, 3)
    idx = np.asarray(q, np.uint8).astype(np.int32)
    if not alpha.all():
        idx = idx + 1
        idx[~alpha] = 0
        pal = np.vstack([[0, 0, 0], pal])[:colours]
    pal4 = np.concatenate([pal, np.full((len(pal), 1), 255, np.uint8)], 1)
    clut = to15(pal4)
    if not alpha.all():
        clut[0] = 0
    out = np.zeros(colours, np.uint16)
    out[:len(clut)] = clut
    return out, idx


def vram_blocks(mask: np.ndarray, w: int, h: int, xstep: int):
    """w x h halfword blocks inside `mask` (VRAM x 512..1023), not crossing a 256-row half."""
    out = []
    used = np.zeros_like(mask)
    for y in range(0, 512 - h + 1, h // 2 if h >= 64 else h):
        if (y // 256) != ((y + h - 1) // 256):
            continue
        for x in range(0, 512 - w + 1, xstep):
            if mask[y:y + h, x:x + w].all() and not used[y:y + h, x:x + w].any():
                used[y:y + h, x:x + w] = True
                out.append((x + 512, y))
    return out


# ------------------------------------------------------------------ region records
def pack_vertex(p, origin):
    d = (np.asarray(p) - origin)
    x, y, z = int(d[0]) // 16, int(d[1]) // 16, int(d[2]) // 32
    assert 0 <= x < 2048 and 0 <= y < 2048 and 0 <= z < 1024, (x, y, z)
    return (x << 21) | (y << 10) | z


def rgbx(c):
    c = np.clip(np.round(c), 0, 255).astype(int)
    return struct.pack("<BBBB", c[0], c[1], c[2], 0)



def legacy_materials(faces, arc, sc, rep):
    gdesc = sc['parts'][0][8:]
    gcount = struct.unpack_from('<I',sc['parts'][0],4)[0]
    # ---- materials: (tile key, rect, orientation). Every retail descriptor is a 32 x 32 texel cell: the near
    # renderers build the UV corners as word0 + 0x1f00 / 0x1f1f and word1 - 0x1f00 (80024f2c, 800253d4, 8002654c),
    # so a part-tile rect (half / quarter) needs its own cells holding the stretched part. Cells are scarce
    # (Glimmer's terrain VRAM: 248 near cells, 62 far cells), so the most used part rects get exact cells and the
    # rest show the whole tile on their (smaller) face.
    from collections import Counter
    use = Counter()
    for f in faces:
        rect, g, strip, rot, err = assign(f)
        f.update(key=MB.MAT[f["mat"]], rect=rect, g=g, strip=strip, trot=rot, uverr=err)
        use[(f["key"], rect, g)] += 1
    tex_need = {(f["key"], f["g"]): True for f in faces}
    rep["faces_uv_snapped"] = sum(1 for f in faces if f["uverr"])
    part = sorted([k for k in use if k[1] != WHOLE], key=lambda k: (-use[k], str(k)))
    rep["part_rect_faces"] = sum(use[k] for k in part)
    # Glimmer's terrain VRAM (near 8bpp quadrants, far 4bpp), and its CLUT positions
    near_mask = np.zeros((512, 512), bool)
    far_mask = np.zeros((512, 512), bool)
    cl8, cl4 = [], []
    for m in range(gcount):
        for k in range(6):
            d = gdesc[(m * 6 + k) * 8:(m * 6 + k + 1) * 8]
            l, t, clut, r, b, page = d[0], d[1], struct.unpack_from("<H", d, 2)[0], d[4], d[5], d[6]
            depth = (page >> 7) & 1
            ppw = 2 if depth else 4
            x0, x1 = (page & 15) * 64 + l // ppw - 512, (page & 15) * 64 + r // ppw - 512
            y0, y1 = ((page >> 4) & 1) * 256 + t, ((page >> 4) & 1) * 256 + b
            (near_mask if k >= 2 else far_mask)[y0:y1 + 1, x0:x1 + 1] = True
            if k == 2:
                cl8.append(clut)
            if k == 0:
                cl4.append(clut)
    near_cells = vram_blocks(near_mask, 16, 32, 16)  # 32 x 32 texels at 8 bpp (a quadrant)
    far_cells = vram_blocks(far_mask, 8, 32, 8)      # 32 x 32 texels at 4 bpp
    rep["vram_cells"] = dict(near=len(near_cells), far=len(far_cells), cluts8=len(cl8), cluts4=len(cl4))
    assert len(set(cl8)) == len(cl8) and len(set(cl4)) == len(cl4)
    assert len(tex_need) <= min(len(far_cells), len(cl8), len(cl4)) and 4 * len(tex_need) <= len(near_cells), rep["vram_cells"]
    n_exact = min(len(part), (len(near_cells) - 4 * len(tex_need)) // 4)
    exact = set(part[:n_exact])
    for f in faces:                                   # the other part rects show the whole tile
        if f["rect"] != WHOLE and (f["key"], f["rect"], f["g"]) not in exact:
            f["rect"] = WHOLE
    mats = {}
    for f in faces:
        mk = (f["key"], f["rect"], f["g"])
        if mk not in mats:
            mats[mk] = len(mats)
        f["material"] = mats[mk]
    rep["materials"], rep["texture_slots"] = len(mats), len(tex_need)
    rep["exact_part_materials"] = dict(count=len(exact), faces=sum(use[k] for k in exact),
                                       whole_fallback_faces=rep["part_rect_faces"] - sum(use[k] for k in exact))
    assert len(mats) <= 128, len(mats)
    vram_at = struct.unpack_from("<I", arc, 0)[0]

    def put(x, y, words):
        h, w = words.shape
        for j in range(h):
            o = vram_at + ((y + j) * 512 + (x - 512)) * 2
            arc[o:o + 2 * w] = words[j].astype("<u2").tobytes()

    pack8 = lambda i: (i[:, 0::2] | (i[:, 1::2] << 8)).astype(np.uint16)  # noqa: E731
    pack4 = lambda i: (i[:, 0::4] | (i[:, 1::4] << 4) | (i[:, 2::4] << 8) | (i[:, 3::4] << 12)).astype(np.uint16)  # noqa: E731
    nfree, ffree = list(near_cells), list(far_cells)
    slots, flat = {}, {}
    for k, (key, g) in enumerate(sorted(tex_need, key=str)):
        img = orient(tile_rgba(key), g)
        clut, idx = quantize(img, 256)
        cells = [nfree.pop(0) for _ in range(4)]
        for qd, (x, y) in enumerate(cells):
            qx, qy = qd % 2, qd // 2
            put(x, y, pack8(idx[qy * 32:qy * 32 + 32, qx * 32:qx * 32 + 32]))
        cy, cx = (cl8[k] >> 6) & 511, (cl8[k] & 63) * 16
        put(cx, cy, clut[None, :])
        small = np.asarray(Image.fromarray(img).resize((32, 32), Image.BOX))
        c4, i4 = quantize(small, 16)
        fx, fy = ffree.pop(0)
        put(fx, fy, pack4(i4))
        cy4, cx4 = (cl4[k] >> 6) & 511, (cl4[k] & 63) * 16
        put(cx4, cy4, far_clut_fade(c4))              # 16 rows: the far path picks row 0xf - depth (80024e28)
        slots[(key, g)] = dict(near=cells, far=(fx, fy), clut8=cl8[k], clut4=cl4[k], idx=idx, i4=i4)
        a = img[..., 3] >= 128
        flat[(key, g)] = img[a][:, :3].mean(0) if a.any() else np.array([128, 128, 128])

    def desc(x, y, clut, depth):
        """8-byte descriptor of the 32 x 32 texel cell at VRAM (x, y) (u1 = u0 + 31, v1 = v0 + 31, as the renderers
        assume)."""
        ppw = 2 if depth else 4
        page_x = (x // 64) * 64
        u = (x - page_x) * ppw
        v = y % 256
        assert u + 31 <= 255
        page = (page_x // 64) | ((y // 256) << 4) | (depth << 7)
        return bytes([u, v]) + struct.pack("<H", clut) + bytes([u + 31, v + 31, page, 0])

    def slot_rect(rect, g):
        """The material square's region in the oriented slot texture (a plain rect: the slot's (s, t) texel is
        tile D4[g](s, t), so the square maps to D4[g]^-1 of the tile rect)."""
        u0, u1, v0, v1 = rect
        cs = [(a, b) for a in (0, 1) for b in (0, 1)]
        inv = next(gi for gi in range(8) if all(np.allclose(D4[gi](*D4[g](a, b)), (a, b)) for a, b in cs))
        q = np.array([D4[inv](u, v) for u in (u0, u1) for v in (v0, v1)])
        return q.min(0), q.max(0)

    def stretch(ind, lo, hi, n):
        """Index image region [lo, hi) (fractions) resampled to n x n, nearest (no new colours: the slot's CLUT)."""
        H = ind.shape[0]
        ys = np.clip(((lo[1] + (hi[1] - lo[1]) * (np.arange(n) + 0.5) / n) * H).astype(int), 0, H - 1)
        xs = np.clip(((lo[0] + (hi[0] - lo[0]) * (np.arange(n) + 0.5) / n) * H).astype(int), 0, H - 1)
        return ind[np.ix_(ys, xs)]

    part0 = bytearray()
    own_far = 0
    for (key, rect, g), m in sorted(mats.items(), key=lambda kv: kv[1]):
        s = slots[(key, g)]
        lo, hi = slot_rect(rect, g)
        material_image = orient(tile_rgba(key), g)
        h = material_image.shape[0]
        material_image = material_image[int(round(lo[1]*h)):int(round(hi[1]*h)),
                                        int(round(lo[0]*h)):int(round(hi[0]*h))]
        opaque = material_image[..., 3] >= 128
        flat[m] = material_image[opaque][:, :3].mean(0) if opaque.any() else np.array([128., 128., 128.])
        if rect == WHOLE:
            ncells, fcell = s["near"], s["far"]
        else:
            lo, hi = slot_rect(rect, g)
            big = stretch(s["idx"], lo, hi, 64)
            ncells = [nfree.pop(0) for _ in range(4)]
            for qd, (x, y) in enumerate(ncells):
                qx, qy = qd % 2, qd // 2
                put(x, y, pack8(big[qy * 32:qy * 32 + 32, qx * 32:qx * 32 + 32]))
            if ffree:
                fcell = ffree.pop(0)
                put(*fcell, pack4(stretch(s["i4"], lo, hi, 32)))
                own_far += 1
            else:
                fcell = s["far"]                      # mid distance: the whole tile (it fades to neutral anyway)
        far = [desc(*fcell, s["clut4"], 0)] * 2
        near = [desc(x, y, s["clut8"], 1) for x, y in ncells]
        part0 += b"".join(far + near)
    rep["exact_part_materials"]["own_far_cells"] = own_far
    rep["vram_cells_used"] = dict(near=len(near_cells) - len(nfree), far=len(far_cells) - len(ffree))
    nm = len(mats)
    part0 = struct.pack("<II", 8 + nm * 48, nm) + bytes(part0)
    return mats, flat, part0


def build(args):
    t0 = time.time()
    rep = {}
    work = getattr(args, 'work', WORK)
    B = MB.build_model(PROJECT / "data/soi/levels/06-market-mesa/level.json",stone_polish=getattr(args,'stone_polish',False))
    if 'stone_polish' in B:rep['stone_polish']=B['stone_polish']
    M = B["M"]
    faces, split_back = ps1_faces(B)
    rep["ps1_faces"], rep["groups_kept_split"] = len(faces), split_back
    # ---- coordinates (positions after sculpt)
    P = np.array(B["P"], float)
    used_v = sorted({v for f in faces for v in f["vs"]})
    fr = Frame(P[used_v])
    R = fr(P)
    Rq = R.copy()
    Rq[:, :2] = np.round(Rq[:, :2] / 16) * 16
    Rq[:, 2] = np.round(Rq[:, 2] / 32) * 32
    rep["frame_offset"] = fr.off.tolist()        # retail = (x H, -y H, z V) * 1024 + offset
    rep["retail_bounds"] = [Rq[used_v].min(0).tolist(), Rq[used_v].max(0).tolist()]
    # ---- winding: front faces have cross(B-A, C-A) . outward < 0 (retail coordinates)
    for f in faces:
        n = fr.normal(f["n"])
        pts = R[f["vs"]]
        tri_n = np.cross(pts[1] - pts[0], pts[2] - pts[0])
        if np.dot(tri_n, n) > 0:
            f["vs"], f["uv"], f["col"] = f["vs"][::-1], f["uv"][::-1], f["col"][::-1]
    # ---- the template archive
    arc = bytearray(read_archive(DISC, SLOT))
    ch = s2scene_build.archive_children(DISC, SLOT)[1]
    scene = bytes(arc[ch[1].offset: ch[1].offset + ch[1].size])
    sc = s2scene_build.split(scene)
    gdesc = sc["parts"][0][8:]
    gcount = struct.unpack_from("<I", sc["parts"][0], 4)[0]
    if getattr(args, 'uv_polish', False):
        import mesa_uv
        rep['uv_material_limit']=getattr(args,'uv_material_limit',128)
        mats, flat, part0 = mesa_uv.build(faces, arc, sc, rep)
        rep['release_eligible'] = False
        rep['release_blockers'] = ['Experimental UV path requires complete original-consumer/runtime review',
                                   'Market Mesa content/character continuation is unfinished']
    else:
        mats, flat, part0 = legacy_materials(faces, arc, sc, rep)
    nm = len(mats)
    # ---- the material's orientation is in its texture; corners of the material square are plain
    # ---- regions
    keyf = np.array([R[f["vs"]].mean(0) for f in faces])
    compact_colours = bool(getattr(args, 'uv_polish', False))
    rep['near_colour_sharing'] = ('identical quantized A/B pairs within the same material; no spatial averaging'
                                 if compact_colours else 'per vertex/material')

    def colour_keys(i):
        f = faces[i]
        return {(0 if compact_colours else v, ckq(f["col"][f["vs"].index(v)]), f["material"]) for v in f["vs"]}

    ckeys = [colour_keys(i) for i in range(len(faces))]
    # Count the exact far payload before accepting a partition. Colour indices may
    # share identical RGB bytes, but positions and material boundaries never move.
    far_ckeys = [{rgbx(flat[faces[i]['material']] * (np.array(k[1]) * 8) / 128.)
                  for k in ckeys[i]} if faces[i]['kind'] != 'decor' else set()
                 for i in range(len(faces))]

    def partition(ids):
        vs = {v for i in ids for v in faces[i]["vs"]}
        nc = len(set().union(*(ckeys[i] for i in ids)))
        far_ids = [i for i in ids if faces[i]['kind'] != 'decor']
        lv = len({v for i in far_ids for v in faces[i]['vs']})
        lc = len(set().union(*(far_ckeys[i] for i in far_ids)))
        if (len(vs) <= MAX_REGION_VERTS and len(ids) <= MAX_REGION_FACES and nc <= MAX_REGION_COLOURS
                and lv <= 127 and lc <= 127 and lv + lc + 2 * len(far_ids) <= FAR_WORDS):
            return [ids]
        assert len(ids) > 1, ('single face exceeds region budget', ids, lv, lc)
        c = keyf[ids]
        ax = int(np.argmax(c.max(0) - c.min(0)))
        order = sorted(ids, key=lambda i: keyf[i][ax])
        h = len(order) // 2
        return partition(order[:h]) + partition(order[h:])

    regions = partition(list(range(len(faces))))
    rep["regions"] = len(regions)
    assert len(regions) <= 255
    # global face index (per-face byte buffer): 1 + faces of the earlier regions
    gidx, start = {}, 1
    for ri, ids in enumerate(regions):
        for li, i in enumerate(ids):
            gidx[i] = start + li
        start += len(ids)
    rep["near_faces"] = start - 1
    assert start < FACE_BUFFER, start
    # neighbours across shared edges (quantized positions)
    edge = defaultdict(list)

    def edges_of(f):
        s = f["strip"]
        loop = [s[0], s[1], s[3], s[2]] if len(s) == 4 else [s[0], s[1], s[2]]
        return [(loop[j], loop[(j + 1) % len(loop)]) for j in range(len(loop))]

    for i, f in enumerate(faces):
        for a, b in edges_of(f):
            edge[frozenset((tuple(Rq[a]), tuple(Rq[b])))].append(i)
    records, far_stats, far_near_indices = [], [], []
    for ri, ids in enumerate(regions):
        vlist = sorted({v for i in ids for v in faces[i]["vs"]})
        vpos = Rq[vlist]
        origin = np.array([math.floor(vpos[:, 0].min() / 16) * 16, math.floor(vpos[:, 1].min() / 16) * 16,
                           math.floor(vpos[:, 2].min() / 32) * 32])
        vi = {v: k for k, v in enumerate(vlist)}
        verts = b"".join(struct.pack("<I", pack_vertex(Rq[v], origin)) for v in vlist)
        # UV mode reuses exactly identical A/B pairs within a material. Its
        # constant flat B multiplier and the identical quantized A key prove
        # both bytes match. Geometry/vertices are never merged or averaged.
        ckey, cA, cB = {}, [], []
        hp, lp = [], []
        cidx_of = {}
        for i in ids:
            f = faces[i]
            fl = flat[f['material']]
            cidx = []
            for v in f["strip"]:
                col = f["col"][f["vs"].index(v)] * 128.0
                k = (0 if compact_colours else v, ckq(f["col"][f["vs"].index(v)]), f['material'])
                if k not in ckey:
                    ckey[k] = len(cA)
                    col = np.array(k[1], float) * 8
                    cA.append(col)
                    cB.append(fl * col / 128.0)
                cidx.append(ckey[k])
            cidx_of[i] = list(cidx)
            s = [vi[v] for v in f["strip"]]
            nb4 = []
            for a, b in edges_of(f):
                o = [j for j in edge[frozenset((tuple(Rq[a]), tuple(Rq[b])))] if j != i]
                nb4.append(gidx[o[0]] if o else 0)
            mat = f["material"]
            two = 0x800 if f["kind"] == "decor" and f["mat"] in ("flag", "cloth", "banner", "foliage", "awning") else 0
            if len(s) == 4:
                v3, v2, v0, v1 = s
                c3, c2, c0, c1 = cidx
                w2 = (nb4[0] << 19) | (nb4[1] << 6) | (nb4[2] & 0x3f)
                w3 = mat | two | (((nb4[2] >> 6) & 0x7f) << 12) | (nb4[3] << 19)
                hp.append(bytes([v0, v1, v2, v3, c0, c1, c2, c3]) + struct.pack("<II", w2, w3))
            else:
                v3, v2, v1 = s
                c3, c2, c1 = cidx
                w2 = (nb4[0] << 19) | (nb4[1] << 6) | (nb4[2] & 0x3f)
                w3 = mat | two | (f["trot"] << 12)
                hp.append(bytes([v1, v1, v2, v3, ((nb4[2] >> 6) & 0x7f) << 1, c1, c2, c3]) + struct.pack("<II", w2, w3))
        assert len(cA) <= MAX_REGION_COLOURS and len(vlist) <= MAX_REGION_VERTS, (len(cA), len(vlist))
        # near colours: A (close) and B (at distance: 80024c84 blends A -> B by depth while the far CLUT row fades
        # the 4 bpp texture to neutral grey, so a distant face shows B). Retail B is about the tile's mean colour
        # x A (Glimmer: B ~ 0.88 A tex / 128); the far mesh carries the same flat colours, so the switch is seamless.
        # (test01 / test02 drew B through Glimmer's fade rows, which looked double-tinted; test03 writes the rows.)
        hpc = b"".join(rgbx(c) for c in cA) + b"".join(rgbx(c) for c in cB)
        lverts, lpc, lfaces = far_mesh(ids, faces, Rq, origin, cidx_of, cB)
        ctr = (vpos.min(0) + vpos.max(0)) / 2
        rad = float(np.linalg.norm(vpos - ctr, axis=1).max()) + 32
        nv, nc, nf = len(vlist), len(cA), len(ids)
        f12 = gidx[ids[0]]
        lv, lc, lf = len(lverts) // 4, len(lpc) // 4, len(lfaces) // 8
        # flag 0x2000: no far mesh, never put on the far list (80023ddc). Retail sets it on every region whose far
        # mesh is empty; without it the far renderer's vertex loop (80028c2c, software-pipelined, exit test
        # t7 == s0) never ends for 0 vertices and runs past the scratchpad: a data bus error (test01's freeze).
        nofar = 0x2000 if lv == 0 else 0
        head = struct.pack("<HHHHHHHH", int(ctr[1]) // 16, int(ctr[0]) // 16, min(4095, int(rad) // 16 + 1) | nofar,
                           int(ctr[2]) // 16, int(origin[1]) // 16, int(origin[0]) // 16, f12, int(origin[2]) // 16)
        head += bytes([lv, lc, lf, 0, nv, nc, nf, lv + lc + 2 * lf]) + struct.pack("<I", 0xFFFFFFFF)
        records.append(s2regions.Record(head, lverts, lpc, lfaces, verts, hpc, b"".join(hp)))
        far_stats.append((lv, lf))
        far_near_indices.append([j for j, i in enumerate(ids) if faces[i]['kind'] != 'decor'])
    part1 = s2regions.encode(b"", records)
    rep["far_mesh"] = dict(verts=sum(a for a, _ in far_stats), faces=sum(b for _, b in far_stats))
    rep['far_mesh'].update(mode='exact terrain copy; decor omitted', near_face_indices=far_near_indices)
    rep['material_names'] = [next(f['mat'] for f in faces if f['material'] == m) for m in range(nm)]
    rep["near_mesh"] = dict(verts=sum(len(r.hp_verts) // 4 for r in records), colours=sum(len(r.hp_colors) // 8 for r in records),
                            faces=sum(len(r.hp_faces) // 16 for r in records))
    from collections import Counter as _C
    rep["faces_by_kind"] = dict(_C(f["kind"] for f in faces))
    # ---- collision: the PC build's split faces that collide (flags & 2), undersides and distant scenery left out
    far_start = len(M.faces) - (len(B["parts"]) - B["nparts"])
    tris = []
    for i, f in enumerate(M.faces[:far_start]):
        if not (f.get("flags", 3) & 2) or f["kind"] == "under" or f["matname"] in NO_COLLISION:
            continue
        if f["kind"] == "cliff" and max(B["P"][v][2] for v in f["vs"]) <= LOW_CLIFF_Z:
            continue
        vs = f["vs"]
        n = fr.normal(f["n"])
        for a, b, c in ([(0, 1, 2), (0, 2, 3)] if len(vs) == 4 else [(0, 1, 2)]):
            p = np.round(R[[vs[a], vs[b], vs[c]]] / 16).astype(int)
            if np.dot(np.cross(p[1] - p[0], p[2] - p[0]), n) > 0:
                p = p[[0, 2, 1]]
            tris += split_tri(p)
    tri_bytes = b"".join(pack_tri(p) for p in tris)
    cells = cells_exact(tri_bytes)
    coll = s2collision.encode(tri_bytes, b"", b"", cells)
    rep["collision_triangles"] = len(tris)
    # ---- the scene child
    from mesa_visibility import encode as encode_visibility
    part2 = encode_visibility(len(records))
    rep['visibility'] = dict(mode='conservative all-visible', bytes=len(part2), draw_distance=0x2c000)
    # part 5 +0x10: the GTE far (fog) colour that 80023778 loads each frame (RFC / GFC / BFC); Glimmer's is khaki
    # (c0 c0 80), which turned Market Mesa's distant islands green. Market Mesa's horizon is pink-lavender.
    part5 = bytearray(sc["parts"][5])
    assert len(part5) == 28 and part5[16:20] == bytes([0xC0, 0xC0, 0x80, 0])
    part5[16:20] = bytes(FOG_RGB) + bytes(1)
    rep["fog_colour"] = list(FOG_RGB)
    sc2 = dict(parts=[part0, part1, part2] + sc["parts"][3:5] + [bytes(part5)], words=sc["words"], collision=coll, sky=sc["sky"],
               tail=sc["tail"])
    child1 = s2scene_build.join(sc2)
    rep["scene_child"] = dict(bytes=len(child1), limit=ch[1].size, materials=len(part0), regions=len(part1),
                              collision=len(coll) + 4)
    # ---- gameplay child
    g = bytearray(arc[ch[3].offset: ch[3].offset + ch[3].size])
    gp = gameplay(g, B, fr, rep)
    # ---- the archive: header, child 0 (VRAM, kept in place), the new scene child padded to 2048, then the
    # model bank and the gameplay child moved by the growth; the header's model directory (absolute archive
    # offsets inside child 2, from byte 64) moves with them
    pad = lambda b: bytes(b) + bytes((-len(b)) % 2048)  # noqa: E731
    c1 = pad(child1)
    delta = len(c1) - ch[1].size
    head = bytearray(arc[:2048])
    c2 = bytes(arc[ch[2].offset: ch[2].offset + ch[2].size])
    moved = 0
    for o in range(64, 2048, 4):
        w = struct.unpack_from("<I", head, o)[0]
        if w == 0:
            break
        if ch[2].offset <= w < ch[2].offset + ch[2].size:
            struct.pack_into("<I", head, o, w + delta)
            moved += 1
    offs = [ch[0].offset, ch[1].offset, ch[2].offset + delta, ch[3].offset + delta]
    sizes = [ch[0].size, len(c1), ch[2].size, len(pad(gp))]
    for k in range(4):
        struct.pack_into("<II", head, 8 * k, offs[k], sizes[k])
    arc = bytearray(head) + arc[ch[0].offset: ch[0].offset + ch[0].size] + c1 + c2 + pad(gp)
    rep["archive_layout"] = dict(offsets=offs, sizes=sizes, growth=delta, model_directory_entries_moved=moved,
                                 total=len(arc), retail_max_scene_slot=2181120)
    assert len(arc) <= 2181120, len(arc)
    work.mkdir(parents=True, exist_ok=True)
    out = work / "slot17-mesa.bin"
    out.write_bytes(bytes(arc))
    rep["archive"] = str(out)
    rep['archive_sha256'] = hashlib.sha256(arc).hexdigest()
    rep["seconds"] = round(time.time() - t0, 1)
    if not getattr(args, 'uv_polish', False):
        (PROJECT / "data/soi/ps1/mesa-ps1-report.json").write_text(json.dumps(rep, indent=1, default=str))
    (work / 'mesa-ps1-report.json').write_text(json.dumps(rep, indent=1, default=str))
    console = {k:v for k,v in rep.items() if k not in ('far_mesh','uv_polish')}
    if 'uv_polish' in rep:
        console['uv_polish'] = {k:v for k,v in rep['uv_polish'].items() if k not in ('faces','mappings','source_textures')}
    print(json.dumps(console, indent=1, default=str))
    print('far_mesh', {k:v for k,v in rep['far_mesh'].items() if k != 'near_face_indices'})
    return rep


def ckq(c):
    """Vertex colours (1.0 = 128) on a step of 8: neighbouring faces share colour entries."""
    return tuple(int(x) for x in np.clip(np.round(np.asarray(c) * 16.0), 0, 31))


def cells_exact(tri: bytes, margin: int = 256) -> dict:
    """Cell lists (4096-unit cells) of every triangle whose box grown by `margin` touches the cell (a
    separating-axis test, tighter than the bounds-only superset)."""
    cells = {}
    for i in range(len(tri) // 12):
        p = s2collision.tri_points(tri, i)
        lo = [max(0, min(q[a] for q in p) - margin) >> 12 for a in range(3)]
        hi = [(max(q[a] for q in p) + margin) >> 12 for a in range(3)]
        for z in range(lo[2], hi[2] + 1):
            for y in range(lo[1], hi[1] + 1):
                for x in range(lo[0], hi[0] + 1):
                    b0 = (x * 4096 - margin, y * 4096 - margin, z * 4096 - margin)
                    b1 = (x * 4096 + 4096 + margin, y * 4096 + 4096 + margin, z * 4096 + 4096 + margin)
                    if s2collision.tri_box_overlap(p, b0, b1):
                        cells.setdefault((z, y, x), []).insert(0, i)
    return cells


def far_mesh(ids, faces, Rq, origin, cidx_of, cB):
    """Exact terrain faces/positions; only identical packed B colours are shared.

    Partitioning enforces the original 7-bit indices and 255-word record limit.
    Decor keeps the existing near-only policy; roofs/walls are terrain, included.
    """
    vs = sorted({v for i in ids if faces[i]["kind"] != "decor" for v in faces[i]["vs"]})
    if not vs:
        return b"", b"", b""
    vi = {v:j for j,v in enumerate(vs)}
    colours, lf = {}, []
    for i in ids:
        f = faces[i]
        if f['kind'] == 'decor':
            continue
        vv = [vi[v] for v in f['strip']]
        cc = []
        for c in cidx_of[i]:
            packed = rgbx(cB[c])
            cc.append(colours.setdefault(packed, len(colours)))
        if len(vv) == 3:
            vv.append(vv[-1]); cc.append(cc[-1])
        a,b,c,d = vv
        ca,cb,cc_,cd = cc
        lf.append(struct.pack('<II', (a<<24)|(b<<17)|(c<<10)|(d<<3),
                              (ca<<25)|(cb<<18)|(cc_<<11)|(cd<<4)))
    assert len(vs) <= 127 and len(colours) <= 127 and len(vs)+len(colours)+2*len(lf) <= FAR_WORDS
    return (b''.join(struct.pack('<I', pack_vertex(Rq[v],origin)) for v in vs),
            b''.join(colours), b''.join(lf))


def split_tri(p):
    """Collision triangles must have 9-bit signed x / y and 8-bit z deltas (units of 16): split long edges."""
    p = np.asarray(p, int)
    d = np.abs(p[:, None, :] - p[None, :, :]).max(axis=(0, 1))
    if d[0] <= 250 and d[1] <= 250 and d[2] <= 250:
        return [p]
    L = [np.abs(p[(k + 1) % 3] - p[k]).max() for k in range(3)]
    k = int(np.argmax(L))
    a, b, c = p[k], p[(k + 1) % 3], p[(k + 2) % 3]
    m = (a + b) // 2
    return split_tri([a, m, c]) + split_tri([m, b, c])


def pack_tri(p):
    """12-byte collision triangle (8001c214): the lowest-z vertex first (cyclic, the winding is kept)."""
    k = int(np.argmin([q[2] for q in p]))
    p = [p[k], p[(k + 1) % 3], p[(k + 2) % 3]]
    (x0, y0, z0), (x1, y1, z1), (x2, y2, z2) = [tuple(int(v) for v in q) for q in p]
    assert all(0 <= v < 16384 for v in (x0, y0, z0)), (x0, y0, z0)
    dx1, dy1, dz1, dx2, dy2, dz2 = x1 - x0, y1 - y0, z1 - z0, x2 - x0, y2 - y0, z2 - z0
    assert -256 <= min(dx1, dy1, dx2, dy2) and max(dx1, dy1, dx2, dy2) < 256 and 0 <= dz1 < 256 and 0 <= dz2 < 256
    X = x0 | ((dx1 & 511) << 14) | ((dx2 & 511) << 23)
    Y = y0 | ((dy1 & 511) << 14) | ((dy2 & 511) << 23)
    Z = z0 | (dz1 << 16) | (dz2 << 24)
    return struct.pack("<3I", X, Y, Z)


# ------------------------------------------------------------------ gameplay child
OBJ_TABLE = 0x327C            # Glimmer: after the header (0x2c) and the eight size-prefixed sections


def gameplay(g: bytearray, B: dict, fr: Frame, rep: dict) -> bytearray:
    L, level = B["L"], B["level"]
    hc, ox, oy = L["hc"], L["ox"], L["oy"]

    def ground(x, y):
        i, j = int((x - ox) // MB.G), int((y - oy) // MB.G)
        if 0 <= j < hc.shape[0] and 0 <= i < hc.shape[1] and hc[j, i] >= 0:
            return float(hc[j, i])
        return None

    def at(x, y, z, lift=0):
        gz = ground(x, y)
        z = gz if gz is not None and abs(gz - z) < 30 else z
        r = fr((x, y, z))
        return int(r[0]), int(r[1]), int(r[2]) + lift

    sx, sy, sz = level["objects"]["start"]["pos"]
    spawn = at(sx, sy, sz, 64)
    struct.pack_into("<iii", g, 0, *spawn)
    rep["spawn"] = spawn
    # the seven terrain-animation sections (0x2c ...): count 0, payload cleared, sizes kept so no offset moves
    at_ = 0x2C
    for k in range(7):
        n = struct.unpack_from("<I", g, at_)[0]
        g[at_ + 4: at_ + n] = bytes(n - 4)
        at_ += n
    sec7 = struct.unpack_from("<I", g, at_)[0]
    at_ += sec7
    assert at_ == OBJ_TABLE, hex(at_)
    # Market Mesa's objects
    objs = level["objects"]["objects"]
    gems = sorted([e for e in objs if e.get("gem_value")], key=lambda e: -e["gem_value"])
    import mesa_gems
    rep['gem_table_expansion'] = mesa_gems.expand(g,len(gems))
    variants, values, meshes, colours = mesa_gems.tables()
    count = struct.unpack_from('<I',g,at_+4)[0]
    rec = lambda i: at_ + 8 + 88 * i  # noqa: E731
    types = [struct.unpack_from('<H',g,rec(i)+54)[0] for i in range(count)]
    # Retail 8004132c marks initial-table removals 0xfd. Unlike 0xff this
    # survives the loader's table walk; 80040e94 and 8007043c skip it.
    # Keeping the record preserves every original ID and persistence bit.
    moneybags=types.index(0x220)
    g[rec(moneybags)+0x48]=0xfd
    rep['moneybags_inert']=dict(index=moneybags,type=0x220,state=0xfd,
                               mechanism='retail initial-table removed state (8004132c)')
    rep['gem_records'] = []
    enemies = [e for e in objs if str(e.get("family", "")).startswith("enem") or e.get("family_name", "").lower().startswith("rhynoc")]
    if not enemies:
        enemies = [e for e in objs if 140 <= e.get("type", 0) <= 244]
    boxes = [e for e in objs if e.get("type", 0) in range(126, 140)]
    if 'uv_polish' in rep:
        import mesa_containers
        container_source,rep['container_oracle'] = mesa_containers.sources()
        container_values = {e['source_index']:e for e in container_source}
        assert len(boxes)==len(container_values)==15
        rep['container_records'] = []
    rep["gem_positions"] = [at(*e["pos"], 256) for e in gems][:sum(1 for t in types if t == 0x1)]
    pools = {0x1: [at(*e["pos"], 256) for e in gems], 0x1F0: [], 0x22B: [], 0xC8: [at(*e["pos"]) for e in boxes]}
    ens = [at(*e["pos"]) for e in enemies]
    pools[0x1F0], pools[0x22B] = ens[0::2], ens[1::2]
    start = (sx, sy, sz)
    near_start = [at(sx + dx, sy + dy, sz) for dx, dy in ((14, -14), (-20, 10), (24, 24))]
    park = (2048, 2048, 512)
    placed = defaultdict(int)
    parked = defaultdict(int)
    for i, t in enumerate(types):
        p = rec(i)
        if t in pools and pools[t]:
            pos = pools[t].pop(0)
            placed[t] += 1
            if t == 1:
                e=gems[placed[t]-1]
                value=int(e['gem_value']); variant=variants[value]
                assert variant<len(values) and values[variant]==value
                g[p+80]=value; g[p+73]=variant; g[p+60]=meshes[variant]; g[p+84]=colours[variant]
                rep['gem_records'].append(dict(index=i,position=pos,value=value,variant=variant,
                                              model=meshes[variant],colour=colours[variant]))
            if t==0xc8 and 'container_records' in rep:
                source=container_values[boxes[placed[t]-1]['index']]
                previous=g[p+80];g[p+80]=source['value']
                rep['container_records'].append(dict(source,index=i,position=pos,previous_value=previous,
                                                     mechanism='original basket interaction/release; retail basket model retained',
                                                     difference='C8 also accepts flame; source BG_B charge-only restriction not implemented' if source['source_type']>=135 else
                                                                'Retail basket geometry; source background container art not yet authored'))
        elif t == 0x1AA:
            pos = near_start[0]
            placed[t] += 1
        elif t == 0x110 and near_start[1:]:
            pos = near_start.pop(1)
            placed[t] += 1
        else:
            pos = park
            parked[t] += 1
        struct.pack_into("<iii", g, p + 12, *pos)
    rep["objects"] = dict(count=count, placed={hex(k): v for k, v in placed.items()},
                          parked={hex(k): v for k, v in parked.items()},
                          soi=dict(gems=len(gems), enemies=len(enemies), containers=len(boxes)))
    assert placed[1]==len(gems) and not pools[1]
    rep['loose_gem_total']=sum(e['value'] for e in rep['gem_records'])
    return g


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("command", choices=["build"])
    ap.add_argument('--work', type=Path, default=WORK)
    ap.add_argument('--uv-polish', action='store_true', help='experimental source-UV bake; retains measured approximation')
    ap.add_argument('--stone-polish',action='store_true',help='experimental original-stone crop and coherent bridge/paving UVs; requires --uv-polish')
    ap.add_argument('--uv-material-limit',type=int,default=128,choices=range(1,129),metavar='1..128',help='source-chart budget within the original 7-bit limit; exact floors/panels remain mandatory')
    a = ap.parse_args()
    if a.stone_polish and not a.uv_polish:ap.error('--stone-polish requires --uv-polish')
    build(a)


if __name__ == "__main__":
    main()

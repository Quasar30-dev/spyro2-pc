"""Spyro 2 (retail PS1) scene reader for the Season of Ice port: terrain regions, textures, collision.

Format oracle: the project's C++ decoders (src/level.cpp, src/terrain.cpp), which cite the retail
functions (8006fa98 relocation, 80024664 / 80028be4 vertex unpack, 80024a00 / 80024c60 strips,
8001c214 collision). This module reads every byte of a region record so it can be re-encoded.

Region record (in the scene's region chunk, directory at 4 + u32 chunk+4):
  +0x00 4 x u16   (header words, not yet named)    +0x08 u16 y0, x0, w12, z0 (origin / 16)
  +0x10 u8 lp_verts, lp_colors, lp_faces, pad      +0x14 u8 hp_verts, hp_colors, hp_faces, lp_words
  +0x18 u32 (0xffffffff in every Glimmer record)
  +0x1c LP mesh: verts*4, colors*4, faces*8  (lp_words words)
        HP mesh: verts*4, colors*8, faces*16
Vertices pack x 11 bits <<21, y 11 bits <<10, z 10 bits; position = (origin + offset) * 16, z * 2.
"""
from __future__ import annotations

import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np

PROJECT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT / "tools"))
from spyrodisc import wad_index  # noqa: E402


def u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def u16(b, o):
    return struct.unpack_from("<H", b, o)[0]


@dataclass
class Region:
    offset: int
    header: bytes
    lp_verts: np.ndarray
    lp_colors: list
    lp_faces: list
    hp_verts: np.ndarray
    hp_colors: list
    hp_faces: list
    size: int
    flags: int = 0

    @property
    def origin(self):
        h = self.header
        return np.array([u16(h, 10), u16(h, 8), u16(h, 14)], float) * 16


@dataclass
class Scene:
    slot: int
    archive: bytes
    children: list
    scene: bytes
    texture_count: int
    descriptors: bytes
    region_chunk: bytes
    regions: list = field(default_factory=list)


def unpack(words: np.ndarray, origin: np.ndarray, vscale: float = 16.0) -> np.ndarray:
    x = (words >> 21).astype(float) * 16
    y = ((words >> 10) & 2047).astype(float) * 16
    z = ((words & 1023) * 2).astype(float) * vscale
    return np.stack([x, y, z], 1) + origin


def read_archive(disc: Path, slot: int) -> bytes:
    with (disc / "WAD.WAD").open("rb") as f:
        head = f.read(0x800)
        off, size = struct.unpack_from("<II", head, 8 * slot)
        f.seek(off)
        return f.read(size)


def read_scene(disc: Path, slot: int) -> Scene:
    arc = read_archive(disc, slot)
    ch = wad_index(arc[:0x10000], len(arc))
    s = arc[ch[1].offset: ch[1].offset + ch[1].size]
    tb, tc = u32(s, 0), u32(s, 4)
    assert tb == 8 + tc * 48
    csz = u32(s, tb)
    R = s[tb: tb + csz]
    sc = Scene(slot, arc, ch, s, tc, s[8: 8 + tc * 48], R)
    d = 4 + u32(R, 4)
    n = u32(R, d)
    for i in range(n):
        a = 4 + u32(R, d + 4 + 4 * i)
        h = R[a: a + 28]
        lv, lc, lf = h[16], h[17], h[18]
        hv, hc, hf, start = h[20], h[21], h[22], h[23]
        p = a + 28
        assert start * 4 == lv * 4 + lc * 4 + lf * 8, (i, start, lv, lc, lf)
        lpv = np.frombuffer(R, "<u4", lv, p); p += lv * 4
        lpc = [R[p + 4 * k: p + 4 * k + 4] for k in range(lc)]; p += lc * 4
        lpf = [R[p + 8 * k: p + 8 * k + 8] for k in range(lf)]; p += lf * 8
        hpv = np.frombuffer(R, "<u4", hv, p); p += hv * 4
        hpc = [R[p + 8 * k: p + 8 * k + 8] for k in range(hc)]; p += hc * 8
        hpf = [R[p + 16 * k: p + 16 * k + 16] for k in range(hf)]; p += hf * 16
        sc.regions.append(Region(a, h, lpv, lpc, lpf, hpv, hpc, hpf, p - a, u16(h, 4)))
    return sc


def hp_mesh(r: Region):
    """HP (near, textured) faces as (quad?, [vertex idx], [color idx], material, raw)."""
    v = unpack(r.hp_verts, r.origin, 2.0 if r.flags & 0x1000 else 16.0)
    out = []
    for f in r.hp_faces:
        tri = f[0] == f[1]
        idx = [f[3], f[2], f[1]] if tri else [f[3], f[2], f[0], f[1]]  # strip order 3,2,0,1
        cidx = [f[7], f[6], f[5]] if tri else [f[7], f[6], f[4], f[5]]
        out.append((not tri, idx, cidx, f[12] & 127, f))
    return v, out


def atlas(sc: Scene) -> np.ndarray:
    """Terrain material atlas (16 tiles per row, 64x64 each, RGBA), as terrain.cpp texture_atlas:
    six 8-byte descriptors per material (two far, four 32x32 near quadrants)."""
    arc = sc.archive
    vram_at = u32(arc, 0)
    vram = np.frombuffer(arc, "<u2", 512 * 512, vram_at).reshape(512, 512)  # x 512..1023
    n = sc.texture_count
    out = np.zeros((((n + 15) // 16) * 64, 16 * 64, 4), np.uint8)
    for m in range(n):
        for q in range(4):
            d = sc.descriptors[(m * 6 + 2 + q) * 8: (m * 6 + 3 + q) * 8]
            page, flags, clut = d[6], d[7], u16(d, 2)
            left, top, right, bottom = d[0], d[1], d[4], d[5]
            depth = (page >> 7) & 1
            ppw = 2 if depth else 4
            for y in range(32):
                for x in range(32):
                    sx, sy = x, y
                    for _ in range((flags >> 4) & 3):
                        sx, sy = 31 - sy, sx
                    if flags & 0x40:
                        sx, sy = sy, sx
                    u = left + sx * (right - left) // 31
                    v = top + sy * (bottom - top) // 31
                    wx = (page & 15) * 64 + u // ppw - 512
                    wy = ((page >> 4) & 1) * 256 + v
                    packed = int(vram[wy, wx])
                    idx = (packed >> ((u % ppw) * (8 if depth else 4))) & (255 if depth else 15)
                    color = int(vram[(clut >> 6) & 511, (clut & 63) * 16 + idx - 512])
                    px = (m % 16) * 64 + (q % 2) * 32 + x
                    py = (m // 16) * 64 + (q // 2) * 32 + y
                    out[py, px, :3] = [((color >> (5 * c)) & 31) * 255 // 31 for c in range(3)]
                    out[py, px, 3] = 0 if color == 0 else (128 if color & 0x8000 else 255)
    return out

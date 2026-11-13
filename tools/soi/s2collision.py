"""Spyro 2 (retail PS1) scene collision chunk: decode and encode (Season of Ice port, PS1 target).

Format oracle: the retail code. 8001c214 unpacks a 12-byte triangle (14-bit base x / y / z, signed 9-bit
x / y deltas, unsigned 8-bit z deltas, units of 16 retail units); 8003ce38 / 8003dc64 walk the grid:
header +0x0c -> a three-level tree of u16 tables (u16 count, count u16 byte offsets from the tree base,
0xffff = empty) indexed by z >> 12, then y >> 12, then x >> 12 (retail units; 4096 = 4 world units);
the leaf value indexes the u16 list array at header +0x10. A cell's list is a run of triangle indices
whose first entry carries bit 15; the run ends at the next entry with bit 15 set. Header +0x14 points to
the triangles, +0x18 to the special-surface flag bytes (one per triangle 0..special-1); +0x04 is the
special count. The chunk is stored length-prefixed in the scene child after part 5 and three words.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass


def u16(b, o):
    return struct.unpack_from("<H", b, o)[0]


def u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def s9(v):
    v &= 511
    return v - 512 if v >= 256 else v


@dataclass
class Collision:
    header: list          # the header words (data-relative offsets as stored)
    tri: bytes            # count x 12 bytes
    flags: bytes          # special-surface flag bytes (header +0x04 count, +0x18 pointer)
    flags2: bytes         # a second per-triangle byte array (header +0x08 count, +0x1c pointer; 8003ce38 -> 8006ab98)
    cells: dict           # (zc, yc, xc) -> [triangle indices]
    raw: bytes            # the whole chunk payload (after the length word)
    extra: bytes = b""    # header +0x20: an optional trailing block (slots 25, 39, 47, 71), kept opaque

    @property
    def count(self):
        return len(self.tri) // 12


def tri_points(t: bytes, i: int):
    """Triangle i as three (x, y, z) points in retail units (8001c214)."""
    x, y, z = struct.unpack_from("<3I", t, 12 * i)
    bx, by, bz = x & 0x3fff, y & 0x3fff, z & 0x3fff
    P = [(bx, by, bz), (bx + s9(x >> 14), by + s9(y >> 14), bz + ((z >> 16) & 255)),
         (bx + s9(x >> 23), by + s9(y >> 23), bz + (z >> 24))]
    return [(a * 16, b * 16, c * 16) for a, b, c in P]


def decode(payload: bytes) -> Collision:
    h = [u32(payload, 4 * k) for k in range(9)]
    count, special, second, tree, lists, tri, flags, flags2 = h[0], h[1], h[2], h[3], h[4], h[5], h[6], h[7]
    cells = {}
    n1 = u16(payload, tree)
    for zc in range(n1):
        o2 = u16(payload, tree + 2 + 2 * zc)
        if o2 == 0xffff:
            continue
        n2 = u16(payload, tree + o2)
        for yc in range(n2):
            o3 = u16(payload, tree + o2 + 2 + 2 * yc)
            if o3 == 0xffff:
                continue
            n3 = u16(payload, tree + o3)
            for xc in range(n3):
                leaf = u16(payload, tree + o3 + 2 + 2 * xc)
                if leaf == 0xffff:
                    continue
                at = lists + 2 * leaf
                run = [u16(payload, at) & 0x7fff]
                at += 2
                while at < tri and not (u16(payload, at) & 0x8000):
                    run.append(u16(payload, at))
                    at += 2
                cells[(zc, yc, xc)] = run
    return Collision(h, payload[tri: tri + 12 * count], payload[flags: flags + special],
                     payload[flags2: flags2 + second], cells, payload, payload[h[8]:] if h[8] else b"")


def encode(tri: bytes, flags: bytes, flags2: bytes, cells: dict, extra: bytes = b"") -> bytes:
    """The chunk payload from triangles, both byte arrays and the cell lists (retail layout: the
    level-1 table, every level-2 table in z order, every level-3 table, 4-byte padding; lists in
    traversal order, unshared, each led by bit 15, closed by 0x8000 and padded)."""
    zs = sorted({k[0] for k in cells})
    n1 = max(zs) + 1 if zs else 0
    l2 = {z: sorted({k[1] for k in cells if k[0] == z}) for z in zs}
    t = bytearray()
    off2, off3 = {}, {}
    at = 2 + 2 * n1
    for z in zs:
        off2[z] = at
        at += 2 + 2 * (max(l2[z]) + 1)
    for z in zs:
        for y in l2[z]:
            off3[(z, y)] = at
            at += 2 + 2 * (max(k[2] for k in cells if k[0] == z and k[1] == y) + 1)
    t += struct.pack("<H", n1) + b"".join(struct.pack("<H", off2.get(z, 0xffff)) for z in range(n1))
    for z in zs:
        n2 = max(l2[z]) + 1
        t += struct.pack("<H", n2) + b"".join(struct.pack("<H", off3.get((z, y), 0xffff)) for y in range(n2))
    lists = []
    leaf = {}
    for z in zs:
        for y in l2[z]:
            xs = sorted(k[2] for k in cells if k[0] == z and k[1] == y)
            for x in xs:
                leaf[(z, y, x)] = len(lists)
                run = cells[(z, y, x)]
                lists += [run[0] | 0x8000] + list(run[1:])
    for z in zs:
        for y in l2[z]:
            n3 = max(k[2] for k in cells if k[0] == z and k[1] == y) + 1
            t += struct.pack("<H", n3) + b"".join(struct.pack("<H", leaf.get((z, y, x), 0xffff)) for x in range(n3))
    if len(t) % 4:
        t += bytes(1) * (4 - len(t) % 4)
    lists.append(0x8000)
    lb = b"".join(struct.pack("<H", v) for v in lists)
    if len(lb) % 4:
        lb += bytes(1) * (4 - len(lb) % 4)
    tree_at = 36
    lists_at = tree_at + len(t)
    tri_at = lists_at + len(lb)
    f_at = tri_at + len(tri)
    fpad = (-len(flags)) % 4            # the second array starts 4-byte aligned
    f2_at = f_at + len(flags) + fpad
    body = bytes(t) + lb + tri + flags + bytes(fpad) + flags2
    if (36 + len(body)) % 4:
        body += bytes(1) * (4 - (36 + len(body)) % 4)
    word8 = 36 + len(body) if extra else 0
    head = struct.pack("<9I", len(tri) // 12, len(flags), len(flags2), tree_at, lists_at, tri_at, f_at, f2_at, word8)
    return head + body + extra


def cells_conservative(tri: bytes, margin: int = 256) -> dict:
    """Cell lists for a new level: every 4096-unit cell touched by a triangle's bounds grown by
    `margin` retail units (a superset of retail's own assignment, which is not reproduced)."""
    cells = {}
    for i in range(len(tri) // 12):
        p = tri_points(tri, i)
        lo = [max(0, min(q[a] for q in p) - margin) >> 12 for a in range(3)]
        hi = [(max(q[a] for q in p) + margin) >> 12 for a in range(3)]
        for z in range(lo[2], hi[2] + 1):
            for y in range(lo[1], hi[1] + 1):
                for x in range(lo[0], hi[0] + 1):
                    cells.setdefault((z, y, x), []).insert(0, i)     # retail lists run high to low
    return cells


def tri_box_overlap(p, lo, hi) -> bool:
    """Separating-axis test of a triangle against an axis-aligned box (Akenine-Moller)."""
    import numpy as np
    c = (np.asarray(lo, float) + np.asarray(hi, float)) / 2
    e = (np.asarray(hi, float) - np.asarray(lo, float)) / 2
    v = [np.asarray(q, float) - c for q in p]
    f = [v[1] - v[0], v[2] - v[1], v[0] - v[2]]
    for a in range(3):
        if min(q[a] for q in v) > e[a] or max(q[a] for q in v) < -e[a]:
            return False
    n = np.cross(f[0], f[1])
    r = float(e @ np.abs(n))
    if abs(float(n @ v[0])) > r:
        return False
    for ei in np.eye(3):
        for fj in f:
            ax = np.cross(ei, fj)
            if not ax.any():
                continue
            pr = [float(ax @ q) for q in v]
            r = float(e @ np.abs(ax))
            if min(pr) > r or max(pr) < -r:
                return False
    return True

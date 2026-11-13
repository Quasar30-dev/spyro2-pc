"""Spyro 2 (retail PS1) terrain region chunk: decode / encode (Season of Ice port, PS1 target).

The chunk is scene part 1 (after the material descriptors). Layout, as relocated by 8006fa98 and read
by terrain.cpp / s2scene.py:
  u32 size, u32 dir_rel (directory at 4 + dir_rel), 28-byte moving-region records (dir_rel > 4 only;
  8003c310: their second u16 is the moving group), u32 n, n x u32 (record offset - 4), the records.
A record: 28-byte header, the far mesh (lp_verts x u32, lp_colors x u32, lp_faces x 8 bytes), the near
mesh (hp_verts x u32, hp_colors x 8 bytes, hp_faces x 16 bytes). Header bytes +16..+23 hold the six
counts and lp_words (= the far mesh size in words, the near mesh's start).
This module rebuilds the chunk from those parts so a new level can be written in the same layout; it
reproduces every retail chunk byte for byte (python tools/soi/s2regions.py).
"""
from __future__ import annotations

import struct
import sys
from dataclasses import dataclass
from pathlib import Path


def u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


@dataclass
class Record:
    header: bytes        # 28 bytes
    lp_verts: bytes
    lp_colors: bytes
    lp_faces: bytes
    hp_verts: bytes
    hp_colors: bytes
    hp_faces: bytes
    tail: bytes = b""    # bytes between this record's end and the next record (padding)


def decode(chunk: bytes):
    size, dir_rel = u32(chunk, 0), u32(chunk, 4)
    d = 4 + dir_rel
    moving = chunk[8:d]
    n = u32(chunk, d)
    offs = [4 + u32(chunk, d + 4 + 4 * i) for i in range(n)]
    recs = []
    order = sorted(range(n), key=lambda i: offs[i])
    ends = {}
    for k, i in enumerate(order):
        ends[i] = offs[order[k + 1]] if k + 1 < n else size
    for i in range(n):
        a = offs[i]
        h = chunk[a: a + 28]
        lv, lc, lf, hv, hc, hf, start = h[16], h[17], h[18], h[20], h[21], h[22], h[23]
        p = a + 28
        parts = []
        for cnt, w in ((lv, 4), (lc, 4), (lf, 8), (hv, 4), (hc, 8), (hf, 16)):
            parts.append(chunk[p: p + cnt * w])
            p += cnt * w
        recs.append(Record(h, *parts, tail=chunk[p: ends[i]]))
    return dict(size=size, moving=moving, records=recs, order=order, head_gap=chunk[d + 4 + 4 * n: offs[order[0]]] if n else b"")


def encode(moving: bytes, records: list, order=None, head_gap: bytes = b"") -> bytes:
    """Records are laid out in `order` (default: directory order); the directory keeps their index."""
    n = len(records)
    order = list(range(n)) if order is None else order
    d = 8 + len(moving)
    at = d + 4 + 4 * n + len(head_gap)
    offs = [0] * n
    body = b""
    for i in order:
        r = records[i]
        offs[i] = at
        blob = r.header + r.lp_verts + r.lp_colors + r.lp_faces + r.hp_verts + r.hp_colors + r.hp_faces + r.tail
        body += blob
        at += len(blob)
    out = struct.pack("<II", at, d - 4) + moving + struct.pack("<I", n)
    out += b"".join(struct.pack("<I", o - 4) for o in offs) + head_gap + body
    return out


def scene_chunk(disc: Path, slot: int):
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
    from spyrodisc import wad_index
    from s2scene import read_archive
    arc = read_archive(disc, slot)
    ch = wad_index(arc[:0x10000], len(arc))
    s = arc[ch[1].offset: ch[1].offset + ch[1].size]
    tb = u32(s, 0)
    return s[tb: tb + u32(s, tb)]


if __name__ == "__main__":
    disc = Path(__file__).resolve().parents[2] / "data/disc"
    ok, bad = 0, []
    for slot in range(15, 74, 2):
        try:
            c = scene_chunk(disc, slot)
        except Exception as e:      # noqa: BLE001
            bad.append((slot, "read", str(e)[:40]))
            continue
        m = decode(c)
        e = encode(m["moving"], m["records"], m["order"], m["head_gap"])
        if e == c:
            ok += 1
        else:
            bad.append((slot, len(e), len(c), next((i for i in range(min(len(e), len(c))) if e[i] != c[i]), None)))
    print(f"region chunks byte-exact: {ok}; differ: {bad}")

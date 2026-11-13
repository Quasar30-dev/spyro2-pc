"""Spyro 2 (retail PS1) scene child (WAD child 1): split into its sections and rebuild (SoI port, PS1).

Sections in order (decode_level / decode_terrain / 8006fa98):
  part 0  u32 size, u32 material count, count x 48 bytes (six 8-byte texture descriptors per material)
  part 1  the terrain region chunk (s2regions.py)
  part 2  region visibility (enabled when size > 8)
  part 3  sky visibility
  part 4  special-surface directory (8002b078)
  part 5  (28 bytes in Glimmer; kept as is)
  12 bytes (three words)
  the collision chunk, u32 size + payload (s2collision.py)
  the sky section (u32 size + data, 8001c5ec)
  a tail up to the child's end (kept as is)
python tools/soi/s2scene_build.py rebuilds every retail scene child from its parsed parts (regions and
collision re-encoded, not copied) and compares it byte for byte.
"""
from __future__ import annotations

import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent))
import s2collision  # noqa: E402
import s2regions  # noqa: E402


def u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def split(child: bytes) -> dict:
    at = 0
    parts = []
    for _ in range(6):
        size = u32(child, at)
        parts.append(child[at: at + size])
        at += size
    words = child[at: at + 12]
    at += 12
    cs = u32(child, at)
    coll = child[at + 4: at + cs]
    at += cs
    ss = u32(child, at)
    sky = child[at: at + ss]
    at += ss
    return dict(parts=parts, words=words, collision=coll, sky=sky, tail=child[at:])


def join(sc: dict) -> bytes:
    coll = sc["collision"]
    return b"".join(sc["parts"]) + sc["words"] + struct.pack("<I", len(coll) + 4) + coll + sc["sky"] + sc["tail"]


def rebuild(child: bytes) -> bytes:
    sc = split(child)
    m = s2regions.decode(sc["parts"][1])
    sc["parts"][1] = s2regions.encode(m["moving"], m["records"], m["order"], m["head_gap"])
    c = s2collision.decode(sc["collision"])
    sc["collision"] = s2collision.encode(c.tri, c.flags, c.flags2, c.cells, c.extra)
    return join(sc)


def archive_children(disc: Path, slot: int):
    from spyrodisc import wad_index
    from s2scene import read_archive
    arc = read_archive(disc, slot)
    return arc, wad_index(arc[:0x10000], len(arc))


if __name__ == "__main__":
    disc = HERE.parents[1] / "data/disc"
    ok, bad = 0, []
    for slot in range(15, 74, 2):
        arc, ch = archive_children(disc, slot)
        child = arc[ch[1].offset: ch[1].offset + ch[1].size]
        out = rebuild(child)
        if out == child:
            ok += 1
        else:
            bad.append((slot, len(out), len(child)))
    print(f"scene children rebuilt byte-exact: {ok}; differ: {bad}")

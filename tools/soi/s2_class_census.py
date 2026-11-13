"""Census of Spyro 2 retail object classes per campaign level (read-only), for the SoI mapping table.

Reads the gameplay child (index 3) of each scene slot the way 8001c734 does (docs/OBJECTS_AND_GEMS.md):
from +0x2c skip eight length-prefixed chunks; the ninth holds u32 size, u32 count and 88-byte records
with the class at +0x36. Level names come from the executable table 800649b4 (slot = 15 + 2 * i).
"""
import json, struct, sys, collections
from pathlib import Path
PROJECT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT / "tools"))
sys.path.insert(0, str(PROJECT / "tools" / "soi"))
from spyrodisc import wad_index
from s2_budgets import exe_names

def main():
    disc = PROJECT / "data" / "disc"
    names = exe_names((disc / "SCUS_944.25").read_bytes())
    f = (disc / "WAD.WAD").open("rb"); head = f.read(0x800)
    toc = [struct.unpack_from("<II", head, 8 * i) for i in range(197)]
    out = {}
    total = gems = 0
    for i, name in enumerate(names):
        slot = 15 + 2 * i
        off, size = toc[slot]; f.seek(off)
        e = wad_index(f.read(0x10000), size)[3]
        f.seek(off + e.offset); g = f.read(e.size)
        p = 0x2c
        for _ in range(8):
            p += struct.unpack_from("<I", g, p)[0]
        n = struct.unpack_from("<I", g, p + 4)[0]
        cls = collections.Counter(struct.unpack_from("<H", g, p + 8 + 88 * k + 0x36)[0] for k in range(n))
        total += n; gems += cls.get(1, 0)
        out[name] = dict(slot=slot, records=n, classes={f"{c:03x}": v for c, v in sorted(cls.items())})
    (PROJECT / "data" / "soi" / "ps1" / "s2_class_census.json").write_text(json.dumps(out, indent=1))
    print("campaign records", total, "class-1 records", gems)

if __name__ == "__main__":
    main()

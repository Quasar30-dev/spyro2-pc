"""Build the Season of Ice PS1 mod disc: retail Spyro 2 with Market Mesa in Glimmer's slot (scene 17).

The new slot-17 archive (tools/soi/mesa_ps1.py) is larger than Glimmer's: it is written in slot 17's place and every
later slot moves by the growth (2048-aligned; WAD.WAD grows into the 3,116 empty sectors before SPEECH.STR at LBA
90,000), so TOC offsets keep increasing as the PC build's parser requires. WAD.WAD's ISO directory size is updated.
The image carries no test fixture: the pad fixture and shots run over DuckStation's GDB server at play time only. Every other sector is retail's; sectors are re-encoded (EDC / ECC)
by ps1disc. The retail disc is only read.

    python tools/soi/mesa_disc.py [VERSION]   # -> D:/Theia-staging/spyro2-mod/Spyro2-SoI-MarketMesa-<VERSION>.bin / .cue
"""
from __future__ import annotations

import json
import argparse
import hashlib
import struct
import sys
import time
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent))
import ps1disc  # noqa: E402

STAGE = Path("D:/Theia-staging/spyro2-mod")
ARCHIVE = STAGE / "work" / "slot17-mesa.bin"
VERSION = "test05"
OUT_BIN = STAGE / f"Spyro2-SoI-MarketMesa-{VERSION}.bin"
OUT_CUE = STAGE / f"Spyro2-SoI-MarketMesa-{VERSION}.cue"
SLOT = 17
WAD_LBA = 500


def find_record(user: np.ndarray, name: bytes):
    """The ISO9660 directory record of `name` in the root directory: (lba, byte offset in that sector)."""
    pvd = bytes(user[16, :2048])
    assert pvd[1:6] == b"CD001"
    root = pvd[156:156 + 34]
    lba, size = struct.unpack_from("<I", root, 2)[0], struct.unpack_from("<I", root, 10)[0]
    for s in range(lba, lba + (size + 2047) // 2048):
        sec = bytes(user[s, :2048])
        o = 0
        while o < 2048 and sec[o]:
            ln = sec[o]
            nl = sec[o + 32]
            if sec[o + 33:o + 33 + nl] == name:
                return s, o
            o += ln
    raise SystemExit(f"{name!r} not in the root directory")


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('version',nargs='?',default=VERSION)
    ap.add_argument('--archive',type=Path,default=ARCHIVE)
    ap.add_argument('--report',type=Path,default=HERE.parents[1]/'data/soi/ps1/mesa-disc-report.json')
    options=ap.parse_args()
    out_bin=STAGE/f'Spyro2-SoI-MarketMesa-{options.version}.bin'
    out_cue=out_bin.with_suffix('.cue')
    if out_bin.exists() or out_cue.exists():
        raise SystemExit('Refusing to replace an existing image: '+str(out_bin))
    t0 = time.time()
    arc = options.archive.read_bytes()
    model = ps1disc.DiscModel(ps1disc.RETAIL_CUE)
    user, stats = ps1disc.rebuild(model, log=lambda *a: None)
    wad = next(f for f in stats["files"] if f["path"] == "WAD.WAD")
    assert wad["lba"] == WAD_LBA
    old_size = wad["size"]
    toc = stats["wad"]["toc"]
    # slot 17 grows in place and every later slot moves by the growth, so offsets keep increasing (the PC
    # build's WAD parser requires that; the retail loader only reads TOC offsets from LBA 500)
    o17, s17 = toc[SLOT]
    new17 = len(arc) + (-len(arc)) % 2048
    delta = new17 - (s17 + (-s17) % 2048)
    assert delta >= 0 and delta % 2048 == 0, delta
    wad_old = bytes(user[WAD_LBA: WAD_LBA + wad["sectors"], :2048].reshape(-1))[:old_size]
    new_size = old_size + delta
    wad_new = bytearray(new_size + (-new_size) % 2048)
    wad_new[:2048] = wad_old[:2048]
    new_toc = []
    for i, (o, n) in enumerate(toc):
        if i == SLOT:
            new_toc.append((o, len(arc)))
            wad_new[o:o + len(arc)] = arc
            continue
        no = o + delta if o > o17 else o
        new_toc.append((no, n))
        if n:
            wad_new[no:no + n] = wad_old[o:o + n]
    for i, (o, n) in enumerate(new_toc):
        struct.pack_into("<II", wad_new, 8 * i, o, n)
    nsec_old, nsec_new = wad["sectors"], len(wad_new) // 2048
    new_off = o17
    gap_end = min(f["lba"] for f in stats["files"] if f["lba"] > WAD_LBA)
    assert WAD_LBA + nsec_new <= gap_end, ("WAD would overrun the next file", WAD_LBA + nsec_new, gap_end)
    # the gap holds empty form-2 sectors (subheader 00 00 20 00): turn the ones WAD.WAD now covers into form-1
    # data sectors like WAD.WAD's own, and move the end-of-file mark (submode 0x89) to the new last sector
    gap = slice(WAD_LBA + nsec_old, WAD_LBA + nsec_new)
    if (model.raw[gap, 24:24 + 2324] != 0).any():
        raise SystemExit("gap sectors are not empty")
    mid = model.sub[WAD_LBA + 1].copy()
    last = model.sub[WAD_LBA + nsec_old - 1].copy()
    model.sub[WAD_LBA + nsec_old - 1] = mid
    model.sub[gap] = mid
    model.sub[WAD_LBA + nsec_new - 1] = last
    model.form2[gap] = False
    model.edc_zero[gap] = False
    user[WAD_LBA: WAD_LBA + nsec_new, :2048] = np.frombuffer(bytes(wad_new), np.uint8).reshape(-1, 2048)
    user[WAD_LBA + nsec_new: WAD_LBA + nsec_new, :] = 0
    # ISO directory size of WAD.WAD (both-endian at +10)
    ds, do = find_record(user, b"WAD.WAD;1")
    sec = bytearray(user[ds, :2048])
    assert struct.unpack_from("<I", sec, do + 10)[0] == old_size
    struct.pack_into("<I", sec, do + 10, new_size)
    struct.pack_into(">I", sec, do + 14, new_size)
    user[ds, :2048] = np.frombuffer(bytes(sec), np.uint8)
    STAGE.mkdir(parents=True, exist_ok=True)
    sha, differing, first_diff = ps1disc.stream_image(model, user, out_bin)
    out_cue.write_text(f'FILE "{out_bin.name}" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n')
    # read back: TOC, slot bytes, directory size
    back = ps1disc.DiscModel(out_cue)
    wb = bytes(back.raw[WAD_LBA, 24:24 + 2048])
    bo, bs = struct.unpack_from("<II", wb, 8 * SLOT)
    first_lba = WAD_LBA + bo // 2048
    got = bytes(back.raw[first_lba: first_lba + (bs + 2047) // 2048, 24:24 + 2048].reshape(-1))[:bs]
    ok = got == arc and (bo, bs) == (new_off, len(arc))
    # every other slot equal to retail; TOC offsets increasing (the PC build's WAD parser)
    btoc = [struct.unpack_from("<II", wb, 8 * i) for i in range(len(toc))]
    used = [(o, n) for o, n in btoc if o or n]
    mono = all(used[k][0] >= used[k - 1][0] + used[k - 1][1] for k in range(1, len(used)))
    others = 0
    for i, ((o, n), (ro, rn)) in enumerate(zip(btoc, toc)):
        if i == SLOT or not n:
            continue
        a = bytes(back.raw[WAD_LBA + o // 2048: WAD_LBA + (o + n + 2047) // 2048, 24:24 + 2048].reshape(-1))[:n]
        r = bytes(model.raw[WAD_LBA + ro // 2048: WAD_LBA + (ro + rn + 2047) // 2048, 24:24 + 2048].reshape(-1))[:rn]
        others += a != r
    print(f"readback: slot 17 equal {ok}, TOC increasing {mono}, other slots differing {others}")
    ok = ok and mono and others == 0
    rep = dict(image=str(out_bin), sha256=sha, archive=str(options.archive.resolve()), archive_sha256=hashlib.sha256(arc).hexdigest(),
               fixture=False, sectors_changed_vs_retail=differing, first_changed=first_diff,
               slot17=dict(offset=new_off, size=len(arc), lba=first_lba), wad_size=[old_size, new_size],
               wad_sectors=[nsec_old, nsec_new], gap_end_lba=gap_end, readback_slot_equal=ok,
               seconds=round(time.time() - t0))
    print(json.dumps(rep, indent=1))
    options.report.write_text(json.dumps(rep, indent=1))
    print("DISC BUILD", "PASS" if ok else "FAIL")
    if not ok:
        raise SystemExit(1)


if __name__ == "__main__":
    main()

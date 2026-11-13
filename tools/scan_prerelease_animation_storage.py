"""Bounded scan for unused Spyro descriptors in the supplied early WADs.

Includes descriptors outside directories and alternate common face banks. A
negative result does not prove absence from deleted disc sectors or a different
codec. No assets are downloaded, modified, or embedded in source.
"""
from pathlib import Path
import hashlib, json, struct
import numpy as np
from spyrodisc import wad_index, u32
from probe_spyro_model import decode

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'data/analysis/prerelease-20260930'
report = {}
for build, sub in [('july-1999', ''), ('winter-jampack-1999', 'SPYRO2/')]:
    wad = (ROOT / 'data/builds' / build / 'disc' / (sub + 'WAD.WAD')).read_bytes()
    entries = wad_index(wad[:65536], len(wad))
    e = entries[0]
    global_bank = wad[e.offset:e.offset + e.size]
    base = u32(global_bank, 4)
    common_at = base + u32(global_bank, base + 56)
    common = global_bank[common_at:]
    known = {}
    for i in range(u32(global_bank, base)):
        pointer = u32(global_bank, base + 60 + i * 4)
        if pointer != 0xffffffff:
            known[e.offset + base + pointer] = dict(bank=0, clip=i, kind='global')
    for e in entries[1:]:
        a = wad[e.offset:e.offset + e.size]
        if len(a) < 0x80000 or u32(a, 0) != 0x800:
            continue
        if not all(0x800 <= u32(a, i*8) <= len(a) and
                   u32(a, i*8) + u32(a, i*8+4) <= len(a) for i in range(4)):
            continue
        directory = 0x20 if u32(a, 0x20) == u32(a, 0x10) else 0x40
        p = u32(a, directory)
        for _ in range(63):
            if p + 4 > len(a):
                break
            clip = u32(a, p)
            if clip == 0xffffffff:
                break
            if clip >= 62 or p+8+u32(a,p+4) > len(a):
                raise ValueError('Invalid referenced scene animation')
            known[e.offset+p+8] = dict(bank=e.index, clip=clip, kind='scene')
            p += 8 + u32(a, p+4)
    # An early descriptor is 4 bytes of dimensions followed by four offsets.
    # Allow different vertex counts/partitions, unlike a 230-vertex-only scan.
    dimensions = np.frombuffer(wad[:len(wad)//4*4], dtype=np.uint8).reshape(-1, 4)
    mask = ((dimensions[:,0] > 0) & (dimensions[:,0] <= 128) &
            (dimensions[:,1] > 0) & (dimensions[:,1] <= 128) &
            (dimensions[:,3] >= 32) & (dimensions[:,2] <= dimensions[:,3]))
    hits = []
    for p in np.flatnonzero(mask)*4:
        p = int(p)
        if p+20 > len(wad): continue
        frames, colors, split, vertices = wad[p:p+4]
        v, f, c, stream = struct.unpack_from('<4I', wad, p+4)
        if f+4 > len(common) or c+colors*4 > len(common) or p+20+frames*20 > len(wad): continue
        length = u32(common, f)
        if not length or length % 20 or length > 30000 or f+4+length > len(common): continue
        valid = True
        for q in range(f+4, f+4+length, 20):
            vi, ci = struct.unpack_from('<2I', common, q)
            if any((vi >> s & 255) >= vertices for s in (24,16,8,0)) or any((ci >> s & 127) >= colors for s in (24,17,10,3)):
                valid = False; break
        if not valid: continue
        if p not in known:
            if v+vertices*4 > len(common) or stream >= len(common): continue
            # Bits 22..31 of control and 30..31 of the three 10-bit
            # partition cursors are unused. Texture words often resemble
            # descriptors until the bounded frame directory is inspected.
            for frame in range(frames):
                control, cursors = struct.unpack_from('<2I', wad, p+20+frame*20)
                if control >> 22 or cursors >> 30:
                    valid = False; break
                start = stream + (control & 65535)
                if any(start+x >= len(common) for x in (0, cursors & 1023, cursors >> 10 & 1023, cursors >> 20)):
                    valid = False; break
            if not valid: continue
        hits.append(dict(wad_offset=hex(p), frames=frames, vertices=vertices,
                         split=split, faces=length//20, reference=known.get(p)))
    exe_name = 'SPYRO2.EXE' if not sub else 'SPYRO2/DEMO2.EXE'
    exe = (ROOT/'data/builds'/build/'disc'/exe_name).read_bytes()
    codebook_va = 0x800649d8 if not sub else 0x80069134
    lookup = struct.unpack_from('<125I',exe,codebook_va-u32(exe,24)+0x800)
    accepted, rejected = [], []
    for hit in hits:
        if hit['reference'] is not None: continue
        p = int(hit['wad_offset'],16)
        candidate = bytearray(global_bank)
        struct.pack_into('<I',candidate,base+60+16*4,len(candidate)-base)
        candidate += wad[p:p+20+hit['frames']*20]
        try:
            for frame in range(hit['frames']): decode(candidate,16,frame,lookup)
            accepted.append(hit)
        except (ValueError,IndexError,struct.error) as error:
            rejected.append(dict(**hit, rejected_frame=frame, reason=str(error)))
    report[build] = dict(wad_bytes=len(wad), global_bytes=len(global_bank),
        global_sha256=hashlib.sha256(global_bank).hexdigest(),
        referenced_descriptors=len(known), structurally_valid_candidates=hits,
        unreferenced_candidates=accepted, rejected_candidates=rejected)
    print(build, 'referenced',len(known),'candidates',len(hits),
          'unreferenced',len(report[build]['unreferenced_candidates']), flush=True)
(OUT/'animation-storage-scan.json').write_text(json.dumps(report, indent=2))

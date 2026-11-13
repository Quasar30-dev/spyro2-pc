"""Spyro 2 PS1 disc (MODE2/2352) reader and rebuilder: the first step of the ROM-mod pipeline.

  python tools/soi/ps1disc.py roundtrip [--cue CUE] [--out-dir D:/Theia-staging/spyro2-mod] [--keep]
  python tools/soi/ps1disc.py manifest  [--cue CUE] [--out JSON]

The round trip decomposes the retail image into a model and rebuilds every raw sector from it:

  * ISO 9660 file extents (spyrodisc.Disc), file bytes and their sector tail padding;
  * WAD.WAD rebuilt from its own table of contents: 197 (offset, size) slot pairs read the way the
    retail executable reads them (SCUS_944.25 8001_1b70: LBA 500 -> 0x800 bytes; 8001b40c copies
    0x628 = 197 * 8 bytes to 800676d8; slot reads go through 80013810(base LBA, dst, size, offset),
    which seeks to base + offset >> 11, so offsets must be 2048-aligned);
  * everything outside a file extent (system area, volume descriptors, path tables, directories,
    the gap after WAD.WAD, the post-gap) as raw user data;
  * the XA subheader of every sector (file/channel/submode/coding; both copies);
  * then re-encodes sync, MSF header, EDC (Mode 2 Form 1 and Form 2) and Form 1 P/Q ECC.

A sector-exact SHA-256 match with the retail image proves the model holds everything a modded
image needs. The rebuilt copy is written to the staging directory, checked, then deleted with the
Theia safe_delete helper unless --keep is given. The retail image is opened read-only.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import subprocess
import sys
import time
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parents[1]
sys.path.insert(0, str(PROJECT / "tools"))
from spyrodisc import Disc  # noqa: E402

RETAIL_CUE = PROJECT / "Spyro 2 - Riptos Rage (USA)" / "Spyro 2 - Ripto's Rage! (USA).cue"
RETAIL_SHA256 = None  # measured on first run and recorded in the manifest
SAFE_DELETE = Path("C:/Users/Shadow/Documents/Theia/tools/claude_guard/safe_delete.py")
SECTOR = 2352
SYNC = bytes([0] + [0xFF] * 10 + [0])
WAD_LBA, WAD_SLOTS = 500, 197  # SCUS_944.25: 80011b70 (LBA 500), 80011ba8 (0x628 bytes)

# ------------------------------------------------------------------ EDC / ECC (ECMA-130)

def _edc_lut():
    lut = np.zeros(256, np.uint32)
    for i in range(256):
        e = i
        for _ in range(8):
            e = (e >> 1) ^ (0xD8018001 if e & 1 else 0)
        lut[i] = e
    return lut


EDC_LUT = _edc_lut()


def _edc_distance_table(maxlen: int) -> np.ndarray:
    """T[d][b] = EDC of byte b followed by d zero bytes (init 0). EDC is linear, so the EDC of a
    block is the XOR of T[len-1-i][block[i]]."""
    t = np.zeros((maxlen, 256), np.uint32)
    t[0] = EDC_LUT
    for d in range(1, maxlen):
        prev = t[d - 1]
        t[d] = (prev >> 8) ^ EDC_LUT[prev & 0xFF]
    return t


EDC_T = _edc_distance_table(0x91C)


def edc(block: np.ndarray) -> np.ndarray:
    """EDC of each row of an (N, L) uint8 array."""
    L = block.shape[1]
    pos = np.arange(L - 1, -1, -1)
    return np.bitwise_xor.reduce(EDC_T[pos[None, :], block], axis=1)


F_LUT = np.zeros(256, np.uint8)
B_LUT = np.zeros(256, np.uint8)
for _i in range(256):
    _j = ((_i << 1) ^ (0x11D if _i & 0x80 else 0)) & 0xFF
    F_LUT[_i] = _j
    B_LUT[_i ^ _j] = _i


def _ecc_index(major_count, minor_count, major_mult, minor_inc):
    size = major_count * minor_count
    idx = np.zeros((major_count, minor_count), np.int64)
    for major in range(major_count):
        index = (major >> 1) * major_mult + (major & 1)
        for minor in range(minor_count):
            idx[major, minor] = index
            index += minor_inc
            if index >= size:
                index -= size
    return idx


P_IDX = _ecc_index(86, 24, 2, 86)
Q_IDX = _ecc_index(52, 43, 86, 88)


def _ecc_block(src: np.ndarray, idx: np.ndarray) -> np.ndarray:
    n_major, n_minor = idx.shape
    a = np.zeros((src.shape[0], n_major), np.uint8)
    b = np.zeros_like(a)
    for minor in range(n_minor):
        t = src[:, idx[:, minor]]
        a ^= t
        b ^= t
        a = F_LUT[a]
    a = B_LUT[F_LUT[a] ^ b]
    return np.concatenate([a, a ^ b], axis=1)


def encode(sectors: np.ndarray, lba0: int, form2: np.ndarray, edc_zero: np.ndarray) -> None:
    """Fill sync, header, EDC and ECC in place. sectors: (N, 2352) with subheaders and user data set."""
    n = sectors.shape[0]
    sectors[:, :12] = np.frombuffer(SYNC, np.uint8)
    lba = np.arange(lba0, lba0 + n) + 150
    mm, ss, ff = lba // (75 * 60), (lba // 75) % 60, lba % 75
    bcd = lambda v: ((v // 10) << 4 | (v % 10)).astype(np.uint8)
    sectors[:, 12], sectors[:, 13], sectors[:, 14], sectors[:, 15] = bcd(mm), bcd(ss), bcd(ff), 2
    f1 = ~form2
    if f1.any():
        s1 = sectors[f1]
        e = edc(s1[:, 16:16 + 0x808])
        s1[:, 0x818:0x81C] = e.view(np.uint8).reshape(-1, 4)
        work = s1[:, 12:].copy()
        work[:, :4] = 0  # Mode 2: the header is excluded from the ECC
        work[:, 0x810:0x810 + 172] = _ecc_block(work, P_IDX)
        work[:, 0x8BC:0x8BC + 104] = _ecc_block(work, Q_IDX)
        s1[:, 0x81C:] = work[:, 0x810:]
        sectors[f1] = s1
    if form2.any():
        s2 = sectors[form2]
        e = edc(s2[:, 16:16 + 0x91C])
        e[edc_zero[form2]] = 0
        s2[:, 0x92C:0x930] = e.view(np.uint8).reshape(-1, 4)
        sectors[form2] = s2


# ------------------------------------------------------------------ disc model

def bin_path(cue: Path) -> Path:
    text = cue.read_text(encoding="utf-8-sig")
    m = re.search(r'FILE\s+"(.+)"\s+BINARY', text, re.I)
    if not m or "MODE2/2352" not in text.upper():
        raise SystemExit("expected one MODE2/2352 BINARY track")
    return cue.parent / m.group(1)


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        while chunk := f.read(1 << 24):
            h.update(chunk)
    return h.hexdigest()


class DiscModel:
    def __init__(self, cue: Path):
        self.cue = cue
        self.bin = bin_path(cue)
        raw = np.memmap(self.bin, dtype=np.uint8, mode="r")
        if raw.size % SECTOR:
            raise SystemExit("partial sector")
        self.raw = raw.reshape(-1, SECTOR)
        self.n = self.raw.shape[0]
        if not (self.raw[:, :12] == np.frombuffer(SYNC, np.uint8)).all() or not (self.raw[:, 15] == 2).all():
            raise SystemExit("expected Mode 2 sectors throughout")
        sub = self.raw[:, 16:24]
        if not (sub[:, :4] == sub[:, 4:]).all():
            raise SystemExit("subheader copies differ")
        self.sub = np.array(sub[:, :4])
        self.form2 = (self.sub[:, 2] & 0x20) != 0
        stored = self.raw[:, 0x92C:0x930].copy().view("<u4").ravel()
        self.edc_zero = self.form2 & (stored == 0)
        with Disc(cue) as d:
            self.files = sorted(d.entries(), key=lambda e: e.lba)

    def user(self, i0, i1):
        """User data of sectors [i0, i1): 2324 bytes (form 2) or 2048 (form 1) in a 2324 field."""
        blk = self.raw[i0:i1]
        out = np.zeros((i1 - i0, 2324), np.uint8)
        f2 = self.form2[i0:i1]
        out[f2] = blk[f2, 24:24 + 2324]
        out[~f2, :2048] = blk[~f2, 24:24 + 2048]
        return out

    def wad_toc(self, wad: bytes):
        # Retail physically stores197 entries and caches197 at startup. An
        # expanded disc may append physical entries in the same0x800 header;
        # its asset loader handles access beyond that original cached range.
        boundary = struct.unpack_from("<I", wad, 0)[0]
        assert boundary == 0x800 and len(wad) >= boundary
        toc = []
        for at in range(0, boundary, 8):
            entry = struct.unpack_from("<II", wad, at)
            if entry == (0, 0): break
            toc.append(entry)
        assert len(toc) >= WAD_SLOTS
        return toc


def rebuild_wad(wad: bytes, toc) -> tuple[bytes, dict]:
    """Rebuild WAD.WAD from its header and its slot payloads only. Returns the bytes and stats."""
    size = len(wad)
    out = bytearray(size)
    header_end = min(o for o, s in toc if s) if any(s for o, s in toc) else 0x800
    out[:header_end] = wad[:header_end]  # the 197-slot TOC (0x628 bytes) and its sector padding
    unaligned = 0
    covered = header_end
    for o, s in toc:
        if s == 0:
            continue
        if o % 2048:
            unaligned += 1
        out[o:o + s] = wad[o:o + s]
        covered += s
    nonzero_gap = sum(1 for i in range(0, size, 2048) if out[i:i + 2048] != wad[i:i + 2048])
    return bytes(out), dict(header_bytes=header_end, slots=len(toc), used_slots=sum(1 for o, s in toc if s),
                            unaligned_offsets=unaligned, payload_bytes=covered - header_end,
                            sectors_differing_after_slot_rebuild=nonzero_gap)


def rebuild(model: DiscModel, log=print):
    """Assemble user data from the model's components, then re-encode every raw sector."""
    n = model.n
    owned = np.zeros(n, bool)
    user = np.zeros((n, 2324), np.uint8)
    stats = dict(files=[], unowned_sectors=0, unowned_nonzero_sectors=0, tail_padding_nonzero=0)
    wad_stats = None
    for f in model.files:
        nsec = (f.size + 2047) // 2048
        lba = f.lba
        ff2 = model.form2[lba:lba + nsec]
        if ff2.any():
            # streamed XA file (SPEECH.STR, S00.XA ...): its sectors' user data is the file
            user[lba:lba + nsec] = model.user(lba, lba + nsec)
            kind = "xa-stream"
        else:
            data = bytes(model.raw[lba:lba + nsec, 24:24 + 2048].reshape(-1))
            body = data[:f.size]
            if f.path == "WAD.WAD":
                toc = model.wad_toc(body)
                body, wad_stats = rebuild_wad(body, toc)
                if body != data[:f.size]:
                    raise SystemExit("WAD rebuild from TOC + slots differs")
                wad_stats["toc"] = [[o, s] for o, s in toc]
            tail = data[f.size:]
            if any(tail):
                stats["tail_padding_nonzero"] += 1
            buf = np.zeros(nsec * 2048, np.uint8)
            buf[:f.size] = np.frombuffer(body, np.uint8)
            buf[f.size:] = np.frombuffer(tail, np.uint8)
            user[lba:lba + nsec, :2048] = buf.reshape(nsec, 2048)
            kind = "form1"
        owned[lba:lba + nsec] = True
        stats["files"].append(dict(path=f.path, lba=lba, size=f.size, sectors=nsec, kind=kind))
    free = np.flatnonzero(~owned)
    for i0 in range(0, len(free), 8192):
        idx = free[i0:i0 + 8192]
        u = np.zeros((len(idx), 2324), np.uint8)
        f2 = model.form2[idx]
        blk = model.raw[idx]
        u[f2] = blk[f2, 24:24 + 2324]
        u[~f2, :2048] = blk[~f2, 24:24 + 2048]
        user[idx] = u
        stats["unowned_nonzero_sectors"] += int((u != 0).any(axis=1).sum())
    stats["unowned_sectors"] = int(len(free))
    stats["wad"] = wad_stats
    return user, stats


def stream_image(model: DiscModel, user: np.ndarray, out_path: Path | None, chunk=8192):
    """Encode the image chunk by chunk; hash it and optionally write it. Returns sha256 and the
    number of sectors that differ from the retail image."""
    h = hashlib.sha256()
    differing = 0
    first_diff = None
    fh = out_path.open("wb") if out_path else None
    try:
        for i0 in range(0, model.n, chunk):
            i1 = min(model.n, i0 + chunk)
            s = np.zeros((i1 - i0, SECTOR), np.uint8)
            s[:, 16:20] = model.sub[i0:i1]
            s[:, 20:24] = model.sub[i0:i1]
            f2 = model.form2[i0:i1]
            s[f2, 24:24 + 2324] = user[i0:i1][f2]
            s[~f2, 24:24 + 2048] = user[i0:i1][~f2, :2048]
            encode(s, i0, f2, model.edc_zero[i0:i1])
            # Expanded images may append new file extents beyond the source
            # image. Keep the source read-only without allocating a duplicate
            # raw image merely to compare its nonexistent appended sectors.
            old_end = min(i1, len(model.raw))
            d = np.ones(i1-i0, bool)
            if old_end > i0:
                d[:old_end-i0] = (s[:old_end-i0] != model.raw[i0:old_end]).any(axis=1)
            if d.any():
                differing += int(d.sum())
                if first_diff is None:
                    first_diff = int(i0 + np.flatnonzero(d)[0])
            b = s.tobytes()
            h.update(b)
            if fh:
                fh.write(b)
    finally:
        if fh:
            fh.close()
    return h.hexdigest(), differing, first_diff


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("command", choices=["roundtrip", "manifest"])
    ap.add_argument("--cue", type=Path, default=RETAIL_CUE)
    ap.add_argument("--out-dir", type=Path, default=Path("D:/Theia-staging/spyro2-mod"))
    ap.add_argument("--out", type=Path, default=PROJECT / "data" / "soi" / "ps1" / "retail-disc-model.json")
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    t0 = time.time()
    model = DiscModel(a.cue)
    retail_sha = sha256_file(model.bin)
    print(f"retail image {model.bin.name}: {model.n} sectors, sha256 {retail_sha}")
    sub_keys = (model.sub[:, 0].astype(np.uint32) << 24 | model.sub[:, 1].astype(np.uint32) << 16 |
                model.sub[:, 2].astype(np.uint32) << 8 | model.sub[:, 3])
    runs = int((np.diff(sub_keys) != 0).sum() + 1)
    user, stats = rebuild(model)
    files = stats["files"]
    wad = next(f for f in files if f["path"] == "WAD.WAD")
    nxt = min(f["lba"] for f in files if f["lba"] > wad["lba"])
    info = dict(retail_bin=str(model.bin), retail_sha256=retail_sha, sectors=model.n,
                form2_sectors=int(model.form2.sum()), form2_edc_zero=int(model.edc_zero.sum()),
                subheader_runs=runs, files=files, unowned_sectors=stats["unowned_sectors"],
                unowned_nonzero_sectors=stats["unowned_nonzero_sectors"],
                tail_padding_nonzero_files=stats["tail_padding_nonzero"],
                wad=dict(lba=wad["lba"], size=wad["size"], sectors=wad["sectors"],
                         free_sectors_before_next_file=nxt - (wad["lba"] + wad["sectors"]),
                         next_file_lba=nxt, **{k: v for k, v in stats["wad"].items() if k != "toc"}),
                wad_toc=stats["wad"]["toc"])
    if a.command == "manifest":
        a.out.parent.mkdir(parents=True, exist_ok=True)
        a.out.write_text(json.dumps(info, indent=1))
        print("wrote", a.out)
        return
    # 1) in-memory re-encode check
    sha_mem, differing, first = stream_image(model, user, None)
    print(f"re-encoded in memory: sha256 {sha_mem}, differing sectors {differing} (first {first}) "
          f"[{time.time() - t0:.0f}s]")
    if sha_mem != retail_sha:
        raise SystemExit("round trip FAILED in memory")
    # 2) write the rebuilt image to staging, hash the written file
    a.out_dir.mkdir(parents=True, exist_ok=True)
    rt_dir = a.out_dir / "roundtrip"
    rt_dir.mkdir(exist_ok=True)
    out_bin = rt_dir / "Spyro2-roundtrip.bin"
    out_cue = rt_dir / "Spyro2-roundtrip.cue"
    stream_image(model, user, out_bin)
    out_cue.write_text(f'FILE "{out_bin.name}" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n')
    written = sha256_file(out_bin)
    ok = written == retail_sha
    print(f"written {out_bin}: sha256 {written} -> {'MATCH' if ok else 'MISMATCH'} [{time.time() - t0:.0f}s]")
    info["roundtrip"] = dict(date=time.strftime("%Y-%m-%d %H:%M"), in_memory_sha256=sha_mem,
                             written_sha256=written, match=ok, written_to=str(out_bin),
                             seconds=round(time.time() - t0))
    if ok and not a.keep:
        res = subprocess.run([sys.executable, str(SAFE_DELETE), str(out_bin), str(out_cue)],
                             capture_output=True, text=True)
        info["roundtrip"]["deleted"] = res.returncode == 0 and not out_bin.exists()
        info["roundtrip"]["delete_log"] = (res.stdout + res.stderr).strip()[-400:]
        print("deleted round-trip copy:", info["roundtrip"]["deleted"])
    a.out.parent.mkdir(parents=True, exist_ok=True)
    a.out.write_text(json.dumps(info, indent=1))
    print("wrote", a.out)
    if not ok:
        raise SystemExit(1)


if __name__ == "__main__":
    main()

"""Disassemble retail Spyro 2 (SCUS_944.25) ranges with capstone (the oracle for the PS1 encoder's unknowns).

    python tools/soi/mipsdis.py START END            # main executable, addresses in hex
    python tools/soi/mipsdis.py START END --slot 16   # a WAD overlay slot (loaded at its own base, --base)
"""
from __future__ import annotations

import argparse
import struct
from pathlib import Path

import capstone

PROJECT = Path(__file__).resolve().parents[2]
EXE = PROJECT / "data" / "disc" / "SCUS_944.25"


def exe_image():
    b = EXE.read_bytes()
    load = struct.unpack_from("<I", b, 0x18)[0]
    return load, b[0x800:]


def dis(code: bytes, base: int, start: int, end: int):
    md = capstone.Cs(capstone.CS_ARCH_MIPS, capstone.CS_MODE_MIPS32 + capstone.CS_MODE_LITTLE_ENDIAN)
    out = []
    for a in range(start, end, 4):
        w = code[a - base: a - base + 4]
        if len(w) < 4:
            break
        ins = list(md.disasm(w, a))
        txt = f"{ins[0].mnemonic} {ins[0].op_str}" if ins else f".word 0x{struct.unpack('<I', w)[0]:08x}"
        out.append(f"{a:08x}  {struct.unpack('<I', w)[0]:08x}  {txt}")
    return out


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("start")
    ap.add_argument("end")
    ap.add_argument("--slot", type=int)
    ap.add_argument("--base", default="0")
    a = ap.parse_args()
    if a.slot is not None:
        import sys
        sys.path.insert(0, str(PROJECT / "tools" / "soi"))
        from s2scene import read_archive
        code, base = read_archive(PROJECT / "data" / "disc", a.slot), int(a.base, 16)
    else:
        base, code = exe_image()
    print("\n".join(dis(code, base, int(a.start, 16), int(a.end, 16))))

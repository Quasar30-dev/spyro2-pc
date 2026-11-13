"""Shared Season of Ice (GBA, ASYE) helpers: ROM access and the GBA BIOS decompressors.

The ROM is opened read-only. The decompressors implement the BIOS SWI formats the game calls
through its wrappers (0805d230 = SWI 0x11 LZ77UnCompWram, 0805d22c = SWI 0x12 LZ77UnCompVram,
0805d228 = SWI 0x13 HuffUnComp, 0805d238 = SWI 0x14 RLUnCompWram); see docs/SEASON_OF_ICE_PORT.md.
"""
from __future__ import annotations
import struct
from pathlib import Path

ROM_PATH = Path("D:/Spyro - Season of Ice (USA)/Spyro - Season of Ice (USA).gba")
ROM_SHA256 = "d331abe19e3f02e2855aa4bbc2de14626810aca83e0c216862eb1d8ca8d024b0"
BASE = 0x08000000


class Rom:
    def __init__(self, path: Path = ROM_PATH):
        self.data = Path(path).read_bytes()
        self.size = len(self.data)

    def off(self, addr: int) -> int:
        o = addr - BASE
        if not 0 <= o < self.size:
            raise ValueError(f"not a ROM address: {addr:#x}")
        return o

    def is_ptr(self, v: int) -> bool:
        return BASE <= v < BASE + self.size

    def u8(self, a): return self.data[self.off(a)]
    def u16(self, a): return struct.unpack_from("<H", self.data, self.off(a))[0]
    def s16(self, a): return struct.unpack_from("<h", self.data, self.off(a))[0]
    def u32(self, a): return struct.unpack_from("<I", self.data, self.off(a))[0]
    def bytes(self, a, n): o = self.off(a); return self.data[o:o + n]


class DecompressError(ValueError):
    pass


def lz77(data: bytes, pos: int = 0, limit: int = 1 << 24) -> tuple[bytes, int]:
    """BIOS LZ77 (type 0x10). Returns (output, bytes consumed)."""
    if data[pos] != 0x10:
        raise DecompressError("not LZ77")
    size = data[pos + 1] | data[pos + 2] << 8 | data[pos + 3] << 16
    if size == 0 or size > limit:
        raise DecompressError("bad size")
    out = bytearray()
    i = pos + 4
    n = len(data)
    while len(out) < size:
        if i >= n:
            raise DecompressError("truncated")
        flags = data[i]; i += 1
        for bit in range(8):
            if len(out) >= size:
                break
            if flags & (0x80 >> bit):
                if i + 1 >= n:
                    raise DecompressError("truncated")
                b0, b1 = data[i], data[i + 1]; i += 2
                length = (b0 >> 4) + 3
                disp = ((b0 & 0xF) << 8 | b1) + 1
                if disp > len(out):
                    raise DecompressError("back-reference before start")
                for _ in range(length):
                    out.append(out[-disp])
            else:
                if i >= n:
                    raise DecompressError("truncated")
                out.append(data[i]); i += 1
    return bytes(out[:size]), i - pos


def rl(data: bytes, pos: int = 0, limit: int = 1 << 24) -> tuple[bytes, int]:
    """BIOS run-length (type 0x30)."""
    if data[pos] != 0x30:
        raise DecompressError("not RL")
    size = data[pos + 1] | data[pos + 2] << 8 | data[pos + 3] << 16
    if size == 0 or size > limit:
        raise DecompressError("bad size")
    out = bytearray(); i = pos + 4
    while len(out) < size:
        f = data[i]; i += 1
        if f & 0x80:
            out += bytes([data[i]]) * ((f & 0x7F) + 3); i += 1
        else:
            out += data[i:i + (f & 0x7F) + 1]; i += (f & 0x7F) + 1
    return bytes(out[:size]), i - pos


def huffman(data: bytes, pos: int = 0, limit: int = 1 << 24) -> tuple[bytes, int]:
    """BIOS Huffman (type 0x2x, x = 4 or 8 bit symbols)."""
    hdr = data[pos]
    if hdr & 0xF0 != 0x20 or (hdr & 0xF) not in (4, 8):
        raise DecompressError("not Huffman")
    bits = hdr & 0xF
    size = data[pos + 1] | data[pos + 2] << 8 | data[pos + 3] << 16
    if size == 0 or size > limit:
        raise DecompressError("bad size")
    tree_size = data[pos + 4]
    tree = pos + 5
    stream = pos + 4 + (tree_size + 1) * 2
    out = bytearray(); acc = 0; nacc = 0
    i = stream
    while len(out) < size:
        word = struct.unpack_from("<I", data, i)[0]; i += 4
        # walk the 32-bit unit MSB first
        bitpos = 31
        node_off = tree
        while bitpos >= 0 and len(out) < size:
            node_val = data[node_off]
            bit = (word >> bitpos) & 1; bitpos -= 1
            nxt = (node_off & ~1) + ((node_val & 0x3F) + 1) * 2 + bit
            is_leaf = node_val & (0x40 if bit else 0x80)
            if is_leaf:
                sym = data[nxt]
                acc |= sym << nacc; nacc += bits
                if nacc == 8:
                    out.append(acc); acc = 0; nacc = 0
                node_off = tree
            else:
                node_off = nxt
    return bytes(out[:size]), i - pos


def decompress(data: bytes, pos: int = 0, limit: int = 1 << 24) -> tuple[bytes, int, str]:
    t = data[pos]
    if t == 0x10:
        o, n = lz77(data, pos, limit); return o, n, "lz77"
    if t == 0x30:
        o, n = rl(data, pos, limit); return o, n, "rl"
    if t & 0xF0 == 0x20:
        o, n = huffman(data, pos, limit); return o, n, "huff"
    raise DecompressError(f"unknown type {t:#x}")

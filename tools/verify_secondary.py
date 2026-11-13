"""Compare native head-animation transitions against the user's retail MIPS code.

This bounded instruction interpreter is an RE test oracle, not a game runtime.
It executes 8002e304, 8002eb00, 8002ebc8 and 80030a14. Primary animation
updates are supplied as inputs; no secondary result is replaced by a hook.
"""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import zlib
from spyrodisc import Disc, wad_index, u32


def signed(value, bits=32):
    value &= (1 << bits) - 1
    return value - (1 << bits) if value & (1 << (bits - 1)) else value


class Mips:
    def __init__(self, memory=None):
        self.memory = bytearray(2 * 1024 * 1024) if memory is None else bytearray(memory)
        self.instructions = 0

    def extent(self, address, size):
        at = address & 0x1fffffff
        if at + size > len(self.memory) or (size > 1 and at % size):
            raise ValueError(f"Unmapped / misaligned oracle memory: {address:08x}/{size}")
        return at

    def read(self, address, size=4):
        at = self.extent(address, size)
        return int.from_bytes(self.memory[at:at + size], 'little')

    def write(self, address, value, size=4):
        at = self.extent(address, size)
        self.memory[at:at + size] = (value & ((1 << (size * 8)) - 1)).to_bytes(size, 'little')

    def call(self, entry, hooks=(), registers=None):
        r = [0] * 32
        for index,value in (registers or {}).items(): r[index]=value & 0xffffffff
        hi=lo=0
        sentinel = 0x80fffff0
        r[29], r[31] = 0x801ff000, sentinel
        pc, next_pc, pending = entry, entry + 4, None
        for _ in range(10000):
            if pc == sentinel:
                return r
            if pc in hooks:
                if pending:
                    raise ValueError("Pending load at an oracle input hook")
                pc, next_pc = r[31], r[31] + 4
                continue
            here = pc
            word = self.read(pc)
            pc, next_pc = next_pc, (next_pc + 4) & 0xffffffff
            op, rs, rt, rd, shift, fn = word >> 26, (word >> 21) & 31, (word >> 16) & 31, (word >> 11) & 31, (word >> 6) & 31, word & 63
            imm, simm = word & 0xffff, signed(word, 16)
            written, value, new_load = None, 0, None
            if op == 0:
                written = rd
                if fn == 0: value = r[rt] << shift
                elif fn == 2: value = r[rt] >> shift
                elif fn == 3: value = signed(r[rt]) >> shift
                elif fn == 4: value = r[rt] << (r[rs] & 31)
                elif fn == 6: value = r[rt] >> (r[rs] & 31)
                elif fn == 7: value = signed(r[rt]) >> (r[rs] & 31)
                elif fn == 8: next_pc, written = r[rs], None
                elif fn == 9: next_pc, value = r[rs], here+8
                elif fn == 0x10: value = hi
                elif fn == 0x11: hi, written = r[rs], None
                elif fn == 0x12: value = lo
                elif fn == 0x13: lo, written = r[rs], None
                elif fn in (0x18,0x19):
                    product=(signed(r[rs])*signed(r[rt])) if fn==0x18 else r[rs]*r[rt]
                    hi,lo,written=(product>>32)&0xffffffff,product&0xffffffff,None
                elif fn in (0x20, 0x21): value = r[rs] + r[rt]
                elif fn in (0x22, 0x23): value = r[rs] - r[rt]
                elif fn == 0x24: value = r[rs] & r[rt]
                elif fn == 0x25: value = r[rs] | r[rt]
                elif fn == 0x2a: value = int(signed(r[rs]) < signed(r[rt]))
                elif fn == 0x2b: value = int(r[rs] < r[rt])
                else: raise ValueError(f"Unmapped oracle SPECIAL {fn:x} at {here:08x}")
            elif op in (2, 3):
                next_pc = ((here + 4) & 0xf0000000) | ((word & 0x3ffffff) << 2)
                if op == 3: written, value = 31, here + 8
            elif op in (1, 4, 5, 6, 7):
                if op == 1:
                    if rt not in (0, 1): raise ValueError("Unmapped oracle REGIMM")
                    taken = signed(r[rs]) < 0 if rt == 0 else signed(r[rs]) >= 0
                elif op == 4: taken = r[rs] == r[rt]
                elif op == 5: taken = r[rs] != r[rt]
                elif op == 6: taken = signed(r[rs]) <= 0
                else: taken = signed(r[rs]) > 0
                if taken: next_pc = (here + 4 + simm * 4) & 0xffffffff
            elif op in (8, 9, 10, 11, 12, 13, 15):
                written = rt
                if op in (8, 9): value = r[rs] + simm
                elif op == 10: value = int(signed(r[rs]) < simm)
                elif op == 11: value = int(r[rs] < (simm & 0xffffffff))
                elif op == 12: value = r[rs] & imm
                elif op == 13: value = r[rs] | imm
                else: value = imm << 16
            elif op in (0x20, 0x21, 0x23, 0x24, 0x25):
                size = 4 if op == 0x23 else (2 if op in (0x21, 0x25) else 1)
                loaded = self.read((r[rs] + simm) & 0xffffffff, size)
                if op in (0x20, 0x21): loaded = signed(loaded, size * 8)
                new_load = (rt, loaded & 0xffffffff)
            elif op in (0x28, 0x29, 0x2b):
                self.write((r[rs] + simm) & 0xffffffff, r[rt], {0x28: 1, 0x29: 2, 0x2b: 4}[op])
            else:
                raise ValueError(f"Unmapped oracle opcode {op:x} at {here:08x}")
            # R3000 load and branch delay slots remain visible to the program.
            if pending and pending[0] != written and pending[0]: r[pending[0]] = pending[1]
            if written: r[written] = value & 0xffffffff
            pending = new_load
            r[0] = 0
            self.instructions += 1
        raise ValueError(f"Oracle instruction budget exceeded at {pc:08x}")


class Retail:
    def __init__(self, path):
        self.path = path
        self.disc = None if path.is_dir() else Disc(path)
        self.files = {} if self.disc is None else {e.path: e for e in self.disc.entries()}

    def read(self, name, offset=0, size=None):
        if self.disc is None:
            with (self.path / name).open('rb') as f:
                f.seek(offset)
                return f.read() if size is None else f.read(size)
        entry = self.files[name]
        if size is None: size = entry.size - offset
        if offset < 0 or offset + size > entry.size: raise ValueError("Retail extent outside file")
        skip = offset % 2048
        return self.disc.read(entry.lba + offset // 2048, size + skip)[skip:]

    def size(self, name):
        return (self.path / name).stat().st_size if self.disc is None else self.files[name].size

    def close(self):
        if self.disc is not None: self.disc.__exit__(None, None, None)


def frame_banks(global_bank, level):
    model = u32(global_bank, 4)
    result = {}
    for clip in range(62):
        offset = u32(global_bank, model + 60 + clip * 4)
        if offset != 0xffffffff:
            at = model + offset
            result[clip] = global_bank[at:at + 20 + global_bank[at] * 20]
    at = u32(level, 0x40)
    if at:
        for _ in range(63):
            clip = u32(level, at)
            if clip == 0xffffffff: break
            size = u32(level, at + 4)
            if clip >= 62 or size < 20 or at + 8 + size > len(level): raise ValueError("Invalid extra animation bank")
            result[clip] = level[at + 8:at + 28 + level[at + 8] * 20]
            at += 8 + size
        else: raise ValueError("Unterminated extra animation bank")
    return result


def put_pose(cpu, pose, head=False):
    at = 0x8006a008 if head else 0x8006a004
    for i, value in enumerate(pose[:4]): cpu.write(at + i, value, 1)
    cpu.write(0x8006a00d if head else 0x8006a00c, pose[4], 1)


def verify(source, probe, slot, executable, entries):
    read_bank = lambda n: source.read('WAD.WAD', entries[n].offset, entries[n].size)
    cpu = Mips()
    load, size = u32(executable, 0x18), u32(executable, 0x1c)
    at = cpu.extent(load, 1)
    cpu.memory[at:at + size] = executable[0x800:0x800 + size]
    cpu.write(0x80066f24, 0x80080000)
    for clip, frames in frame_banks(read_bank(0), read_bank(slot)).items():
        pointer = 0x80100000 + clip * 0x1000
        cpu.write(0x80080000 + clip * 4, pointer)
        at = cpu.extent(pointer, 1)
        cpu.memory[at:at + len(frames)] = frames
    cpu.write(0x80067018, 1)
    run = subprocess.run([str(probe), str(source.path), str(slot)], capture_output=True, text=True, check=True)
    previous_scenario = None
    count = 0
    for line in run.stdout.splitlines():
        row = json.loads(line)
        if row['scenario'] != previous_scenario:
            put_pose(cpu, row['initial'], True)
            cpu.write(0x8006a1ec, 0); cpu.write(0x8006a1f0, 6)
            previous_scenario = row['scenario']
        put_pose(cpu, row['primary'])
        cpu.write(0x8006a15c, row['action'])
        cpu.call(0x8002e304, hooks=(0x8002e440, 0x8002e64c))
        pose = [cpu.read(0x8006a008 + i, 1) for i in range(4)] + [cpu.read(0x8006a00d, 1)]
        actual = (pose, cpu.read(0x8006a1f0), cpu.read(0x8006a1ec))
        expected = (row['head'], row['mode'], row['observed_action'])
        if actual != expected:
            raise ValueError(f"Slot {slot}, scenario {row['scenario']}, tick {row['tick']}: MIPS {actual} != native {expected}")
        count += 1
    if not count: raise ValueError("Native animation probe produced no states")
    print(f"Slot {slot}: {count} native states match {cpu.instructions} retail MIPS instructions", flush=True)
    return count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('game', type=Path)
    parser.add_argument('--probe', type=Path, default=Path('build/Release/spyro_animation_probe.exe'))
    parser.add_argument('--slot', type=int, default=15)
    parser.add_argument('--all-levels', action='store_true')
    args = parser.parse_args()
    source = Retail(args.game.resolve())
    try:
        exe = source.read('SCUS_944.25')
        if zlib.crc32(exe) != 0xcdc0917c: raise ValueError("Oracle requires the mapped USA executable")
        entries = {e.index: e for e in wad_index(source.read('WAD.WAD', 0, 0x10000), source.size('WAD.WAD'))}
        total = sum(verify(source, args.probe.resolve(), slot, exe, entries) for slot in (range(15, 96, 2) if args.all_levels else [args.slot]))
        print(f"Verified {total} secondary animation states")
    finally:
        source.close()


if __name__ == '__main__':
    main()

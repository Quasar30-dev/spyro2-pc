"""Compare sky visibility against the mounted executable's original 80048d18.

The native sky test supplies masks at 512 positions per scene. No visibility
result is hooked: this executes the retail octree traversal in the RE oracle.
"""
import argparse
import json
import subprocess
from pathlib import Path
from verify_secondary import Mips, Retail
from spyrodisc import wad_index, u32


class SkyMips(Mips):
    def extent(self, address, size):
        if 0x1f800000 <= address < 0x1f800400:
            return 0x200000 + address - 0x1f800000
        return super().extent(address, size)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--game', type=Path, default=Path('data/disc'))
    ap.add_argument('--probe', type=Path, default=Path('build/Release/spyro_sky_tests.exe'))
    ap.add_argument('--output', type=Path, default=Path('data/validation/sky-oracle.json'))
    args = ap.parse_args()
    masks = args.output.with_suffix('.masks.txt')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    run = subprocess.run([str(args.probe.resolve()), str(args.game), str(masks)], capture_output=True, text=True, check=True)
    args.output.with_suffix('.native.log').write_text(run.stdout)
    source = Retail(args.game)
    try:
        exe = source.read('SCUS_944.25')
        index = wad_index(source.read('WAD.WAD', 0, 65536), source.size('WAD.WAD'))
        cpu = SkyMips(bytearray(0x200400))
        cpu.memory[0x10000:0x10000+len(exe)-0x800] = exe[0x800:]
        # li a0,1; j 80048d18; nop. Preserve the oracle's return sentinel.
        cpu.write(0x801ef000, 0x24040001)
        cpu.write(0x801ef004, 0x08012346)
        cpu.write(0x801ef008, 0)
        last = None
        checked = 0
        for line in masks.read_text().splitlines():
            fields = line.split();slot,x,y,z = map(int, fields[:4]);expected = int(fields[4],16)
            if slot != last:
                e = index[slot];a = source.read('WAD.WAD',e.offset,e.size)
                at = u32(a,8)
                for _ in range(2): at += u32(a,at)
                terrain = u32(a,at);at += terrain;size = u32(a,at)
                tree = a[at+4:at+size] if size>8 else b'\xff'*4
                cpu.memory[0x100000:0x100000+len(tree)] = tree
                cpu.write(0x800673e0,0x80100000)
                cpu.write(0x800673e4,int(terrain>8))
                last = slot
            for k,v in enumerate((x,y,z)):cpu.write(0x80067eb8+k*4,v)
            cpu.call(0x801ef000)
            actual = sum((cpu.read(0x1f8003c0+k,1)!=0)<<k for k in range(64))
            if actual != expected:raise ValueError(f'Sky PVS mismatch slot {slot}, XYZ {x,y,z}: {actual:016x} != {expected:016x}')
            checked += 1
        report={'profile':'SCUS_944.25','function':'80048d18','scenes':41,'positions':checked,'instructions':cpu.instructions,'matched':True}
        args.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
    finally:
        source.close()


if __name__ == '__main__':main()

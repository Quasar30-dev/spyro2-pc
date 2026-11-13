"""Verify Glimmer bridge initial collision and timelines against retail MIPS.

Runs resident 800231f4 and the near-track portion of 800225f8. Other scene
directories are empty inputs; collision interpolation's GTE path is not run.
The native trace comes from the same Level decoder/controller used in gameplay.
"""
import argparse
import json
from pathlib import Path
import subprocess
import zlib
from spyrodisc import u32, wad_index
from verify_secondary import Mips, Retail, signed


def verify(game, probe):
    source = Retail(game)
    try:
        exe = source.read('SCUS_944.25')
        if zlib.crc32(exe) != 0xcdc0917c:
            raise ValueError('Oracle requires the mapped USA executable')
        entries = {e.index: e for e in wad_index(source.read('WAD.WAD', 0, 0x10000), source.size('WAD.WAD'))}
        entry = entries[17]
        archive = source.read('WAD.WAD', entry.offset, entry.size)
        child = wad_index(archive, len(archive))[3]
        data = archive[child.offset:child.offset + child.size]
    finally:
        source.close()
    cpu = Mips()
    at = cpu.extent(u32(exe, 0x18), 1)
    size = u32(exe, 0x1c)
    cpu.memory[at:at + size] = exe[0x800:0x800 + size]
    base = 0x80100000
    if len(data) > 0xf0000:
        raise ValueError('Gameplay overlaps oracle stack')
    cpu.memory[0x100000:0x100000 + len(data)] = data
    # Empty unused directories rather than invoking unrelated material scripts.
    for address in range(0x800681d4, 0x8006820c, 4):
        cpu.write(address, 0)
    cpu.write(0x800673d4, 0x80080000)
    for i in range(256):
        cpu.write(0x80080000 + i * 4, 0x80081000 + i * 32)
    cpu.write(0x800673fc, 0x8008a000)
    cpu.write(0x8008a014, 0x80090000)
    near, collision = [], []
    section = 0x2c
    for kind in range(7):
        size, count = u32(data, section), u32(data, section + 4)
        if kind in (2, 4):
            records = near if kind == 2 else collision
            for i in range(count):
                offset = section + 4 + u32(data, section + 8 + i * 4)
                if data[offset] == 1:
                    records.append(base + offset)
        section += size
    if len(near) != 2 or len(collision) != 1:
        raise ValueError('Unexpected Glimmer group-1 track count')
    for header, directory, records in ((0x800681e4, 0x80080400, near), (0x80068204, 0x80080410, collision)):
        cpu.write(header, len(records))
        cpu.write(header + 4, directory)
        for i, record in enumerate(records):
            cpu.write(directory + i * 4, record)
    cpu.call(0x800231f4)
    native = subprocess.run([str(probe), str(game), '--trace'], capture_output=True, text=True, check=True)
    rows = [json.loads(line) for line in native.stdout.splitlines()]
    initial = rows.pop(0)['initial_collision']
    expected = []
    for record in collision:
        first, count = cpu.read(record + 6, 2), cpu.read(record + 4, 2)
        for i in range(count):
            at = 0x80090000 + (first + i) * 12
            x, y, z = (cpu.read(at + j * 4) for j in range(3))
            bx, by, bz = x & 0x3fff, y & 0x3fff, z & 0x3fff
            points = ((bx, by, bz), (bx + signed(x >> 14, 9), by + signed(y >> 14, 9), bz + ((z >> 16) & 255)),
                      (bx + signed(x >> 23, 9), by + signed(y >> 23, 9), bz + (z >> 24)))
            expected.append([[x / 64, z / 64, y / 64] for x, y, z in points])
    if len(initial) != len(expected):
        raise ValueError('Initial collision count differs')
    for i, (actual, retail) in enumerate(zip(initial, expected)):
        # Resident repacking cyclically rotates vertices to put the lowest Z
        # first. Preserve winding while accepting that harmless rotation.
        if not any(actual == retail[j:] + retail[:j] for j in range(3)):
            raise ValueError(f'Initial triangle {i}: native {actual} != MIPS {retail}')
    cpu.write(0x80068204, 0)  # GTE collision interpolation is outside this oracle.
    for record in near:
        cpu.write(record + 1, cpu.read(record + 1, 1) & ~3, 1)
    for tick, row in enumerate(rows):
        if row['tick'] != tick:
            raise ValueError('Missing native tick')
        if tick:
            cpu.call(0x800225f8, registers={4: 1})
        retail = [[cpu.read(record + j, 1) for j in (2, 1, 3)] for record in near]
        if row['near'] != retail:
            raise ValueError(f'Tick {tick}: native {row["near"]} != MIPS {retail}')
    if len(rows) != 141 or any(state[0] != 59 or state[1] != 2 for state in rows[-1]['near']):
        raise ValueError('Bridge did not reach its authored stopping node')
    return dict(initial_collision_triangles=len(initial), near_timeline_states=len(rows) * len(near),
                retail_instructions=cpu.instructions, terminal_node=59,
                scope='Initial collision and near-track timelines; no GTE interpolation or camera equivalence claim')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('game', type=Path)
    parser.add_argument('--probe', type=Path, default=Path('build/misc-candidate/Release/spyro_campaign_tests.exe'))
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    report = verify(args.game.resolve(), args.probe.resolve())
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')
    print(json.dumps(report))


if __name__ == '__main__':
    main()

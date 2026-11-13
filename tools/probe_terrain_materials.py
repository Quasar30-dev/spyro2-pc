"""Read-only audit of retail terrain primitive flags and palette STP coverage.

Outputs metadata only. Supports the same extracted/ISO/CUE/BIN sources as the
other retail oracles; no game assets are written to the report.
"""
import argparse
from collections import Counter
import json
from pathlib import Path
import struct
import zlib

from spyrodisc import u32, wad_index
from verify_secondary import Retail


def audit(archive):
    start, size = u32(archive, 8), u32(archive, 12)
    scene = archive[start:start + size]
    count = u32(scene, 4)
    if len(scene) != size or not 0 < count <= 256 or u32(scene, 0) != 8 + count * 48:
        raise ValueError('Invalid terrain material directory')
    vram_at = u32(archive, 0)
    vram = archive[vram_at:vram_at + 0x80000]
    if len(vram) != 0x80000:
        raise ValueError('Missing scene VRAM')

    def word(x, y):
        if not 512 <= x < 1024 or not 0 <= y < 512:
            raise ValueError('Terrain material outside resident VRAM')
        return struct.unpack_from('<H', vram, (y * 512 + x - 512) * 2)[0]

    translucent = []
    for material in range(count):
        at = 8 + material * 48
        descriptors = [scene[at + q * 8:at + (q + 1) * 8] for q in range(2, 6)]
        modes = {(bool(d[7] & 128), d[6] >> 5 & 3) for d in descriptors}
        if len(modes) != 1:
            raise ValueError(f'Mixed near-quadrant blend settings: material {material}')
        enabled, mode = next(iter(modes))
        if not enabled:
            continue
        coverage = Counter()
        for d in descriptors:
            page, clut = d[6], struct.unpack_from('<H', d, 2)[0]
            depth = page >> 7 & 1
            pixels_per_word, bits = (2, 8) if depth else (4, 4)
            if d[4] - d[0] != 31 or d[5] - d[1] != 31:
                raise ValueError('Unexpected near-quadrant dimensions')
            for y in range(d[1], d[5] + 1):
                for x in range(d[0], d[4] + 1):
                    packed = word((page & 15) * 64 + x // pixels_per_word, (page >> 4 & 1) * 256 + y)
                    index = packed >> ((x % pixels_per_word) * bits) & ((1 << bits) - 1)
                    color = word((clut & 63) * 16 + index, clut >> 6 & 511)
                    coverage['clear' if color == 0 else 'stp' if color & 0x8000 else 'opaque'] += 1
        translucent.append({'material': material, 'blend_mode': mode, 'texels': dict(coverage)})
    return {'material_count': count, 'translucent': translucent}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('game', type=Path)
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    retail = Retail(args.game)
    try:
        crc = zlib.crc32(retail.read('SCUS_944.25'))
        if crc != 0xcdc0917c:
            raise ValueError('Unverified retail executable profile')
        entries = wad_index(retail.read('WAD.WAD', 0, 2048), retail.size('WAD.WAD'))
        scenes = []
        for slot in range(15, 96, 2):
            entry = entries[slot]
            scenes.append({'wad_slot': slot, **audit(retail.read('WAD.WAD', entry.offset, entry.size))})
        modes = Counter(m['blend_mode'] for s in scenes for m in s['translucent'])
        report = {'profile_crc32': f'{crc:08x}', 'scene_count': len(scenes),
                  'material_count': sum(s['material_count'] for s in scenes),
                  'translucent_material_count': sum(modes.values()),
                  'translucent_blend_modes': dict(modes), 'scenes': scenes}
        if args.out:
            args.out.parent.mkdir(parents=True, exist_ok=True)
            args.out.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
        print(json.dumps({k: v for k, v in report.items() if k != 'scenes'}, indent=2))
    finally:
        retail.close()


if __name__ == '__main__':
    main()

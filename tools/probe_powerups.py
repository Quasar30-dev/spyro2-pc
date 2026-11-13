"""Inventory retail power-up clips, level overlays and resident effect references.

Read-only. Reports metadata/hashes, not redistributable game data. The loader's
original MIPS callback selector is executed to resolve overlay addresses.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib
from spyrodisc import u32, wad_index
from verify_secondary import Mips, Retail, frame_banks, signed

BASE = 0x8006d264
CALLBACKS = {'movement': 0x80067078, 'movement_effects': 0x80067074, 'enter_state': 0x800670a8,
             'select_state': 0x80067010, 'objects': 0x800670f0,
             'spawn_object': 0x800670b4, 'particles': 0x800670fc}
GLOBALS = {0x8006a1b4: 'flight_timer', 0x8006a1b8: 'flight_activation',
           0x8006a1bc: 'charge_boost_flag', 0x8006a1c0: 'charge_activation',
           0x8006a1c8: 'superflame_timer',0x8006a1cc: 'ice_timer',0x8006a1d4: 'green_bomb_timer'}
CLIPS = {7: 'Flame / superflame head', 25: 'Supercharge jump', 33: 'Superfly',
         34: 'Superfly climb / flap', 35: 'Superfly dive', 61: 'Supercharge'}
GATE_KINDS = {1: 'Supercharge', 2: 'Superfly', 3: 'Superflame', 10: 'Permanent superflame'}


def gates_and_trail(level, slot):
    if slot == 95: return [], None
    start, size = u32(level, 24), u32(level, 28)
    if start + size > len(level): raise ValueError('Gameplay child outside scene')
    data = level[start:start + size]
    at = 0x2c
    for _ in range(8):
        size = u32(data, at)
        if size < 4 or size % 4 or at + size > len(data): raise ValueError('Invalid gameplay section')
        at += size
    size, count = u32(data, at), u32(data, at + 4)
    if size < 8 or count * 88 > size - 8 or at + size > len(data): raise ValueError('Invalid object table')
    gates = []
    for record in range(count):
        p = at + 8 + record * 88
        moby_class=struct.unpack_from('<H', data, p + 54)[0]
        if not 0x1cc <= moby_class <= 0x1d3: continue
        parameters = u32(data, p)
        kind = u32(data, parameters + 0x60)
        gates.append({'record': record, 'class': f'{moby_class:03x}', 'kind': kind,
                      'name': GATE_KINDS.get(kind, f'Gate kind {kind}'),
                      'duration_vblanks': u32(data, parameters + 0x58),
                      'enemy_requirement': u32(data, parameters + 0x64)})
    left, right = u32(data, 0x24), u32(data, 0x28)
    page, clut = right >> 16, left >> 16
    u, v = left & 255, left >> 8 & 255
    width, height = (right & 255) - u + 1, (right >> 8 & 255) - v + 1
    if width != 8 or height != 32 or ((page >> 7) & 3) != 0:
        raise ValueError('Unmapped wing-trail texture geometry/depth')
    vram_at = u32(level, 0)
    if u32(level, 4) < 0x80000: raise ValueError('Missing trail VRAM upload')
    vram = level[vram_at:vram_at + 0x80000]
    def word(x, y):
        if not 512 <= x < 1024 or not 0 <= y < 512: raise ValueError('Trail material outside VRAM')
        return struct.unpack_from('<H', vram, ((y * 512) + x - 512) * 2)[0]
    pixels = bytearray()
    for y in range(v, v + height):
        for x in range(u, u + width):
            packed = word((page & 15) * 64 + x // 4, ((page >> 4) & 1) * 256 + y)
            color = word((clut & 63) * 16 + (packed >> (x % 4 * 4) & 15), (clut >> 6) & 511)
            for shift in (0, 5, 10):
                component = color >> shift & 31
                pixels.append(component << 3 | component >> 2)
            pixels.append(255 if color else 0)
    return gates, {'width': width, 'height': height, 'u': u, 'v': v,
                   'clut': f'{clut:04x}', 'page': f'{page:04x}',
                   'blend_mode': page >> 5 & 3, 'rgba_sha256': hashlib.sha256(pixels).hexdigest(),
                   'nontransparent_texels': sum(pixels[k] != 0 for k in range(3, len(pixels), 4))}


def memory_references(code):
    # Conservative short straight-line constant propagation. These are search
    # leads, not proof of reachable code: overlay data may resemble instructions.
    values, ages, result = {0: 0}, {0: -1}, []
    for at in range(0, len(code) - 3, 4):
        w = u32(code, at)
        op, rs, rt, rd = w >> 26, w >> 21 & 31, w >> 16 & 31, w >> 11 & 31
        imm = signed(w, 16)
        values = {r: v for r, v in values.items() if r == 0 or at - ages[r] <= 64}
        address = (values[rs] + imm) & 0xffffffff if rs in values else None
        if op in (0x20, 0x21, 0x23, 0x24, 0x25, 0x28, 0x29, 0x2b) and address in GLOBALS:
            result.append({'pc': f'{BASE + at:08x}', 'global': GLOBALS[address],
                           'access': 'write' if op >= 0x28 else 'read'})
        dest, value = None, None
        if op == 15: dest, value = rt, (w & 65535) << 16
        elif op in (8, 9): dest, value = rt, address
        elif op == 13:
            dest, value = rt, values[rs] | (w & 65535) if rs in values else None
        elif op in (0x20, 0x21, 0x23, 0x24, 0x25): dest = rt
        elif op == 0 and (w & 63) in (0x21, 0x25):
            dest = rd
            if rs in values and rt in values:
                value = values[rs] + values[rt] if (w & 63) == 0x21 else values[rs] | values[rt]
        elif op == 0 and (w & 63) not in (8, 9, 0x18, 0x19, 0x1a, 0x1b): dest = rd
        elif op in (10, 11, 12, 14): dest = rt
        if dest:
            values.pop(dest, None)
            if value is not None: values[dest], ages[dest] = value & 0xffffffff, at
        if op in (1, 2, 3, 4, 5, 6, 7) or (op == 0 and (w & 63) in (8, 9)):
            values, ages = {0: 0}, {0: -1}
    return result


def inspect(source):
    retail = Retail(source)
    try:
        exe = retail.read('SCUS_944.25')
        if zlib.crc32(exe) != 0xcdc0917c: raise ValueError('Unmapped executable revision')
        slots = wad_index(retail.read('WAD.WAD', 0, 0x800), retail.size('WAD.WAD'))
        read_slot = lambda s: retail.read('WAD.WAD', slots[s].offset, slots[s].size)
        global_bank = read_slot(0)
        cpu = Mips()
        cpu.memory[0x10000:0x10000 + len(exe) - 0x800] = exe[0x800:]
        loader = read_slot(1)
        cpu.memory[BASE & 0x1fffffff:(BASE & 0x1fffffff) + len(loader)] = loader
        scenes = []
        for slot in range(15, 96, 2):
            level, overlay = read_slot(slot), read_slot(slot - 1)
            banks = frame_banks(global_bank, level)
            entry = {'scene_slot': slot, 'name': f'Scene bank {slot}',
                     'overlay_slot': slot - 1 if overlay else None,
                     'overlay_bytes': len(overlay), 'clips': [], 'callbacks': {}}
            index = (slot - 15) // 2
            if index < 29:
                level_id = cpu.read(0x80064994 + index, 1)
                if cpu.read(0x80064940 + level_id, 1) != index: raise ValueError('Level directory mismatch')
                ptr = cpu.read(0x800649b4 + index * 4)
                text_at = cpu.extent(ptr, 1)
                entry['name'] = bytes(cpu.memory[text_at:cpu.memory.index(0, text_at)]).decode('ascii')
                entry['level_id'] = level_id
                cpu.write(0x80066f90, level_id)
                cpu.call(0x8006d960)
                for name, address in CALLBACKS.items():
                    callback = cpu.read(address)
                    if not BASE <= callback < BASE + len(overlay): raise ValueError('Callback outside selected overlay')
                    entry['callbacks'][name] = f'{callback:08x}'
            for clip, name in CLIPS.items():
                if clip not in banks: continue
                raw = banks[clip]
                loop = [cpu.read(0x80060334 + clip * 4 + k, 1) for k in range(4)]
                entry['clips'].append({'id': clip, 'name': name, 'frames': raw[0],
                    'vertices': raw[3], 'split_vertex': raw[2],
                    'frame_ticks': [((u32(raw, 20 + k * 20) >> 18) & 15) + 1 for k in range(raw[0])],
                    'loop': loop, 'descriptor_and_frames_sha256': hashlib.sha256(raw).hexdigest()})
            entry['global_references_candidates'] = memory_references(overlay)
            models = []
            for i in range(1, 63):
                start, end = u32(level, 0x40 + i * 4), u32(level, 0x44 + i * 4)
                if not end: break
                if not 0 < start <= end <= len(level): raise ValueError('Invalid model bank extent')
                model_id = struct.unpack_from('<H', level, 0x140 + i * 2)[0]
                if model_id in (0x1e3, 0x1f8, 0x270, 0x2c2):
                    models.append({'class': f'{model_id:03x}', 'offset': start, 'bytes': end - start})
            entry['projectile_models'] = models
            entry['gates'], entry['wing_trail_texture'] = gates_and_trail(level, slot)
            scenes.append(entry)
        return {'profile': 'SCUS_944.25 / cdc0917c', 'scenes': scenes,
                'wing_trails': {'vertices': [63, 62], 'history_samples': 6,
                    'climb_history_samples': 3, 'width_parameter': -3,
                    'color': '303030', 'allocate': '800502fc', 'tick': '800504a8',
                    'render': '8004b72c', 'attachment': '8003305c'},
                'notes': ['References marked candidates require disassembly confirmation.',
                    'Clips alone do not establish complete power-up behavior.',
                    'Extra scene banks retain numbered names until their loader paths are mapped.']}
    finally:
        retail.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('game', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    report = inspect(args.game)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    else:
        print(json.dumps(report, indent=2))


if __name__ == '__main__': main()

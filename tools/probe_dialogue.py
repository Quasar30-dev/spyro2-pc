"""Read the shared retail dialogue commands and resident purchase table.

Outputs evidence only; command bytes retain their original one-based arguments.
No dialogue text is used to infer rewards or world-object actions.
"""
import argparse
import json
import struct
from pathlib import Path
from spyrodisc import wad_index, u32


SPEAKERS = {0x1bc, 0x110, 0x77, 0x16d, 0x1b6, 0x22a, 0x220, 0x242,
            0x10f, 0x193, 0x31, 0x73, 0x149, 0x14e, 0xa1, 0xbb, 0xb6,
            0x16c, 0x62, 0x61, 0xae, 0x180, 0xbe, 0x138, 0x205, 0x1a6,
            0x1b9, 0x1af, 0x1b0, 0x189, 0x188, 0x14a, 0xfe, 0x8d, 0x8a,
            0x1fb, 0x1fe, 0x25c, 0x1c7, 0x10d, 0x250, 0x29f}


def cstring(data, at):
    end = at
    while end < len(data) and end-at < 8192 and data[end] not in (0, 255):
        end += 1
    if end == len(data) or end-at == 8192:
        raise ValueError('Unterminated text')
    return data[at:end].decode('ascii', errors='replace')


def probe(root):
    wad = (root/'WAD.WAD').read_bytes()
    exe = (root/'SCUS_944.25').read_bytes()
    records = []
    purchases = set()
    for entry in wad_index(wad, len(wad)):
        if entry.index < 15 or entry.index > 93 or entry.index % 2 == 0:
            continue
        archive = wad[entry.offset:entry.offset+entry.size]
        child = wad_index(archive, len(archive))[3]
        data = archive[child.offset:child.offset+child.size]
        at = 0x2c
        for _ in range(8):
            at += u32(data, at)
        for i in range(u32(data, at+4)):
            record = at+8+i*88
            kind = struct.unpack_from('<H', data, record+54)[0]
            if kind not in SPEAKERS:
                continue
            param = u32(data, record)
            if data[param+3] == 255:
                continue
            lines = []
            for j in range(data[param+3]+1):
                p = u32(data, param+16+j*4)
                size = data[p]
                if size == 255:
                    lines.append(dict(node=j, commands=[], text=''))
                    continue
                if not 1 <= size <= 64:
                    raise ValueError('Bad command header')
                commands = []
                k = 1
                while k < size:
                    op = data[p+k]
                    n = {0x23: 3, 0x25: 5, 0x26: 1, 0x2a: 1, 0x2b: 1}.get(op, 2)
                    if k+n > size:
                        raise ValueError('Truncated command')
                    args = list(data[p+k+1:p+k+n])
                    commands.append(dict(op=f'{op:02x}', args=args))
                    if op == 0x25 and args[2] != 255:
                        purchases.add(args[2]-1)
                    k += n
                lines.append(dict(node=j, commands=commands, text=cstring(data, p+size)))
            records.append(dict(slot=entry.index, object=i, type=f'{kind:03x}',
                                subtype=data[param], initial=data[param+2], automatic=data[param+5],
                                name=cstring(data, u32(data, param+12)), lines=lines))
    prices = {i: struct.unpack_from('<hBB', exe, 0x800+0x54670+i*4) for i in sorted(purchases)}
    return dict(purchase_table=prices, npcs=records)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--disc', type=Path, default=Path('data/disc'))
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = probe(args.disc)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2), encoding='utf8')
    print(f'{len(result["npcs"])} NPC records; purchase entries: {result["purchase_table"]}')

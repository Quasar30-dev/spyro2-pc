"""Conservative terrain visibility, per retail 80048d18 and loader 8006fa98.

The leaf lists every region after the 0xff clear/set separator. It deliberately
does no occlusion rejection: an open island level must not lose a distant island.
Unlike the disabled size-8 section this takes the normal visibility return path
and the loader retains retail's 0x2c000 draw distance. No executable patch.
"""
import struct


def encode(count):
    assert 0 < count <= 255
    if count <= 252:
        node = bytes([count + 3, 0, 255, *range(count)])
    else:
        # The byte-sized node length caps a leaf's list. Parent sets 0..249;
        # every octant's leaf adds the remaining indices to that inherited set.
        leaf = bytes([count - 250 + 3, 0, 255, *range(250, count)])
        node = bytes([255, 255, 255, 0, 255, *range(250)]) + leaf * 8
    node += bytes((-len(node)) % 4)
    if len(node) < 8:  # loader enables visibility only for section size > 8
        node += bytes(8-len(node))
    return struct.pack('<I', len(node) + 4) + node


def decode(part, position=(0, 0, 0)):
    """Translate the original consumer's byte operations; also reads retail trees.

    position is the three unsigned 2048-unit cell coordinates the renderer reads
    at 80067eb8..c0. Returns its visibility bytes and return value.
    """
    visible = bytearray(256)
    if len(part) <= 8 or part[4:8] == b'\xff' * 4:
        return bytes([255]) * 256, 0
    at, cell = 4, [0, 0, 0]
    for depth in range(7, -1, -1):
        size, mask = part[at:at + 2]
        end = at + size
        p = at + ((4 if mask else 2) if depth else 1)
        assert at < end <= len(part) and p <= end
        while p < end and part[p] != 255:
            old = part[p]
            visible[old] = 0
            p += 1
            if p >= end or old >= part[p]:
                break
        while p < end:
            if part[p] != 255:
                visible[part[p]] = 255
            p += 1
        if not depth or not mask:
            return bytes(visible), 1
        octant = 0
        for axis, bit in enumerate((4, 2, 1)):
            cell[axis] *= 2
            if position[axis] >= ((cell[axis] + 1) << depth) >> 1:
                cell[axis] += 1
                octant |= bit
        if not mask & (1 << octant):
            return bytes([255]) * 256, 0
        at += struct.unpack_from('<H', part, at + 2)[0]
        for i in range(octant):
            if mask & (1 << i):
                at += part[at]
    raise AssertionError('unterminated visibility tree')

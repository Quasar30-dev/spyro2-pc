"""Crownworks geometry polish (EXPANSION_LEVEL_PLANS 6.5.1 and 6.4 slicing).

Applied to the pre-polish world (revisions/pre-pylons/crownworks.s2world,
the 27 September export), writing pc/crownworks.s2world:

1. Dry crossings. The north and south crossings over the molten ring were
   4.8 x 4.8 boxes, leaving dead lava corners against the ring's curved
   edges. They become ring sectors (radius 11.6..16.4) about 7 units wide,
   flush with both curves. The decks keep Ripto's bordered floor slab (one
   slab per deck cell), the sides and gold edging move to the new edges.
2. Pylons. Four tall panels (2.2 wide, 0.6 thick) on the outer lower floor
   (radius 17.4) on the diagonals, outside the molten ring. They are the only
   geometry the Convexity Void Beam may cut here (the SLICEABLE section, one
   box per pylon with an inward fall direction): cut, a pylon topples on its
   broad face across the molten ring as a new crossing.

Old crossing triangles are replaced in place, with the same flags, so every
visual/collision index the ARENA mesh ranges use is unchanged; new triangles
are appended to the end of the triangle list.

    python tools/crownworks_polish.py          # write pc/crownworks.s2world
    python tools/crownworks_polish.py --check  # report only
"""
import math, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ARENA = ROOT / 'data/custom-levels/cyborg-ripto-arena'
WORLD = ARENA / 'pc/crownworks.s2world'
SOURCE = ARENA / 'revisions/pre-pylons/crownworks.s2world'

# Materials in the world's table (see source/design.json).
BRONZE, CORNICE, BLUE, SLAB = 3, 4, 5, 11
GOLD = (1.0, 0.9, 0.62)
STEEL = (0.92, 0.94, 1.0)
DECK_TOP, DECK_SIDE = (0.8, 0.84, 0.96), (0.616, 0.6468, 0.7392)   # the old crossing's colours
EDGE = (1.0, 0.86, 0.55)
R0, R1, HALF_ANGLE = 11.6, 16.4, math.asin(3.5 / 14.0)            # ~7 units wide at mid-span
PYLON_RADIUS, PYLON_ANGLES = 17.4, (45, 135, 225, 315)
STEEL_TILE = 'textures/crown-steel.rgba'
SHAFT_BOTTOM, SHAFT_TOP = 0.5, 7.1


def fmt(v):
    return f'{v:.6g}'


def tri_line(flags, mat, a, b, c, color, uv, outward):
    """One world triangle wound so cross(b-a, c-a) faces outward."""
    ab = [b[i] - a[i] for i in range(3)]
    ac = [c[i] - a[i] for i in range(3)]
    n = (ab[1] * ac[2] - ab[2] * ac[1], ab[2] * ac[0] - ab[0] * ac[2], ab[0] * ac[1] - ab[1] * ac[0])
    if sum(n[i] * outward[i] for i in range(3)) < 0:
        b, c = c, b
        uv = (uv[0], uv[2], uv[1])
    parts = [str(flags), str(mat)]
    for p, (u, w) in zip((a, b, c), uv):
        parts += [fmt(p[0]), fmt(p[1]), fmt(p[2]), fmt(color[0]), fmt(color[1]), fmt(color[2]), fmt(u), fmt(w)]
    return ' '.join(parts)


def quad(out, flags, mat, p00, p10, p11, p01, color, outward):
    """Corners in UV order (0,0) (1,0) (1,1) (0,1): one tile per quad."""
    out.append(tri_line(flags, mat, p00, p10, p11, color, ((0, 0), (1, 0), (1, 1)), outward))
    out.append(tri_line(flags, mat, p00, p11, p01, color, ((0, 0), (1, 1), (0, 1)), outward))


# ---- crossings -------------------------------------------------------------
def sector_point(sign, r, phi, y):
    return (r * math.sin(phi), y, sign * r * math.cos(phi))


def crossing(sign):
    """Ring-sector deck over the molten ring on the +z (sign 1) or -z axis.
    Returns (deck triangles with flags 3, extra triangles, gold edge triangles)."""
    deck, extra, edges = [], [], []
    radii, angles = (R0, (R0 + R1) / 2, R1), [-HALF_ANGLE + 2 * HALF_ANGLE * k / 3 for k in range(4)]
    for i in range(2):
        for k in range(3):
            q = [sector_point(sign, radii[i], angles[k], 0), sector_point(sign, radii[i + 1], angles[k], 0),
                 sector_point(sign, radii[i + 1], angles[k + 1], 0), sector_point(sign, radii[i], angles[k + 1], 0)]
            quad(deck, 3, SLAB, *q, DECK_TOP, (0, 1, 0))
    for phi, side in ((-HALF_ANGLE, -1), (HALF_ANGLE, 1)):
        outward = (side * math.cos(phi), 0, -side * sign * math.sin(phi))
        for i in range(2):
            q = [sector_point(sign, radii[i], phi, -0.6), sector_point(sign, radii[i + 1], phi, -0.6),
                 sector_point(sign, radii[i + 1], phi, 0), sector_point(sign, radii[i], phi, 0)]
            quad(extra, 3, SLAB, *q, DECK_SIDE, outward)
    for i in range(2):
        q = [sector_point(sign, radii[i], -HALF_ANGLE, -0.6), sector_point(sign, radii[i + 1], -HALF_ANGLE, -0.6),
             sector_point(sign, radii[i + 1], HALF_ANGLE, -0.6), sector_point(sign, radii[i], HALF_ANGLE, -0.6)]
        quad(extra, 1, SLAB, *q, DECK_SIDE, (0, -1, 0))
    # Gold edging: a 0.15-wide, 0.04-high strip along each radial edge.
    for phi, side in ((-HALF_ANGLE, -1), (HALF_ANGLE, 1)):
        d = (math.sin(phi), 0, sign * math.cos(phi))
        n = (side * math.cos(phi), 0, -side * sign * math.sin(phi))      # outward across the edge
        def at(r, lateral, y):
            return (d[0] * r + n[0] * lateral, y, d[2] * r + n[2] * lateral)
        lo, hi = -0.175, -0.025
        box = {
            (0, 1, 0): [at(R0, lo, .04), at(R1, lo, .04), at(R1, hi, .04), at(R0, hi, .04)],
            (0, -1, 0): [at(R0, lo, 0), at(R1, lo, 0), at(R1, hi, 0), at(R0, hi, 0)],
            n: [at(R0, hi, 0), at(R1, hi, 0), at(R1, hi, .04), at(R0, hi, .04)],
            tuple(-x for x in n): [at(R0, lo, 0), at(R1, lo, 0), at(R1, lo, .04), at(R0, lo, .04)],
            d: [at(R1, lo, 0), at(R1, hi, 0), at(R1, hi, .04), at(R1, lo, .04)],
            tuple(-x for x in d): [at(R0, lo, 0), at(R0, hi, 0), at(R0, hi, .04), at(R0, lo, .04)],
        }
        for outward, q in box.items():
            quad(edges, 1, CORNICE, *q, EDGE, outward)
    return deck, extra, edges


# ---- pylons ----------------------------------------------------------------
def block(out, center, radial, tangent, half_r, half_t, y0, y1, mat, color, rows=1, top=True, bottom=False):
    """A block aligned to (radial, tangent); faces split into ~tile-wide cells."""
    cx, cz = center

    def at(r, t, y):
        return (cx + radial[0] * r + tangent[0] * t, y, cz + radial[1] * r + tangent[1] * t)

    sides = [((1, 0), (0, 1), half_t), ((0, 1), (-1, 0), half_r), ((-1, 0), (0, -1), half_t), ((0, -1), (1, 0), half_r)]
    for (nr, nt), (sr, st), half in sides:
        outward = (radial[0] * nr + tangent[0] * nt, 0, radial[1] * nr + tangent[1] * nt)
        columns = max(1, round(2 * half / 1.1))
        for col in range(columns):
            s0, s1 = -half + 2 * half * col / columns, -half + 2 * half * (col + 1) / columns
            for row in range(rows):
                h0, h1 = y0 + (y1 - y0) * row / rows, y0 + (y1 - y0) * (row + 1) / rows
                p = [at(nr * half_r + sr * s, nt * half_t + st * s, h) for s, h in ((s0, h0), (s1, h0), (s1, h1), (s0, h1))]
                quad(out, 3, mat, *p, color, outward)
    corners = [at(-half_r, -half_t, 0), at(half_r, -half_t, 0), at(half_r, half_t, 0), at(-half_r, half_t, 0)]
    if top:
        quad(out, 3, mat, *[(x, y1, z) for x, _, z in corners], color, (0, 1, 0))
    if bottom:
        quad(out, 3, mat, *[(x, y0, z) for x, _, z in corners], color, (0, -1, 0))


def pylon(out, angle):
    """A tall panel facing the arena centre: cut, it falls on its broad face."""
    a = math.radians(angle)
    radial, tangent = (math.cos(a), math.sin(a)), (-math.sin(a), math.cos(a))
    center = (PYLON_RADIUS * radial[0], PYLON_RADIUS * radial[1])
    block(out, center, radial, tangent, 0.55, 1.4, 0.0, SHAFT_BOTTOM, BRONZE, GOLD)
    block(out, center, radial, tangent, 0.3, 1.1, SHAFT_BOTTOM, SHAFT_TOP, BLUE, STEEL, rows=6, top=False)
    block(out, center, radial, tangent, 0.42, 1.25, SHAFT_TOP, SHAFT_TOP + 0.4, CORNICE, GOLD, bottom=True)
    block(out, center, radial, tangent, 0.25, 0.5, SHAFT_TOP + 0.4, SHAFT_TOP + 0.9, BRONZE, GOLD)
    reach = (0.45 + 1.3) * math.sqrt(0.5) + 0.1
    lo = [center[0] - reach, 0.45, center[1] - reach]
    hi = [center[0] + reach, SHAFT_TOP + 1.0, center[1] + reach]
    return lo, hi, (-radial[0], 0, -radial[1])     # falls toward the arena centre


# ---- apply -----------------------------------------------------------------
def fields(line):
    f = line.split(' ')
    return int(f[0]), int(f[1]), [(float(f[2 + 8 * k]), float(f[3 + 8 * k]), float(f[4 + 8 * k])) for k in range(3)]


def main(args):
    lines = SOURCE.read_text(encoding='utf-8').split('\n')
    if any(line.startswith('SLICEABLE') for line in lines):
        raise SystemExit('The source world is already polished')
    count_at = 5 + int(lines[4])   # header, bank, title, spawn, material count, materials
    triangles = int(lines[count_at])
    first = count_at + 1
    old_deck, old_edges = {1: [], -1: []}, []
    for i in range(first, first + triangles):
        flags, mat, pts = fields(lines[i])
        ax, az, ys = [abs(p[0]) for p in pts], [abs(p[2]) for p in pts], [p[1] for p in pts]
        if mat == SLAB and flags == 3 and max(ax) <= 2.41 and min(az) >= 11.59 and max(az) <= 16.41 and min(ys) >= -0.61:
            old_deck[1 if pts[0][2] > 0 else -1].append(i)
        elif mat == CORNICE and flags == 1 and 2.42 <= min(ax) and max(ax) <= 2.58 and min(az) >= 11.59 and max(az) <= 16.41 and min(ys) >= -0.01:
            old_edges.append(i)
    assert len(old_deck[1]) == 12 and len(old_deck[-1]) == 12 and len(old_edges) == 48, (len(old_deck[1]), len(old_deck[-1]), len(old_edges))
    appended, new_edges = [], []
    for sign in (1, -1):
        deck, extra, edges = crossing(sign)
        assert len(deck) == 12
        for index, line in zip(old_deck[sign], deck):
            lines[index] = line
        appended += extra
        new_edges += edges
    assert len(new_edges) == 48
    for index, line in zip(old_edges, new_edges):
        lines[index] = line
    volumes = []
    for angle in PYLON_ANGLES:
        before = len(appended)
        volumes.append(pylon(appended, angle))
    print(f'{triangles} triangles: 24 crossing and 48 edging replaced in place, {len(appended)} appended, {len(volumes)} sliceable pylons')
    if '--check' in args:
        return
    end = first + triangles
    out = lines[:count_at] + [str(triangles + len(appended))] + lines[first:end] + appended + lines[end:]
    while out and out[-1] == '':
        out.pop()
    out += ['SLICEABLE 1', str(len(volumes))]
    for lo, hi, fall in volumes:
        out.append(' '.join(fmt(v) for v in (*lo, *hi, *fall)))
    # 3. Steel: the machinery's borrowed Autumn Plains ribbed conduit (33/11)
    # becomes the authored Crownworks steel (tools/author_crown_steel.py).
    if (WORLD.parent / STEEL_TILE).exists():
        out += ['TILE_OVERRIDES 1', '1', f'33 11 "{STEEL_TILE}"']
    WORLD.write_text('\n'.join(out) + '\n', encoding='utf-8', newline='\n')
    print('wrote', WORLD.relative_to(ROOT))


if __name__ == '__main__':
    main(sys.argv[1:])

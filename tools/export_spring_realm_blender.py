"""Export the Spring Realm Blender scene (run inside Blender) to S2WORLD 4.

The retail Dragon Realm (WAD 73) terrain is not copied: it is referenced
through RETAIL_TERRAIN and loaded from the player's disc, minus the
exclusion boxes the stages recorded (the removed south door, the pond's
deepest bowl cells). Liquid boxes turn the pond's floor-like surface into
water. The sky is Summer Forest's: the actor bank's own (WAD 15).
Every authored mesh in the expansion collections is written as triangles:

* Builder meshes carry their (bank, tile) on the material and use tile-space
  UVs that may run across several tile repeats; each triangle is clipped at
  the integer UV lines so every piece maps inside one tile, as the format
  requires.
* Retail copies (mirrored facade, wall bays, rebuilt tower sides, trees)
  use the retail atlas UVs; their tile comes from the atlas cell.

Actors come from Summer Forest bank 15 (vases, baskets, life jars, gems,
orbs) and, listed in IMPORTS, from other scenes (Glimmer's lizards and
Superfly gate). Level portals fill the Dragon Realm's own blank arches
(stage_dressing.ARCH_PORTALS) with Summer Forest stand-in pictures until the
expansion levels exist. The underground's moving slabs go in GAMEPLAY,
whirlwinds in LIFTS, the lake / passage / spring pool in WATER_VOLUMES.
Coordinates are Blender (x, y, z-up) = engine (x, z, y), one unit = 1024
retail units.
"""
import bpy, json, math
from pathlib import Path
from mathutils import Vector

ROOT = Path(r'C:/Users/Shadow/Documents/Spyro 2_PC/data/custom-levels/spring-realm')
SRC = ROOT / 'source'
OUT = ROOT / 'pc'
scene = bpy.context.scene
info = json.loads((SRC / 'retail-73' / 'scene.json').read_text())
AW, AH = info['atlas']['width'], info['atlas']['height']
SKY = json.loads((SRC / 'retail-15' / 'scene.json').read_text())['sky']       # Summer Forest clear colour
COLLECTIONS = ('01 Expansion ground', '02 Castle completion', '03 Spring dressing', '05 Crown gate', '06 Courtyard completion',
               '07 Castle interiors', '08 Underground', '10 Tripo areas', '04 Gameplay')
SKIP_MATERIALS = {'Portal violet'}
TILE_OVERRIDES = (11, 12)        # intro leaf tiles repainted spring green
BANK = 15
MAX_MATERIALS = 128


def xyz(v):
    return (v[0], v[2], v[1])


def line(v):
    return ' '.join(f'{x:.7g}' for x in v)


def quote(s):
    return json.dumps(s, ensure_ascii=True)


def clip(poly, axis, k, keep_above):
    out = []
    n = len(poly)
    for i in range(n):
        a, b = poly[i], poly[(i + 1) % n]
        ia = (a[2][axis] >= k) if keep_above else (a[2][axis] <= k)
        ib = (b[2][axis] >= k) if keep_above else (b[2][axis] <= k)
        if ia:
            out.append(a)
        if ia != ib:
            f = (k - a[2][axis]) / (b[2][axis] - a[2][axis])
            out.append((a[0].lerp(b[0], f), tuple(a[1][j] + (b[1][j] - a[1][j]) * f for j in range(3)),
                        tuple(a[2][j] + (b[2][j] - a[2][j]) * f for j in range(2))))
    return out


def tile_pieces(verts):
    """Split one triangle [(pos, col, uv)] into pieces inside single tiles."""
    us = [v[2][0] for v in verts]
    vs = [v[2][1] for v in verts]
    eps = 1e-5
    pieces = []
    for i in range(math.floor(min(us) + eps), math.ceil(max(us) - eps)):
        for j in range(math.floor(min(vs) + eps), math.ceil(max(vs) - eps)):
            p = clip(verts, 0, i, True)
            p = clip(p, 0, i + 1, False) if p else p
            p = clip(p, 1, j, True) if p else p
            p = clip(p, 1, j + 1, False) if p else p
            if len(p) < 3:
                continue
            q = [(pos, col, (min(1, max(0, uv[0] - i)), min(1, max(0, uv[1] - j)))) for pos, col, uv in p]
            for k in range(1, len(q) - 1):
                tri = [q[0], q[k], q[k + 1]]
                if (tri[1][0] - tri[0][0]).cross(tri[2][0] - tri[0][0]).length > 1e-7:
                    pieces.append(tri)
    if not pieces and max(us) - min(us) < eps or not pieces and max(vs) - min(vs) < eps:
        i, j = math.floor(min(us) + eps), math.floor(min(vs) + eps)
        pieces.append([(pos, col, (min(1, max(0, uv[0] - i)), min(1, max(0, uv[1] - j)))) for pos, col, uv in verts])
    return pieces


def material_key(m, uv_centroid):
    if m.get('source_slot') == -2:                   # original tile shipped beside the world
        return -2, str(m['source_file']), False, 'tile'
    if m.get('source_slot') is not None:
        return int(m['source_slot']), int(m['source_tile']), bool(m.get('water', False)), 'tile'
    tx = int(uv_centroid[0] * AW // 64)
    ty = int((1 - uv_centroid[1]) * AH // 64)
    return 73, tx + 16 * ty, m.name.endswith('translucent'), 'atlas'


def export():
    """Triangles of every exported mesh. Moving-slab objects (custom property
    'mechanism') are written last, each as one contiguous range."""
    materials, mat_index, triangles = [], {}, []
    stats = {'objects': 0, 'clipped_pieces': 0, 'skipped_degenerate': 0}
    objects = []
    for cname in COLLECTIONS:
        col = bpy.data.collections.get(cname)
        if not col:
            continue
        for o in col.all_objects:
            if o.type == 'MESH' and o.get('s2_export') != 'reference':
                objects.append(o)
    objects.sort(key=lambda o: 1 if o.get('mechanism') else 0)
    ranges = {}
    render = collision = 0
    for o in objects:
        collide = bool(o.get('collision', True)) and not o.name.startswith('Tree ')
        md = o.data
        md.calc_loop_triangles()
        uvl = md.uv_layers.active
        cols = md.color_attributes.get('Color')
        stats['objects'] += 1
        first_v, first_t = render * 3, collision
        for t in md.loop_triangles:
            m = md.materials[t.material_index] if md.materials else None
            if m is None or m.name in SKIP_MATERIALS:
                continue
            loops = list(reversed(t.loops))          # axis swap flips handedness
            verts = []
            for li in loops:
                p = o.matrix_world @ md.vertices[md.loops[li].vertex_index].co
                c = tuple(min(2.0, max(0.0, x)) for x in (cols.data[li].color[:3] if cols else (1, 1, 1)))
                verts.append((Vector(xyz(p)), c, tuple(uvl.data[li].uv)))
            cu = sum(v[2][0] for v in verts) / 3
            cv = sum(v[2][1] for v in verts) / 3
            slot, tile, water, mode = material_key(m, (cu, cv))
            key = (slot, tile, water)
            if key not in mat_index:
                mat_index[key] = len(materials)
                materials.append(key)
            flags = 1 | (2 if collide and not water else 0)
            if mode == 'atlas':
                tx, ty = tile % 16, tile // 16
                local = [(pos, c, (min(1, max(0, uv[0] * AW / 64 - tx)), min(1, max(0, 1 - ((1 - uv[1]) * AH / 64 - ty)))))
                         for pos, c, uv in verts]
                pieces = [local]
            else:
                pieces = tile_pieces(verts)
                stats['clipped_pieces'] += max(0, len(pieces) - 1)
            for tri in pieces:
                a, b, c = (Vector(tuple(float(f'{x:.7g}') for x in v[0])) for v in tri)
                if (b - a).cross(c - a).length < 1e-6:
                    stats['skipped_degenerate'] += 1
                    continue
                triangles.append(f'{flags} {mat_index[key]} ' + ' '.join(line((*pos, *col, *uv)) for pos, col, uv in tri))
                render += 1
                if flags & 6:
                    collision += 1
        if o.get('mechanism'):
            ranges[o.name] = (first_v, render * 3 - first_v, first_t, collision - first_t)
    assert len(materials) <= MAX_MATERIALS, f'{len(materials)} materials exceed the format limit of {MAX_MATERIALS}'
    stats['collision_triangles'] = collision
    return materials, triangles, stats, ranges


def ground(x, y, top=60.0):
    dg = bpy.context.evaluated_depsgraph_get()
    hit, loc, n, idx, ob, mw = scene.ray_cast(dg, Vector((x, y, top)), Vector((0, 0, -1)))
    return loc.z if hit else None


def arch_aperture(entry):
    """Five aperture points (Blender coords) wound toward the arch's front."""
    name, ref, (x, y, z), (fx, fy), offset, w, hs, apex, two = entry
    f = Vector((fx, fy, 0)).normalized()
    side = Vector((-f.y, f.x, 0))
    base = Vector((x, y, z)) + f * offset
    pts = [base - side * w, base - side * w + Vector((0, 0, hs)), base + Vector((0, 0, apex)),
           base + side * w + Vector((0, 0, hs)), base + side * w]
    eng = [Vector(xyz(p)) for p in pts]
    n = (eng[1] - eng[0]).cross(eng[2] - eng[0])
    if n.dot(Vector(xyz(f))) < 0:
        eng.reverse()
    return eng


def build():
    OUT.mkdir(exist_ok=True)
    materials, triangles, stats, ranges = export()
    gp = bpy.data.collections['04 Gameplay']
    # authored ids: the surface actors' integers first (unchanged order), then the underground's names
    actors = sorted((o for o in gp.objects if o.get('kind') == 'moby'),
                    key=lambda o: (0, o['stable_id'], '') if isinstance(o['stable_id'], int) else (1, 0, str(o['stable_id'])))
    index = {o.name: i for i, o in enumerate(actors)}
    imports = sorted({(int(o.get('source_slot', BANK)), int(o['class_id'])) for o in actors if int(o.get('source_slot', BANK)) != BANK})
    lifts = [o for o in gp.objects if o.get('kind') == 'lift']
    dr = {}
    exec(open(SRC / 'stage_dressing.py').read().split('def ground_z')[0], dr)
    sx, sy = 60.93, 50.0
    spawn = (sx, sy, ground(sx, sy) + 0.05)
    tour = [(60.93, 50.0), (57.6, 54.0), (58.5, 60.0), (58.5, 66.0), (58.5, 72.5), (58.5, 76.5), (58.5, 81.0), (58.5, 86.5),
            (58.5, 91.5), (58.5, 100.0), (56.5, 109.0), (53.5, 109.0), (48.0, 108.0), (47.0, 111.5), (48.0, 108.0),
            (53.5, 109.0), (58.5, 112.0), (58.5, 120.0), (58.5, 126.0), (58.5, 129.5)]
    route = [(x, y, ground(x, y, top=16.0)) for x, y in tour]
    exclude = json.loads(scene.get('retail_terrain_exclude', '[]')) + dr.get('ARCH_EXCLUDE', [])
    liquid = json.loads(scene.get('retail_terrain_liquid', '[]')) + dr.get('ARCH_LIQUID', [])
    volumes = json.loads(scene.get('water_volumes', '[]'))
    objectives = json.loads(scene.get('custom_objectives', '[]'))
    mechanisms = [json.loads(o['mechanism']) for o in bpy.data.objects if o.get('mechanism') and o.name in ranges]
    path = OUT / 'spring-realm.s2world'
    with path.open('w', encoding='utf-8', newline='\n') as f:
        f.write(f'S2WORLD 4\n{BANK}\nIMPORTS {len(imports)}\n')
        for slot, cls in imports:
            f.write(f'{slot} {cls}\n')
        f.write('"Spring Realm"\n')
        f.write(line((*xyz(spawn), 0.0, *SKY)) + '\n')
        f.write(str(len(materials)) + '\n')
        for slot, tile, water in materials:
            # -2: an original tile shipped beside the world (pc/textures/*.rgba).
            f.write(f'{slot} "{tile}" {int(water)}\n' if slot == -2 else f'{slot} {tile} {int(water)}\n')
        f.write(str(len(triangles)) + '\n' + '\n'.join(triangles) + '\n')
        f.write(str(len(actors)) + '\n')
        for o in actors:
            yaw = float(o.get('yaw', 0.0))
            f.write(f'{int(o.get("source_slot", BANK))} {o["class_id"]} {o["source_record"]} ' +
                    line((*xyz(o.location), yaw)) + f' {o["gem_value"]} {int(o.get("spirit", 255))} {int(o.get("requirement", 1000))}\n')
        f.write(str(len(route)) + '\n')
        for p in route:
            f.write(line(xyz(p)) + '\n')
        f.write('0\n')                                   # no Summer Forest tree references
        # World-to-world doors (mode 2, stage_crown_gate): the picture's Summer
        # Forest portal, the aperture, the world (relative to data/custom-levels),
        # the emblems that open it and presentation flags.
        world_portals = json.loads(scene.get('world_portals', '[]'))
        f.write(str(len(dr['ARCH_PORTALS']) + len(world_portals)) + '\n')
        for entry in dr['ARCH_PORTALS']:
            ap = arch_aperture(entry)
            f.write(f'{entry[1]} 1 {int(entry[8])} ' + ' '.join(line(p) for p in ap) + '\n')
        for w in world_portals:
            ap = arch_aperture((w['name'], w['picture'], tuple(w['base']), tuple(w['facing']), w['offset'], w['half'], w['side'], w['apex'], w['two_sided']))
            f.write(f'{w["picture"]} 2 {int(w["two_sided"])} ' + ' '.join(line(p) for p in ap) + f' {quote(w["world"])} {w["emblems"]} {w["flags"]}\n')
        f.write('0\n')                                   # conversations
        f.write(str(len(objectives)) + '\n')
        for g in objectives:
            f.write(f'{g["bit"]} {len(actors)} {index[g["reward"]]} {int(g["underwater"])} {quote(g["title"])} {len(g["checkpoints"])}\n')
            for p in g['checkpoints']:
                f.write(line(xyz(p)) + '\n')
        f.write('RETAIL_TERRAIN 3\n73\n0\n' + str(len(exclude)) + '\n')        # 0: keep Summer Forest's sky
        for box in exclude:
            lo, hi = box['lo'], box['hi']
            f.write(line(xyz((lo[0] - .05, lo[1] - .05, lo[2] - .05))) + ' ' + line(xyz((hi[0] + .05, hi[1] + .05, hi[2] + .05))) + '\n')
        f.write(str(len(liquid)) + '\n')
        for box in liquid:
            f.write(line(xyz(box['lo'])) + ' ' + line(xyz(box['hi'])) + '\n')
        if volumes:
            f.write('WATER_VOLUMES 1\n' + str(len(volumes)) + '\n')
            for v in volumes:
                f.write(line(xyz(v['lo'])) + ' ' + line(xyz(v['hi'])) + f' {v["height"]:.7g}\n')
        if mechanisms:
            f.write('GAMEPLAY 1\n' + str(len(mechanisms)) + '\n')
            for m in mechanisms:
                f.write(f'{m["kind"]} {m["objective"]} {quote(m["name"])} ' +
                        line((*xyz(m['center']), *xyz(m['extent']), *xyz(m['axis']), m['travel'], m['period'])) + ' 1\n')
                f.write(line(ranges[m['name']]) + '\n')
            f.write('0\n')                               # no timed trials
        f.write('LIFTS 1\n' + str(len(lifts)) + '\n')
        for o in lifts:
            f.write(line((*xyz(o.location), o['yaw'], o['height'], o['radius'])) + '\n')
        # Spring: the intro's autumn-yellow canopies (tiles 11, 12) are repainted
        # green (source/paint_spring_leaves.py) for the retail trees and the copies.
        overrides = [(73, t, f'textures/spring-leaves-{t}.rgba') for t in TILE_OVERRIDES
                     if (OUT / f'textures/spring-leaves-{t}.rgba').exists()]
        if overrides:
            f.write(f'TILE_OVERRIDES 1\n{len(overrides)}\n' + ''.join(f'{s} {t} "{p}"\n' for s, t, p in overrides))
    report = {'file': str(path), 'format': 4, 'materials': len(materials), 'triangles': len(triangles), 'actors': len(actors),
              'imports': imports, 'portals': [e[0] for e in dr['ARCH_PORTALS']], 'lifts': len(lifts), 'mechanisms': len(mechanisms),
              'water_volumes': len(volumes), 'objectives': [g['title'] for g in objectives], 'route_points': len(route), **stats}
    routes = json.loads(scene.get('grotto_routes', '{}'))

    def conv(v):
        if isinstance(v, dict):
            return {k: conv(x) for k, x in v.items()}
        if v and isinstance(v[0], (list, tuple)):
            return [conv(x) for x in v]
        return list(xyz(v))
    routes = {k: conv(v) for k, v in routes.items()}
    flat = {}
    for k, v in routes.items():
        if isinstance(v, dict):
            for k2, v2 in v.items():
                flat[f'{k}_{k2}'] = [v2]
        elif v and isinstance(v[0], list):
            flat[k] = v
        else:
            flat[k] = [v]
    with (OUT / 'routes.txt').open('w', encoding='utf-8', newline='\n') as rf:   # test routes, engine coordinates
        for k, pts in flat.items():
            rf.write(f'ROUTE {k} {len(pts)}\n' + ''.join(line(q) + '\n' for q in pts))
    (OUT / 'budget.json').write_text(json.dumps(report, indent=2))
    return report


result = build()

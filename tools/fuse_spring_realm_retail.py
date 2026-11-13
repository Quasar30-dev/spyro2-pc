"""Fuse the on-disc Dragon Realm intro scene (WAD 73) into a copy of the
Spring Realm master, then render previews of the result. Run headless:

    spyro_scene_export <disc.cue> 73 <export dir>        (atlas.rgba -> atlas.png beside it)
    blender --background "data/custom-levels/spring-realm/Spring Realm.blend" \
        --python tools/fuse_spring_realm_retail.py -- <export dir> <spring-realm.s2world> <out.blend> <previews dir>

The master is opened read-only and saved under a new name; it is never
written. In the copy:

* The master's own retail reference (an earlier import, minus the castle's
  south door) is replaced by a fresh import of WAD 73 from the disc: the
  visual terrain with its atlas UVs, PS1 vertex colours (0..2) and texture
  atlas (packed), and the collision mesh. Coordinates are the game's: Blender
  (x, y, z-up) = engine (x, z, y), retail units / 1024, no offset, exactly as
  the world's RETAIL_TERRAIN section places the scene.
* Faces the game drops (centroid inside one of the world's RETAIL_TERRAIN
  exclusion boxes) go to a separate hidden object, so the visible retail
  mesh is what the game draws. Collision faces inside the liquid boxes carry
  the face attribute 'water_in_game'.
* The world's TILE_OVERRIDES (spring-green leaf tiles 11 and 12) are applied
  to an "in game" copy of the atlas used by every WAD 73 material; the disc's
  original atlas stays in the file (fake user).
* The scene's sky (camera-centred in game) is added scaled around the world,
  hidden.
"""
import bpy, bmesh, json, math, sys
from pathlib import Path
from mathutils import Vector

ARGS = sys.argv[sys.argv.index('--') + 1:]
EXPORT, WORLD, OUT_BLEND, PREVIEWS = (Path(a) for a in ARGS[:4])
ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'data/custom-levels/spring-realm/source'
exec(compile((SRC / 'retail_import.py').read_text(encoding='utf-8'), str(SRC / 'retail_import.py'), 'exec'))
RETAIL = '00 Retail Dragon Realm (WAD 73)'
report = {}


def world_sections(path):
    """RETAIL_TERRAIN exclusion / liquid boxes and TILE_OVERRIDES of an S2WORLD."""
    text = path.read_text(encoding='utf-8')
    tail = text[text.rindex('RETAIL_TERRAIN'):].split('\n')
    tok = ' '.join(tail[1:]).split()
    i = 0
    def take():
        nonlocal i
        i += 1
        return tok[i - 1]
    slot, sky = int(take()), int(take())
    boxes = [[float(take()) for _ in range(6)] for _ in range(int(take()))]
    liquid = [[float(take()) for _ in range(6)] for _ in range(int(take()))]
    overrides = []
    if 'TILE_OVERRIDES' in text:
        lines = text[text.rindex('TILE_OVERRIDES'):].split('\n')
        for ln in lines[2:2 + int(lines[1])]:
            s, t, f = ln.split(None, 2)
            overrides.append((int(s), int(t), json.loads(f)))
    return slot, sky, boxes, liquid, overrides


def inside(boxes, p):          # p in Blender coords; boxes in engine (x, y-up, z)
    e = (p[0], p[2], p[1])
    return any(all(b[k] <= e[k] <= b[k + 3] for k in range(3)) for b in boxes)


def split(obj, boxes, name, note):
    """Move faces whose centroid is in a box to a new hidden object."""
    removed = obj.copy()
    removed.data = obj.data.copy()
    removed.name = removed.data.name = name
    for o, keep_inside in ((obj, False), (removed, True)):
        bm = bmesh.new()
        bm.from_mesh(o.data)
        doomed = [f for f in bm.faces if inside(boxes, f.calc_center_median()) != keep_inside]
        bmesh.ops.delete(bm, geom=doomed, context='FACES')
        bm.to_mesh(o.data)
        bm.free()
    for c in obj.users_collection:
        c.objects.link(removed)
    removed.hide_set(True)
    removed.hide_render = True
    removed['note'] = note
    return len(removed.data.polygons)


def centroids(obj):
    m = obj.matrix_world
    return sorted(tuple(round(c, 3) for c in (m @ p.center)) for p in obj.data.polygons)


def fuse():
    slot, _, boxes, liquid, overrides = world_sections(WORLD)
    assert slot == 73, slot
    coll = bpy.data.collections[RETAIL]
    old = bpy.data.objects.get('Retail Dragon Realm (WAD 73)')
    old_centres = centroids(old) if old else []
    for o in list(coll.objects):
        bpy.data.objects.remove(o, do_unlink=True)
    coll.name = '00 Retail Dragon Realm intro (WAD 73, from disc)'
    visual, collision, info = import_retail_scene(EXPORT, 'WAD 73 disc', coll)
    visual.name, collision.name = 'Dragon Realm intro terrain (WAD 73, disc)', 'Dragon Realm intro collision (WAD 73, disc)'
    total = len(visual.data.polygons)
    dropped = split(visual, boxes, 'Dragon Realm intro faces the world removes (hidden)',
                    'WAD 73 faces whose centroid lies in a RETAIL_TERRAIN exclusion box: the game does not draw them')
    cdropped = split(collision, boxes, 'Dragon Realm intro collision the world removes (hidden)',
                     'WAD 73 collision faces inside a RETAIL_TERRAIN exclusion box: not solid in game')
    water = collision.data.attributes.new('water_in_game', 'BOOLEAN', 'FACE')
    wet = 0
    for p, a in zip(collision.data.polygons, water.data):
        a.value = inside(liquid, collision.matrix_world @ p.center)
        wet += a.value
    for o in coll.objects:
        o['origin'] = 'Spyro 2 disc, WAD 73 (Dragon Realm intro scene), extracted by spyro_scene_export'
    # Same faces as the master's earlier reference, apart from the door it had cut.
    new_centres = centroids(visual)
    report['retail'] = {'disc_faces': total, 'drawn_faces': len(visual.data.polygons), 'removed_faces': dropped,
                        'collision_faces': len(collision.data.polygons) + cdropped, 'collision_removed': cdropped,
                        'collision_water_faces': wet, 'master_reference_faces': len(old_centres),
                        'master_reference_faces_matched': len(set(old_centres) & set(new_centres)),
                        'exclusion_boxes': len(boxes), 'liquid_boxes': len(liquid)}
    # Tile overrides: an in-game copy of the atlas, used by every WAD 73 material.
    disc_atlas = bpy.data.images['WAD 73 disc atlas']
    disc_atlas.use_fake_user = True
    W, H = disc_atlas.size
    px = list(disc_atlas.pixels)
    for s, t, f in overrides:
        if s != 73:
            continue
        raw = (WORLD.parent / f).read_bytes()
        assert len(raw) == 64 * 64 * 4
        x0, y0 = t % 16 * 64, t // 16 * 64
        for y in range(64):
            for x in range(64):
                d = ((H - 1 - (y0 + y)) * W + x0 + x) * 4      # Blender rows run bottom-up
                s4 = (y * 64 + x) * 4
                px[d:d + 4] = [raw[s4 + k] / 255 for k in range(4)]
    game = bpy.data.images.new('WAD 73 atlas (in game: Spring Realm tile overrides)', W, H, alpha=True)
    game.pixels = px
    game.pack()
    swapped = 0
    for m in bpy.data.materials:
        if m.use_nodes:
            for n in m.node_tree.nodes:
                if n.type == 'TEX_IMAGE' and n.image and n.image.name in ('WAD 73 atlas', 'WAD 73 disc atlas'):
                    n.image = game
                    n.interpolation = 'Closest'
                    swapped += 1
    report['tile_overrides'] = {'tiles': [t for _, t, _ in overrides], 'texture_nodes_switched': swapped}
    # Sky: vertex-coloured, drawn around the camera in game; shown here as a dome.
    verts, colors, _, faces, _ = _read_obj(EXPORT / 'sky.obj')
    lo, hi = Vector(info['bounds'][0]), Vector(info['bounds'][1])
    centre, radius = Vector(((lo.x + hi.x) / 2, (lo.y + hi.y) / 2, 10.0)), 260.0
    mesh = bpy.data.meshes.new('Dragon Realm intro sky (WAD 73, disc)')
    mesh.from_pydata([centre + Vector(v) * radius for v in verts], [], faces)
    colour = mesh.color_attributes.new('Color', 'FLOAT_COLOR', 'CORNER')
    for loop in mesh.loops:
        c = colors[loop.vertex_index]
        colour.data[loop.index].color = (*c, 1)
    sky_mat = bpy.data.materials.new('WAD 73 disc sky')
    sky_mat.use_nodes = True
    nt = sky_mat.node_tree
    nt.nodes.clear()
    vc, em, out = nt.nodes.new('ShaderNodeVertexColor'), nt.nodes.new('ShaderNodeEmission'), nt.nodes.new('ShaderNodeOutputMaterial')
    vc.layer_name = 'Color'
    nt.links.new(vc.outputs['Color'], em.inputs['Color'])
    nt.links.new(em.outputs[0], out.inputs['Surface'])
    mesh.materials.append(sky_mat)
    sky = bpy.data.objects.new(mesh.name, mesh)
    sky_coll = bpy.data.collections.new('00b Retail Dragon Realm intro sky (WAD 73, camera-centred, hidden)')
    bpy.context.scene.collection.children.link(sky_coll)
    sky_coll.objects.link(sky)
    sky['origin'] = 'Spyro 2 disc, WAD 73 sky'
    sky['note'] = f'The game draws this around the camera with translation cleared (80029170); shown here centred at {tuple(round(c, 2) for c in centre)}, x{radius}. Spring Realm itself keeps Summer Forest\'s sky.'
    bpy.context.view_layer.layer_collection.children[sky_coll.name].hide_viewport = True
    sky_coll.hide_render = True
    spawn = bpy.data.objects.new('Dragon Realm intro spawn (WAD 73)', None)
    spawn.location = info['spawn']
    spawn.empty_display_type, spawn.empty_display_size = 'SINGLE_ARROW', 2
    coll.objects.link(spawn)
    report['sky'] = {'triangles': len(mesh.polygons), 'centre': list(centre), 'scale': radius}
    bpy.context.scene['fused_retail'] = json.dumps(report)


def look(cam, eye, target):
    cam.location = eye
    d = Vector(target) - Vector(eye)
    cam.rotation_euler = d.to_track_quat('-Z', 'Y').to_euler()


def previews():
    PREVIEWS.mkdir(parents=True, exist_ok=True)
    sc = bpy.context.scene
    camd = bpy.data.cameras.new('Preview camera')
    cam = bpy.data.objects.new('Preview camera', camd)
    sc.collection.objects.link(cam)
    sc.camera = cam
    sc.render.resolution_x, sc.render.resolution_y = 1600, 1000
    sc.render.image_settings.file_format = 'PNG'
    world = sc.world or bpy.data.worlds.new('Preview sky')
    sc.world = world
    world.use_nodes = True
    world.node_tree.nodes['Background'].inputs['Color'].default_value = (0.55, 0.72, 0.95, 1)
    collision = [o for o in sc.objects if 'collision' in o.name.lower() or o.display_type == 'WIRE']
    for o in collision:
        o.hide_render = True
    views = {
        'overview-oblique': ('PERSP', (58, -40, 70), (58, 70, 10), 50),
        'castle-oblique': ('PERSP', (100, 40, 48), (58, 88, 12), 45),
        'courtyard-overhead': ('ORTHO', (58, 35, 120), (58, 35.001, 0), 70),
        'world-overhead': ('ORTHO', (85, 50, 200), (85, 50.001, 0), 215),
    }
    out = {}
    try:
        sc.render.engine = 'BLENDER_EEVEE'
    except TypeError:
        sc.render.engine = 'BLENDER_EEVEE_NEXT'
    sc.eevee.taa_render_samples = 8
    for name, (kind, eye, target, size) in views.items():
        camd.type = kind
        camd.clip_end = 2000
        if kind == 'ORTHO':
            camd.ortho_scale = size
        else:
            camd.angle = math.radians(size)
        look(cam, eye, target)
        sc.render.filepath = str(PREVIEWS / f'{name}.png')
        bpy.ops.render.render(write_still=True)
        out[name] = sc.render.filepath
    # False colour: disc scene blue, Spring Realm additions orange (Workbench).
    sc.render.engine = 'BLENDER_WORKBENCH'
    sh = sc.display.shading
    sh.light, sh.color_type = 'STUDIO', 'OBJECT'
    sh.show_object_outline = True
    retail = set(bpy.data.collections['00 Retail Dragon Realm intro (WAD 73, from disc)'].all_objects)
    for o in sc.objects:
        o.color = (0.15, 0.45, 1.0, 1) if o in retail else (1.0, 0.55, 0.1, 1)
    for name, (kind, eye, target, size) in (('false-colour-world-overhead', views['world-overhead']),
                                             ('false-colour-courtyard-overhead', views['courtyard-overhead']),
                                             ('false-colour-castle-oblique', views['castle-oblique'])):
        camd.type = kind
        if kind == 'ORTHO':
            camd.ortho_scale = size
        else:
            camd.angle = math.radians(size)
        look(cam, eye, target)
        sc.render.filepath = str(PREVIEWS / f'{name}.png')
        bpy.ops.render.render(write_still=True)
        out[name] = sc.render.filepath
    for o in sc.objects:
        o.color = (1, 1, 1, 1)
    for o in collision:
        o.hide_render = True
    bpy.data.objects.remove(cam, do_unlink=True)
    bpy.data.cameras.remove(camd)
    return out


try:
    fuse()
    OUT_BLEND.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT_BLEND), compress=True, relative_remap=False)
    report['previews'] = previews()
    report['blend'] = str(OUT_BLEND)
    (PREVIEWS / 'fuse-report.json').write_text(json.dumps(report, indent=2, default=str))
    print('[fuse] ' + json.dumps(report, default=str), flush=True)
except Exception:
    import traceback
    traceback.print_exc()
    sys.exit(1)

"""Blender (background): author the four Convexity demons as PS1-style rigged models.

    blender --background --factory-startup --python tools/build_demon_actors.py

Each demon is a low-poly segmented mesh (<=256 vertices, the retail 8-bit animated
index limit) with one bone per body part and 100% rigid skinning, exactly like PS1
segmented characters. All five clips (erupt, move, attack, sink, idle) are keyed on
one timeline; markers 'clip:<name>' delimit them. The script saves an editable
assets/actors/demons.blend and exports per-frame deformed vertices, faces, texel UVs
(into a 128x128 four-swatch texture) and baked Gouraud light to
assets/actors/build/<id>.json for tools/pack_pc_actors.py.
"""
import bpy, json, math, os
from mathutils import Vector, Euler, Matrix

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
OUT = os.path.join(ROOT, 'assets', 'actors')
BUILD = os.path.join(OUT, 'build')
os.makedirs(BUILD, exist_ok=True)
CLIPS = ['erupt', 'move', 'attack', 'sink', 'idle']
FRAMES = 8          # frames per clip
TICKS = 3           # VBlanks per frame (20 fps keys, interpolated by the engine)
QUAD = {'skin': (0, 0), 'second': (1, 0), 'bone': (0, 1), 'glow': (1, 1)}   # 64x64 swatches
LIGHT = Vector((-0.45, -0.55, 0.7)).normalized()   # baked PS1 Gouraud key light

class Builder:
    def __init__(self):
        self.verts, self.faces, self.parts = [], [], []   # faces: (indices, swatch)
        self.vpart = []
    def part(self, name, bone):
        self.parts.append((name, bone)); return len(self.parts) - 1
    def add(self, part, points, faces, swatch):
        base = len(self.verts)
        self.verts.extend(points); self.vpart.extend([part] * len(points))
        for f in faces: self.faces.append(([base + i for i in f], swatch, part))
    # Primitives (Blender space: X right, Y forward, Z up).
    def ellipsoid(self, part, c, r, swatch, rings=4, segs=6):
        pts = [Vector((c[0], c[1], c[2] + r[2]))]
        for i in range(1, rings):
            t = math.pi * i / rings
            for j in range(segs):
                a = 2 * math.pi * j / segs
                pts.append(Vector((c[0] + r[0] * math.sin(t) * math.cos(a), c[1] + r[1] * math.sin(t) * math.sin(a), c[2] + r[2] * math.cos(t))))
        pts.append(Vector((c[0], c[1], c[2] - r[2])))
        f = []
        for j in range(segs): f.append((0, 1 + j, 1 + (j + 1) % segs))
        for i in range(rings - 2):
            for j in range(segs):
                a, b = 1 + i * segs + j, 1 + i * segs + (j + 1) % segs
                f.append((a, a + segs, b + segs, b))
        last = 1 + (rings - 1) * segs
        for j in range(segs): f.append((last, 1 + (rings - 2) * segs + (j + 1) % segs, 1 + (rings - 2) * segs + j))
        self.add(part, pts, f, swatch)
    def tube(self, part, a, b, ra, rb, swatch, segs=5, caps=True):
        a, b = Vector(a), Vector(b); axis = (b - a).normalized()
        s0 = axis.cross(Vector((0, 0, 1)) if abs(axis.z) < 0.9 else Vector((1, 0, 0))).normalized(); s1 = axis.cross(s0)
        pts = []
        for c, r in ((a, ra), (b, rb)):
            for j in range(segs):
                t = 2 * math.pi * j / segs; pts.append(c + (s0 * math.cos(t) + s1 * math.sin(t)) * r)
        f = [(j, (j + 1) % segs, segs + (j + 1) % segs, segs + j) for j in range(segs)]
        if caps:
            pts += [a, b]
            for j in range(segs):
                f.append((2 * segs, (j + 1) % segs, j)); f.append((2 * segs + 1, segs + j, segs + (j + 1) % segs))
        self.add(part, pts, f, swatch)
    def cone(self, part, base, tip, r, swatch, segs=4):
        base, tip = Vector(base), Vector(tip); axis = (tip - base).normalized()
        s0 = axis.cross(Vector((0, 0, 1)) if abs(axis.z) < 0.9 else Vector((1, 0, 0))).normalized(); s1 = axis.cross(s0)
        pts = [base + (s0 * math.cos(2 * math.pi * j / segs) + s1 * math.sin(2 * math.pi * j / segs)) * r for j in range(segs)] + [tip]
        f = [(j, (j + 1) % segs, segs) for j in range(segs)] + [tuple(reversed(range(segs)))] if segs == 4 else \
            [(j, (j + 1) % segs, segs) for j in range(segs)]
        self.add(part, pts, f, swatch)
    def box(self, part, c, s, swatch):
        x, y, z = s
        pts = [Vector((c[0] + dx * x, c[1] + dy * y, c[2] + dz * z)) for dz in (-1, 1) for dy in (-1, 1) for dx in (-1, 1)]
        f = [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)]
        self.add(part, pts, f, swatch)

# ----------------------------------------------------------------------------
# Demon definitions: parts -> bones (head/tail pivots), and a pose function per clip.
# Poses return {bone: (Euler degrees (x,y,z), location offset)}.
def imp():
    b = Builder()
    P = {n: b.part(n, n) for n in ['body', 'head', 'arm_l', 'fore_l', 'arm_r', 'fore_r', 'leg_l', 'leg_r', 'tail']}
    b.ellipsoid(P['body'], (0, 0, .36), (.17, .15, .2), 'skin')
    b.ellipsoid(P['body'], (0, .07, .3), (.11, .08, .13), 'second', rings=3, segs=5)
    b.ellipsoid(P['head'], (0, .07, .62), (.14, .13, .12), 'skin')
    for s in (-1, 1):
        b.cone(P['head'], (s * .07, .05, .7), (s * .16, -.05, .84), .035, 'bone')       # curved horns
        b.cone(P['head'], (s * .12, .02, .64), (s * .3, -.04, .72), .04, 'skin', segs=3) # big ears
        b.ellipsoid(P['head'], (s * .055, .185, .64), (.032, .02, .032), 'glow', rings=2, segs=4)   # eyes
    for s, a, f in ((-1, 'arm_l', 'fore_l'), (1, 'arm_r', 'fore_r')):
        b.tube(P[a], (s * .16, .02, .48), (s * .26, .06, .32), .045, .04, 'skin')
        b.tube(P[f], (s * .26, .06, .32), (s * .3, .12, .14), .04, .035, 'skin')
        for k in (-1, 0, 1): b.cone(P[f], (s * .3 + k * .025, .12, .14), (s * .31 + k * .04, .2, .06), .015, 'bone', segs=3)
    for s, l in ((-1, 'leg_l'), (1, 'leg_r')):
        b.tube(P[l], (s * .08, 0, .22), (s * .1, .02, .04), .05, .04, 'skin')
        b.box(P[l], (s * .1, .06, .02), (.045, .07, .02), 'bone')
    b.tube(P['tail'], (0, -.12, .25), (0, -.3, .2), .035, .025, 'skin', segs=4)
    b.tube(P['tail'], (0, -.3, .2), (0, -.45, .26), .025, .012, 'skin', segs=4)
    b.cone(P['tail'], (0, -.45, .26), (0, -.55, .32), .035, 'glow', segs=3)
    bones = {'body': ((0, 0, .3), None), 'head': ((0, .05, .52), 'body'), 'arm_l': ((-.16, .02, .48), 'body'), 'fore_l': ((-.26, .06, .32), 'arm_l'),
             'arm_r': ((.16, .02, .48), 'body'), 'fore_r': ((.26, .06, .32), 'arm_r'), 'leg_l': ((-.08, 0, .22), 'body'), 'leg_r': ((.08, 0, .22), 'body'),
             'tail': ((0, -.12, .25), 'body')}
    def pose(clip, t):
        w = math.sin(2 * math.pi * t)
        if clip == 'idle':
            return {'body': ((3 * w, 0, 0), (0, 0, .008 * w)), 'head': ((4 * w, 0, 6 * w), None), 'arm_l': ((0, -6 * w, 0), None),
                    'arm_r': ((0, 6 * w, 0), None), 'tail': ((0, 0, 18 * w), None)}
        if clip == 'move':
            return {'body': ((10, 0, 4 * w), (0, 0, .025 * abs(w))), 'leg_l': ((40 * w, 0, 0), None), 'leg_r': ((-40 * w, 0, 0), None),
                    'arm_l': ((-35 * w, 0, 0), None), 'arm_r': ((35 * w, 0, 0), None), 'fore_l': ((-20, 0, 0), None), 'fore_r': ((-20, 0, 0), None),
                    'head': ((-6, 0, 0), None), 'tail': ((0, 0, 30 * w), None)}
        if clip == 'attack':
            k = math.sin(math.pi * min(1, t * 1.5))
            return {'body': ((18 * k, 0, -12 * k), None), 'arm_r': ((-110 + 150 * min(1, t * 2), 0, -20), None), 'fore_r': ((-40 * k, 0, 0), None),
                    'arm_l': ((25 * k, 0, 0), None), 'head': ((-12 * k, 0, 0), None), 'tail': ((0, 0, 35 * k), None)}
        if clip in ('erupt', 'sink'):
            u = t if clip == 'erupt' else 1 - t
            c = 1 - u
            return {'body': ((45 * c, 0, 0), (0, 0, -.12 * c)), 'arm_l': ((-150 * c - 20 * u, 0, -15 * c), None), 'arm_r': ((-150 * c - 20 * u, 0, 15 * c), None),
                    'fore_l': ((-30 * c, 0, 0), None), 'fore_r': ((-30 * c, 0, 0), None), 'leg_l': ((-50 * c, 0, 0), None), 'leg_r': ((-50 * c, 0, 0), None),
                    'head': ((-25 * c, 0, 0), None), 'tail': ((0, 0, 40 * math.sin(math.pi * u)), None)}
        return {}
    return b, bones, pose, [P['head'], P['fore_r']], [(0, 260)]

def hound():
    b = Builder()
    P = {n: b.part(n, n) for n in ['body', 'head', 'leg_fl', 'leg_fr', 'leg_bl', 'leg_br', 'tail']}
    b.ellipsoid(P['body'], (0, 0, .48), (.15, .32, .16), 'skin', segs=6)
    b.ellipsoid(P['body'], (0, 0, .4), (.11, .26, .09), 'second', rings=3, segs=5)
    for k in range(5):
        b.cone(P['body'], (0, .22 - k * .11, .6), (0, .17 - k * .11, .74 - abs(k - 2) * .03), .04, 'bone', segs=3)
    b.tube(P['head'], (0, .28, .56), (0, .38, .66), .07, .06, 'skin')
    b.ellipsoid(P['head'], (0, .46, .66), (.1, .13, .08), 'skin')
    b.box(P['head'], (0, .56, .64), (.06, .07, .05), 'bone')                      # bony mask
    for s in (-1, 1):
        b.ellipsoid(P['head'], (s * .055, .54, .7), (.028, .02, .022), 'glow', rings=2, segs=4)
        b.cone(P['head'], (s * .06, .4, .72), (s * .1, .32, .84), .03, 'bone', segs=3)
    for name, x, y in (('leg_fl', -.1, .2), ('leg_fr', .1, .2), ('leg_bl', -.1, -.2), ('leg_br', .1, -.2)):
        b.tube(P[name], (x, y, .42), (x, y + .03, .05), .05, .035, 'skin', segs=4)
        b.box(P[name], (x, y + .07, .025), (.045, .07, .025), 'bone')
    b.tube(P['tail'], (0, -.3, .5), (0, -.48, .58), .045, .03, 'second', segs=4)
    b.cone(P['tail'], (0, -.48, .58), (0, -.68, .6), .05, 'glow', segs=4)
    bones = {'body': ((0, 0, .45), None), 'head': ((0, .28, .56), 'body'), 'leg_fl': ((-.1, .2, .42), 'body'), 'leg_fr': ((.1, .2, .42), 'body'),
             'leg_bl': ((-.1, -.2, .42), 'body'), 'leg_br': ((.1, -.2, .42), 'body'), 'tail': ((0, -.3, .5), 'body')}
    def pose(clip, t):
        w = math.sin(2 * math.pi * t)
        if clip == 'idle':
            return {'body': ((2 * w, 0, 0), (0, 0, .01 * w)), 'head': ((5 * w, 0, 8 * math.sin(math.pi * t)), None), 'tail': ((0, 0, 25 * w), None)}
        if clip == 'move':
            g = math.sin(2 * math.pi * t + 1.2)
            return {'body': ((6 * w, 0, 0), (0, 0, .04 * abs(w))), 'leg_fl': ((45 * w, 0, 0), None), 'leg_fr': ((45 * g, 0, 0), None),
                    'leg_bl': ((-45 * w, 0, 0), None), 'leg_br': ((-45 * g, 0, 0), None), 'head': ((-8 * w, 0, 0), None), 'tail': ((15, 0, 30 * w), None)}
        if clip == 'attack':
            k = math.sin(math.pi * t)
            return {'body': ((-20 * k, 0, 0), (0, .12 * k, .08 * k)), 'head': ((30 * k, 0, 0), None), 'leg_fl': ((-60 * k, 0, 0), None), 'leg_fr': ((-60 * k, 0, 0), None),
                    'leg_bl': ((30 * k, 0, 0), None), 'leg_br': ((30 * k, 0, 0), None), 'tail': ((-20 * k, 0, 0), None)}
        if clip in ('erupt', 'sink'):
            u = t if clip == 'erupt' else 1 - t; c = 1 - u
            return {'body': ((-40 * c, 0, 0), (0, 0, -.15 * c)), 'head': ((35 * c, 0, 0), None), 'leg_fl': ((-80 * c, 0, 0), None), 'leg_fr': ((-80 * c, 0, 0), None),
                    'leg_bl': ((50 * c, 0, 0), None), 'leg_br': ((50 * c, 0, 0), None), 'tail': ((0, 0, 30 * math.sin(math.pi * u)), None)}
        return {}
    return b, bones, pose, [P['head']], [(0, 300)]

def wraith():
    b = Builder()
    P = {n: b.part(n, n) for n in ['body', 'hood', 'arm_l', 'arm_r', 'wisps']}
    rings = [(.95, .16), (.75, .2), (.52, .24), (.3, .27)]
    pts, segs = [], 8
    for z, r in rings:
        for j in range(segs):
            a = 2 * math.pi * j / segs; pts.append(Vector((r * math.cos(a), r * math.sin(a) * .85, z)))
    f = []
    for i in range(len(rings) - 1):
        for j in range(segs): f.append((i * segs + j, i * segs + (j + 1) % segs, (i + 1) * segs + (j + 1) % segs, (i + 1) * segs + j))
    b.add(P['body'], pts, f, 'second')
    for j in range(segs):
        a = 2 * math.pi * (j + .5) / segs
        b.cone(P['wisps'], (.24 * math.cos(a), .2 * math.sin(a), .33), (.3 * math.cos(a), .25 * math.sin(a), .02 + .06 * (j % 2)), .06, 'skin', segs=3)
    b.ellipsoid(P['hood'], (0, 0, 1.06), (.2, .2, .2), 'skin', segs=6)
    b.ellipsoid(P['hood'], (0, .11, 1.03), (.12, .07, .12), 'glow', rings=3, segs=5)           # hollow face glow
    for s in (-1, 1):
        b.ellipsoid(P['hood'], (s * .05, .18, 1.06), (.03, .02, .03), 'glow', rings=2, segs=4)
    for s, a in ((-1, 'arm_l'), (1, 'arm_r')):
        b.tube(P[a], (s * .17, .02, .9), (s * .3, .15, .7), .06, .04, 'skin')
        for k in (-1, 0, 1): b.cone(P[a], (s * .3 + k * .02, .15, .7), (s * .32 + k * .05, .27, .62), .016, 'bone', segs=3)
    bones = {'body': ((0, 0, .5), None), 'hood': ((0, 0, .92), 'body'), 'arm_l': ((-.17, .02, .9), 'body'), 'arm_r': ((.17, .02, .9), 'body'), 'wisps': ((0, 0, .33), 'body')}
    def pose(clip, t):
        w = math.sin(2 * math.pi * t)
        if clip in ('idle', 'move'):
            amp = 1 if clip == 'idle' else 1.6
            return {'body': ((4 * amp * w if clip == 'move' else 2 * w, 0, 3 * w), (0, 0, .03 * w)), 'hood': ((-3 * w, 0, 5 * w), None),
                    'arm_l': ((10 * w, 0, 0), None), 'arm_r': ((-10 * w, 0, 0), None), 'wisps': ((0, 0, 20 * w * amp), None)}
        if clip == 'attack':
            k = math.sin(math.pi * t)
            return {'body': ((15 * k, 0, 0), None), 'arm_l': ((-70 * k, 0, -20 * k), None), 'arm_r': ((-70 * k, 0, 20 * k), None), 'hood': ((-10 * k, 0, 0), None)}
        if clip in ('erupt', 'sink'):
            u = t if clip == 'erupt' else 1 - t; c = 1 - u
            return {'body': ((0, 0, 90 * c), (0, 0, -.3 * c)), 'hood': ((30 * c, 0, 0), None), 'arm_l': ((-130 * c, 0, 0), None), 'arm_r': ((-130 * c, 0, 0), None),
                    'wisps': ((0, 0, 60 * c), None)}
        return {}
    return b, bones, pose, [P['hood'], P['arm_r']], [(0, 380)]

def maw():
    b = Builder()
    P = {n: b.part(n, n) for n in ['base', 'jaw_a', 'jaw_b', 'throat']}
    segs = 8
    for part, sign in ((P['jaw_a'], 1), (P['jaw_b'], -1)):
        pts = []
        for k, (r, z) in enumerate(((.75, .0), (.85, .45), (.62, .95))):
            for j in range(segs + 1):
                a = math.pi * j / segs
                pts.append(Vector((r * math.cos(a), sign * r * math.sin(a) * .9, z)))
        f = []
        for i in range(2):
            for j in range(segs): f.append((i * (segs + 1) + j, i * (segs + 1) + j + 1, (i + 1) * (segs + 1) + j + 1, (i + 1) * (segs + 1) + j))
        b.add(part, pts, f, 'second')
        for j in range(segs):
            a = math.pi * (j + .5) / segs
            base = (.6 * math.cos(a), sign * .6 * math.sin(a) * .9, .95)
            b.cone(part, base, (.42 * math.cos(a), sign * .42 * math.sin(a) * .9, 1.45 - .12 * (j % 2)), .07, 'bone', segs=3)
        for j in (1, 4, 7):
            a = math.pi * j / segs
            b.cone(part, (.84 * math.cos(a), sign * .76 * math.sin(a), .5), (1.05 * math.cos(a), sign * .95 * math.sin(a), .7), .05, 'bone', segs=3)
    b.ellipsoid(P['throat'], (0, 0, .35), (.45, .4, .35), 'glow', segs=6)
    b.tube(P['base'], (0, 0, -.3), (0, 0, .05), .82, .78, 'skin', segs=8, caps=False)
    bones = {'base': ((0, 0, 0), None), 'jaw_a': ((0, 0, .1), 'base'), 'jaw_b': ((0, 0, .1), 'base'), 'throat': ((0, 0, .2), 'base')}
    def pose(clip, t):
        if clip == 'attack':
            o = math.sin(math.pi * min(1, t * 1.25))
            return {'jaw_a': ((-45 * o, 0, 0), None), 'jaw_b': ((45 * o, 0, 0), None), 'throat': ((0, 0, 0), (0, 0, .1 * o))}
        if clip == 'erupt':
            return {'jaw_a': ((-20 * t, 0, 0), None), 'jaw_b': ((20 * t, 0, 0), None)}
        if clip == 'sink':
            return {'jaw_a': ((-20 * (1 - t), 0, 0), None), 'jaw_b': ((20 * (1 - t), 0, 0), None)}
        w = math.sin(2 * math.pi * t)
        return {'jaw_a': ((-8 - 6 * w, 0, 0), None), 'jaw_b': ((8 + 6 * w, 0, 0), None), 'throat': ((0, 0, 0), (0, 0, .03 * w))}
    return b, bones, pose, [P['throat']], [(0, 900)]

DEMONS = [('imp', 1, imp), ('hound', 2, hound), ('wraith', 3, wraith), ('maw', 4, maw)]

def uv_for(face_pts, swatch):
    """Cylindrical projection around Z into the 64x64 swatch (texel coordinates)."""
    qx, qy = QUAD[swatch]
    c = sum(face_pts, Vector()) / len(face_pts)
    angles = [math.atan2(p.y, p.x) / (2 * math.pi) % 1.0 for p in face_pts]
    ref = math.atan2(c.y, c.x) / (2 * math.pi) % 1.0
    out = []
    for p, a in zip(face_pts, angles):
        if a - ref > .5: a -= 1
        if ref - a > .5: a += 1
        u = (a * 2.0) % 1.0
        v = (p.z * 1.6) % 1.0
        out.append((min(127, int(qx * 64 + 2 + u * 59)), min(127, int(qy * 64 + 2 + (1 - v) * 59))))
    return out

def build(name, kind, factory):
    b, bones, pose, attach_parts, spheres = factory()
    assert len(b.verts) <= 256, (name, len(b.verts))
    assert len(b.faces) <= 600, (name, len(b.faces))
    mesh = bpy.data.meshes.new(name + '-mesh')
    mesh.from_pydata([tuple(v) for v in b.verts], [], [tuple(f[0]) for f in b.faces])
    mesh.update()
    obj = bpy.data.objects.new(name, mesh); bpy.context.scene.collection.objects.link(obj)
    arm_data = bpy.data.armatures.new(name + '-rig'); rig = bpy.data.objects.new(name + '-rig', arm_data)
    bpy.context.scene.collection.objects.link(rig)
    bpy.context.view_layer.objects.active = rig; bpy.ops.object.mode_set(mode='EDIT')
    for bone, (head, parent) in bones.items():
        e = arm_data.edit_bones.new(bone); e.head = head; e.tail = (head[0], head[1], head[2] + .08)
    for bone, (head, parent) in bones.items():
        if parent: arm_data.edit_bones[bone].parent = arm_data.edit_bones[parent]
    bpy.ops.object.mode_set(mode='OBJECT')
    for i, (pname, bone) in enumerate(b.parts):
        g = obj.vertex_groups.get(bone) or obj.vertex_groups.new(name=bone)
        g.add([v for v, p in enumerate(b.vpart) if p == i], 1.0, 'REPLACE')
    mod = obj.modifiers.new('rig', 'ARMATURE'); mod.object = rig; obj.parent = rig
    # Key every clip on one timeline; markers delimit them.
    scene = bpy.context.scene
    for c, clip in enumerate(CLIPS):
        start = c * (FRAMES + 2) + 1
        scene.timeline_markers.new('clip:' + clip, frame=start)
        for f in range(FRAMES):
            t = f / FRAMES if clip in ('move', 'idle') else f / (FRAMES - 1)
            p = pose(clip, t)
            for bone in bones:
                pb = rig.pose.bones[bone]; pb.rotation_mode = 'XYZ'
                rot, loc = p.get(bone, ((0, 0, 0), None))
                pb.rotation_euler = Euler([math.radians(a) for a in rot], 'XYZ')
                pb.location = loc if loc else (0, 0, 0)
                pb.keyframe_insert('rotation_euler', frame=start + f); pb.keyframe_insert('location', frame=start + f)
    # Export per-frame deformed vertices (Blender -> engine: x, z, y; +Z forward).
    to_engine = lambda v: [int(round(v.x * 1024)), int(round(v.z * 1024)), int(round(v.y * 1024))]
    depsgraph = bpy.context.evaluated_depsgraph_get()
    clips = []
    for c, clip in enumerate(CLIPS):
        start = c * (FRAMES + 2) + 1; frames, attach = [], []
        for f in range(FRAMES):
            scene.frame_set(start + f); depsgraph = bpy.context.evaluated_depsgraph_get()
            ev = obj.evaluated_get(depsgraph); m = ev.to_mesh()
            pts = [ev.matrix_world @ v.co for v in m.vertices]
            frames.append([to_engine(p) for p in pts])
            att = []
            for part in attach_parts:
                vs = [pts[v] for v, p in enumerate(b.vpart) if p == part]
                att.append(to_engine(sum(vs, Vector()) / len(vs)))
            attach.append(att); ev.to_mesh_clear()
        clips.append({'name': clip, 'ticks': TICKS, 'frames': frames, 'attachments': attach})
    # Faces: texel UVs per swatch and baked Gouraud light (128 = 1.0).
    rest = [Vector(v) for v in b.verts]
    faces = []
    for idx, swatch, part in b.faces:
        pts = [rest[i] for i in idx]
        n = (pts[1] - pts[0]).cross(pts[2] - pts[0])
        centre = sum(pts, Vector()) / len(pts)
        if n.length > 1e-9: n.normalize()
        if n.dot(centre - Vector((0, 0, centre.z * .5))) < 0: n = -n
        light = .58 + .62 * max(0.0, n.dot(LIGHT))
        level = 182 if swatch == 'glow' else int(128 * light)
        quad = len(idx) == 4
        order = [idx[0], idx[1], idx[3], idx[2]] if quad else idx   # engine quads draw (0,1,2)+(1,3,2)
        face_pts = [rest[i] for i in order]
        faces.append({'v': order, 'quad': quad, 'uv': uv_for(face_pts, swatch), 'rgb': [[level, level, min(255, int(level * 1.04))]] * len(order), 'swatch': swatch})
    data = {'id': name, 'kind': kind, 'vertices': len(b.verts), 'faces': faces, 'clips': clips, 'spheres': spheres,
            'attachment_parts': [b.parts[p][0] for p in attach_parts]}
    with open(os.path.join(BUILD, name + '.json'), 'w') as fh: json.dump(data, fh)
    print('demon', name, 'vertices', len(b.verts), 'faces', len(faces))
    return obj, rig

for o in list(bpy.data.objects): bpy.data.objects.remove(o)
for i, (name, kind, factory) in enumerate(DEMONS):
    obj, rig = build(name, kind, factory)
    rig.location.x = i * 2.5   # side by side in the editable .blend (exports are rig-local)
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT, 'demons.blend'))
print('saved', os.path.join(OUT, 'demons.blend'))

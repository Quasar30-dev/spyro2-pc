"""Market Mesa props, modelled by hand in Blender (headless bpy) after Season of Ice's art.

  "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" -b --factory-startup \
      --python tools/soi/mesa_props_blender.py

Props (world units = Spyro 2 units, Z up, base centre at the origin), sized and counted for a PS1
level: SoI's mint pagoda pines (big three-tier, small two-tier), the striped market stall, the pink
cloth rack on two A-frames, the pole with its pale banner, a crate. Every face carries a material
name of mesa_build.MAT and a UV map inside one tile (one tile per face, as Spyro 2's terrain).

Writes the master data/soi/pilot/mesa_props.blend, data/soi/pc/mesa_props.json (SoI units:
x = X / H, y = Y / H, z = Z / V, normals transformed accordingly) and a review render
data/soi/pc/shots/props-<tag>.png. mesa_build.py instances the JSON.
"""
import json
import math
import sys
from pathlib import Path

import bmesh
import bpy
from mathutils import Vector

PROJECT = Path(__file__).resolve().parents[2]
TEX = PROJECT / "data/soi/pc/textures/png"
ATLAS = PROJECT / "data/soi/retail-atlas"
V = 1.0 / 24.0
H = V * math.sqrt(6.0)
TAG = sys.argv[sys.argv.index("--") + 1] if "--" in sys.argv else "v2f"

# material name -> preview image (authored tile, or a retail atlas tile)
PREVIEW = {"foliage": "pine", "bark": "bark_soi", "post": "wood_soi", "awning": "awning_soi", "pole": "wood_soi",
           "flag": "flag_cyan", "cloth": "cloth_pink", "counter": (39, 44), "crate": (61, 78)}


class Prop:
    def __init__(self, name):
        self.name = name
        self.bm = bmesh.new()
        self.uv = self.bm.loops.layers.uv.new("UVMap")
        self.bm.faces.layers.int.new("flags")
        self.mats = []

    def face(self, pts, mat, uvs=None, flags=3):
        vs = [self.bm.verts.new(Vector(p)) for p in pts]
        f = self.bm.faces.new(vs)
        if mat not in self.mats:
            self.mats.append(mat)
        f.material_index = self.mats.index(mat)
        uvs = uvs or ([(0, 0), (1, 0), (1, 1), (0, 1)] if len(pts) == 4 else [(0, 0), (1, 0), (0.5, 1)])
        for loop, uv in zip(f.loops, uvs):
            loop[self.uv].uv = uv
        f.smooth = False
        f[self.flag_layer()] = flags        # 1 visual only, 3 visual + collision
        return f

    def flag_layer(self):
        return self.bm.faces.layers.int.get("flags")

    def ring(self, c, r, z, n, a0=0.0, wob=None):
        return [(c[0] + r * (wob[k] if wob else 1) * math.cos(a0 + 2 * math.pi * k / n),
                 c[1] + r * (wob[k] if wob else 1) * math.sin(a0 + 2 * math.pi * k / n), z) for k in range(n)]

    def band(self, lo, hi, mat, v0=0.0, v1=1.0, flags=3):
        n = len(lo)
        for k in range(n):
            a, b = lo[k], lo[(k + 1) % n]
            c, d = hi[(k + 1) % n], hi[k]
            self.face([a, b, c, d], mat, [(0, v0), (1, v0), (1, v1), (0, v1)], flags)

    def fan(self, ring, apex, mat, v0=0.0, flags=3, down=False):
        n = len(ring)
        for k in range(n):
            a, b = ring[k], ring[(k + 1) % n]
            if down:
                a, b = b, a
            self.face([a, b, apex], mat, [(0, v0), (1, v0), (0.5, 1)], flags)

    def prism(self, base, top, r, sides, mat, flags=3, a0=0.0):
        """Thin post between two points (no caps)."""
        base, top = Vector(base), Vector(top)
        ax = (top - base).normalized()
        side = ax.orthogonal().normalized()
        up = ax.cross(side)
        ring = lambda c: [c + (side * math.cos(a0 + 2 * math.pi * k / sides) + up * math.sin(a0 + 2 * math.pi * k / sides)) * r for k in range(sides)]
        self.band(ring(base), ring(top), mat, flags=flags)

    def to_object(self):
        me = bpy.data.meshes.new(self.name)
        self.bm.to_mesh(me)
        ob = bpy.data.objects.new(self.name, me)
        for m in self.mats:
            ob.data.materials.append(material(m))
        bpy.context.collection.objects.link(ob)
        return ob


_mats = {}


def material(name):
    if name in _mats:
        return _mats[name]
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    src = PREVIEW.get(name)
    img = None
    if isinstance(src, str) and (TEX / f"{src}.png").exists():
        img = bpy.data.images.load(str(TEX / f"{src}.png"))
    elif isinstance(src, tuple):
        atlas = bpy.data.images.load(str(ATLAS / f"atlas-{src[0]}.png"))
        img = crop_tile(atlas, src[1], name)
    if img:
        nt = m.node_tree
        tex = nt.nodes.new("ShaderNodeTexImage")
        tex.image = img
        tex.interpolation = "Closest"
        bsdf = nt.nodes.get("Principled BSDF")
        nt.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    _mats[name] = m
    return m


def crop_tile(atlas, t, name):
    w, h = atlas.size
    px = list(atlas.pixels)
    x0, y0 = (t % 16) * 64, h - (t // 16) * 64 - 64
    out = bpy.data.images.new(f"tile_{name}", 64, 64)
    data = []
    for y in range(64):
        row = (y0 + y) * w + x0
        data += px[row * 4:(row + 64) * 4]
    out.pixels = data
    return out


# ------------------------------------------------------------------------------------ the props
def pine(name, tiers, sides, height):
    """SoI's pagoda pine: drooping tiers (a star rim of alternating radii, a shoulder, an apex),
    an orange trunk; the lowest tier is capped underneath."""
    p = Prop(name)
    tr = 0.08 * height
    p.band(p.ring((0, 0), tr, 0, 4), p.ring((0, 0), tr * 0.6, 0.5 * height, 4), "bark")
    z = 0.22 * height
    for t in range(tiers):
        frac = 1 - t / tiers                       # 1 at the bottom tier
        r = (0.20 + 0.26 * frac) * height
        h = height * (0.30 if t < tiers - 1 else 0.46)
        a0 = t * 0.5
        wob = [1.0 if k % 2 == 0 else 0.8 for k in range(sides)]
        # an umbrella: a flat crown, then the fronds droop steeply to a star-shaped rim
        rim = p.ring((0, 0), r, z, sides, a0, wob)
        shoulder = p.ring((0, 0), r * 0.62, z + h * 0.52, sides, a0)
        p.band(rim, shoulder, "foliage", 0.0, 0.6)
        p.fan(shoulder, (0, 0, z + h), "foliage", 0.6)
        if t == 0:
            p.fan(rim, (0, 0, z + 0.05 * height), "foliage", 0.0, down=True)
        z += h * 0.58
    return p


def stall(name):
    """SoI's market stall: a plank counter, four posts, a striped gable awning (ridge along X)."""
    p = Prop(name)
    w, d, ch, ph = 0.78, 0.55, 0.5, 1.12
    # counter (top + four sides)
    a = [(-w, -d), (w, -d), (w, d), (-w, d)]
    for k in range(4):
        (xa, ya), (xb, yb) = a[k], a[(k + 1) % 4]
        p.face([(xa, ya, 0), (xb, yb, 0), (xb, yb, ch), (xa, ya, ch)], "counter")
    p.face([(-w, -d, ch), (w, -d, ch), (w, d, ch), (-w, d, ch)], "counter")
    for sx, sy in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
        p.prism((sx * (w - 0.06), sy * (d - 0.06), ch), (sx * (w - 0.06), sy * (d - 0.06), ph), 0.05, 3, "post")
    ew, ed, zr, zt = w + 0.22, d + 0.28, ph - 0.04, ph + 0.42
    for sy in (-1, 1):
        q = [(-ew, sy * ed, zr), (ew, sy * ed, zr), (ew, 0, zt), (-ew, 0, zt)]
        uv = [(0, 0), (1, 0), (1, 1), (0, 1)]
        if sy > 0:
            q, uv = q[::-1], uv[::-1]
        p.face(q, "awning", uv)
        p.face(q[::-1], "awning", uv[::-1], flags=1)          # underside, visual only
    for sx in (-1, 1):
        t = [(sx * ew, -ed, zr), (sx * ew, ed, zr), (sx * ew, 0, zt)]
        p.face(t if sx > 0 else t[::-1], "awning", [(0, 0), (1, 0), (0.5, 1)] if sx > 0 else [(1, 0), (0, 0), (0.5, 1)], flags=1)
    return p


def rack(name):
    """SoI's pink cloth rack: two A-frames, a ridge pole and the draped cloth (both faces)."""
    p = Prop(name)
    L, sp, hgt = 1.0, 0.55, 1.15
    for x in (-L, L):
        for s in (-1, 1):
            p.prism((x, s * sp, 0), (x, 0, hgt), 0.05, 3, "post")
    p.prism((-L - 0.12, 0, hgt), (L + 0.12, 0, hgt), 0.045, 3, "post", a0=0.5)
    # cloth: over the pole, sagging between the frames, down to 0.35 above the roof
    xs = (-L, 0.0, L)
    for s in (-1, 1):
        for k in range(2):
            x0, x1 = xs[k], xs[k + 1]
            sag0, sag1 = (0.0, 0.10) if k == 0 else (0.10, 0.0)
            top0, top1 = (x0, 0, hgt + 0.02 - sag0), (x1, 0, hgt + 0.02 - sag1)
            bot0, bot1 = (x0, s * sp * 0.78, 0.38 - sag0), (x1, s * sp * 0.78, 0.38 - sag1)
            q = [bot0, bot1, top1, top0]
            uv = [(k * 0.5, 0), (k * 0.5 + 0.5, 0), (k * 0.5 + 0.5, 1), (k * 0.5, 1)]
            if s > 0:
                q, uv = q[::-1], uv[::-1]
            p.face(q, "cloth", uv, flags=1)
            p.face(q[::-1], "cloth", uv[::-1], flags=1)
    return p


def flagpole(name):
    """A pole with a finial and SoI's pale striped banner (two panels, a slight wave)."""
    p = Prop(name)
    hgt = 2.6
    p.band(p.ring((0, 0), 0.06, 0, 3), p.ring((0, 0), 0.045, hgt, 3), "pole")
    p.fan(p.ring((0, 0), 0.09, hgt, 3, 0.5), (0, 0, hgt + 0.16), "pole")
    top, bot = hgt - 0.15, hgt - 1.25
    xs, ys = (0.06, 0.32, 0.62), (0.0, 0.07, -0.02)
    for k in range(2):
        q = [(xs[k], ys[k], bot), (xs[k + 1], ys[k + 1], bot - 0.06 * k), (xs[k + 1], ys[k + 1], top), (xs[k], ys[k], top)]
        uv = [(k * 0.5, 0), (k * 0.5 + 0.5, 0), (k * 0.5 + 0.5, 1), (k * 0.5, 1)]
        p.face(q, "flag", uv, flags=1)
        p.face(q[::-1], "flag", uv[::-1], flags=1)
    return p


def crate(name, s=0.32):
    p = Prop(name)
    a = [(-s, -s), (s, -s), (s, s), (-s, s)]
    for k in range(4):
        (xa, ya), (xb, yb) = a[k], a[(k + 1) % 4]
        p.face([(xa, ya, 0), (xb, yb, 0), (xb, yb, 2 * s * 0.9), (xa, ya, 2 * s * 0.9)], "crate")
    p.face([(-s, -s, 2 * s * 0.9), (s, -s, 2 * s * 0.9), (s, s, 2 * s * 0.9), (-s, s, 2 * s * 0.9)], "crate")
    return p


PROPS = [pine("pine_big", 3, 6, 2.5), pine("pine_small", 2, 5, 1.7), stall("stall"), rack("rack"),
         flagpole("flagpole"), crate("crate")]


def export(path: Path):
    out = {}
    for pr in PROPS:
        bm = pr.bm
        bm.normal_update()
        lay = pr.flag_layer()
        verts = [(v.co.x / H, v.co.y / H, v.co.z / V) for v in bm.verts]
        bm.verts.index_update()
        faces = []
        for f in bm.faces:
            n = Vector((f.normal.x * H, f.normal.y * H, f.normal.z * V)).normalized()
            faces.append(dict(v=[l.vert.index for l in f.loops], uv=[tuple(round(c, 4) for c in l[pr.uv].uv) for l in f.loops],
                              mat=pr.mats[f.material_index], flags=f[lay], n=tuple(round(c, 5) for c in n)))
        out[pr.name] = dict(verts=[tuple(round(c, 4) for c in v) for v in verts], faces=faces,
                            triangles=sum(len(f["v"]) - 2 for f in faces))
    path.write_text(json.dumps(out))
    return {k: v["triangles"] for k, v in out.items()}


def review(path: Path):
    """All props in a row, textured, a three-quarter view (Workbench)."""
    for ob in list(bpy.data.objects):          # the factory scene's cube, light and camera
        bpy.data.objects.remove(ob, do_unlink=True)
    for k, pr in enumerate(PROPS):
        ob = pr.to_object()
        ob.location = (k * 3.0, 0, 0)
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_WORKBENCH"
    sh = sc.display.shading
    sh.light = "STUDIO"
    sh.color_type = "TEXTURE"
    sh.show_backface_culling = True
    sc.render.film_transparent = False
    sc.world = bpy.data.worlds.new("w") if not sc.world else sc.world
    cam = bpy.data.cameras.new("cam")
    cam.lens = 40
    co = bpy.data.objects.new("cam", cam)
    sc.collection.objects.link(co)
    mid = Vector(((len(PROPS) - 1) * 1.5, 0, 0.9))
    co.location = mid + Vector((0, -17.5, 5.0))
    co.rotation_euler = (mid - co.location).to_track_quat("-Z", "Y").to_euler()
    sc.camera = co
    sc.render.resolution_x, sc.render.resolution_y = 1600, 560
    sc.render.filepath = str(path)
    bpy.ops.render.render(write_still=True)


if __name__ == "__main__":
    counts = export(PROJECT / "data/soi/pc/mesa_props.json")
    review(PROJECT / f"data/soi/pc/shots/props-{TAG}.png")
    bpy.ops.wm.save_as_mainfile(filepath=str(PROJECT / "data/soi/pilot/mesa_props.blend"), compress=True)
    print("PROPS", json.dumps(counts))

"""Build a metric 3D blockout of one extracted Season of Ice level in Blender (headless only).

  "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" -b --factory-startup \
      --python tools/soi/blender_blockout.py -- --level data/soi/levels/06-market-mesa/level.json \
      --out data/soi/pilot [--h 0.15]

Coordinates (see docs/SEASON_OF_ICE_PORT.md, "Scale"):
  SoI world (x, y, z) from the ROM, projected by the game as px = 2(x - y), py = x + y - z (0801ec60).
  Blender X = x * H, Y = -y * H (SoI axes are left-handed), Z = z * V, with V = H / sqrt(6): the
  value that makes the game's projection an exact orthographic view at 30 degrees elevation and
  45 degrees azimuth, so the blockout seen from that camera lines up with the original map.
  H defaults to 0.15 Blender metres (= Spyro 2 world units, 1024 retail units) per SoI unit.
Only the solid prisms (the collision the game walks on) become terrain; the sprite-priority
occluders are kept as hidden wireframes for reference. Objects become plain marker primitives.
"""
import json
import math
import sys
from pathlib import Path

import bmesh
import bpy
from mathutils import Matrix, Vector

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []


def arg(name, default=None):
    return argv[argv.index(name) + 1] if name in argv else default


LEVEL = Path(arg("--level")).resolve()  # absolute: Blender resolves relative render paths against C:/
OUT = Path(arg("--out")).resolve()
H = float(arg("--h", "0.15"))
V = H / math.sqrt(6.0)
FLOOR = float(arg("--floor", "-48"))  # SoI z of the blockout's underside

L = json.loads(LEVEL.read_text(encoding="utf-8"))
S = L["summary"]
name = S["name"]


def P(x, y, z):
    return Vector((x * H, -y * H, z * V))


# ---------------------------------------------------------------- scene
bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.unit_settings.system = "METRIC"
root = bpy.data.collections.new(f"SoI {S['level']:02d} {name} blockout")
scene.collection.children.link(root)


def coll(label, parent=root):
    c = bpy.data.collections.new(label)
    parent.children.link(c)
    return c


c_terrain, c_occ, c_obj, c_cam = coll("terrain (solid prisms)"), coll("occluders (reference)"), coll("objects (markers)"), coll("cameras")

# height-level materials
tops = sorted({p["top"] for p in L["collision"]["prisms"] if p["role"] == "solid"})
lo, hi = tops[0], tops[-1]


def ramp(t):
    # dark blue -> green -> amber, readable in both workbench and eevee
    stops = [(0.0, (0.10, 0.18, 0.45)), (0.5, (0.20, 0.62, 0.35)), (1.0, (0.95, 0.62, 0.15))]
    for (a, ca), (b, cb) in zip(stops, stops[1:]):
        if t <= b:
            f = (t - a) / (b - a)
            return tuple(ca[i] + (cb[i] - ca[i]) * f for i in range(3))
    return stops[-1][1]


mats = {}
for t in tops:
    m = bpy.data.materials.new(f"soi_top_{t:03d}")
    col = ramp((t - lo) / max(1, hi - lo))
    m.diffuse_color = (*col, 1.0)
    mats[t] = m
side_mat = bpy.data.materials.new("soi_side")
side_mat.diffuse_color = (0.42, 0.38, 0.36, 1.0)


def build_prisms(prisms, mesh_name, collection, materials=True):
    me = bpy.data.meshes.new(mesh_name)
    bm = bmesh.new()
    mat_index = {}
    if materials:
        me.materials.append(side_mat)
        for t in tops:
            mat_index[t] = len(me.materials)
            me.materials.append(mats[t])
    idx_layer = bm.faces.layers.int.new("soi_prism")
    for p in prisms:
        poly = p["polygon"]
        if len(poly) < 3:
            continue
        top = p["top"]
        bot = [bm.verts.new(P(x, y, FLOOR)) for x, y in poly]
        up = [bm.verts.new(P(x, y, top)) for x, y in poly]
        # SoI polygons are listed so that edge normals point outward; make faces wind CCW in Blender
        area = sum(P(*a, 0).x * P(*b, 0).y - P(*b, 0).x * P(*a, 0).y for a, b in zip(poly, poly[1:] + poly[:1]))
        order = list(range(len(poly))) if area > 0 else list(reversed(range(len(poly))))
        ftop = bm.faces.new([up[i] for i in order])
        fbot = bm.faces.new([bot[i] for i in reversed(order)])
        for f in (ftop, fbot):
            f[idx_layer] = p["index"]
        if materials:
            ftop.material_index = mat_index[top]
        for j in range(len(order)):
            a, b = order[j], order[(j + 1) % len(order)]
            f = bm.faces.new([bot[a], bot[b], up[b], up[a]])
            f[idx_layer] = p["index"]
    bm.to_mesh(me)
    bm.free()
    ob = bpy.data.objects.new(mesh_name, me)
    collection.objects.link(ob)
    return ob


solids = [p for p in L["collision"]["prisms"] if p["role"] == "solid"]
occ = [p for p in L["collision"]["prisms"] if p["role"] == "occluder"]
terrain = build_prisms(solids, "terrain_solids", c_terrain)
terrain["soi_level"] = S["level"]
terrain["soi_units"] = f"X = x*{H}, Y = -y*{H}, Z = z*{V:.6f} (Blender m = Spyro 2 world units)"
occ_ob = build_prisms(occ, "occluders_wire", c_occ, materials=False)
occ_ob.display_type = "WIRE"
occ_ob.hide_render = True

# ---------------------------------------------------------------- objects
GEM_COL = {1: (0.85, 0.15, 0.15), 2: (0.20, 0.75, 0.25), 5: (0.25, 0.40, 0.95), 10: (0.95, 0.80, 0.20)}


def marker_mat(label, col):
    m = bpy.data.materials.get(label) or bpy.data.materials.new(label)
    m.diffuse_color = (*col, 1.0)
    return m


def add_marker(kind, loc, size, mat, label):
    if kind == "gem":
        bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=1, radius=size, location=loc)
    elif kind == "enemy":
        bpy.ops.mesh.primitive_cone_add(vertices=8, radius1=size, depth=size * 2.2, location=loc + Vector((0, 0, size * 1.1)))
    elif kind == "start":
        bpy.ops.mesh.primitive_cylinder_add(vertices=12, radius=size, depth=size * 0.3, location=loc)
    else:
        bpy.ops.mesh.primitive_cube_add(size=size * 1.6, location=loc + Vector((0, 0, size * 0.8)))
    ob = bpy.context.active_object
    ob.name = label
    ob.data.materials.append(mat)
    for c in ob.users_collection:
        c.objects.unlink(ob)
    c_obj.objects.link(ob)
    return ob


def category(t):
    if 117 <= t <= 120:
        return "gem"
    if t >= 140 and t != 245:
        return "enemy"
    return "prop"


for e in L["objects"]["objects"]:
    x, y, z = e["pos"]
    cat = category(e["type"])
    loc = P(x, y, z)
    if cat == "gem":
        v = e["gem_value"]
        ob = add_marker("gem", loc + Vector((0, 0, 0.25)), 0.18, marker_mat(f"gem_{v}", GEM_COL[v]), f"gem{v}_{e['index']:03d}")
    elif cat == "enemy":
        ob = add_marker("enemy", loc, 0.35, marker_mat("enemy", (0.85, 0.1, 0.6)), f"t{e['type']}_{e['index']:03d}")
    else:
        ob = add_marker("prop", loc, 0.3, marker_mat("prop", (0.1, 0.8, 0.85)), f"t{e['type']}_{e['index']:03d}")
    ob["soi_type"] = e["type"]
    ob["soi_param"] = e["param"]
    ob["soi_handler"] = e["handler"] or ""
sx, sy, sz = L["objects"]["start"]["pos"]
add_marker("start", P(sx, sy, sz), 0.6, marker_mat("player_start", (1, 1, 1)), "player_start")

# ---------------------------------------------------------------- cameras and renders
scene.render.engine = "BLENDER_WORKBENCH"
sh = scene.display.shading
sh.light = "STUDIO"
sh.color_type = "MATERIAL"
sh.show_cavity = True
sh.cavity_type = "WORLD"
sh.show_object_outline = True
scene.render.film_transparent = True
scene.view_settings.view_transform = "Standard"

xs = [v for p in solids for v in (p["bbox"][0], p["bbox"][2])]
ys = [v for p in solids for v in (p["bbox"][1], p["bbox"][3])]
cx, cy = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2
centre = P(cx, cy, (lo + hi) / 2)
span = max(max(xs) - min(xs), max(ys) - min(ys)) * H


def camera(label, loc, look, ortho=None, lens=35):
    cam = bpy.data.cameras.new(label)
    if ortho:
        cam.type = "ORTHO"
        cam.ortho_scale = ortho
    else:
        cam.lens = lens
    cam.clip_end = 5000
    ob = bpy.data.objects.new(label, cam)
    c_cam.objects.link(ob)
    ob.location = loc
    d = (look - loc).normalized()
    ob.rotation_euler = d.to_track_quat("-Z", "Y").to_euler()
    return ob


# SoI camera: orthographic, exact inverse of px = 2(x - y), py = x + y - z for the 2400 x 1600 map
r = Vector((1, 1, 0)).normalized()
u = Vector((-1 / (2 * math.sqrt(2)), 1 / (2 * math.sqrt(2)), math.sqrt(3) / 2))
zc = r.cross(u)
W_PX, H_PX = S["bg_px"]
px_m = H / (2 * math.sqrt(2))  # metres per map pixel
# solve x - y = W/4, x + y = H/2 (z = 0) for the map centre pixel
x0 = (W_PX / 4 + H_PX / 2) / 2
y0 = (H_PX / 2 - W_PX / 4) / 2
iso = camera("soi_iso_camera", P(x0, y0, 0) + zc * 400, P(x0, y0, 0), ortho=W_PX * px_m)
iso.rotation_euler = Matrix((r, u, zc)).transposed().to_euler()

top = camera("top_down", centre + Vector((0, 0, 500)), centre, ortho=span * 1.05)
over = camera("overview_persp", centre + Vector((span * 0.55, -span * 0.75, span * 0.55)), centre, lens=30)
# a low route view from the player start toward the level centre
st = P(sx, sy, sz) + Vector((0, 0, 1.2))
low = camera("start_view", st - (centre - st).normalized() * 4 + Vector((0, 0, 1.5)), centre, lens=24)

OUT.mkdir(parents=True, exist_ok=True)
rend = OUT / "renders"
rend.mkdir(exist_ok=True)


def render(cam, fname, res, occluders=False):
    scene.camera = cam
    scene.render.resolution_x, scene.render.resolution_y = res
    scene.render.resolution_percentage = 100
    scene.render.filepath = str(rend / fname)
    bpy.ops.render.render(write_still=True)


render(iso, "iso_match.png", (W_PX // 2, H_PX // 2))
render(top, "top_down.png", (1200, 1200))
render(over, "overview.png", (1600, 900))
render(low, "start_view.png", (1600, 900))

slug = LEVEL.parent.name
bpy.ops.wm.save_as_mainfile(filepath=str(OUT / f"{slug}_blockout.blend"), compress=True)
meta = dict(level=S["level"], name=name, h_m_per_unit=H, v_m_per_unit=V, floor_z=FLOOR,
            solids=len(solids), occluders=len(occ), objects=len(L["objects"]["objects"]),
            terrain_triangles=sum(len(f.vertices) - 2 for f in terrain.data.polygons),
            extent_m=[(max(xs) - min(xs)) * H, (max(ys) - min(ys)) * H, (hi - lo) * V],
            renders=[str((rend / n).as_posix()) for n in ("iso_match.png", "top_down.png", "overview.png", "start_view.png")],
            blend=str((OUT / f"{slug}_blockout.blend").as_posix()))
(OUT / "blockout.json").write_text(json.dumps(meta, indent=1))
print("BLOCKOUT", json.dumps(meta))

"""Hand-author the dedicated Summer Forest Mesa portal in headless Blender.

Meshes, UVs, cameras and the original Summer Forest stone sheet stay editable.
No image/model generator is used. Runtime export is taken from saved geometry.
"""
import bpy
import bmesh
import math
import json
import hashlib
import sys
import argparse
from pathlib import Path
from mathutils import Vector

ROOT=Path('C:/Users/Shadow/Documents/Spyro 2_PC')
ap=argparse.ArgumentParser();ap.add_argument('--out',type=Path,required=True)
args=ap.parse_args(sys.argv[sys.argv.index('--')+1:]);OUT=args.out.resolve()
OUT.mkdir(parents=True,exist_ok=False)
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
CENTRE=(133000,17500,37376)
INNER=[(640,0),(1000,1500),(0,2304),(-1000,1500),(-640,0)]
# Ring cross-section follows the original five-sided opening, enlarged for
# a distinct icy mesa arch. The bottom is deliberately open for Spyro.
OUTER=[(896,0),(1280,1632),(0,2672),(-1280,1632),(-896,0)]
stone=bpy.data.materials.new('SummerForest original stone53, cool vertex tint')
stone.diffuse_color=(.42,.58,.72,1);stone.use_nodes=True
node=stone.node_tree.nodes.get('Principled BSDF');node.inputs['Base Color'].default_value=stone.diffuse_color;node.inputs['Roughness'].default_value=1
sheet=bpy.data.images.load(str(ROOT/'data/soi/summer-portal-20261105/summer-stone53.png'));sheet.pack()
texture=stone.node_tree.nodes.new('ShaderNodeTexImage');texture.image=sheet;texture.interpolation='Closest'
stone.node_tree.links.new(texture.outputs['Color'],node.inputs['Base Color'])
crystal=bpy.data.materials.new('Ice facets on original stone53');crystal.diffuse_color=(.65,.85,1,1)
parts=[]
def piece(name,vertices,faces,tint=(96,112,128)):
    mesh=bpy.data.meshes.new(name);mesh.from_pydata(vertices,[],faces);mesh.update()
    obj=bpy.data.objects.new(name,mesh);bpy.context.collection.objects.link(obj);mesh.materials.append(stone)
    obj['ps1_material']=53;obj['ps1_rgb']=tint;uv=mesh.uv_layers.new(name='OriginalStone53')
    for poly in mesh.polygons:
        coords=[(0,0),(1,0),(1,1),(0,1)] if len(poly.vertices)==4 else [(0,0),(1,0),(.5,1)]
        for li,p in zip(poly.loop_indices,coords):uv.data[li].uv=p
    bm=bmesh.new();bm.from_mesh(mesh);bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(mesh);bm.free()
    obj.location=Vector(CENTRE)/1024
    parts.append(obj);return obj
for segment in range(4):
    ai,bi=INNER[segment:segment+2];ao,bo=OUTER[segment:segment+2]
    # Two masonry blocks per sloped segment retain real mortar seams.
    for half in range(2):
        t0=half/2+.006;t1=(half+1)/2-.006
        lerp=lambda a,b,t:((a[0]+(b[0]-a[0])*t)/1024,(a[1]+(b[1]-a[1])*t)/1024)
        q=[lerp(ai,bi,t0),lerp(ai,bi,t1),lerp(ao,bo,t1),lerp(ao,bo,t0)]
        v=[(depth,y,z) for depth in (-.18,.18) for y,z in q]
        piece(f'Arch masonry {segment}.{half}',v,[(3,2,1,0),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)])
for side in (-1,1):
    y=side*.87
    piece('Footing '+str(side),[(x,y+dy,z) for z in (0,.20) for dy in (-.30,.30) for x in (-.28,.28)],
          [(0,2,3,1),(4,5,7,6),(0,1,5,4),(2,6,7,3),(0,4,6,2),(1,3,7,5)],(104,120,128))
    # A solid crystal crest on each shoulder, never blocking the opening.
    piece('Ice crest '+str(side),[(-.10,side*1.10,1.61),(.10,side*1.10,1.61),(0,side*1.45,2.24),
                                 (-.10,side*1.25,1.51),(.10,side*1.25,1.51)],
          [(0,2,1),(3,4,2),(0,1,4,3),(0,3,2),(1,2,4)],(112,124,128))
piece('Crown ice crest',[(-.12,-.16,2.55),(.12,-.16,2.55),(.12,.16,2.55),(-.12,.16,2.55),(0,0,3.03)],
      [(3,2,1,0),(0,1,4),(1,2,4),(2,3,4),(3,0,4)],(112,124,128))
masters=list(parts)
for obj in masters:obj.hide_render=True;obj.hide_viewport=True;obj['authoring_master']=True
# Summer Forest ships8160 near faces; the original13-bit face scratch index
# leaves only31. A single closed arch shell plus its crown costs23, rather
# than silently growing the resident renderer's fixed scratch buffer.
cross=INNER+OUTER
v=[(depth,y/1024,z/1024) for depth in (-.18,.18) for y,z in cross]
f=[]
for i in range(4):
    f += [(i,i+1,i+6,i+5),(i+15,i+16,i+11,i+10),
          (i,i+10,i+11,i+1),(i+5,i+6,i+16,i+15)]
f += [(0,5,15,10),(4,14,19,9)]
arch=piece('PS1 closed arch LOD0',v,f)
crown=masters[-1].copy();crown.data=masters[-1].data.copy();crown.name='PS1 crown LOD0'
bpy.context.collection.objects.link(crown);crown.hide_render=False;crown.hide_viewport=False
parts=[arch,crown]
stats=[];faces=[]
for obj in parts:
    bm=bmesh.new();bm.from_mesh(obj.data)
    assert all(e.is_manifold for e in bm.edges),(obj.name,'open solid')
    assert bm.calc_volume(signed=True)>0,(obj.name,'inward solid');bm.free()
    stats.append(dict(name=obj.name,vertices=len(obj.data.vertices),faces=len(obj.data.polygons)))
    for poly in obj.data.polygons:
        coords=[tuple((obj.matrix_world@obj.data.vertices[i].co)*1024) for i in poly.vertices]
        # Explicit transform update below is required before serialization.
        faces.append(dict(object=obj.name,vertices=coords,uv=[list(obj.data.uv_layers.active.data[i].uv) for i in poly.loop_indices],
                          material=obj['ps1_material'],rgb=list(obj['ps1_rgb'])))
bpy.context.view_layer.update()
faces=[]
for obj in parts:
    for poly in obj.data.polygons:
        faces.append(dict(object=obj.name,vertices=[list((obj.matrix_world@obj.data.vertices[i].co)*1024) for i in poly.vertices],
                          uv=[list(obj.data.uv_layers.active.data[i].uv) for i in poly.loop_indices],material=obj['ps1_material'],rgb=list(obj['ps1_rgb'])))
for name,eye,target in [('front',(126,17.1,39.3),(129.88,17.1,37.8)),('reverse',(134,17.1,39.3),(129.88,17.1,37.8)),
                        ('overhead',(129.88,17.1,46),(129.88,17.1,37.8)),('oblique',(126,12.8,42),(129.88,17.1,38.1))]:
    data=bpy.data.cameras.new(name);obj=bpy.data.objects.new(name,data);bpy.context.collection.objects.link(obj)
    obj.location=eye;obj.rotation_euler=(Vector(target)-obj.location).to_track_quat('-Z','Y').to_euler();data.type='ORTHO';data.ortho_scale=5
    obj['authoring_view']=True
light_data=bpy.data.lights.new('Soft daylight','AREA');light=bpy.data.objects.new('Soft daylight',light_data);bpy.context.collection.objects.link(light)
light.location=(126,12,45);light_data.energy=1300;light_data.shape='DISK';light_data.size=8
scene=bpy.context.scene;scene.world.color=(.22,.25,.30);scene.render.engine='BLENDER_EEVEE_NEXT' if bpy.app.version<(5,0,0) else 'CYCLES'
if scene.render.engine=='CYCLES':scene.cycles.samples=12
scene.render.resolution_x=640;scene.render.resolution_y=640;scene.render.resolution_percentage=100
blend=OUT/'market-mesa-special-portal.blend';bpy.ops.wm.save_as_mainfile(filepath=str(blend))
payload=dict(centre=CENTRE,aperture=[[CENTRE[0],CENTRE[1]+y,CENTRE[2]+z] for y,z in INNER],faces=faces,
             parts=stats,master_components=len(masters),runtime_face_budget=31,blend=str(blend),blend_sha256=hashlib.sha256(blend.read_bytes()).hexdigest(),
             material_source='Summer Forest original material53, retained source UVs',
             limits=['Blender authoring only; placement/PS1 near-far/collision/portal travel checks required.'])
(OUT/'portal-mesh.json').write_text(json.dumps(payload,indent=2)+'\n')
for name in ('front','reverse','overhead','oblique'):
    scene.camera=bpy.data.objects[name];scene.render.filepath=str(OUT/(name+'.png'));bpy.ops.render.render(write_still=True)
print('PORTAL BLENDER CLOSED GEOMETRY',payload['blend_sha256'],len(parts),'closed solids',len(faces),'faces; editable UVs/cameras retained')
